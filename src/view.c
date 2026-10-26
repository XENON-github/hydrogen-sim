#include "app.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cairo.h>
#include <math.h>
#include <time.h>
#include "colors.h"

/* ------------------------------------------------------------------ */
/* parameter plumbing                                                  */
/* ------------------------------------------------------------------ */

static void fill_render_params(View *v, RenderParams *p, const FrameOverride *ov) {
    AppState *s = v->app;
    float ext = app_view_extent(s, v->kind);
    float step = app_grid_step(s, v->kind);
    const double t = ov ? ov->t : s->sim_t;
    const double tau = ov ? ov->tau : s->tr_tau;
    const double phase = ov ? ov->phase : s->phase;
    const int is_super = s->mode != MODE_STATIONARY;
    memset(p, 0, sizeof(*p));
    p->view_kind = v->kind;
    p->scheme = s->scheme;
    p->complex_basis = s->spec.complex_basis || is_super;
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
    p->phase = (float)fmod(phase, 2.0 * M_PI);
    p->slice_mode = s->slice_mode;
    p->slice_axis = s->slice_axis;
    p->slice_pos = s->slice_pos * ext;
    p->slice_half = fmaxf(0.002f * ext, s->slice_thick * ext);
    p->time_s = (float)(g_get_monotonic_time() % 100000000) * 1e-6f;
    p->render_mode = s->render_mode;

    SuperSpec sp;
    app_effective_spec_tau(s, &sp, tau);
    if (is_super && v->cloud_super && v->super_count > 0 && s->render_mode != RENDER_ISO)
        render_params_set_super(p, &sp, &v->super_res, t, phase, v->kind == VIEW_WAVE);
    if (s->render_mode != RENDER_PARTICLES && v->vol_ready && v->vol.K == sp.count) {
        int wheel = color_scheme_get(s->scheme)->wheel;
        render_params_set_iso(p, &sp, &v->vol, is_super ? t : 0.0, phase, s->iso_prob,
                              v->kind == VIEW_WAVE && !wheel, s->iso_opacity);
    }
}

static void fill_hud(View *v, HudState *h, SuperHud *sh, const Camera *cam, int w, int hgt, const FrameOverride *ov) {
    AppState *s = v->app;
    memset(h, 0, sizeof(*h));
    h->cam = cam;
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
    h->point_count = v->cloud_super ? v->super_count : v->point_count;
    h->n_pos = v->n_pos;
    h->n_neg = v->n_neg;
    h->generating = v->generating || v->vol_generating;
    h->phase_anim = s->phase_anim;
    h->phase = (float)(ov ? ov->phase : s->phase);
    h->iso_mode = (s->render_mode == RENDER_ISO);
    if (s->mode != MODE_STATIONARY) {
        app_fill_super_hud(s, sh, ov);
        h->sup = sh;
    }
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
    atomic_init(&v->vol_id, 0);
    atomic_init(&v->vol_progress, 0);
    v->cloud_req_version = v->cloud_version = v->vol_req_version = -1;
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
    int gen_id, version, super;
    SamplingRequest req;
    SuperSamplingRequest sreq;
    PointVertex *verts;
    SuperVertex *sverts;
    SamplingResult res;
    SuperSamplingResult sres;
    int ok;
    gint64 t0_us;
} GenTask;

static void update_load_box(View *v) {
    gtk_widget_set_visible(v->load_box, v->generating || v->vol_generating);
}

static void update_status(AppState *s) {
    char buf[256];
    View *d = &s->views[VIEW_DENSITY], *w = &s->views[VIEW_WAVE];
    if (s->render_mode == RENDER_ISO)
        snprintf(buf, sizeof(buf), "Isosurface mode  ·  volume %s", d->vol_ready ? "ready" : "building…");
    else
        snprintf(buf, sizeof(buf), "Density: %d pts (%.2f s)   ·   Wave: %d pts (%.2f s)",
                 d->cloud_super ? d->super_count : d->point_count, d->gen_seconds,
                 w->cloud_super ? w->super_count : w->point_count, w->gen_seconds);
    app_set_status(s, buf);
}

