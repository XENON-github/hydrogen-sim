#include "app.h"
#include "colors.h"
#include "platform.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

/* ------------------------------------------------------------------ */
/* small widget helpers                                                */
/* ------------------------------------------------------------------ */

static GtkWidget *add_section(GtkWidget *vbox, const char *title, gboolean open) {
    GtkWidget *ex = gtk_expander_new(NULL);
    char *esc = g_markup_escape_text(title, -1);
    char *markup = g_strdup_printf("<b>%s</b>", esc);
    GtkWidget *lbl = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(lbl), markup);
    g_free(markup);
    g_free(esc);
    gtk_expander_set_label_widget(GTK_EXPANDER(ex), lbl);
    gtk_expander_set_expanded(GTK_EXPANDER(ex), open);
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 7);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
    gtk_widget_set_margin_top(grid, 8);
    gtk_widget_set_margin_bottom(grid, 6);
    gtk_expander_set_child(GTK_EXPANDER(ex), grid);
    gtk_box_append(GTK_BOX(vbox), ex);
    return grid;
}

static void add_row(GtkWidget *grid, int row, const char *label, GtkWidget *w) {
    GtkWidget *l = gtk_label_new(label);
    gtk_widget_set_halign(l, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), l, 0, row, 1, 1);
    gtk_widget_set_hexpand(w, TRUE);
    gtk_grid_attach(GTK_GRID(grid), w, 1, row, 1, 1);
}

static void add_wide(GtkWidget *grid, int row, GtkWidget *w) {
    gtk_widget_set_hexpand(w, TRUE);
    gtk_grid_attach(GTK_GRID(grid), w, 0, row, 2, 1);
}

static GtkWidget *make_scale(double lo, double hi, double step, double val, int digits, GCallback cb, gpointer d) {
    GtkWidget *sc = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, lo, hi, step);
    gtk_range_set_value(GTK_RANGE(sc), val);
    gtk_scale_set_digits(GTK_SCALE(sc), digits);
    gtk_scale_set_draw_value(GTK_SCALE(sc), TRUE);
    gtk_scale_set_value_pos(GTK_SCALE(sc), GTK_POS_RIGHT);
    g_signal_connect(sc, "value-changed", cb, d);
    return sc;
}

static GtkWidget *make_check(const char *label, gboolean active, GCallback cb, gpointer d) {
    GtkWidget *c = gtk_check_button_new_with_label(label);
    gtk_check_button_set_active(GTK_CHECK_BUTTON(c), active);
    g_signal_connect(c, "toggled", cb, d);
    return c;
}

static GtkWidget *make_button(const char *label, GCallback cb, gpointer d) {
    GtkWidget *b = gtk_button_new_with_label(label);
    g_signal_connect(b, "clicked", cb, d);
    return b;
}

static GtkWidget *make_dropdown(const char *const *items, int selected, GCallback cb, gpointer d) {
    GtkWidget *dd = gtk_drop_down_new_from_strings(items);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(dd), (guint)selected);
    g_signal_connect(dd, "notify::selected", cb, d);
    return dd;
}

static GtkWidget *small_spin(double lo, double hi, double step, double val, int chars) {
    GtkWidget *sp = gtk_spin_button_new_with_range(lo, hi, step);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(sp), val);
    gtk_editable_set_width_chars(GTK_EDITABLE(sp), chars);
    return sp;
}

static GtkWidget *small_label(const char *t) {
    GtkWidget *l = gtk_label_new(t);
    gtk_widget_add_css_class(l, "section-hint");
    return l;
}

#define CB_CHECK(fn, field) \
    static void fn(GtkCheckButton *b, gpointer d) { AppState *s = d; s->field = gtk_check_button_get_active(b); app_mark_dirty(s); }
#define CB_SCALE(fn, field) \
    static void fn(GtkRange *r, gpointer d) { AppState *s = d; s->field = (float)gtk_range_get_value(r); app_mark_dirty(s); }
#define CB_DROP(fn, field) \
    static void fn(GObject *o, GParamSpec *p, gpointer d) { (void)p; AppState *s = d; s->field = (int)gtk_drop_down_get_selected(GTK_DROP_DOWN(o)); app_mark_dirty(s); }
#define CB_VIEW(fn, preset) \
    static void fn(GtkButton *b, gpointer d) { (void)b; app_reset_views((AppState *)d, preset); }

CB_CHECK(cb_grid, show_grid)
CB_CHECK(cb_axes, show_axes)
CB_CHECK(cb_nucleus, show_nucleus)
CB_CHECK(cb_sphere, show_sphere90)
CB_CHECK(cb_bbox, show_bbox)
CB_CHECK(cb_info, show_info)
CB_CHECK(cb_gizmo, show_gizmo)
CB_CHECK(cb_legend, show_legend)
CB_CHECK(cb_bloom, bloom)
CB_CHECK(cb_rotate, auto_rotate)
CB_SCALE(cb_size, particle_size)
CB_SCALE(cb_brightness, brightness)
CB_SCALE(cb_bloom_strength, bloom_strength)
CB_SCALE(cb_nucleus_glow, nucleus_glow)
CB_SCALE(cb_slice_pos, slice_pos)
CB_SCALE(cb_slice_thick, slice_thick)
CB_SCALE(cb_rotate_speed, rotate_speed)
CB_SCALE(cb_phase_speed, phase_speed)
CB_DROP(cb_scheme, scheme)
CB_DROP(cb_style, style)
CB_DROP(cb_slice_mode, slice_mode)
CB_DROP(cb_slice_axis, slice_axis)
CB_VIEW(cb_view_iso, CAMERA_VIEW_ISO)
CB_VIEW(cb_view_front, CAMERA_VIEW_FRONT)
CB_VIEW(cb_view_side, CAMERA_VIEW_SIDE)
CB_VIEW(cb_view_top, CAMERA_VIEW_TOP)

static void cb_phase(GtkCheckButton *b, gpointer d) {
    AppState *s = d;
    s->phase_anim = gtk_check_button_get_active(b);
    s->sim_last_us = 0;
    app_mark_dirty(s);
}

static void cb_link(GtkCheckButton *b, gpointer d) {
    AppState *s = d;
    s->link_cameras = gtk_check_button_get_active(b);
    if (s->link_cameras) {
        View *src = &s->views[VIEW_DENSITY], *dst = &s->views[VIEW_WAVE];
        dst->cam.target = src->cam.target; dst->cam.distance = src->cam.distance;
        dst->cam.yaw = src->cam.yaw; dst->cam.pitch = src->cam.pitch;
    }
    app_mark_dirty(s);
}

static void cb_wave_window(GtkCheckButton *b, gpointer d) {
    AppState *s = d;
    GtkWidget *w = s->views[VIEW_WAVE].window;
    if (!w) return;
    if (gtk_check_button_get_active(b)) gtk_window_present(GTK_WINDOW(w));
    else gtk_widget_set_visible(w, FALSE);
}

