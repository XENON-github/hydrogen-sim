/* Scenes for superposition particles, isosurfaces and the energy-level diagram. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "scene.h"

static SuperSpec single(OrbitalSpec o) {
    SuperSpec s; memset(&s, 0, sizeof(s));
    s.count = 1; s.Z = o.Z; s.complex_basis = o.complex_basis;
    s.c[0] = (SuperComponent){o.n, o.l, o.m, 1.0, 0.0};
    return s;
}

static void base(Scene *s, int kind, float ext) {
    memset(&s->p, 0, sizeof(s->p));
    s->view_kind = kind;
    float step = nice_step(ext / 6.0f);
    s->p.view_kind = kind; s->p.extent = ext;
    s->p.particle_size = 1.0f; s->p.brightness = 1.0f;
    s->p.bloom = 1; s->p.bloom_strength = 0.6f;
    s->p.show_grid = 1; s->p.grid_step = step; s->p.grid_half = ceilf(ext * 1.15f / (5.0f * step)) * 5.0f * step;
    s->p.show_nucleus = 1; s->p.nucleus_glow = 1.0f; s->p.slice_axis = 1;
    camera_set_preset(&s->cam, CAMERA_VIEW_ISO);
    camera_update_clip(&s->cam, ext);
}

static double max_extent(const SuperSpec *sp) {
    double e = 0;
    for (int k = 0; k < sp->count; k++) { OrbitalSpec o = super_component_spec(sp, k); OrbitalProps pr = hydrogen_compute_props(&o); if (pr.r_extent > e) e = pr.r_extent; }
    return e;
}

static int build_volume(Scene *s, const SuperSpec *sp, double half, int res, Volume *v) {
    VolumeRequest rq = { *sp, res, half };
    if (!volume_generate(&rq, v, NULL, 0, NULL)) return 0;
    renderer_set_volume(&s->r, v);
    return 1;
}

static SuperSpec g_spec;
static double g_t;
static int g_super_draw, g_wave;
static SuperSamplingResult g_res;
static float g_yaw0;
static Volume g_vol;
static int g_iso_anim, g_trans, g_diagram, g_wheel;
static int g_init_n, g_init_l, g_fin_n, g_fin_l;
static double g_span, g_prog;

int scene_setup_extra(Scene *s, const char *name, int w, int h) {
    int wave = strstr(name, "wave") != NULL;
    SuperSpec sp;
    double t = 0.0, phase = 0.0;
    int res = 96;
    if (!strncmp(name, "super_", 6) || !strncmp(name, "isosup_", 7)) {
        int idx = !strncmp(name, "isosup_", 7) ? 0 : 0;
        sp = super_preset(idx, 1.0, 0);
        if (strstr(name, "2s2p")) sp = super_preset(1, 1.0, 0);
        if (strstr(name, "rot")) sp = super_preset(4, 1.0, 1);
        if (strstr(name, "three")) sp = super_preset(5, 1.0, 0);
        double w_ = super_max_omega(&sp);
        t = w_ > 0 ? 0.5 * M_PI / w_ * (strstr(name, "t2") ? 2.0 : 1.0) : 0;   /* quarter period (or 3/4) */
        if (strstr(name, "t0")) t = 0;
        s->spec = (OrbitalSpec){sp.c[0].n, sp.c[0].l, sp.c[0].m, 1, sp.complex_basis};
        s->props = hydrogen_compute_props(&s->spec);
        s->props.r_extent = max_extent(&sp);
        double e = s->props.r_extent * (wave ? 1.3 : 1.0);
        base(s, wave ? VIEW_WAVE : VIEW_DENSITY, (float)e);
        s->p.complex_basis = 1;
        snprintf(s->name, sizeof(s->name), "superposition");
        if (!strncmp(name, "super_", 6)) {
            SuperSamplingRequest rq = { sp, 700000, wave ? SAMPLE_AMPLITUDE : SAMPLE_DENSITY, 7 };
            SuperVertex *v; SuperSamplingResult res_;
            if (!sampling_generate_super(&rq, &v, &res_, NULL, 0, NULL)) return 0;
            renderer_set_super_points(&s->r, v, res_.count);
            render_params_set_super(&s->p, &sp, &res_, t, phase, wave);
            g_res = res_; g_wave = wave;
            free(v);
        }
        if (!strncmp(name, "isosup_", 7)) {
            Volume vol; if (!build_volume(s, &sp, e * 1.08, res, &vol)) return 0;
            s->p.render_mode = RENDER_ISO;
            render_params_set_iso(&s->p, &sp, &vol, t, phase, 0.9, wave && 0, 0.9f);
            if (wave) { s->p.iso_signed = 1; }
            g_vol = vol; g_vol.tex[0] = g_vol.tex[1] = NULL;      /* metadata only: textures are on the GPU */
            g_iso_anim = 1; g_wheel = strstr(name, "wheel") != NULL; g_wave = wave;
            volume_free(&vol);
        }
        s->p.scheme = strstr(name, "wheel") ? 4 : 0;
        g_spec = sp; g_t = t; g_super_draw = 1;
        camera_fit(&s->cam, s->p.extent * 0.9f, (float)w / h);
        g_yaw0 = s->cam.yaw;
        return 1;
    }
    if (!strncmp(name, "trans_", 6)) {
        int fn = 2, fl = 0, in = 3, il = 1;
        if (strstr(name, "2p1s")) { in = 2; il = 1; fn = 1; fl = 0; }
        if (strstr(name, "2s1s")) { in = 2; il = 0; fn = 1; fl = 0; }
        if (strstr(name, "4f3d")) { in = 4; il = 3; fn = 3; fl = 2; }
        sp.count = 2; sp.Z = 1.0; sp.complex_basis = 0;
        sp.c[0] = (SuperComponent){in, il, 0, 1.0, 0.0};
        sp.c[1] = (SuperComponent){fn, fl, 0, 1.0, -0.5 * M_PI};
        g_spec = sp; g_trans = 1; g_diagram = 1; g_super_draw = 1; g_wave = wave;
        g_init_n = in; g_init_l = il; g_fin_n = fn; g_fin_l = fl;
        s->spec = (OrbitalSpec){in, il, 0, 1, 0};
        s->props = hydrogen_compute_props(&s->spec);
        s->props.r_extent = max_extent(&sp);
        double e = s->props.r_extent * (wave ? 1.3 : 1.0);
        base(s, wave ? VIEW_WAVE : VIEW_DENSITY, (float)e);
        s->p.complex_basis = 1;
        s->p.scheme = strstr(name, "wheel") ? 4 : 0;
        snprintf(s->name, sizeof(s->name), "transition");
        SuperSamplingRequest rq = { sp, 700000, wave ? SAMPLE_AMPLITUDE : SAMPLE_DENSITY, 11 };
        SuperVertex *v;
        if (!sampling_generate_super(&rq, &v, &g_res, NULL, 0, NULL)) return 0;
        renderer_set_super_points(&s->r, v, g_res.count);
        free(v);
        double om = super_max_omega(&sp);
        g_span = 1.5 * 4.0 * (2.0 * M_PI / om / 2.5);          /* a 4 s sweep + 50 % hold, at the app's default time scale */
        camera_fit(&s->cam, s->p.extent * 0.9f, (float)w / h);
        g_yaw0 = s->cam.yaw;
        scene_set_frac(s, strstr(name, "mid") ? 0.45 : 0.0);
        return 1;
    }
    if (!strncmp(name, "iso_", 4)) {
        OrbitalSpec o = {3, 2, 0, 1, 0};
        if (strstr(name, "cplx")) o = (OrbitalSpec){3, 2, 1, 1, 1};
        if (strstr(name, "3s")) o = (OrbitalSpec){3, 0, 0, 1, 0};
        if (strstr(name, "4f")) o = (OrbitalSpec){4, 3, -2, 1, 0};
        s->spec = o; s->props = hydrogen_compute_props(&o);
        hydrogen_orbital_name(&o, s->name, sizeof(s->name));
        sp = single(o);
        double e = s->props.r_extent * (wave ? 1.3 : 1.0);
        base(s, wave ? VIEW_WAVE : VIEW_DENSITY, (float)e);
        s->p.complex_basis = o.complex_basis;
        Volume vol; if (!build_volume(s, &sp, e * 1.08, res, &vol)) return 0;
        s->p.render_mode = strstr(name, "both") ? RENDER_BOTH : RENDER_ISO;
        float op = strstr(name, "glass") ? 0.35f : 0.92f;
        int signed_mode = wave && !strstr(name, "wheel");
        render_params_set_iso(&s->p, &sp, &vol, 0.0, strstr(name, "cplx") ? 0.4 : 0.0, strstr(name, "p50") ? 0.5 : 0.9, signed_mode, op);
        if (strstr(name, "wheel")) s->p.scheme = 4;
        if (strstr(name, "cut")) { s->p.slice_mode = SLICE_CUT; s->p.slice_axis = 1; s->p.slice_pos = 0.0f; }
        if (strstr(name, "both")) {
            SamplingRequest rq = { o, 400000, wave ? SAMPLE_AMPLITUDE : SAMPLE_DENSITY, 3 };
            PointVertex *v; SamplingResult r2; sampling_generate(&rq, &v, &r2, NULL, 0, NULL);
            renderer_set_points(&s->r, v, r2.count); free(v);
        }
        volume_free(&vol);
        camera_fit(&s->cam, s->p.extent * 0.9f, (float)w / h);
        return 1;
    }
    return 0;
}

