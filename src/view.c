#include "app.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cairo.h>
#include <math.h>
#include <time.h>

/* ------------------------------------------------------------------ */
/* parameter plumbing                                                  */
/* ------------------------------------------------------------------ */

static void fill_render_params(View *v, RenderParams *p) {
    AppState *s = v->app;
    float ext = app_view_extent(s, v->kind);
    float step = app_grid_step(s, v->kind);
    memset(p, 0, sizeof(*p));
    p->view_kind = v->kind;
    p->scheme = s->scheme;
    p->complex_basis = s->spec.complex_basis;
    p->style = s->style;
    p->extent = ext;
    p->particle_size = s->particle_size;
    p->brightness = s->brightness;
    p->bloom = s->bloom;
    p->bloom_strength = s->bloom_strength;
    p->show_grid = s->show_grid;
    p->grid_step = step;
    p->grid_half = ceilf(ext * 1.15f / (5.0f * step)) * 5.0f * step;
    p->show_nucleus = s->show_nucleus;
    p->nucleus_glow = s->nucleus_glow;
    p->phase = (float)fmod(s->phase, 2.0 * M_PI);
    p->slice_mode = s->slice_mode;
    p->slice_axis = s->slice_axis;
    p->slice_pos = s->slice_pos * ext;
    p->slice_half = fmaxf(0.002f * ext, s->slice_thick * ext);
    p->time_s = (float)(g_get_monotonic_time() % 100000000) * 1e-6f;
}

static void fill_hud(View *v, HudState *h, int w, int hgt) {
    AppState *s = v->app;
    memset(h, 0, sizeof(*h));
    h->cam = &v->cam;
    h->width = w;
    h->height = hgt;
    h->view_kind = v->kind;
    h->scheme = s->scheme;
    h->complex_basis = s->spec.complex_basis;
    h->spec = &s->spec;
    h->props = &s->props;
    h->orbital_name = s->orbital_name;
    h->extent = app_view_extent(s, v->kind);
    h->grid_step = app_grid_step(s, v->kind);
    h->show_info = s->show_info;
    h->show_axes = s->show_axes;
    h->show_gizmo = s->show_gizmo;
    h->show_legend = s->show_legend;
    h->show_grid = s->show_grid;
    h->show_sphere90 = s->show_sphere90;
    h->show_bbox = s->show_bbox;
    h->slice_mode = s->slice_mode;
    h->slice_axis = s->slice_axis;
    h->slice_pos = s->slice_pos * h->extent;
    h->fps = v->fps;
    h->animating = v->animating;
    h->point_count = v->point_count;
    h->n_pos = v->n_pos;
    h->n_neg = v->n_neg;
    h->generating = v->generating;
    h->phase_anim = s->phase_anim;
    h->phase = (float)s->phase;
}

void view_init(View *v, AppState *s, int kind) {
    memset(v, 0, sizeof(*v));
    v->app = s;
    v->kind = kind;
    v->cam = camera_create();
    v->drag_mode = -1;
    v->dirty = TRUE;
    atomic_init(&v->gen_id, 0);
    atomic_init(&v->progress, 0);
}

void view_mark_dirty(View *v) { v->dirty = TRUE; }

void view_camera_changed(View *v) {
    v->dirty = TRUE;
    AppState *s = v->app;
    if (s->link_cameras) {
        View *o = &s->views[v->kind == VIEW_DENSITY ? VIEW_WAVE : VIEW_DENSITY];
        o->cam.target = v->cam.target;
        o->cam.distance = v->cam.distance;
        o->cam.yaw = v->cam.yaw;
        o->cam.pitch = v->cam.pitch;
        o->dirty = TRUE;
    }
}

/* ------------------------------------------------------------------ */
/* asynchronous sampling                                               */
/* ------------------------------------------------------------------ */

typedef struct {
    View *v;
    int gen_id;
    SamplingRequest req;
    PointVertex *verts;
    SamplingResult res;
    int ok;
    gint64 t0_us;
} GenTask;