static void cb_noop_drop(GObject *o, GParamSpec *p, gpointer d) { (void)o; (void)p; (void)d; }
static void cb_noop_check(GtkCheckButton *b, gpointer d) { (void)b; (void)d; }

static void cb_snapshot(GtkButton *b, gpointer d) { (void)b; app_snapshot((AppState *)d); }
static void cb_regen(GtkButton *b, gpointer d) { (void)b; app_regenerate_now((AppState *)d); }

static void cb_samples(GtkSpinButton *sp, gpointer d) {
    AppState *s = d;
    s->num_samples = gtk_spin_button_get_value_as_int(sp);
    app_schedule_regenerate(s, 450);
}

/* ------------------------------------------------------------------ */
/* superposition, transitions, isosurface, export callbacks             */
/* ------------------------------------------------------------------ */

CB_SCALE(cb_iso_prob, iso_prob)
CB_SCALE(cb_iso_opacity, iso_opacity)
CB_SCALE(cb_tr_dur, tr_duration)
CB_SCALE(cb_time_speed, time_speed)
CB_CHECK(cb_tr_loop, tr_loop)
CB_CHECK(cb_arrow, show_dipole_arrow)

static void cb_render_mode(GObject *o, GParamSpec *p, gpointer d) {
    (void)p;
    AppState *s = d;
    if (s->ui.guard) return;
    s->render_mode = (int)gtk_drop_down_get_selected(GTK_DROP_DOWN(o));
    app_render_mode_changed(s);
}

static void cb_iso_res(GObject *o, GParamSpec *p, gpointer d) {
    (void)p;
    AppState *s = d;
    if (s->ui.guard) return;
    s->iso_res_idx = (int)gtk_drop_down_get_selected(GTK_DROP_DOWN(o));
    for (int i = 0; i < 2; i++) s->views[i].vol_req_version = -1;
    app_render_mode_changed(s);
}

static void cb_mode(GObject *o, GParamSpec *p, gpointer d) {
    (void)p;
    AppState *s = d;
    if (s->ui.guard) return;
    app_set_mode(s, (int)gtk_drop_down_get_selected(GTK_DROP_DOWN(o)));
}

static void cb_super_preset(GObject *o, GParamSpec *p, gpointer d) {
    (void)p;
    AppState *s = d;
    if (s->ui.guard) return;
    int idx = (int)gtk_drop_down_get_selected(GTK_DROP_DOWN(o)) - 1;     /* entry 0 is the "Load a preset…" placeholder */
    if (idx < 0 || idx >= super_preset_count()) return;
    s->super = super_preset(idx, s->spec.Z, s->spec.complex_basis);
    s->ui.guard++;
    gtk_drop_down_set_selected(GTK_DROP_DOWN(o), 0);
    s->ui.guard--;
    if (s->mode != MODE_SUPER) { app_set_mode(s, MODE_SUPER); return; }
    s->sim_t = 0.0;
    app_data_changed(s);
    app_sync_super_widgets(s);
}

static void cb_ncomp(GtkSpinButton *sp, gpointer d) {
    AppState *s = d;
    if (s->ui.guard) return;
    int n = gtk_spin_button_get_value_as_int(sp);
    if (n < 1) n = 1;
    if (n > SUPER_MAX) n = SUPER_MAX;
    for (int k = s->super.count; k < n; k++) {      /* new rows start on the first state not already used */
        for (int nn = 1; nn <= HYDROGEN_MAX_N; nn++)
            for (int ll = 0; ll < nn; ll++) {
                int used = 0;
                for (int j = 0; j < k; j++) if (s->super.c[j].n == nn && s->super.c[j].l == ll && s->super.c[j].m == 0) used = 1;
                if (!used) { s->super.c[k] = (SuperComponent){nn, ll, 0, 1.0, 0.0}; nn = HYDROGEN_MAX_N + 1; break; }
            }
    }
    s->super.count = n;
    app_data_changed(s);
    app_sync_super_widgets(s);
}

static int comp_index(GtkWidget *w) { return GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), "comp")); }

static void cb_comp_nlm(GtkSpinButton *sp, gpointer d) {
    AppState *s = d;
    UiRefs *u = &s->ui;
    if (u->guard) return;
    int k = comp_index(GTK_WIDGET(sp));
    int n = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(u->sp_n[k]));
    int l = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(u->sp_l[k]));
    int m = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(u->sp_m[k]));
    if (l > n - 1) l = n - 1;
    if (m > l) m = l;
    if (m < -l) m = -l;
    s->super.c[k].n = n; s->super.c[k].l = l; s->super.c[k].m = m;
    app_data_changed(s);
    app_sync_super_widgets(s);
}

static void cb_comp_amp(GtkRange *r, gpointer d) {
    AppState *s = d;
    if (s->ui.guard) return;
    s->super.c[comp_index(GTK_WIDGET(r))].amp = gtk_range_get_value(r);
    app_refresh_trace(s);
}

static void cb_comp_phase(GtkSpinButton *sp, gpointer d) {
    AppState *s = d;
    if (s->ui.guard) return;
    s->super.c[comp_index(GTK_WIDGET(sp))].phase = gtk_spin_button_get_value(sp) * M_PI / 180.0;
    app_refresh_trace(s);
}

static void cb_play(GtkCheckButton *b, gpointer d) {
    AppState *s = d;
    if (s->ui.guard) return;
    s->time_play = gtk_check_button_get_active(b);
    s->sim_last_us = 0;
    app_mark_dirty(s);
}

static void cb_time_reset(GtkButton *b, gpointer d) { (void)b; app_reset_time((AppState *)d); }

static void cb_final(GtkSpinButton *sp, gpointer d) {
    (void)sp;
    AppState *s = d;
    UiRefs *u = &s->ui;
    if (u->guard) return;
    int n = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(u->spin_fn));
    int l = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(u->spin_fl));
    int m = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(u->spin_fm));
    if (l > n - 1) l = n - 1;
    if (m > l) m = l;
    if (m < -l) m = -l;
    s->tr_final_n = n; s->tr_final_l = l; s->tr_final_m = m;
    s->tr_tau = 0.0;
    app_data_changed(s);
    app_sync_super_widgets(s);
}

static void cb_play_transition(GtkButton *b, gpointer d) {
    (void)b;
    AppState *s = d;
    if (s->mode != MODE_TRANSITION) { app_set_mode(s, MODE_TRANSITION); return; }
    s->tr_tau = 0.0;
    s->sim_t = 0.0;
    s->sim_last_us = 0;
    s->time_play = TRUE;
    app_sync_super_widgets(s);
    app_mark_dirty(s);
}

/* ---- energy-level diagram ---- */