static gboolean on_gen_done(gpointer data) {
    GenTask *t = data;
    View *v = t->v;
    if (t->gen_id == atomic_load(&v->gen_id)) {
        if (t->ok && v->gl_area && gtk_widget_get_realized(v->gl_area) && v->renderer.ready) {
            gtk_gl_area_make_current(GTK_GL_AREA(v->gl_area));
            if (t->super) {
                renderer_set_super_points(&v->renderer, t->sverts, t->sres.count);
                v->super_count = t->sres.count;
                v->super_res = t->sres;
                v->cloud_super = 1;
            } else {
                renderer_set_points(&v->renderer, t->verts, t->res.count);
                v->point_count = t->res.count;
                v->n_pos = t->res.n_pos;
                v->n_neg = t->res.n_neg;
                v->cloud_super = 0;
            }
            v->cloud_version = t->version;
            v->gen_seconds = (double)(g_get_monotonic_time() - t->t0_us) * 1e-6;
        }
        v->generating = FALSE;
        update_load_box(v);
        v->dirty = TRUE;
        update_status(v->app);
    }
    free(t->verts);
    free(t->sverts);
    g_free(t);
    return G_SOURCE_REMOVE;
}

static gpointer gen_thread(gpointer data) {
    GenTask *t = data;
    if (t->super)
        t->ok = sampling_generate_super(&t->sreq, &t->sverts, &t->sres, &t->v->gen_id, t->gen_id, &t->v->progress) > 0;
    else
        t->ok = sampling_generate(&t->req, &t->verts, &t->res, &t->v->gen_id, t->gen_id, &t->v->progress) > 0;
    g_idle_add_full(G_PRIORITY_DEFAULT, on_gen_done, t, NULL);   /* not g_idle_add: that would starve behind frame painting */
    return NULL;
}

static void request_cloud(View *v) {
    AppState *s = v->app;
    v->cloud_req_version = s->data_version;
    int id = atomic_fetch_add(&v->gen_id, 1) + 1;
    atomic_store(&v->progress, 0);
    v->generating = TRUE;
    v->gen_total = s->num_samples;
    v->dirty = TRUE;

    char buf[160];
    snprintf(buf, sizeof(buf), "Sampling %s  ·  %d points", s->mode == MODE_STATIONARY ? s->orbital_name : "superposition", s->num_samples);
    gtk_label_set_text(GTK_LABEL(v->load_title), buf);
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(v->load_bar), 0.0);
    update_load_box(v);

    GenTask *t = g_new0(GenTask, 1);
    t->v = v;
    t->gen_id = id;
    t->version = s->data_version;
    t->t0_us = g_get_monotonic_time();
    unsigned long long seed = ((unsigned long long)g_random_int() << 32) | g_random_int();
    SampleMode mode = (v->kind == VIEW_DENSITY) ? SAMPLE_DENSITY : SAMPLE_AMPLITUDE;
    if (s->mode != MODE_STATIONARY) {
        t->super = 1;
        app_effective_spec(s, &t->sreq.spec, 0);
        t->sreq.count = s->num_samples;
        t->sreq.mode = mode;
        t->sreq.seed = seed;
    } else {
        t->req.spec = s->spec;
        t->req.count = s->num_samples;
        t->req.mode = mode;
        t->req.seed = seed;
    }
    g_thread_unref(g_thread_new("hydrogen-sampler", gen_thread, t));
}

typedef struct {
    View *v;
    int id, version;
    VolumeRequest req;
    Volume vol;
    int ok;
} VolTask;

static const int k_vol_res[3] = {64, 96, 128};

static gboolean on_vol_done(gpointer data) {
    VolTask *t = data;
    View *v = t->v;
    if (t->id == atomic_load(&v->vol_id)) {
        if (t->ok && v->gl_area && gtk_widget_get_realized(v->gl_area) && v->renderer.ready) {
            gtk_gl_area_make_current(GTK_GL_AREA(v->gl_area));
            renderer_set_volume(&v->renderer, &t->vol);
            v->vol = t->vol;
            v->vol.tex[0] = v->vol.tex[1] = NULL;     /* metadata only: the textures now live on the GPU */
            v->vol_ready = 1;
        }
        v->vol_generating = FALSE;
        update_load_box(v);
        v->dirty = TRUE;
        update_status(v->app);
    }
    volume_free(&t->vol);
    g_free(t);
    return G_SOURCE_REMOVE;
}

static gpointer vol_thread(gpointer data) {
    VolTask *t = data;
    t->ok = volume_generate(&t->req, &t->vol, &t->v->vol_id, t->id, &t->v->vol_progress);
    g_idle_add_full(G_PRIORITY_DEFAULT, on_vol_done, t, NULL);
    return NULL;
}