static gboolean on_gen_done(gpointer data) {
    GenTask *t = data;
    View *v = t->v;
    if (t->gen_id == atomic_load(&v->gen_id)) {
        if (t->ok && v->gl_area && gtk_widget_get_realized(v->gl_area) && v->renderer.ready) {
            gtk_gl_area_make_current(GTK_GL_AREA(v->gl_area));
            renderer_set_points(&v->renderer, t->verts, t->res.count);
            v->point_count = t->res.count;
            v->n_pos = t->res.n_pos;
            v->n_neg = t->res.n_neg;
            v->gen_seconds = (double)(g_get_monotonic_time() - t->t0_us) * 1e-6;
        }
        v->generating = FALSE;
        gtk_widget_set_visible(v->load_box, FALSE);
        v->dirty = TRUE;
        char buf[256];
        AppState *s = v->app;
        snprintf(buf, sizeof(buf), "Density: %d pts (%.2f s)   ·   Wave: %d pts (%.2f s)",
                 s->views[VIEW_DENSITY].point_count, s->views[VIEW_DENSITY].gen_seconds,
                 s->views[VIEW_WAVE].point_count, s->views[VIEW_WAVE].gen_seconds);
        app_set_status(s, buf);
    }
    free(t->verts);
    g_free(t);
    return G_SOURCE_REMOVE;
}

static gpointer gen_thread(gpointer data) {
    GenTask *t = data;
    t->ok = sampling_generate(&t->req, &t->verts, &t->res, &t->v->gen_id, t->gen_id, &t->v->progress) > 0;
    g_idle_add_full(G_PRIORITY_DEFAULT, on_gen_done, t, NULL);   /* not g_idle_add: that would starve behind frame painting */
    return NULL;
}

void view_request_generation(View *v) {
    AppState *s = v->app;
    if (!v->renderer.ready) return;
    int id = atomic_fetch_add(&v->gen_id, 1) + 1;
    atomic_store(&v->progress, 0);
    v->generating = TRUE;
    v->gen_total = s->num_samples;
    v->dirty = TRUE;

    char buf[160];
    snprintf(buf, sizeof(buf), "Sampling %s  ·  %d points", s->orbital_name, s->num_samples);
    gtk_label_set_text(GTK_LABEL(v->load_title), buf);
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(v->load_bar), 0.0);
    gtk_widget_set_visible(v->load_box, TRUE);

    GenTask *t = g_new0(GenTask, 1);
    t->v = v;
    t->gen_id = id;
    t->t0_us = g_get_monotonic_time();
    t->req.spec = s->spec;
    t->req.count = s->num_samples;
    t->req.mode = (v->kind == VIEW_DENSITY) ? SAMPLE_DENSITY : SAMPLE_AMPLITUDE;
    t->req.seed = ((unsigned long long)g_random_int() << 32) | g_random_int();
    g_thread_unref(g_thread_new("hydrogen-sampler", gen_thread, t));
}

/* ------------------------------------------------------------------ */
/* GL callbacks                                                        */
/* ------------------------------------------------------------------ */

static void on_realize(GtkGLArea *area, gpointer data) {
    View *v = data;
    gtk_gl_area_make_current(area);
    if (gtk_gl_area_get_error(area) != NULL) return;
    if (!gladLoadGL()) {
        gtk_gl_area_set_error(area, g_error_new_literal(g_quark_from_static_string("hydrogen"), 1, "Failed to load OpenGL functions"));
        return;
    }
    if (!renderer_init(&v->renderer)) {
        gtk_gl_area_set_error(area, g_error_new_literal(g_quark_from_static_string("hydrogen"), 2,
                              "Failed to build the OpenGL 3.3 pipeline (see terminal output)"));
        return;
    }
    view_request_generation(v);
}

static void on_unrealize(GtkGLArea *area, gpointer data) {
    View *v = data;
    gtk_gl_area_make_current(area);
    if (gtk_gl_area_get_error(area) == NULL && v->renderer.ready) renderer_destroy(&v->renderer);
}

static void on_resize(GtkGLArea *area, int w, int h, gpointer data) {
    (void)area;
    View *v = data;
    if (!v->fitted && w > 0 && h > 0) {   /* frame the orbital once the real aspect ratio is known */
        v->fitted = 1;
        camera_fit(&v->cam, app_view_extent(v->app, v->kind) * 0.9f, (float)w / (float)h);
    }
    v->dirty = TRUE;
}