static void energy_draw(GtkDrawingArea *area, cairo_t *cr, int w, int h, gpointer d) {
    AppState *s = d;
    GdkRGBA fg;
    gtk_widget_get_color(GTK_WIDGET(area), &fg);
    EnergyDiagram e;
    memset(&e, 0, sizeof(e));
    e.Z = s->spec.Z;
    e.mode = s->mode;
    e.init_n = s->spec.n; e.init_l = s->spec.l;
    e.final_n = s->tr_final_n; e.final_l = s->tr_final_l;
    e.has_final = (s->mode == MODE_TRANSITION && s->tr_final_n > 0);
    int nmax = 6;
    if (s->spec.n + 1 > nmax) nmax = s->spec.n + 1;
    if (s->tr_final_n + 1 > nmax) nmax = s->tr_final_n + 1;
    SuperSpec sp;
    app_effective_spec_tau(s, &sp, s->tr_tau);
    e.comp_count = s->mode == MODE_STATIONARY ? 0 : sp.count;
    for (int k = 0; k < e.comp_count; k++) {
        e.comp_n[k] = sp.c[k].n; e.comp_l[k] = sp.c[k].l;
        if (sp.c[k].n + 1 > nmax) nmax = sp.c[k].n + 1;
    }
    Complex c[SUPER_MAX];
    super_coeffs0(&sp, c);
    for (int k = 0; k < e.comp_count; k++) e.comp_w[k] = (float)(c[k].r * c[k].r + c[k].i * c[k].i);
    e.n_max = nmax > HYDROGEN_MAX_N ? HYDROGEN_MAX_N : nmax;
    e.progress = transition_sweep_theta(s->tr_tau > 1.0 ? 1.0 : s->tr_tau) / (0.5 * M_PI);
    e.hover = s->ui.hover;
    e.dark = (0.3 * fg.red + 0.6 * fg.green + 0.1 * fg.blue) > 0.5;
    hud_energy_diagram(cr, w, h, &e, &s->ui.layout);
}

static void toggle_component(AppState *s, int n, int l) {
    SuperSpec *sp = &s->super;
    for (int k = 0; k < sp->count; k++)
        if (sp->c[k].n == n && sp->c[k].l == l) {
            if (sp->count <= 1) return;
            for (int j = k; j + 1 < sp->count; j++) sp->c[j] = sp->c[j + 1];
            sp->count--;
            app_data_changed(s);
            app_sync_super_widgets(s);
            return;
        }
    if (sp->count >= SUPER_MAX) { app_set_status(s, "At most 4 states in a superposition: click a highlighted level to remove it first."); return; }
    sp->c[sp->count++] = (SuperComponent){n, l, 0, 1.0, 0.0};
    app_data_changed(s);
    app_sync_super_widgets(s);
}

static void energy_pressed(GtkGestureClick *g, int n_press, double x, double y, gpointer d) {
    (void)n_press;
    AppState *s = d;
    int idx = level_layout_hit(&s->ui.layout, x, y);
    if (idx < 0) return;
    int n = s->ui.layout.lv[idx].n, l = s->ui.layout.lv[idx].l;
    guint btn = gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(g));
    if (s->mode == MODE_SUPER) {
        if (btn == GDK_BUTTON_PRIMARY) toggle_component(s, n, l);
        return;
    }
    if (btn == GDK_BUTTON_SECONDARY) {
        s->tr_final_n = n; s->tr_final_l = l;
        if (s->tr_final_m > l) s->tr_final_m = l;
        if (s->tr_final_m < -l) s->tr_final_m = -l;
        s->tr_tau = 0.0;
        app_data_changed(s);
        app_sync_super_widgets(s);
    } else {
        s->spec.n = n; s->spec.l = l;
        if (s->spec.m > l) s->spec.m = l;
        if (s->spec.m < -l) s->spec.m = -l;
        app_apply_spec(s);
    }
}

static void energy_motion(GtkEventControllerMotion *c, double x, double y, gpointer d) {
    (void)c;
    AppState *s = d;
    int h = level_layout_hit(&s->ui.layout, x, y);
    if (h != s->ui.hover) { s->ui.hover = h; gtk_widget_queue_draw(s->ui.energy_area); }
}

static void energy_leave(GtkEventControllerMotion *c, gpointer d) {
    (void)c;
    AppState *s = d;
    s->ui.hover = -1;
    gtk_widget_queue_draw(s->ui.energy_area);
}

static void update_decay_label(AppState *s) {
    UiRefs *u = &s->ui;
    if (!u->lbl_decay) return;
    static const char *L = "spdfghik";
    DecayChannel ch[MAX_DECAY_CHANNELS];
    double tot = 0.0;
    int n = transition_decay_channels(s->spec.n, s->spec.l, s->spec.Z, ch, MAX_DECAY_CHANNELS, &tot);
    char buf[1800], line[200];
    snprintf(buf, sizeof(buf), "<b>Decay of %d%c</b> (spontaneous emission, E1)\n", s->spec.n, L[s->spec.l]);
    if (n == 0) {
        strncat(buf, s->spec.n == 1 ? "ground state: stable" : "no E1 decay channel: metastable (decays only via two-photon / slower processes)",
                sizeof(buf) - strlen(buf) - 1);
    } else {
        double tau = 1.0 / tot;
        snprintf(line, sizeof(line), "lifetime  τ = %.3g %s\n", tau >= 1e-6 ? tau * 1e6 : tau * 1e9, tau >= 1e-6 ? "µs" : "ns");
        strncat(buf, line, sizeof(buf) - strlen(buf) - 1);
        for (int i = 0; i < n; i++) {
            char lab[48] = "";
            if (s->spec.Z < 1.5) transition_line_label(s->spec.n, ch[i].nf, lab, sizeof(lab));
            snprintf(line, sizeof(line), "→ %d%c   %.4g nm   A = %.3g s⁻¹   (%.0f%%)  <small>%s</small>%s",
                     ch[i].nf, L[ch[i].lf], ch[i].wavelength_nm, ch[i].A, 100.0 * ch[i].branching, lab, i + 1 < n ? "\n" : "");
            strncat(buf, line, sizeof(buf) - strlen(buf) - 1);
        }
    }
    gtk_label_set_markup(GTK_LABEL(u->lbl_decay), buf);

    if (u->lbl_trans) {
        char t[400];
        if (s->tr_final_n < 1) snprintf(t, sizeof(t), "Pick a final state (right-click a level, or use the boxes).");
        else {
            int allowed = transition_l_allowed(s->spec.l, s->tr_final_l);
            snprintf(t, sizeof(t), "%d%c → %d%c : %s", s->spec.n, L[s->spec.l], s->tr_final_n, L[s->tr_final_l],
                     allowed ? "E1 allowed (Δl = ±1): the charge sloshes back and forth, so it radiates"
                             : "E1 forbidden: no oscillating dipole, so no one-photon emission (look at ⟨r⟩ ≈ 0)");
        }
        gtk_label_set_markup(GTK_LABEL(u->lbl_trans), t);
    }
}

/* ---- export ---- */

