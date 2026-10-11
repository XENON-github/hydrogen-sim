#ifndef HUD_H
#define HUD_H

#include <cairo.h>
#include "camera.h"
#include "hydrogen.h"

typedef struct {
    const Camera *cam;
    int width, height;              /* logical pixels of the overlay */
    int view_kind;                  /* VIEW_DENSITY / VIEW_WAVE */
    int scheme, complex_basis;
    const OrbitalSpec *spec;
    const OrbitalProps *props;
    const char *orbital_name;
    float extent;                   /* scene radius (a0) */
    float grid_step;
    int show_info, show_axes, show_gizmo, show_legend, show_grid;
    int show_sphere90, show_bbox;
    int slice_mode, slice_axis;
    float slice_pos;
    double fps;
    int animating;
    int point_count;
    long n_pos, n_neg;
    int generating;
    int phase_anim;
    float phase;
} HudState;

void hud_draw(cairo_t *cr, const HudState *s);
void hud_radial_plot(cairo_t *cr, int w, int h, const OrbitalProps *p, const OrbitalSpec *spec, int dark);

#endif /* HUD_H */