static void save_snapshot(View *v, int w, int h, int scale) {
    AppState *s = v->app;
    guchar *px = g_malloc((gsize)w * h * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px);
    cairo_surface_t *sf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    guchar *dst = cairo_image_surface_get_data(sf);
    int stride = cairo_image_surface_get_stride(sf);
    for (int y = 0; y < h; y++) {
        const guchar *src = px + (gsize)(h - 1 - y) * w * 4;
        guchar *o = dst + (gsize)y * stride;
        for (int x = 0; x < w; x++) { o[4 * x] = src[4 * x + 2]; o[4 * x + 1] = src[4 * x + 1]; o[4 * x + 2] = src[4 * x]; o[4 * x + 3] = 255; }
    }
    cairo_surface_mark_dirty(sf);
    cairo_t *cr = cairo_create(sf);
    cairo_scale(cr, scale, scale);
    HudState hs;
    fill_hud(v, &hs, w / scale, h / scale);
    hs.animating = 0;
    hud_draw(cr, &hs);
    cairo_destroy(cr);

    char name[200];
    snprintf(name, sizeof(name), "hydrogen_n%d_l%d_m%d%s_%s_%ld.png", s->spec.n, s->spec.l, s->spec.m,
             s->spec.complex_basis ? "c" : "", v->kind == VIEW_DENSITY ? "density" : "wave", (long)time(NULL));
    cairo_status_t st = cairo_surface_write_to_png(sf, name);
    char msg[260];
    snprintf(msg, sizeof(msg), st == CAIRO_STATUS_SUCCESS ? "Saved screenshot: %s" : "Could not save %s", name);
    app_set_status(s, msg);
    g_print("%s\n", msg);
    cairo_surface_destroy(sf);
    g_free(px);
}

static gboolean on_render(GtkGLArea *area, GdkGLContext *ctx, gpointer data) {
    (void)ctx;
    View *v = data;
    AppState *s = v->app;
    if (!v->renderer.ready) return TRUE;
    int scale = gtk_widget_get_scale_factor(GTK_WIDGET(area));
    int w = gtk_widget_get_width(GTK_WIDGET(area)) * scale;
    int h = gtk_widget_get_height(GTK_WIDGET(area)) * scale;
    GLint fbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fbo);

    camera_update_clip(&v->cam, app_view_extent(s, v->kind));
    RenderParams p;
    fill_render_params(v, &p);
    renderer_render(&v->renderer, &v->cam, &p, (GLuint)fbo, w, h);
    v->fps_frames++;

    if (v->snapshot_pending) {
        v->snapshot_pending = FALSE;
        save_snapshot(v, w, h, scale);
    }
    return TRUE;
}

static void on_hud_draw(GtkDrawingArea *area, cairo_t *cr, int w, int h, gpointer data) {
    (void)area;
    View *v = data;
    HudState hs;
    fill_hud(v, &hs, w, h);
    hud_draw(cr, &hs);
}

/* ------------------------------------------------------------------ */
/* frame loop: only redraw when something changes                      */
/* ------------------------------------------------------------------ */

static gboolean on_tick(GtkWidget *widget, GdkFrameClock *clock, gpointer data) {
    (void)widget;
    View *v = data;
    AppState *s = v->app;
    gint64 now = gdk_frame_clock_get_frame_time(clock);
    double dt = v->last_tick_us ? (double)(now - v->last_tick_us) * 1e-6 : 0.0;
    v->last_tick_us = now;
    if (dt > 0.1) dt = 0.1;

    int animating = 0;
    if (s->auto_rotate && v->drag_mode < 0 && (!s->link_cameras || v->kind == VIEW_DENSITY)) {
        camera_orbit(&v->cam, s->rotate_speed * (float)dt, 0.0f);
        view_camera_changed(v);
        animating = 1;
    } else if (s->auto_rotate && s->link_cameras) {
        animating = 1;   /* the linked camera is rotated by the other view */
    }
    if (s->phase_anim) {
        if (s->phase_last_us && now > s->phase_last_us)
            s->phase += s->phase_speed * (double)(now - s->phase_last_us) * 1e-6;
        if (now > s->phase_last_us) s->phase_last_us = now;
        animating = 1;
    } else {
        s->phase_last_us = now;
    }
    v->animating = animating;

    if (v->generating) {
        int done = atomic_load(&v->progress);
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(v->load_bar),
                                      v->gen_total > 0 ? (double)done / (double)v->gen_total : 0.0);
    }
    if (now - v->fps_t0_us >= 1000000) {
        v->fps = (double)v->fps_frames * 1e6 / (double)(now - v->fps_t0_us);
        v->fps_frames = 0;
        v->fps_t0_us = now;
    }
    if (animating || v->generating || v->dirty || v->snapshot_pending) {
        v->dirty = FALSE;
        gtk_gl_area_queue_render(GTK_GL_AREA(v->gl_area));
        gtk_widget_queue_draw(v->hud_area);
    }
    return G_SOURCE_CONTINUE;
}