static void cb_record(GtkButton *b, gpointer d) {
    (void)b;
    AppState *s = d;
    UiRefs *u = &s->ui;
    if (s->rec.active) { app_cancel_recording(s); return; }
    static const int sizes[5][2] = {{640, 480}, {960, 720}, {1280, 720}, {1280, 960}, {1920, 1080}};
    int sz = (int)gtk_drop_down_get_selected(GTK_DROP_DOWN(u->dd_rec_size));
    if (sz < 0 || sz > 4) sz = 1;
    RecState *r = &s->rec;
    r->view = (int)gtk_drop_down_get_selected(GTK_DROP_DOWN(u->dd_rec_view));
    r->fmt = (RecFormat)gtk_drop_down_get_selected(GTK_DROP_DOWN(u->dd_rec_fmt));
    r->what = (int)gtk_drop_down_get_selected(GTK_DROP_DOWN(u->dd_rec_what));
    r->w = sizes[sz][0]; r->h = sizes[sz][1];
    r->fps = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(u->spin_rec_fps));
    r->total = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(u->spin_rec_secs));   /* seconds; converted to frames on start */
    r->cycles = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(u->spin_rec_cycles));
    r->include_hud = gtk_check_button_get_active(GTK_CHECK_BUTTON(u->chk_rec_hud));
    app_start_recording(s);
    if (s->rec.active) gtk_button_set_label(GTK_BUTTON(u->btn_rec), "■ Stop recording  (V)");
}

/* ------------------------------------------------------------------ */
/* keep widgets in sync with the model                                  */
/* ------------------------------------------------------------------ */

void app_sync_super_widgets(AppState *s) {
    UiRefs *u = &s->ui;
    if (!u->dd_mode) return;
    u->guard++;
    gtk_drop_down_set_selected(GTK_DROP_DOWN(u->dd_mode), (guint)s->mode);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(u->spin_ncomp), s->super.count);
    for (int k = 0; k < SUPER_MAX; k++) {
        gtk_widget_set_visible(u->comp_row[k], k < s->super.count);
        if (k >= s->super.count) continue;
        SuperComponent *c = &s->super.c[k];
        gtk_spin_button_set_range(GTK_SPIN_BUTTON(u->sp_l[k]), 0, c->n - 1);
        gtk_spin_button_set_range(GTK_SPIN_BUTTON(u->sp_m[k]), -c->l, c->l);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(u->sp_n[k]), c->n);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(u->sp_l[k]), c->l);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(u->sp_m[k]), c->m);
        gtk_range_set_value(GTK_RANGE(u->sc_amp[k]), c->amp);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(u->sp_phase[k]), c->phase * 180.0 / M_PI);
    }
    gtk_widget_set_sensitive(u->spin_ncomp, s->mode == MODE_SUPER);
    gtk_widget_set_sensitive(u->dd_super_preset, TRUE);
    gtk_check_button_set_active(GTK_CHECK_BUTTON(u->chk_play), s->time_play);
    gtk_widget_set_sensitive(u->chk_play, s->mode != MODE_STATIONARY);
    gtk_widget_set_sensitive(u->sc_time_speed, s->mode != MODE_STATIONARY);
    if (s->tr_final_n >= 1) {
        gtk_spin_button_set_range(GTK_SPIN_BUTTON(u->spin_fl), 0, s->tr_final_n - 1);
        gtk_spin_button_set_range(GTK_SPIN_BUTTON(u->spin_fm), -s->tr_final_l, s->tr_final_l);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(u->spin_fn), s->tr_final_n);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(u->spin_fl), s->tr_final_l);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(u->spin_fm), s->tr_final_m);
    }
    gtk_drop_down_set_selected(GTK_DROP_DOWN(u->dd_render), (guint)s->render_mode);
    u->guard--;
    update_decay_label(s);
    gtk_widget_queue_draw(u->energy_area);
}

/* ------------------------------------------------------------------ */
/* orbital selection                                                   */
/* ------------------------------------------------------------------ */

static void rebuild_presets(AppState *s) {
    UiRefs *u = &s->ui;
    int n = hydrogen_preset_count();
    static char names[64][96];
    const char *ptrs[65];
    for (int i = 0; i < n && i < 63; i++) {
        OrbitalPreset p = hydrogen_preset(i);
        OrbitalSpec sp = {p.n, p.l, p.m, 1.0, s->spec.complex_basis};
        hydrogen_orbital_name(&sp, names[i], sizeof(names[i]));
        ptrs[i] = names[i];
    }
    ptrs[n] = "Custom (n, l, m)";
    ptrs[n + 1] = NULL;
    GtkStringList *list = gtk_string_list_new(ptrs);
    gtk_drop_down_set_model(GTK_DROP_DOWN(u->dd_preset), G_LIST_MODEL(list));
    g_object_unref(list);
}

static void sync_preset_selection(AppState *s) {
    UiRefs *u = &s->ui;
    int idx = hydrogen_find_preset(s->spec.n, s->spec.l, s->spec.m);
    if (idx < 0) idx = hydrogen_preset_count();
    u->guard++;
    gtk_drop_down_set_selected(GTK_DROP_DOWN(u->dd_preset), (guint)idx);
    u->guard--;
}

static void sync_quantum_widgets(AppState *s) {
    UiRefs *u = &s->ui;
    u->guard++;
    gtk_spin_button_set_range(GTK_SPIN_BUTTON(u->spin_l), 0, s->spec.n - 1);
    gtk_spin_button_set_range(GTK_SPIN_BUTTON(u->spin_m), -s->spec.l, s->spec.l);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(u->spin_n), s->spec.n);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(u->spin_l), s->spec.l);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(u->spin_m), s->spec.m);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(u->spin_z), s->spec.Z);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(u->dd_basis), (guint)s->spec.complex_basis);
    u->guard--;
    sync_preset_selection(s);
}

static void cb_nlm(GtkSpinButton *sp, gpointer d) {
    (void)sp;
    AppState *s = d;
    UiRefs *u = &s->ui;
    if (u->guard) return;
    int n = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(u->spin_n));
    int l = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(u->spin_l));
    int m = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(u->spin_m));
    if (l > n - 1) l = n - 1;
    if (m > l) m = l;
    if (m < -l) m = -l;
    s->spec.n = n; s->spec.l = l; s->spec.m = m;
    sync_quantum_widgets(s);
    app_apply_spec(s);
}

static void cb_z(GtkSpinButton *sp, gpointer d) {
    AppState *s = d;
    if (s->ui.guard) return;
    s->spec.Z = gtk_spin_button_get_value(sp);
    app_apply_spec(s);
}

static void cb_basis(GObject *o, GParamSpec *p, gpointer d) {
    (void)p;
    AppState *s = d;
    if (s->ui.guard) return;
    s->spec.complex_basis = (int)gtk_drop_down_get_selected(GTK_DROP_DOWN(o));
    s->ui.guard++;
    rebuild_presets(s);
    s->ui.guard--;
    sync_preset_selection(s);
    app_apply_spec(s);
}

