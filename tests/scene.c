/* Test scenes for the headless harness. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <glad/glad.h>
#include "scene.h"

static void base_params(Scene *s, int kind) {
    memset(&s->p, 0, sizeof(s->p));
    s->view_kind = kind;
    float ext = (float)s->props.r_extent * (kind == VIEW_WAVE ? 1.3f : 1.0f);
    float step = nice_step(ext / 6.0f);
    s->p.view_kind = kind;
    s->p.complex_basis = s->spec.complex_basis;
    s->p.extent = ext;
    s->p.particle_size = 1.0f; s->p.brightness = 1.0f;
    s->p.bloom = 1; s->p.bloom_strength = 0.6f;
    s->p.show_grid = 1; s->p.grid_step = step; s->p.grid_half = ceilf(ext * 1.15f / (5.0f * step)) * 5.0f * step;
    s->p.show_nucleus = 1; s->p.nucleus_glow = 1.0f;
    s->p.slice_axis = 1;
    camera_set_preset(&s->cam, CAMERA_VIEW_ISO);
    camera_update_clip(&s->cam, ext);
}

static void gen_points(Scene *s, int count, SampleMode mode) {
    SamplingRequest rq = { s->spec, count, mode, 12345 };
    PointVertex *v; SamplingResult res;
    if (!sampling_generate(&rq, &v, &res, NULL, 0, NULL)) { fprintf(stderr, "sampling failed\n"); exit(1); }
    renderer_set_points(&s->r, v, res.count);
    free(v);
}

int scene_setup(Scene *s, const char *name, int w, int h) {
    memset(s, 0, sizeof(*s));
    s->cam = camera_create();
    s->hud = 1;
    if (!renderer_init(&s->r)) return 0;
    snprintf(s->name, sizeof(s->name), "%s", name);

    if (!strcmp(name, "dens_4dz2") || !strcmp(name, "wave_cplx") || !strcmp(name, "slice_3s") || !strcmp(name, "spheres_2pz")) {
        int wave = !strcmp(name, "wave_cplx");
        s->spec = !strcmp(name, "dens_4dz2") ? (OrbitalSpec){4, 2, 0, 1, 0} :
                  wave ? (OrbitalSpec){3, 2, 1, 1, 1} :
                  !strcmp(name, "slice_3s") ? (OrbitalSpec){3, 0, 0, 1, 0} : (OrbitalSpec){2, 1, 0, 1, 0};
        s->props = hydrogen_compute_props(&s->spec);
        hydrogen_orbital_name(&s->spec, s->name, sizeof(s->name));
        base_params(s, wave ? VIEW_WAVE : VIEW_DENSITY);
        gen_points(s, 600000, wave ? SAMPLE_AMPLITUDE : SAMPLE_DENSITY);
        if (wave) { s->p.scheme = 4; s->p.phase = 0.7f; }
        if (!strcmp(name, "slice_3s")) { s->p.slice_mode = SLICE_CUT; s->p.slice_axis = 1; s->p.slice_pos = 0.0f; }
        if (!strcmp(name, "spheres_2pz")) s->p.style = STYLE_SPHERES;
        camera_fit(&s->cam, s->p.extent * 0.9f, (float)w / h);
        return 1;
    }
    return scene_setup_extra(s, name, w, h);
}

void scene_render(Scene *s, unsigned fbo, int w, int h) {
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    if (getenv("SCENE_BRIGHT")) s->p.brightness = (float)atof(getenv("SCENE_BRIGHT"));
    if (getenv("SCENE_DEBUG")) fprintf(stderr, "super=%d count=%d mode=%d extent=%g coef=(%g,%g)(%g,%g) wg=%g sg=%g alpha-brightness=%g\n", s->p.super, s->r.super_count, s->p.render_mode, s->p.extent, s->p.coef[0][0], s->p.coef[0][1], s->p.coef[1][0], s->p.coef[1][1], s->p.weight_gain, s->p.scalar_gain, s->p.brightness);
    camera_update_clip(&s->cam, s->p.extent);
    renderer_render(&s->r, &s->cam, &s->p, fbo, w, h);
    glFinish();
    clock_gettime(CLOCK_MONOTONIC, &t1);
    s->render_ms = (t1.tv_sec - t0.tv_sec) * 1e3 + (t1.tv_nsec - t0.tv_nsec) * 1e-6;
}

void scene_draw_hud(Scene *s, cairo_t *cr, int w, int h) {
    HudState *hs = &s->hs;
    memset(hs, 0, sizeof(*hs));
    hs->cam = &s->cam; hs->width = w; hs->height = h;
    hs->view_kind = s->view_kind; hs->scheme = s->p.scheme; hs->complex_basis = s->spec.complex_basis;
    hs->spec = &s->spec; hs->props = &s->props; hs->orbital_name = s->name;
    hs->extent = s->p.extent; hs->grid_step = s->p.grid_step;
    hs->show_info = hs->show_axes = hs->show_gizmo = hs->show_legend = hs->show_grid = 1;
    hs->slice_mode = s->p.slice_mode; hs->slice_axis = s->p.slice_axis; hs->slice_pos = s->p.slice_pos;
    hs->point_count = s->r.point_count;
    hs->phase = s->p.phase;
    scene_hud_extra(s, hs);
    hud_draw(cr, hs);
    scene_draw_extra(s, cr, w, h);
}
