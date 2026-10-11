#include "app.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* derived scene values                                                */
/* ------------------------------------------------------------------ */

float app_view_extent(const AppState *s, int kind) {
    /* |psi| sampling reaches further out than |psi|^2 sampling. */
    return (float)s->props.r_extent * (kind == VIEW_WAVE ? 1.3f : 1.0f);
}

float app_grid_step(const AppState *s, int kind) {
    return nice_step(app_view_extent(s, kind) / 6.0f);
}

void app_set_status(AppState *s, const char *text) {
    if (s->ui.lbl_status) gtk_label_set_text(GTK_LABEL(s->ui.lbl_status), text);
}

void app_mark_dirty(AppState *s) {
    for (int i = 0; i < 2; i++) view_mark_dirty(&s->views[i]);
}

static float view_aspect(View *v) {
    int w = v->gl_area ? gtk_widget_get_width(v->gl_area) : 0;
    int h = v->gl_area ? gtk_widget_get_height(v->gl_area) : 0;
    return (w > 0 && h > 0) ? (float)w / (float)h : 1.4f;
}

void app_reset_views(AppState *s, int preset) {
    for (int i = 0; i < 2; i++) {
        View *v = &s->views[i];
        camera_set_preset(&v->cam, (CameraPreset)preset);
        camera_update_clip(&v->cam, app_view_extent(s, i));
        camera_fit(&v->cam, app_view_extent(s, i) * 0.9f, view_aspect(v));
        v->dirty = TRUE;
    }
}

/* ------------------------------------------------------------------ */
/* regeneration                                                        */
/* ------------------------------------------------------------------ */

void app_regenerate_now(AppState *s) {
    if (s->regen_timer) { g_source_remove(s->regen_timer); s->regen_timer = 0; }
    for (int i = 0; i < 2; i++) view_request_generation(&s->views[i]);
}

static gboolean regen_cb(gpointer data) {
    AppState *s = data;
    s->regen_timer = 0;
    app_regenerate_now(s);
    return G_SOURCE_REMOVE;
}

void app_schedule_regenerate(AppState *s, guint delay_ms) {
    if (s->regen_timer) g_source_remove(s->regen_timer);
    s->regen_timer = g_timeout_add(delay_ms, regen_cb, s);
}

void app_apply_spec(AppState *s) {
    s->props = hydrogen_compute_props(&s->spec);
    hydrogen_orbital_name(&s->spec, s->orbital_name, sizeof(s->orbital_name));
    for (int i = 0; i < 2; i++) {
        View *v = &s->views[i];
        camera_update_clip(&v->cam, app_view_extent(s, i));
        camera_fit(&v->cam, app_view_extent(s, i) * 0.9f, view_aspect(v));
        v->dirty = TRUE;
    }
    ui_sync_info(s);
    app_schedule_regenerate(s, 120);
}

void app_snapshot(AppState *s) {
    for (int i = 0; i < 2; i++) s->views[i].snapshot_pending = TRUE;
}

/* ------------------------------------------------------------------ */
/* keyboard shortcuts                                                  */
/* ------------------------------------------------------------------ */

static void toggle(GtkWidget *chk) {
    if (chk) gtk_check_button_set_active(GTK_CHECK_BUTTON(chk), !gtk_check_button_get_active(GTK_CHECK_BUTTON(chk)));
}

static gboolean on_key(GtkEventControllerKey *c, guint key, guint code, GdkModifierType st, gpointer data) {
    (void)c; (void)code; (void)st;
    AppState *s = data;
    UiRefs *u = &s->ui;
    switch (key) {
    case GDK_KEY_r: case GDK_KEY_R: app_reset_views(s, CAMERA_VIEW_ISO); return TRUE;
    case GDK_KEY_1: app_reset_views(s, CAMERA_VIEW_ISO); return TRUE;
    case GDK_KEY_2: app_reset_views(s, CAMERA_VIEW_FRONT); return TRUE;
    case GDK_KEY_3: app_reset_views(s, CAMERA_VIEW_SIDE); return TRUE;
    case GDK_KEY_4: app_reset_views(s, CAMERA_VIEW_TOP); return TRUE;
    case GDK_KEY_g: toggle(u->chk_grid); return TRUE;
    case GDK_KEY_a: toggle(u->chk_axes); return TRUE;
    case GDK_KEY_n: toggle(u->chk_nucleus); return TRUE;
    case GDK_KEY_b: toggle(u->chk_bloom); return TRUE;
    case GDK_KEY_p: toggle(u->chk_phase); return TRUE;
    case GDK_KEY_h: toggle(u->chk_info); return TRUE;
    case GDK_KEY_space: toggle(u->chk_rotate); return TRUE;
    case GDK_KEY_s: app_snapshot(s); return TRUE;
    default: return FALSE;
    }
}

