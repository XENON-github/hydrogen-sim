#include "app.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

/* ------------------------------------------------------------------ */
/* derived scene values                                                */
/* ------------------------------------------------------------------ */

float app_view_extent(const AppState *s, int kind) {
    /* |psi| sampling reaches further out than |psi|^2 sampling. */
    return (float)s->r_extent_eff * (kind == VIEW_WAVE ? 1.3f : 1.0f);
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

void app_apply_spec(AppState *s) { app_data_changed(s); }

/* ------------------------------------------------------------------ */
/* superposition / transition model                                    */
/* ------------------------------------------------------------------ */

static int same_state(const SuperComponent *a, const SuperComponent *b) { return a->n == b->n && a->l == b->l && a->m == b->m; }

static void effective_spec_tau(const AppState *s, SuperSpec *out, double tau, int apply_sweep) {
    memset(out, 0, sizeof(*out));
    out->Z = s->spec.Z;
    out->complex_basis = s->spec.complex_basis;
    SuperComponent init = {s->spec.n, s->spec.l, s->spec.m, 1.0, 0.0};
    if (s->mode == MODE_STATIONARY) { out->count = 1; out->c[0] = init; return; }
    if (s->mode == MODE_TRANSITION) {
        SuperComponent fin = {s->tr_final_n, s->tr_final_l, s->tr_final_m, 1.0, -0.5 * M_PI};
        out->c[0] = init;
        if (same_state(&init, &fin)) { out->count = 1; return; }
        out->count = 2;
        out->c[1] = fin;
        if (apply_sweep) {
            double th = transition_sweep_theta(tau > 1.0 ? 1.0 : tau);
            out->c[0].amp = cos(th);
            out->c[1].amp = sin(th);
        }
        return;
    }
    for (int k = 0; k < s->super.count && out->count < SUPER_MAX; k++) {
        int dup = 0;
        for (int j = 0; j < out->count; j++) if (same_state(&out->c[j], &s->super.c[k])) dup = 1;
        if (!dup) out->c[out->count++] = s->super.c[k];
    }
    if (out->count == 0) { out->count = 1; out->c[0] = init; }
}

void app_effective_spec_tau(const AppState *s, SuperSpec *out, double tau) { effective_spec_tau(s, out, tau, 1); }

void app_effective_spec(const AppState *s, SuperSpec *out, int apply_sweep) { effective_spec_tau(s, out, s->tr_tau, apply_sweep); }

double app_time_rate(const AppState *s) {
    SuperSpec sp;
    effective_spec_tau(s, &sp, 0.0, 0);
    double w = super_max_omega(&sp);
    return (w > 0.0 ? 2.0 * M_PI / w : 40.0) / 2.5;     /* fastest beat takes 2.5 s at 1x */
}

void app_update_dipole(AppState *s) {
    SuperSpec sp;
    effective_spec_tau(s, &sp, 0.0, 0);
    if (s->mode == MODE_STATIONARY || sp.count < 2) { memset(&s->dip, 0, sizeof(s->dip)); s->dip.count = sp.count; s->dip_valid = 1; return; }
    super_dipole_matrix(&sp, &s->dip);
    s->dip_valid = 1;
}

static double trace_window(const AppState *s) {
    SuperSpec sp;
    effective_spec_tau(s, &sp, 0.0, 0);
    double w = super_max_omega(&sp);
    return w > 0.0 ? 3.0 * 2.0 * M_PI / w : 60.0;
}

static void dipole_at(const AppState *s, double t, double tau, double out[3]) {
    SuperSpec sp;
    Complex c[SUPER_MAX];
    effective_spec_tau(s, &sp, tau, 1);
    out[0] = out[1] = out[2] = 0.0;
    if (!s->dip_valid || s->dip.count != sp.count || sp.count < 2) return;
    super_coeffs_at(&sp, t, c);
    super_dipole_expect(&s->dip, c, out);
}

static void trace_prefill(AppState *s) {
    double W = trace_window(s);
    for (int i = 0; i < HUD_TRACE_N; i++) {
        double t = s->sim_t - W + W * i / (HUD_TRACE_N - 1.0), d[3];
        dipole_at(s, t, s->tr_tau, d);
        for (int a = 0; a < 3; a++) s->trace[3 * i + a] = (float)d[a];
    }
    s->trace_head = 0;
    s->trace_next_t = s->sim_t + W / (HUD_TRACE_N - 1.0);
}

void app_refresh_trace(AppState *s) { trace_prefill(s); app_mark_dirty(s); }

void app_reset_time(AppState *s) {
    s->sim_t = 0.0;
    s->tr_tau = 0.0;
    s->sim_last_us = 0;
    trace_prefill(s);
    app_mark_dirty(s);
}

int app_advance(AppState *s, gint64 now) {
    gint64 last = s->sim_last_us;
    if (last == 0 || now < last) { s->sim_last_us = now; return s->advancing; }
    if (now == last) return s->advancing;
    double dt = (double)(now - last) * 1e-6;
    if (dt > 0.1) dt = 0.1;
    s->sim_last_us = now;
    int anim = 0;
    if (s->phase_anim) { s->phase += s->phase_speed * dt; anim = 1; }
    if (s->mode != MODE_STATIONARY && s->time_play) {
        s->sim_t += dt * app_time_rate(s) * s->time_speed;
        anim = 1;
        if (s->mode == MODE_TRANSITION) {
            s->tr_tau += dt / (s->tr_duration > 0.3f ? s->tr_duration : 0.3f);
            if (s->tr_tau >= 1.5) {
                if (s->tr_loop) s->tr_tau -= 1.5;
                else {
                    s->tr_tau = 1.5;
                    s->time_play = FALSE;
                    if (s->ui.chk_play) gtk_check_button_set_active(GTK_CHECK_BUTTON(s->ui.chk_play), FALSE);
                }
            }
        }
        double W = trace_window(s), step = W / (HUD_TRACE_N - 1.0);
        int guard = 0;
        while (s->trace_next_t <= s->sim_t && guard++ < HUD_TRACE_N) {
            double d[3];
            dipole_at(s, s->trace_next_t, s->tr_tau, d);
            for (int a = 0; a < 3; a++) s->trace[3 * s->trace_head + a] = (float)d[a];
            s->trace_head = (s->trace_head + 1) % HUD_TRACE_N;
            s->trace_next_t += step;
        }
        if (guard >= HUD_TRACE_N) s->trace_next_t = s->sim_t + step;
    }
    s->advancing = anim;
    return anim;
}

void app_fill_super_hud(AppState *s, SuperHud *u, const FrameOverride *ov) {
    SuperSpec sp;
    double t = ov ? ov->t : s->sim_t, tau = ov ? ov->tau : s->tr_tau;
    effective_spec_tau(s, &sp, tau, 1);
    memset(u, 0, sizeof(*u));
    superhud_fill(u, &sp, s->dip_valid && s->dip.count == sp.count ? &s->dip : NULL, t, s->mode, tau > 1.0 ? 1.0 : tau);
    u->trace = s->trace;
    u->trace_head = s->trace_head;
    double peak = 0.0;
    for (int i = 0; i < HUD_TRACE_N * 3; i++) if (fabs(s->trace[i]) > peak) peak = fabs(s->trace[i]);
    u->trace_amp = (float)fmax(0.05, peak * 1.15);
    u->trace_span_fs = trace_window(s) * AU_TIME_FS;
    u->show_arrow = s->show_dipole_arrow && peak > 1e-3;
    double sc = peak > 1e-3 ? 0.3 * s->r_extent_eff / peak : 1.0;
    u->arrow_scale = (float)fmin(200.0, fmax(1.0, floor(sc)));
    if (s->mode == MODE_TRANSITION) {
        superhud_transition_line(u, s->spec.n, s->spec.l, s->tr_final_n, s->tr_final_l, s->spec.Z);
    } else if (sp.count == 2 && sp.c[0].n != sp.c[1].n) {
        int hi = sp.c[0].n > sp.c[1].n ? 0 : 1;
        superhud_transition_line(u, sp.c[hi].n, sp.c[hi].l, sp.c[1 - hi].n, sp.c[1 - hi].l, sp.Z);
    }
}

static void default_final_state(AppState *s) {
    DecayChannel ch[MAX_DECAY_CHANNELS];
    double tot;
    int n = transition_decay_channels(s->spec.n, s->spec.l, s->spec.Z, ch, MAX_DECAY_CHANNELS, &tot);
    int best = -1;
    for (int i = 0; i < n; i++) if (best < 0 || ch[i].A > ch[best].A) best = i;
    int fn, fl;
    if (best >= 0) { fn = ch[best].nf; fl = ch[best].lf; }
    else if (s->spec.l + 1 <= s->spec.n) { fn = s->spec.n + 1; fl = s->spec.l + 1; if (fn > HYDROGEN_MAX_N) { fn = s->spec.n; fl = s->spec.l > 0 ? s->spec.l - 1 : 1; } }
    else { fn = s->spec.n + 1; fl = s->spec.l - 1; }
    if (fn > HYDROGEN_MAX_N) fn = HYDROGEN_MAX_N;
    if (fl >= fn) fl = fn - 1;
    if (fl < 0) fl = 0;
    s->tr_final_n = fn; s->tr_final_l = fl;
    int m = s->spec.m;
    if (m > fl) m = fl;
    if (m < -fl) m = -fl;
    s->tr_final_m = m;
}

void app_set_mode(AppState *s, int mode) {
    s->mode = mode;
    if (mode == MODE_SUPER && s->super.count < 2) {
        s->super = super_preset(0, s->spec.Z, s->spec.complex_basis);
    }
    if (mode == MODE_TRANSITION) {
        SuperComponent a = {s->spec.n, s->spec.l, s->spec.m, 1, 0}, b = {s->tr_final_n, s->tr_final_l, s->tr_final_m, 1, 0};
        if (s->tr_final_n < 1 || same_state(&a, &b)) default_final_state(s);
    }
    s->sim_t = 0.0;
    s->tr_tau = 0.0;
    s->sim_last_us = 0;
    s->time_play = (mode != MODE_STATIONARY);
    app_data_changed(s);
    app_sync_super_widgets(s);
}

void app_data_changed(AppState *s) {
    s->props = hydrogen_compute_props(&s->spec);
    hydrogen_orbital_name(&s->spec, s->orbital_name, sizeof(s->orbital_name));
    SuperSpec sp;
    effective_spec_tau(s, &sp, 0.0, 0);
    double e = s->props.r_extent;
    if (s->mode != MODE_STATIONARY) {
        e = 0.0;
        for (int k = 0; k < sp.count; k++) {
            OrbitalSpec o = super_component_spec(&sp, k);
            OrbitalProps pr = hydrogen_compute_props(&o);
            if (pr.r_extent > e) e = pr.r_extent;
        }
    }
    s->r_extent_eff = e;
    s->data_version++;
    app_update_dipole(s);
    trace_prefill(s);
    for (int i = 0; i < 2; i++) {
        View *v = &s->views[i];
        camera_update_clip(&v->cam, app_view_extent(s, i));
        camera_fit(&v->cam, app_view_extent(s, i) * 0.9f, view_aspect(v));
        v->dirty = TRUE;
    }
    ui_sync_info(s);
    app_schedule_regenerate(s, 120);
}

void app_render_mode_changed(AppState *s) {
    for (int i = 0; i < 2; i++) view_ensure_data(&s->views[i]);
    app_mark_dirty(s);
}

/* ------------------------------------------------------------------ */
/* video / GIF recording                                               */
/* ------------------------------------------------------------------ */

void app_cancel_recording(AppState *s) { if (s->rec.active) s->rec.cancel = TRUE; }

void app_start_recording(AppState *s) {
    RecState *r = &s->rec;
    if (r->active) return;
    View *v = &s->views[r->view];
    if (r->view == VIEW_WAVE && (!v->window || !gtk_widget_get_visible(v->window))) {
        app_set_status(s, "Show the wavefunction window first (Windows & export), or record the density view.");
        return;
    }
    if (!v->renderer.ready) { app_set_status(s, "The view is not ready yet."); return; }
    char err[160];
    const char *what = s->mode == MODE_STATIONARY ? s->orbital_name : "superposition";
    char base[160];
    snprintf(base, sizeof(base), "hydrogen_%s_%s", what, r->view == VIEW_DENSITY ? "density" : "wave");
    for (char *c = base; *c; c++) if (!((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9') || *c == '_' || *c == '-')) *c = '_';
    snprintf(r->path, sizeof(r->path), "%s_%ld.%s", base, (long)time(NULL), recorder_extension(r->fmt));
    r->rec = recorder_open(r->fmt, r->path, r->w, r->h, r->fps, GIF_PIX_BGRA, err, sizeof(err));
    if (!r->rec) { char m[220]; snprintf(m, sizeof(m), "Cannot start recording: %s", err); app_set_status(s, m); return; }
    r->w = recorder_width(r->rec); r->h = recorder_height(r->rec);
    r->frame = 0;
    r->total = r->fps * (r->total > 0 ? r->total : 1);        /* UI stores seconds in `total` until now */
    r->yaw0 = v->cam.yaw;
    r->t_start = s->sim_t;
    r->sweep_clip = 0; r->phase_span = 0.0; r->t_span = 0.0;
    SuperSpec sp;
    effective_spec_tau(s, &sp, 0.0, 0);
    double w = super_max_omega(&sp);
    if (s->mode == MODE_TRANSITION) { r->sweep_clip = 1; r->t_span = 1.5 * s->tr_duration * app_time_rate(s) * s->time_speed; }
    else if (s->mode == MODE_SUPER && w > 0.0) r->t_span = r->cycles * 2.0 * M_PI / w;
    else r->phase_span = r->cycles * 2.0 * M_PI;
    r->buf = g_malloc((gsize)r->w * r->h * 4);
    r->cancel = FALSE;
    r->active = TRUE;
    char m[260];
    snprintf(m, sizeof(m), "Recording %d frames to %s …", r->total, r->path);
    app_set_status(s, m);
    view_mark_dirty(v);
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
    case GDK_KEY_t: case GDK_KEY_T: if (s->mode != MODE_STATIONARY) toggle(u->chk_play); return TRUE;
    case GDK_KEY_i: case GDK_KEY_I:
        gtk_drop_down_set_selected(GTK_DROP_DOWN(u->dd_render), (guint)((s->render_mode + 1) % 3));
        return TRUE;
    case GDK_KEY_v: case GDK_KEY_V: g_signal_emit_by_name(u->btn_rec, "clicked"); return TRUE;
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
        if (v->generating || v->vol_generating) ready = 0;
        if (s->render_mode != RENDER_ISO && v->point_count == 0 && v->super_count == 0) ready = 0;
        if (s->render_mode != RENDER_PARTICLES && !v->vol_ready) ready = 0;
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
    s->mode = MODE_STATIONARY;
    s->time_speed = 1.0f; s->time_play = FALSE;
    s->tr_final_n = 0; s->tr_duration = 4.0f; s->tr_loop = TRUE;
    s->show_dipole_arrow = TRUE;
    s->render_mode = RENDER_PARTICLES; s->iso_prob = 0.90f; s->iso_opacity = 0.85f; s->iso_res_idx = 1;
    s->rec = (RecState){0};
    s->rec.view = VIEW_DENSITY; s->rec.w = 960; s->rec.h = 720; s->rec.fps = 30; s->rec.total = 6; s->rec.cycles = 2;
    s->rec.what = REC_WHAT_ORBIT; s->rec.fmt = REC_GIF; s->rec.include_hud = 1;

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
    s->r_extent_eff = s->props.r_extent;
    s->super = super_preset(0, s->spec.Z, s->spec.complex_basis);
    if (getenv("HYDROGEN_MODE")) {            /* testing/automation: 0 stationary, 1 superposition, 2 transition */
        int m = atoi(getenv("HYDROGEN_MODE"));
        if (m >= 0 && m <= 2) s->mode = m;
    }
    if (getenv("HYDROGEN_RENDER")) { int m = atoi(getenv("HYDROGEN_RENDER")); if (m >= 0 && m <= 2) s->render_mode = m; }
    if (s->mode == MODE_TRANSITION) default_final_state(s);
    if (s->mode != MODE_STATIONARY) { s->time_play = TRUE; }
    {
        SuperSpec sp0;
        effective_spec_tau(s, &sp0, 0.0, 0);
        double e0 = 0.0;
        for (int k = 0; k < sp0.count; k++) { OrbitalSpec o = super_component_spec(&sp0, k); OrbitalProps pr = hydrogen_compute_props(&o); if (pr.r_extent > e0) e0 = pr.r_extent; }
        s->r_extent_eff = e0;
        app_update_dipole(s);
        trace_prefill(s);
    }

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