/* ------------------------------------------------------------------ */
/* input                                                               */
/* ------------------------------------------------------------------ */

static void on_drag_begin(GtkGestureDrag *g, double x, double y, gpointer data) {
    (void)x; (void)y;
    View *v = data;
    GdkModifierType st = gtk_event_controller_get_current_event_state(GTK_EVENT_CONTROLLER(g));
    guint btn = gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(g));
    v->drag_mode = (btn == GDK_BUTTON_MIDDLE || btn == GDK_BUTTON_SECONDARY || (st & GDK_SHIFT_MASK)) ? 1 : 0;
    v->drag_yaw = v->cam.yaw;
    v->drag_pitch = v->cam.pitch;
    v->drag_target = v->cam.target;
    gtk_widget_grab_focus(v->gl_area);
}

static void on_drag_update(GtkGestureDrag *g, double dx, double dy, gpointer data) {
    (void)g;
    View *v = data;
    if (v->drag_mode == 1) {
        v->cam.target = v->drag_target;
        camera_pan_pixels(&v->cam, (float)dx, (float)dy, (float)gtk_widget_get_height(v->gl_area));
    } else if (v->drag_mode == 0) {
        v->cam.yaw = v->drag_yaw;
        v->cam.pitch = v->drag_pitch;
        camera_orbit(&v->cam, -(float)dx * 0.006f, (float)dy * 0.006f);
    }
    view_camera_changed(v);
}

static void on_drag_end(GtkGestureDrag *g, double dx, double dy, gpointer data) {
    (void)g; (void)dx; (void)dy;
    ((View *)data)->drag_mode = -1;
}

static gboolean on_scroll(GtkEventControllerScroll *c, double dx, double dy, gpointer data) {
    (void)c; (void)dx;
    View *v = data;
    camera_zoom_factor(&v->cam, expf((float)dy * 0.12f));
    view_camera_changed(v);
    return TRUE;
}

static void on_zoom_begin(GtkGesture *g, GdkEventSequence *seq, gpointer data) {
    (void)g; (void)seq;
    View *v = data;
    v->zoom_start = v->cam.distance;
}

static void on_zoom_scale(GtkGestureZoom *g, double scale, gpointer data) {
    (void)g;
    View *v = data;
    if (scale < 0.05) return;
    v->cam.distance = clampf(v->zoom_start / (float)scale, v->cam.min_distance, v->cam.max_distance);
    view_camera_changed(v);
}

/* ------------------------------------------------------------------ */
/* widget construction                                                 */
/* ------------------------------------------------------------------ */