static void add_shortcuts(GtkWidget *window, AppState *s) {
    GtkEventController *k = gtk_event_controller_key_new();
    g_signal_connect(k, "key-pressed", G_CALLBACK(on_key), s);
    gtk_widget_add_controller(window, k);
}

/* ------------------------------------------------------------------ */
/* application                                                         */
/* ------------------------------------------------------------------ */

static gboolean on_main_close(GtkWindow *w, gpointer data) {
    (void)w;
    AppState *s = data;
    if (s->views[VIEW_WAVE].window) gtk_window_destroy(GTK_WINDOW(s->views[VIEW_WAVE].window));
    return FALSE;
}

/* Developer hook for automated screenshots: HYDROGEN_AUTOSHOT=1 saves both views and quits. */
static gboolean autoshot_cb(gpointer data) {
    AppState *s = data;
    static int stage = 0, wait = 0;
    const char *mode = getenv("HYDROGEN_AUTOSHOT");
    int ready = 1;
    for (int i = 0; i < 2; i++) {
        View *v = &s->views[i];
        if (v->window == NULL && i == VIEW_WAVE) continue;
        if (v->generating || v->point_count == 0) ready = 0;
    }
    if (s->regen_timer) ready = 0;
    if (!ready) return G_SOURCE_CONTINUE;
    if (stage == 0) { app_snapshot(s); stage = 1; wait = 0; return G_SOURCE_CONTINUE; }
    if (stage == 1 && ++wait > 6) {
        if (mode && mode[0] == '2') {
            /* drive the real widgets so the genuine signal handlers run */
            UiRefs *u = &s->ui;
            gtk_spin_button_set_value(GTK_SPIN_BUTTON(u->spin_n), 3);
            gtk_spin_button_set_value(GTK_SPIN_BUTTON(u->spin_l), 2);
            gtk_spin_button_set_value(GTK_SPIN_BUTTON(u->spin_m), 1);
            gtk_drop_down_set_selected(GTK_DROP_DOWN(u->dd_basis), 1);
            gtk_check_button_set_active(GTK_CHECK_BUTTON(u->chk_phase), TRUE);
            gtk_check_button_set_active(GTK_CHECK_BUTTON(u->chk_sphere), TRUE);
            s->scheme = 4;
            s->phase = 0.7;
            stage = 2; wait = 0;
            return G_SOURCE_CONTINUE;
        }
        g_application_quit(G_APPLICATION(s->app));
    }
    if (stage == 2) {
        if (++wait == 3) { gtk_check_button_set_active(GTK_CHECK_BUTTON(s->ui.chk_phase), FALSE); }
        if (wait == 4) { app_snapshot(s); stage = 3; wait = 0; }
        return G_SOURCE_CONTINUE;
    }
    if (stage == 3 && ++wait > 6) g_application_quit(G_APPLICATION(s->app));
    return G_SOURCE_CONTINUE;
}

