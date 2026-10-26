#ifndef SCENE_H
#define SCENE_H
#include <cairo.h>
#include "renderer.h"
#include "hud.h"
#include "sampling.h"

typedef struct {
    Renderer r;
    Camera cam;
    RenderParams p;
    HudState hs;
    OrbitalSpec spec;
    OrbitalProps props;
    char name[96];
    int hud;
    double render_ms;
    int view_kind;
} Scene;

int scene_setup(Scene *s, const char *name, int w, int h);
void scene_render(Scene *s, unsigned fbo, int w, int h);
void scene_draw_hud(Scene *s, cairo_t *cr, int w, int h);
void scene_draw_extra(Scene *s, cairo_t *cr, int w, int h);   /* e.g. energy-level diagram panel */
int scene_set_frac(Scene *s, double frac);   /* animation: 0..1 of a loop; returns 0 if the scene is static */
int scene_setup_extra(Scene *s, const char *name, int w, int h);
void scene_hud_extra(Scene *s, HudState *hs);

#endif