static void cb_preset(GObject *o, GParamSpec *p, gpointer d) {
    (void)p;
    AppState *s = d;
    if (s->ui.guard) return;
    int idx = (int)gtk_drop_down_get_selected(GTK_DROP_DOWN(o));
    if (idx < 0 || idx >= hydrogen_preset_count()) return;   /* "Custom" */
    OrbitalPreset pr = hydrogen_preset(idx);
    s->spec.n = pr.n; s->spec.l = pr.l; s->spec.m = pr.m;
    sync_quantum_widgets(s);
    app_apply_spec(s);
}

/* ------------------------------------------------------------------ */
/* properties panel + radial plot                                      */
/* ------------------------------------------------------------------ */

static void plot_draw(GtkDrawingArea *area, cairo_t *cr, int w, int h, gpointer d) {
    AppState *s = d;
    GdkRGBA fg;
    gtk_widget_get_color(GTK_WIDGET(area), &fg);
    int dark = (0.3 * fg.red + 0.6 * fg.green + 0.1 * fg.blue) > 0.5;
    hud_radial_plot(cr, w, h, &s->props, &s->spec, dark);
}

void ui_sync_info(AppState *s) {
    UiRefs *u = &s->ui;
    if (!u->lbl_props) return;
    const OrbitalProps *p = &s->props;
    char lmag[32], nodes[200] = "", buf[1200];
    if (s->spec.l == 0) snprintf(lmag, sizeof(lmag), "0");
    else if (s->spec.l == 1) snprintf(lmag, sizeof(lmag), "√2");
    else snprintf(lmag, sizeof(lmag), "%.3f", p->L_hbar);
    for (int i = 0; i < p->node_count; i++) {
        char t[24];
        snprintf(t, sizeof(t), "%s%.2f", i ? ", " : "  at r = ", p->node_r[i]);
        strncat(nodes, t, sizeof(nodes) - strlen(nodes) - 1);
    }
    if (p->node_count) strncat(nodes, " a₀", sizeof(nodes) - strlen(nodes) - 1);
    char lz[96];
    if (s->spec.complex_basis || s->spec.m == 0) snprintf(lz, sizeof(lz), "L<sub>z</sub> = %d ħ", s->spec.m);
    else snprintf(lz, sizeof(lz), "L<sub>z</sub> not sharp <small>(real orbital mixes ±m)</small>");
    snprintf(buf, sizeof(buf),
             "<b>%s</b>   <small>n=%d  l=%d  m=%+d  ·  Z=%.0f</small>\n"
             "<b>Energy</b>  E = %.3f eV   <small>(%d-fold degenerate)</small>\n"
             "<b>Mean radius</b>  ⟨r⟩ = %.2f a₀    <b>Peak</b>  r = %.2f a₀\n"
             "<b>90%% of probability</b> inside r = %.2f a₀\n"
             "<b>Angular momentum</b>  |L| = %s ħ,  %s\n"
             "<b>Nodes</b>  %d radial%s · %d angular",
             s->orbital_name, s->spec.n, s->spec.l, s->spec.m, s->spec.Z,
             p->energy_ev, p->degeneracy, p->r_expect, p->r_peak, p->r_90, lmag, lz,
             p->radial_nodes, nodes, p->angular_nodes);
    gtk_label_set_markup(GTK_LABEL(u->lbl_props), buf);
    gtk_widget_queue_draw(u->plot);
    sync_quantum_widgets(s);
    update_decay_label(s);
    gtk_widget_queue_draw(u->energy_area);
}

/* ------------------------------------------------------------------ */
/* sidebar                                                             */
/* ------------------------------------------------------------------ */