static void activate(GtkApplication *app, gpointer data) {
    AppState *s = data;
    s->app = app;
    /* The 3D views are dark; match the sidebar. */
    g_object_set(gtk_settings_get_default(), "gtk-application-prefer-dark-theme", TRUE, NULL);

    s->spec = (OrbitalSpec){2, 1, 0, 1.0, 0};
    s->num_samples = 600000;
    s->scheme = 0; s->style = STYLE_GLOW;
    s->particle_size = 1.0f; s->brightness = 1.0f;
    s->bloom = TRUE; s->bloom_strength = 0.6f; s->nucleus_glow = 1.0f;
    s->show_grid = s->show_axes = s->show_nucleus = s->show_info = s->show_gizmo = s->show_legend = TRUE;
    s->show_sphere90 = s->show_bbox = FALSE;
    s->slice_mode = SLICE_OFF; s->slice_axis = 1; s->slice_pos = 0.0f; s->slice_thick = 0.05f;
    s->auto_rotate = FALSE; s->rotate_speed = 0.35f;
    s->phase_anim = FALSE; s->phase_speed = 2.0f;
    s->link_cameras = FALSE;

    const char *env = getenv("HYDROGEN_ORBITAL");   /* "n,l,m,complex,Z" (testing/automation) */
    if (env) {
        int n, l, m, c = 0, z = 1;
        if (sscanf(env, "%d,%d,%d,%d,%d", &n, &l, &m, &c, &z) >= 3 && n >= 1 && n <= HYDROGEN_MAX_N && l >= 0 && l < n && m >= -l && m <= l)
            s->spec = (OrbitalSpec){n, l, m, (double)(z < 1 ? 1 : z), c != 0};
    }
    if (getenv("HYDROGEN_SAMPLES")) s->num_samples = atoi(getenv("HYDROGEN_SAMPLES"));
    if (getenv("HYDROGEN_STYLE")) { s->style = atoi(getenv("HYDROGEN_STYLE")); }
    if (getenv("HYDROGEN_SCHEME")) { s->scheme = atoi(getenv("HYDROGEN_SCHEME")); }
    if (s->num_samples < 10000) s->num_samples = 10000;

    s->props = hydrogen_compute_props(&s->spec);
    hydrogen_orbital_name(&s->spec, s->orbital_name, sizeof(s->orbital_name));

    view_init(&s->views[VIEW_DENSITY], s, VIEW_DENSITY);
    view_init(&s->views[VIEW_WAVE], s, VIEW_WAVE);
    for (int i = 0; i < 2; i++) {
        camera_update_clip(&s->views[i].cam, app_view_extent(s, i));
        camera_fit(&s->views[i].cam, app_view_extent(s, i) * 0.9f, 1.4f);
    }

    GtkCssProvider *css = gtk_css_provider_new();
    gtk_css_provider_load_from_string(css,
        ".loading-card { background-color: rgba(8,12,22,0.88); border: 1px solid rgba(56,189,248,0.45);"
        "  border-radius: 12px; padding: 12px 18px; }"
        ".loading-title { color: #e8f6ff; font-weight: bold; }"
        ".section-hint { opacity: 0.65; font-size: 0.9em; }"
        ".prop-box { background-color: rgba(120,160,255,0.08); border-radius: 8px; padding: 8px 10px; }");
    gtk_style_context_add_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(css),
                                               GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css);

    /* main window: sidebar + density view */
    s->main_window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(s->main_window), "Hydrogen Atom Simulator — Electron Density |ψ|²");
    gtk_window_set_default_size(GTK_WINDOW(s->main_window), 1480, 900);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_append(GTK_BOX(box), ui_build_sidebar(s));
    gtk_box_append(GTK_BOX(box), view_build_widget(&s->views[VIEW_DENSITY]));
    gtk_window_set_child(GTK_WINDOW(s->main_window), box);
    g_signal_connect(s->main_window, "close-request", G_CALLBACK(on_main_close), s);
    add_shortcuts(s->main_window, s);

    /* second window: wavefunction */
    if (!getenv("HYDROGEN_SINGLE")) {
        view_create_window(&s->views[VIEW_WAVE], app, "Hydrogen Atom Simulator — Wavefunction ψ (amplitude & phase)", 940, 700);
        add_shortcuts(s->views[VIEW_WAVE].window, s);
        gtk_window_present(GTK_WINDOW(s->views[VIEW_WAVE].window));
    } else {
        gtk_widget_set_sensitive(s->ui.chk_wave_win, FALSE);
    }
    gtk_window_present(GTK_WINDOW(s->main_window));
    ui_sync_info(s);

    if (getenv("HYDROGEN_AUTOSHOT")) g_timeout_add(400, autoshot_cb, s);
}

int app_run(int argc, char **argv) {
    AppState *s = g_new0(AppState, 1);
    GtkApplication *app = gtk_application_new("org.example.hydrogensimulator", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), s);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;   /* AppState intentionally leaked: worker threads may still hold pointers during exit */
}