static void request_volume(View *v) {
    AppState *s = v->app;
    v->vol_req_version = s->data_version;
    int id = atomic_fetch_add(&v->vol_id, 1) + 1;
    atomic_store(&v->vol_progress, 0);
    v->vol_generating = TRUE;
    v->dirty = TRUE;
    int res = k_vol_res[s->iso_res_idx < 0 || s->iso_res_idx > 2 ? 1 : s->iso_res_idx];
    if (!v->generating) {
        char buf[96];
        snprintf(buf, sizeof(buf), "Building isosurface volume  ·  %d³", res);
        gtk_label_set_text(GTK_LABEL(v->load_title), buf);
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(v->load_bar), 0.0);
    }
    update_load_box(v);

    VolTask *t = g_new0(VolTask, 1);
    t->v = v;
    t->id = id;
    t->version = s->data_version;
    app_effective_spec(s, &t->req.spec, 0);
    t->req.res = res;
    t->req.half = (double)app_view_extent(s, v->kind) * 1.08;
    g_thread_unref(g_thread_new("hydrogen-volume", vol_thread, t));
}

void view_ensure_data(View *v) {
    AppState *s = v->app;
    if (!v->renderer.ready) return;
    if (s->render_mode != RENDER_ISO && v->cloud_req_version != s->data_version) request_cloud(v);
    if (s->render_mode != RENDER_PARTICLES && v->vol_req_version != s->data_version) request_volume(v);
}

void view_request_generation(View *v) {
    v->cloud_req_version = -1;
    v->vol_req_version = -1;
    view_ensure_data(v);
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
    SuperHud sh;
    fill_hud(v, &hs, &sh, &v->cam, w / scale, h / scale, NULL);
    hs.animating = 0;
    hud_draw(cr, &hs);
    cairo_destroy(cr);

    char name[200];
    if (s->mode == MODE_STATIONARY)
        snprintf(name, sizeof(name), "hydrogen_n%d_l%d_m%d%s_%s_%ld.png", s->spec.n, s->spec.l, s->spec.m,
                 s->spec.complex_basis ? "c" : "", v->kind == VIEW_DENSITY ? "density" : "wave", (long)time(NULL));
    else
        snprintf(name, sizeof(name), "hydrogen_%s_%s_%ld.png", s->mode == MODE_SUPER ? "superposition" : "transition",
                 v->kind == VIEW_DENSITY ? "density" : "wave", (long)time(NULL));
    cairo_status_t st = cairo_surface_write_to_png(sf, name);
    char msg[260];
    snprintf(msg, sizeof(msg), st == CAIRO_STATUS_SUCCESS ? "Saved screenshot: %s" : "Could not save %s", name);
    app_set_status(s, msg);
    g_print("%s\n", msg);
    cairo_surface_destroy(sf);
    g_free(px);
}

/* ---- video / GIF recording: renders deterministic frames into a private FBO ---- */

static void finish_recording(View *v, gboolean ok) {
    AppState *s = v->app;
    RecState *r = &s->rec;
    char msg[320];
    int closed = recorder_close(r->rec);
    r->rec = NULL;
    if (!ok || !closed) remove(r->path);
    if (r->fbo) { glDeleteFramebuffers(1, &r->fbo); r->fbo = 0; }
    if (r->tex) { glDeleteTextures(1, &r->tex); r->tex = 0; }
    g_free(r->buf);
    r->buf = NULL;
    r->active = FALSE;
    if (ok && closed) snprintf(msg, sizeof(msg), "Saved %s  (%d frames, %d×%d)", r->path, r->frame, r->w, r->h);
    else if (r->cancel) snprintf(msg, sizeof(msg), "Recording cancelled");
    else snprintf(msg, sizeof(msg), "Recording failed (disk full or ffmpeg error)");
    r->cancel = FALSE;
    app_set_status(s, msg);
    g_print("%s\n", msg);
    if (s->ui.btn_rec) gtk_button_set_label(GTK_BUTTON(s->ui.btn_rec), "● Start recording  (V)");
    if (s->ui.prog_rec) gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(s->ui.prog_rec), ok ? 1.0 : 0.0);
    v->dirty = TRUE;
}