GtkWidget *ui_build_sidebar(AppState *s) {
    UiRefs *u = &s->ui;
    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(scroll, 400, -1);
    gtk_widget_set_hexpand(scroll, FALSE);
    gtk_widget_set_vexpand(scroll, TRUE);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_start(vbox, 14);
    gtk_widget_set_margin_end(vbox, 14);
    gtk_widget_set_margin_top(vbox, 12);
    gtk_widget_set_margin_bottom(vbox, 14);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), vbox);

    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title),
        "<span size='x-large' weight='bold'>Hydrogen Atom</span>\n"
        "<span alpha='70%'>ψₙₗₘ(r,θ,φ) = Rₙₗ(r) · Yₗᵐ(θ,φ)</span>");
    gtk_label_set_xalign(GTK_LABEL(title), 0.0f);
    gtk_box_append(GTK_BOX(vbox), title);

    /* ---- Orbital ---- */
    GtkWidget *g = add_section(vbox, "Orbital", TRUE);
    u->dd_preset = gtk_drop_down_new_from_strings((const char *const[]){"-", NULL});
    rebuild_presets(s);
    g_signal_connect(u->dd_preset, "notify::selected", G_CALLBACK(cb_preset), s);
    add_row(g, 0, "Preset", u->dd_preset);
    static const char *const basis[] = {"Real orbitals (chemistry, ± lobes)", "Complex eigenstates (Yₗᵐ, phase wheel)", NULL};
    u->dd_basis = make_dropdown(basis, s->spec.complex_basis, G_CALLBACK(cb_basis), s);
    add_row(g, 1, "Basis", u->dd_basis);
    u->spin_n = gtk_spin_button_new_with_range(1, HYDROGEN_MAX_N, 1);
    u->spin_l = gtk_spin_button_new_with_range(0, 7, 1);
    u->spin_m = gtk_spin_button_new_with_range(-7, 7, 1);
    u->spin_z = gtk_spin_button_new_with_range(1, 10, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(u->spin_n), s->spec.n);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(u->spin_l), s->spec.l);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(u->spin_m), s->spec.m);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(u->spin_z), s->spec.Z);
    g_signal_connect(u->spin_n, "value-changed", G_CALLBACK(cb_nlm), s);
    g_signal_connect(u->spin_l, "value-changed", G_CALLBACK(cb_nlm), s);
    g_signal_connect(u->spin_m, "value-changed", G_CALLBACK(cb_nlm), s);
    g_signal_connect(u->spin_z, "value-changed", G_CALLBACK(cb_z), s);
    add_row(g, 2, "Principal  n", u->spin_n);
    add_row(g, 3, "Angular  l", u->spin_l);
    add_row(g, 4, "Magnetic  m", u->spin_m);
    add_row(g, 5, "Nuclear charge  Z", u->spin_z);
    gtk_widget_set_tooltip_text(u->spin_z, "1 = hydrogen, 2 = He⁺, 3 = Li²⁺ … (hydrogen-like ions shrink as 1/Z)");

    /* ---- Properties ---- */
    g = add_section(vbox, "Physics", TRUE);
    u->lbl_props = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(u->lbl_props), 0.0f);
    gtk_label_set_wrap(GTK_LABEL(u->lbl_props), TRUE);
    gtk_widget_add_css_class(u->lbl_props, "prop-box");
    add_wide(g, 0, u->lbl_props);
    GtkWidget *cap = gtk_label_new("Radial probability  r²|R(r)|²   (orange: radial nodes · green: ⟨r⟩)");
    gtk_widget_add_css_class(cap, "section-hint");
    gtk_label_set_xalign(GTK_LABEL(cap), 0.0f);
    add_wide(g, 1, cap);
    u->plot = gtk_drawing_area_new();
    gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(u->plot), 150);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(u->plot), plot_draw, s, NULL);
    add_wide(g, 2, u->plot);

    /* ---- Superposition & time evolution ---- */
    g = add_section(vbox, "Superposition & time evolution", FALSE);
    static const char *const modes[] = {"Stationary orbital", "Superposition (time evolution)", "Transition animation", NULL};
    u->dd_mode = make_dropdown(modes, s->mode, G_CALLBACK(cb_mode), s);
    add_row(g, 0, "Mode", u->dd_mode);
    {
        const char *pn[16];
        int np = super_preset_count();
        pn[0] = "Load a preset…";
        for (int i = 0; i < np && i < 14; i++) pn[i + 1] = super_preset_name(i);
        pn[np + 1] = NULL;
        u->dd_super_preset = make_dropdown(pn, 0, G_CALLBACK(cb_super_preset), s);
        add_row(g, 1, "Preset", u->dd_super_preset);
    }
    u->spin_ncomp = small_spin(1, SUPER_MAX, 1, s->super.count, 3);
    g_signal_connect(u->spin_ncomp, "value-changed", G_CALLBACK(cb_ncomp), s);
    add_row(g, 2, "Number of states", u->spin_ncomp);
    for (int k = 0; k < SUPER_MAX; k++) {
        GtkWidget *col = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
        GtkWidget *r1 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5), *r2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
        char tag[16];
        snprintf(tag, sizeof(tag), "ψ%d", k + 1);
        GtkWidget *t = gtk_label_new(tag);
        gtk_widget_set_size_request(t, 26, -1);
        u->sp_n[k] = small_spin(1, HYDROGEN_MAX_N, 1, 1, 2);
        u->sp_l[k] = small_spin(0, 7, 1, 0, 2);
        u->sp_m[k] = small_spin(-7, 7, 1, 0, 2);
        u->sc_amp[k] = make_scale(0.0, 1.0, 0.01, 1.0, 2, G_CALLBACK(cb_comp_amp), s);
        u->sp_phase[k] = small_spin(-180, 180, 15, 0, 4);
        GtkWidget *w[5] = {u->sp_n[k], u->sp_l[k], u->sp_m[k], u->sc_amp[k], u->sp_phase[k]};
        for (int j = 0; j < 5; j++) g_object_set_data(G_OBJECT(w[j]), "comp", GINT_TO_POINTER(k));
        for (int j = 0; j < 3; j++) g_signal_connect(w[j], "value-changed", G_CALLBACK(cb_comp_nlm), s);
        g_signal_connect(u->sp_phase[k], "value-changed", G_CALLBACK(cb_comp_phase), s);
        gtk_box_append(GTK_BOX(r1), t);
        gtk_box_append(GTK_BOX(r1), small_label("n"));  gtk_box_append(GTK_BOX(r1), u->sp_n[k]);
        gtk_box_append(GTK_BOX(r1), small_label("l"));  gtk_box_append(GTK_BOX(r1), u->sp_l[k]);
        gtk_box_append(GTK_BOX(r1), small_label("m"));  gtk_box_append(GTK_BOX(r1), u->sp_m[k]);
        gtk_box_append(GTK_BOX(r2), gtk_label_new("   "));
        gtk_box_append(GTK_BOX(r2), small_label("amplitude"));
        gtk_widget_set_hexpand(u->sc_amp[k], TRUE);
        gtk_box_append(GTK_BOX(r2), u->sc_amp[k]);
        gtk_box_append(GTK_BOX(r2), small_label("phase°"));
        gtk_box_append(GTK_BOX(r2), u->sp_phase[k]);
        gtk_box_append(GTK_BOX(col), r1);
        gtk_box_append(GTK_BOX(col), r2);
        u->comp_row[k] = col;
        add_wide(g, 3 + k, col);
    }
    gtk_widget_set_tooltip_text(u->dd_mode,
        "Superposition: ψ(t) = Σ c_k e^(−iE_k t/ħ) ψ_k.  The cloud oscillates at the energy differences (beating).\n"
        "Transition: the electron is swept from the current orbital to the chosen final state.");
    u->chk_play = make_check("Play time evolution  (T)", s->time_play, G_CALLBACK(cb_play), s);
    add_wide(g, 7, u->chk_play);
    u->sc_time_speed = make_scale(0.1, 4.0, 0.05, s->time_speed, 2, G_CALLBACK(cb_time_speed), s);
    add_row(g, 8, "Time speed", u->sc_time_speed);
    {
        GtkWidget *hb = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        gtk_box_append(GTK_BOX(hb), make_button("Restart (t = 0)", G_CALLBACK(cb_time_reset), s));
        u->chk_arrow = make_check("Show ⟨r⟩ arrow", s->show_dipole_arrow, G_CALLBACK(cb_arrow), s);
        gtk_box_append(GTK_BOX(hb), u->chk_arrow);
        add_wide(g, 9, hb);
    }
    u->lbl_time = small_label("Time is shown in femtoseconds on the HUD. 1 a.u. of time = 0.0242 fs; the animation is slowed so the fastest beat takes ≈ 2.5 s.");
    gtk_label_set_wrap(GTK_LABEL(u->lbl_time), TRUE);
    gtk_label_set_xalign(GTK_LABEL(u->lbl_time), 0.0f);
    add_wide(g, 10, u->lbl_time);

    /* ---- Energy levels & transitions ---- */
    g = add_section(vbox, "Energy levels & transitions", TRUE);
    GtkWidget *eh = small_label("Left-click a level: initial state (in superposition mode: add / remove it).  Right-click: final state of a transition.");
    gtk_label_set_wrap(GTK_LABEL(eh), TRUE);
    gtk_label_set_xalign(GTK_LABEL(eh), 0.0f);
    add_wide(g, 0, eh);
    u->hover = -1;
    u->energy_area = gtk_drawing_area_new();
    gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(u->energy_area), 280);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(u->energy_area), energy_draw, s, NULL);
    {
        GtkGesture *click = gtk_gesture_click_new();
        gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), 0);
        g_signal_connect(click, "pressed", G_CALLBACK(energy_pressed), s);
        gtk_widget_add_controller(u->energy_area, GTK_EVENT_CONTROLLER(click));
        GtkEventController *motion = gtk_event_controller_motion_new();
        g_signal_connect(motion, "motion", G_CALLBACK(energy_motion), s);
        g_signal_connect(motion, "leave", G_CALLBACK(energy_leave), s);
        gtk_widget_add_controller(u->energy_area, motion);
    }
    add_wide(g, 1, u->energy_area);
    {
        GtkWidget *hb = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
        u->spin_fn = small_spin(1, HYDROGEN_MAX_N, 1, 1, 2);
        u->spin_fl = small_spin(0, 7, 1, 0, 2);
        u->spin_fm = small_spin(-7, 7, 1, 0, 2);
        g_signal_connect(u->spin_fn, "value-changed", G_CALLBACK(cb_final), s);
        g_signal_connect(u->spin_fl, "value-changed", G_CALLBACK(cb_final), s);
        g_signal_connect(u->spin_fm, "value-changed", G_CALLBACK(cb_final), s);
        gtk_box_append(GTK_BOX(hb), small_label("n"));  gtk_box_append(GTK_BOX(hb), u->spin_fn);
        gtk_box_append(GTK_BOX(hb), small_label("l"));  gtk_box_append(GTK_BOX(hb), u->spin_fl);
        gtk_box_append(GTK_BOX(hb), small_label("m"));  gtk_box_append(GTK_BOX(hb), u->spin_fm);
        add_row(g, 2, "Final state", hb);
    }
    add_row(g, 3, "Sweep duration (s)", make_scale(1.0, 12.0, 0.5, s->tr_duration, 1, G_CALLBACK(cb_tr_dur), s));
    u->chk_tr_loop = make_check("Loop the transition", s->tr_loop, G_CALLBACK(cb_tr_loop), s);
    add_wide(g, 4, u->chk_tr_loop);
    add_wide(g, 5, make_button("▶  Play / restart transition", G_CALLBACK(cb_play_transition), s));
    u->lbl_trans = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(u->lbl_trans), 0.0f);
    gtk_label_set_wrap(GTK_LABEL(u->lbl_trans), TRUE);
    add_wide(g, 6, u->lbl_trans);
    u->lbl_decay = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(u->lbl_decay), 0.0f);
    gtk_label_set_wrap(GTK_LABEL(u->lbl_decay), TRUE);
    gtk_widget_add_css_class(u->lbl_decay, "prop-box");
    add_wide(g, 7, u->lbl_decay);

    /* ---- Sampling ---- */
    g = add_section(vbox, "Sampling", FALSE);
    u->spin_samples = gtk_spin_button_new_with_range(10000, 4000000, 50000);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(u->spin_samples), s->num_samples);
    g_signal_connect(u->spin_samples, "value-changed", G_CALLBACK(cb_samples), s);
    add_row(g, 0, "Particles", u->spin_samples);
    add_wide(g, 1, make_button("Resample (new random seed)", G_CALLBACK(cb_regen), s));

    /* ---- Appearance ---- */
    g = add_section(vbox, "Appearance", TRUE);
    static const char *schemes[8];
    int ns = color_scheme_count();
    for (int i = 0; i < ns && i < 7; i++) schemes[i] = color_scheme_get(i)->name;
    schemes[ns] = NULL;
    add_row(g, 0, "Colours", make_dropdown(schemes, s->scheme, G_CALLBACK(cb_scheme), s));
    static const char *const styles[] = {"Glowing particles (additive)", "Solid spheres (lit)", NULL};
    add_row(g, 1, "Particle style", make_dropdown(styles, s->style, G_CALLBACK(cb_style), s));
    add_row(g, 2, "Particle size", make_scale(0.3, 3.0, 0.05, s->particle_size, 2, G_CALLBACK(cb_size), s));
    add_row(g, 3, "Brightness", make_scale(0.1, 4.0, 0.05, s->brightness, 2, G_CALLBACK(cb_brightness), s));
    u->chk_bloom = make_check("Bloom (HDR glow)", s->bloom, G_CALLBACK(cb_bloom), s);
    add_wide(g, 4, u->chk_bloom);
    add_row(g, 5, "Bloom strength", make_scale(0.0, 2.0, 0.05, s->bloom_strength, 2, G_CALLBACK(cb_bloom_strength), s));
    add_row(g, 6, "Nucleus glow", make_scale(0.0, 3.0, 0.05, s->nucleus_glow, 2, G_CALLBACK(cb_nucleus_glow), s));
    static const char *const rmodes[] = {"Particles", "Isosurface (ray-marched)", "Isosurface + particles", NULL};
    u->dd_render = make_dropdown(rmodes, s->render_mode, G_CALLBACK(cb_render_mode), s);
    gtk_widget_set_tooltip_text(u->dd_render, "Isosurface: the surface where |ψ| equals a level chosen to enclose the given fraction of the probability.\nIn the wavefunction view the lobes are coloured by sign (or by phase with the phase-wheel colours).");
    add_row(g, 7, "Render mode  (I)", u->dd_render);
    u->sc_iso_prob = make_scale(0.5, 0.99, 0.01, s->iso_prob, 2, G_CALLBACK(cb_iso_prob), s);
    add_row(g, 8, "Enclosed probability", u->sc_iso_prob);
    u->sc_iso_opacity = make_scale(0.1, 1.0, 0.01, s->iso_opacity, 2, G_CALLBACK(cb_iso_opacity), s);
    add_row(g, 9, "Surface opacity", u->sc_iso_opacity);
    static const char *const ires[] = {"Fast (64³)", "Balanced (96³)", "Fine (128³)", NULL};
    u->dd_iso_res = make_dropdown(ires, s->iso_res_idx, G_CALLBACK(cb_iso_res), s);
    add_row(g, 10, "Isosurface detail", u->dd_iso_res);

    /* ---- Cross-section ---- */
    g = add_section(vbox, "Cross-section", FALSE);
    static const char *const smodes[] = {"Off", "Half-space cut (look inside)", "Thin slab", NULL};
    static const char *const saxes[] = {"x  (plane ⟂ x)", "y  (plane ⟂ y)", "z  (plane ⟂ z)", NULL};
    add_row(g, 0, "Mode", make_dropdown(smodes, s->slice_mode, G_CALLBACK(cb_slice_mode), s));
    add_row(g, 1, "Axis", make_dropdown(saxes, s->slice_axis, G_CALLBACK(cb_slice_axis), s));
    add_row(g, 2, "Position", make_scale(-1.0, 1.0, 0.01, s->slice_pos, 2, G_CALLBACK(cb_slice_pos), s));
    add_row(g, 3, "Slab thickness", make_scale(0.01, 0.4, 0.005, s->slice_thick, 3, G_CALLBACK(cb_slice_thick), s));

    /* ---- Scene ---- */
    g = add_section(vbox, "Scene overlays", FALSE);
    u->chk_grid = make_check("Grid  (G)", s->show_grid, G_CALLBACK(cb_grid), s);
    u->chk_axes = make_check("Axes with a₀ ticks  (A)", s->show_axes, G_CALLBACK(cb_axes), s);
    u->chk_nucleus = make_check("Nucleus glow  (N)", s->show_nucleus, G_CALLBACK(cb_nucleus), s);
    u->chk_sphere = make_check("90 % probability sphere", s->show_sphere90, G_CALLBACK(cb_sphere), s);
    u->chk_bbox = make_check("Bounding box", s->show_bbox, G_CALLBACK(cb_bbox), s);
    u->chk_info = make_check("Info panel & stats  (H)", s->show_info, G_CALLBACK(cb_info), s);
    u->chk_gizmo = make_check("Orientation gizmo", s->show_gizmo, G_CALLBACK(cb_gizmo), s);
    u->chk_legend = make_check("Colour legend", s->show_legend, G_CALLBACK(cb_legend), s);
    GtkWidget *cks[8] = {u->chk_grid, u->chk_axes, u->chk_nucleus, u->chk_sphere, u->chk_bbox, u->chk_info, u->chk_gizmo, u->chk_legend};
    for (int i = 0; i < 8; i++) gtk_grid_attach(GTK_GRID(g), cks[i], i % 2, i / 2, 1, 1);

    /* ---- Animation & camera ---- */
    g = add_section(vbox, "Animation & camera", TRUE);
    u->chk_rotate = make_check("Auto-rotate  (Space)", s->auto_rotate, G_CALLBACK(cb_rotate), s);
    add_wide(g, 0, u->chk_rotate);
    add_row(g, 1, "Rotation speed", make_scale(0.05, 2.0, 0.05, s->rotate_speed, 2, G_CALLBACK(cb_rotate_speed), s));
    u->chk_phase = make_check("Animate phase  e^(−iEt/ħ)  (P)", s->phase_anim, G_CALLBACK(cb_phase), s);
    gtk_widget_set_tooltip_text(u->chk_phase, "Evolves the global phase of ψ: signs oscillate for real orbitals, colours cycle for complex ones");
    add_wide(g, 2, u->chk_phase);
    add_row(g, 3, "Phase speed", make_scale(0.1, 8.0, 0.1, s->phase_speed, 1, G_CALLBACK(cb_phase_speed), s));
    u->chk_link = make_check("Link both cameras", s->link_cameras, G_CALLBACK(cb_link), s);
    add_wide(g, 4, u->chk_link);
    GtkWidget *views = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_set_homogeneous(GTK_BOX(views), TRUE);
    gtk_box_append(GTK_BOX(views), make_button("Iso", G_CALLBACK(cb_view_iso), s));
    gtk_box_append(GTK_BOX(views), make_button("Front", G_CALLBACK(cb_view_front), s));
    gtk_box_append(GTK_BOX(views), make_button("Side", G_CALLBACK(cb_view_side), s));
    gtk_box_append(GTK_BOX(views), make_button("Top", G_CALLBACK(cb_view_top), s));
    add_wide(g, 5, views);

    /* ---- Video / GIF export ---- */
    g = add_section(vbox, "Video / GIF export", FALSE);
    static const char *const rv[] = {"Density |ψ|²  (main window)", "Wavefunction ψ  (second window)", NULL};
    u->dd_rec_view = make_dropdown(rv, s->rec.view, G_CALLBACK(cb_noop_drop), s);
    add_row(g, 0, "View", u->dd_rec_view);
    static const char *const rw[] = {"Orbit the camera (360° loop)", "Time evolution", "Orbit + time evolution", NULL};
    u->dd_rec_what = make_dropdown(rw, s->rec.what, G_CALLBACK(cb_noop_drop), s);
    add_row(g, 1, "Animate", u->dd_rec_what);
    static const char *const rf[] = {"GIF (built in)", "MP4 / H.264 (needs ffmpeg)", "WebM / VP9 (needs ffmpeg)", NULL};
    u->dd_rec_fmt = make_dropdown(rf, (int)s->rec.fmt, G_CALLBACK(cb_noop_drop), s);
    add_row(g, 2, "Format", u->dd_rec_fmt);
    static const char *const rs[] = {"640 × 480", "960 × 720", "1280 × 720", "1280 × 960", "1920 × 1080", NULL};
    u->dd_rec_size = make_dropdown(rs, 1, G_CALLBACK(cb_noop_drop), s);
    add_row(g, 3, "Size", u->dd_rec_size);
    u->spin_rec_fps = small_spin(10, 60, 5, s->rec.fps, 3);
    add_row(g, 4, "Frames per second", u->spin_rec_fps);
    u->spin_rec_secs = small_spin(1, 60, 1, s->rec.total, 3);
    add_row(g, 5, "Clip length (s)", u->spin_rec_secs);
    u->spin_rec_cycles = small_spin(1, 12, 1, s->rec.cycles, 3);
    gtk_widget_set_tooltip_text(u->spin_rec_cycles, "How many beat cycles (superposition) or phase cycles (stationary orbital) the clip covers.\nTwo-state superpositions loop seamlessly. A transition clip is always one full sweep.");
    add_row(g, 6, "Beat cycles in clip", u->spin_rec_cycles);
    u->chk_rec_hud = make_check("Include overlay (info, axes, legend)", s->rec.include_hud, G_CALLBACK(cb_noop_check), s);
    add_wide(g, 7, u->chk_rec_hud);
    u->btn_rec = make_button("● Start recording  (V)", G_CALLBACK(cb_record), s);
    add_wide(g, 8, u->btn_rec);
    u->prog_rec = gtk_progress_bar_new();
    add_wide(g, 9, u->prog_rec);
    u->lbl_rec = small_label(plat_have_ffmpeg() ? "ffmpeg found: MP4 and WebM are available. Files are saved in the working directory."
                                                : "ffmpeg not found on PATH: GIF only. Install ffmpeg to enable MP4 / WebM. Files are saved in the working directory.");
    gtk_label_set_wrap(GTK_LABEL(u->lbl_rec), TRUE);
    gtk_label_set_xalign(GTK_LABEL(u->lbl_rec), 0.0f);
    add_wide(g, 10, u->lbl_rec);

    /* ---- Windows & export ---- */
    g = add_section(vbox, "Windows & export", TRUE);
    u->chk_wave_win = make_check("Show wavefunction window", TRUE, G_CALLBACK(cb_wave_window), s);
    add_wide(g, 0, u->chk_wave_win);
    add_wide(g, 1, make_button("Save screenshots as PNG  (S)", G_CALLBACK(cb_snapshot), s));

    GtkWidget *keys = gtk_label_new("Mouse: drag = orbit · Shift/right-drag = pan · wheel = zoom\n"
                                    "Keys: R/1 iso · 2 front · 3 side · 4 top · Space rotate · P phase\n"
                                    "G grid · A axes · N nucleus · B bloom · H info · S save PNG\n"
                                    "T play time · I render mode · V record video/GIF");
    gtk_widget_add_css_class(keys, "section-hint");
    gtk_label_set_xalign(GTK_LABEL(keys), 0.0f);
    gtk_box_append(GTK_BOX(vbox), keys);

    u->lbl_status = gtk_label_new("Ready");
    gtk_label_set_xalign(GTK_LABEL(u->lbl_status), 0.0f);
    gtk_label_set_wrap(GTK_LABEL(u->lbl_status), TRUE);
    gtk_box_append(GTK_BOX(vbox), u->lbl_status);

    sync_quantum_widgets(s);
    app_sync_super_widgets(s);
    return scroll;
}