static SuperHud g_sh;
static DipoleMatrix g_D;
static float g_trace[HUD_TRACE_N * 3];
void scene_hud_extra(Scene *s, HudState *hs) {
    (void)s;
    if (!g_super_draw) return;
    super_dipole_matrix(&g_spec, &g_D);
    memset(&g_sh, 0, sizeof(g_sh));
    superhud_fill(&g_sh, &g_spec, &g_D, g_t, g_trans ? 2 : 1, g_prog);
    double w = super_max_omega(&g_spec), span = 3.0 * 2.0 * M_PI / w;
    double amp = 0.8;
    for (int i = 0; i < HUD_TRACE_N; i++) {
        Complex c[SUPER_MAX]; double d[3] = {0, 0, 0};
        if (g_trans) {                                           /* growing history of the whole sweep */
            double f = i / (HUD_TRACE_N - 1.0), now = g_t / g_span;
            if (f <= now) {
                SuperSpec q = g_spec; double th = transition_sweep_theta(1.5 * f > 1.0 ? 1.0 : 1.5 * f);
                q.c[0].amp = cos(th); q.c[1].amp = sin(th);
                super_coeffs_at(&q, f * g_span, c); super_dipole_expect(&g_D, c, d);
            }
        } else {
            super_coeffs_at(&g_spec, g_t - span + span * i / (HUD_TRACE_N - 1.0), c);
            super_dipole_expect(&g_D, c, d);
        }
        for (int a = 0; a < 3; a++) g_trace[3 * i + a] = (float)d[a];
    }
    amp = 0; for (int i = 0; i < HUD_TRACE_N * 3; i++) amp = fmax(amp, fabs(g_trace[i]));
    amp = fmax(amp * 1.15, 0.1);
    g_sh.trace = g_trace; g_sh.trace_head = 0; g_sh.trace_amp = (float)amp;
    g_sh.show_arrow = 1; g_sh.arrow_scale = g_trans ? 3.0f : 4.0f;
    if (g_trans) superhud_transition_line(&g_sh, g_init_n, g_init_l, g_fin_n, g_fin_l, 1.0);
    else superhud_transition_line(&g_sh, 2, 1, 1, 0, 1.0);
    hs->sup = &g_sh;
    hs->iso_mode = s->p.render_mode == RENDER_ISO;
}