GtkWidget *view_build_widget(View *v) {
    v->gl_area = gtk_gl_area_new();
    gtk_widget_set_hexpand(v->gl_area, TRUE);
    gtk_widget_set_vexpand(v->gl_area, TRUE);
    gtk_widget_set_focusable(v->gl_area, TRUE);
    gtk_gl_area_set_required_version(GTK_GL_AREA(v->gl_area), 3, 3);
    gtk_gl_area_set_allowed_apis(GTK_GL_AREA(v->gl_area), GDK_GL_API_GL);
    gtk_gl_area_set_has_depth_buffer(GTK_GL_AREA(v->gl_area), FALSE);
    gtk_gl_area_set_auto_render(GTK_GL_AREA(v->gl_area), FALSE);
    g_signal_connect(v->gl_area, "realize", G_CALLBACK(on_realize), v);
    g_signal_connect(v->gl_area, "unrealize", G_CALLBACK(on_unrealize), v);
    g_signal_connect(v->gl_area, "resize", G_CALLBACK(on_resize), v);
    g_signal_connect(v->gl_area, "render", G_CALLBACK(on_render), v);

    v->overlay = gtk_overlay_new();
    gtk_widget_set_hexpand(v->overlay, TRUE);
    gtk_widget_set_vexpand(v->overlay, TRUE);
    gtk_overlay_set_child(GTK_OVERLAY(v->overlay), v->gl_area);

    v->hud_area = gtk_drawing_area_new();
    gtk_widget_set_can_target(v->hud_area, FALSE);
    gtk_widget_set_hexpand(v->hud_area, TRUE);
    gtk_widget_set_vexpand(v->hud_area, TRUE);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(v->hud_area), on_hud_draw, v, NULL);
    gtk_overlay_add_overlay(GTK_OVERLAY(v->overlay), v->hud_area);

    v->load_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_add_css_class(v->load_box, "loading-card");
    gtk_widget_set_halign(v->load_box, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(v->load_box, GTK_ALIGN_START);
    gtk_widget_set_margin_top(v->load_box, 16);
    gtk_widget_set_can_target(v->load_box, FALSE);
    v->load_title = gtk_label_new("Sampling…");
    gtk_widget_add_css_class(v->load_title, "loading-title");
    v->load_bar = gtk_progress_bar_new();
    gtk_widget_set_size_request(v->load_bar, 260, -1);
    gtk_box_append(GTK_BOX(v->load_box), v->load_title);
    gtk_box_append(GTK_BOX(v->load_box), v->load_bar);
    gtk_overlay_add_overlay(GTK_OVERLAY(v->overlay), v->load_box);
    gtk_widget_set_visible(v->load_box, FALSE);

    GtkGesture *drag = gtk_gesture_drag_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(drag), 0);   /* any button */
    g_signal_connect(drag, "drag-begin", G_CALLBACK(on_drag_begin), v);
    g_signal_connect(drag, "drag-update", G_CALLBACK(on_drag_update), v);
    g_signal_connect(drag, "drag-end", G_CALLBACK(on_drag_end), v);
    gtk_widget_add_controller(v->gl_area, GTK_EVENT_CONTROLLER(drag));

    GtkEventController *scroll = gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);
    g_signal_connect(scroll, "scroll", G_CALLBACK(on_scroll), v);
    gtk_widget_add_controller(v->gl_area, scroll);

    GtkGesture *zoom = gtk_gesture_zoom_new();
    g_signal_connect(zoom, "begin", G_CALLBACK(on_zoom_begin), v);
    g_signal_connect(zoom, "scale-changed", G_CALLBACK(on_zoom_scale), v);
    gtk_widget_add_controller(v->gl_area, GTK_EVENT_CONTROLLER(zoom));

    gtk_widget_add_tick_callback(v->gl_area, on_tick, v, NULL);
    gtk_widget_set_tooltip_text(v->gl_area,
        "Drag: orbit   ·   Shift/right/middle-drag: pan   ·   Scroll or pinch: zoom");
    return v->overlay;
}

static gboolean on_wave_close(GtkWindow *w, gpointer data) {
    View *v = data;
    gtk_widget_set_visible(GTK_WIDGET(w), FALSE);
    if (v->app->ui.chk_wave_win) gtk_check_button_set_active(GTK_CHECK_BUTTON(v->app->ui.chk_wave_win), FALSE);
    return TRUE;   /* keep the window (and its GL resources) alive, just hide it */
}

void view_create_window(View *v, GtkApplication *app, const char *title, int w, int h) {
    v->window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(v->window), title);
    gtk_window_set_default_size(GTK_WINDOW(v->window), w, h);
    gtk_window_set_child(GTK_WINDOW(v->window), view_build_widget(v));
    g_signal_connect(v->window, "close-request", G_CALLBACK(on_wave_close), v);
}
