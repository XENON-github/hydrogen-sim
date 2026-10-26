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
            volume_free(&vol);
        }
        s->p.scheme = strstr(name, "wheel") ? 4 : 0;
        g_spec = sp; g_t = t; g_super_draw = 1;
        camera_fit(&s->cam, s->p.extent * 0.9f, (float)w / h);
        g_yaw0 = s->cam.yaw;
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
    superhud_fill(&g_sh, &g_spec, &g_D, g_t, 1, 0);
    double w = super_max_omega(&g_spec), span = 3.0 * 2.0 * M_PI / w;
    for (int i = 0; i < HUD_TRACE_N; i++) {
        Complex c[SUPER_MAX]; double d[3];
        super_coeffs_at(&g_spec, g_t - span + span * i / (HUD_TRACE_N - 1.0), c);
        super_dipole_expect(&g_D, c, d);
        for (int a = 0; a < 3; a++) g_trace[3 * i + a] = (float)d[a];
    }
    g_sh.trace = g_trace; g_sh.trace_head = 0; g_sh.trace_amp = 0.8f;
    g_sh.show_arrow = 1; g_sh.arrow_scale = 4.0f;
    superhud_transition_line(&g_sh, 2, 1, 1, 0, 1.0);
    hs->sup = &g_sh;
    hs->iso_mode = s->p.render_mode == RENDER_ISO;
}

int scene_set_frac(Scene *s, double frac) {
    if (!g_super_draw || s->p.render_mode == RENDER_ISO) return 0;
    double w = super_max_omega(&g_spec);
    g_t = w > 0 ? frac * 2.0 * 2.0 * M_PI / w : 0.0;          /* two beat cycles per loop */
    render_params_set_super(&s->p, &g_spec, &g_res, g_t, 0.0, g_wave);
    s->cam.yaw = g_yaw0 + (float)(0.5 * M_PI * frac);          /* gentle quarter-turn orbit */
    return 1;
}