int scene_set_frac(Scene *s, double frac) {
    if (g_trans) {
        double tau = frac * 1.5, th = transition_sweep_theta(tau > 1.0 ? 1.0 : tau);
        g_spec.c[0].amp = cos(th); g_spec.c[1].amp = sin(th);
        g_t = frac * g_span; g_prog = tau > 1.0 ? 1.0 : tau;
        render_params_set_super(&s->p, &g_spec, &g_res, g_t, 0.0, g_wave);
        return 1;
    }
    if (!g_super_draw) return 0;
    double w = super_max_omega(&g_spec);
    g_t = w > 0 ? frac * 2.0 * 2.0 * M_PI / w : 0.0;          /* two beat cycles per loop */
    if (g_iso_anim) render_params_set_iso(&s->p, &g_spec, &g_vol, g_t, 0.0, 0.9, g_wave && !g_wheel, 0.9f);
    else render_params_set_super(&s->p, &g_spec, &g_res, g_t, 0.0, g_wave);
    return 1;
}

void scene_draw_extra(Scene *s, cairo_t *cr, int w, int h) {
    (void)s; (void)h;
    if (!g_diagram) return;
    double pw = 292, ph = 222, x = w - pw - 14, y = 46;
    cairo_save(cr);
    cairo_new_path(cr);
    cairo_arc(cr, x + 10, y + 10, 10, M_PI, 1.5 * M_PI); cairo_arc(cr, x + pw - 10, y + 10, 10, 1.5 * M_PI, 2 * M_PI);
    cairo_arc(cr, x + pw - 10, y + ph - 10, 10, 0, 0.5 * M_PI); cairo_arc(cr, x + 10, y + ph - 10, 10, 0.5 * M_PI, M_PI);
    cairo_close_path(cr);
    cairo_set_source_rgba(cr, 0.03, 0.05, 0.09, 0.78); cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr, 0.45, 0.6, 0.95, 0.35); cairo_set_line_width(cr, 1); cairo_stroke(cr);
    cairo_translate(cr, x, y);
    EnergyDiagram e = { .Z = 1.0, .n_max = 5, .mode = g_trans ? 2 : 1, .init_n = g_init_n, .init_l = g_init_l,
                        .final_n = g_fin_n, .final_l = g_fin_l, .has_final = g_trans, .hover = -1, .dark = 1 };
    e.progress = transition_sweep_theta(g_prog) / (0.5 * M_PI);
    e.comp_count = g_spec.count;
    Complex c[SUPER_MAX]; super_coeffs0(&g_spec, c);
    for (int k = 0; k < g_spec.count; k++) { e.comp_n[k] = g_spec.c[k].n; e.comp_l[k] = g_spec.c[k].l; e.comp_w[k] = (float)(c[k].r * c[k].r + c[k].i * c[k].i); }
    hud_energy_diagram(cr, (int)pw, (int)ph, &e, NULL);
    cairo_restore(cr);
}
