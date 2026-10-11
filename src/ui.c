#include "app.h"
#include "colors.h"
#include <stdio.h>
#include <string.h>

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
    s->phase_last_us = 0;
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

static void cb_snapshot(GtkButton *b, gpointer d) { (void)b; app_snapshot((AppState *)d); }
static void cb_regen(GtkButton *b, gpointer d) { (void)b; app_regenerate_now((AppState *)d); }

static void cb_samples(GtkSpinButton *sp, gpointer d) {
    AppState *s = d;
    s->num_samples = gtk_spin_button_get_value_as_int(sp);
    app_schedule_regenerate(s, 450);
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

    /* ---- Windows & export ---- */
    g = add_section(vbox, "Windows & export", TRUE);
    u->chk_wave_win = make_check("Show wavefunction window", TRUE, G_CALLBACK(cb_wave_window), s);
    add_wide(g, 0, u->chk_wave_win);
    add_wide(g, 1, make_button("Save screenshots as PNG  (S)", G_CALLBACK(cb_snapshot), s));

    GtkWidget *keys = gtk_label_new("Mouse: drag = orbit · Shift/right-drag = pan · wheel = zoom\n"
                                    "Keys: R/1 iso · 2 front · 3 side · 4 top · Space rotate · P phase\n"
                                    "G grid · A axes · N nucleus · B bloom · H info · S save PNG");
    gtk_widget_add_css_class(keys, "section-hint");
    gtk_label_set_xalign(GTK_LABEL(keys), 0.0f);
    gtk_box_append(GTK_BOX(vbox), keys);

    u->lbl_status = gtk_label_new("Ready");
    gtk_label_set_xalign(GTK_LABEL(u->lbl_status), 0.0f);
    gtk_label_set_wrap(GTK_LABEL(u->lbl_status), TRUE);
    gtk_box_append(GTK_BOX(vbox), u->lbl_status);

    sync_quantum_widgets(s);
    return scroll;
}