static void record_frame(View *v, GLint screen_fbo, int ww, int wh) {
    AppState *s = v->app;
    RecState *r = &s->rec;
    if (r->cancel) { finish_recording(v, FALSE); return; }

    if (!r->fbo || r->fbo_w != r->w || r->fbo_h != r->h) {
        if (r->fbo) glDeleteFramebuffers(1, &r->fbo);
        if (r->tex) glDeleteTextures(1, &r->tex);
        glGenTextures(1, &r->tex);
        glBindTexture(GL_TEXTURE_2D, r->tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, r->w, r->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glGenFramebuffers(1, &r->fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, r->fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, r->tex, 0);
        r->fbo_w = r->w; r->fbo_h = r->h;
    }

    const double frac = (double)r->frame / (double)r->total;
    const int do_orbit = r->what != REC_WHAT_TIME, do_time = r->what != REC_WHAT_ORBIT;
    FrameOverride ov = { s->sim_t, s->phase, s->tr_tau };
    if (do_time) {
        ov.t = r->t_start + frac * r->t_span;
        ov.phase = s->phase + frac * r->phase_span;
        if (r->sweep_clip) ov.tau = frac * 1.5;
    }
    Camera cam = v->cam;
    if (do_orbit) cam.yaw = r->yaw0 + (float)(2.0 * M_PI * frac);
    camera_update_clip(&cam, app_view_extent(s, v->kind));

    RenderParams p;
    fill_render_params(v, &p, &ov);
    renderer_render(&v->renderer, &cam, &p, (GLuint)r->fbo, r->w, r->h);

    glBindFramebuffer(GL_FRAMEBUFFER, r->fbo);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, r->w, r->h, GL_RGBA, GL_UNSIGNED_BYTE, r->buf);
    cairo_surface_t *sf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, r->w, r->h);
    guchar *dst = cairo_image_surface_get_data(sf);
    int stride = cairo_image_surface_get_stride(sf);
    for (int y = 0; y < r->h; y++) {
        const guchar *src = r->buf + (gsize)(r->h - 1 - y) * r->w * 4;
        guchar *o = dst + (gsize)y * stride;
        for (int x = 0; x < r->w; x++) { o[4 * x] = src[4 * x + 2]; o[4 * x + 1] = src[4 * x + 1]; o[4 * x + 2] = src[4 * x]; o[4 * x + 3] = 255; }
    }
    cairo_surface_mark_dirty(sf);
    if (r->include_hud) {
        cairo_t *cr = cairo_create(sf);
        HudState hs;
        SuperHud sh;
        fill_hud(v, &hs, &sh, &cam, r->w, r->h, &ov);
        hs.hide_stats = 1;
        hs.animating = 0;
        hud_draw(cr, &hs);
        cairo_destroy(cr);
    }
    cairo_surface_flush(sf);
    int wrote = recorder_write(r->rec, cairo_image_surface_get_data(sf), stride);
    cairo_surface_destroy(sf);

    /* show what is being recorded (letter-boxed) in the live view */
    glBindFramebuffer(GL_READ_FRAMEBUFFER, r->fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)screen_fbo);
    glViewport(0, 0, ww, wh);
    glClearColor(0.02f, 0.02f, 0.03f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    double sc = fmin((double)ww / r->w, (double)wh / r->h);
    int dw = (int)(r->w * sc), dh = (int)(r->h * sc), dx = (ww - dw) / 2, dy = (wh - dh) / 2;
    glBlitFramebuffer(0, 0, r->w, r->h, dx, dy, dx + dw, dy + dh, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)screen_fbo);

    r->frame++;
    if (s->ui.prog_rec) gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(s->ui.prog_rec), (double)r->frame / (double)r->total);
    if (!wrote) finish_recording(v, FALSE);
    else if (r->frame >= r->total) finish_recording(v, TRUE);
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

    if (s->rec.active && s->rec.view == v->kind) {
        record_frame(v, fbo, w, h);
        return TRUE;
    }

    camera_update_clip(&v->cam, app_view_extent(s, v->kind));
    RenderParams p;
    fill_render_params(v, &p, NULL);
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
    SuperHud sh;
    fill_hud(v, &hs, &sh, &v->cam, w, h, NULL);
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
    if (app_advance(s, now)) animating = 1;      /* phase, simulated time, transition sweep: once per frame for both views */
    v->animating = animating;
    if (s->rec.active && s->rec.view == v->kind) animating = 1;

    if (v->generating) {
        int done = atomic_load(&v->progress);
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(v->load_bar),
                                      v->gen_total > 0 ? (double)done / (double)v->gen_total : 0.0);
    } else if (v->vol_generating) {
        int res = s->iso_res_idx == 0 ? 64 : s->iso_res_idx == 2 ? 128 : 96;
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(v->load_bar),
                                      (double)atomic_load(&v->vol_progress) / ((double)res * res * res));
    }
    if (now - v->fps_t0_us >= 1000000) {
        v->fps = (double)v->fps_frames * 1e6 / (double)(now - v->fps_t0_us);
        v->fps_frames = 0;
        v->fps_t0_us = now;
    }
    if (animating || v->generating || v->vol_generating || v->dirty || v->snapshot_pending) {
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
