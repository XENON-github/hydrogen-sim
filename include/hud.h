#ifndef HUD_H
#define HUD_H

#include <cairo.h>
#include "camera.h"
#include "hydrogen.h"
#include "superposition.h"
#include "transitions.h"

/* Extra HUD data when the view shows a superposition / transition instead of one stationary orbital. */
#define HUD_TRACE_N 300
typedef struct {
    int mode;                         /* 1 = custom superposition, 2 = transition animation */
    int count;
    char comp_label[SUPER_MAX][32];   /* "2p_z", "1s", ... */
    float comp_weight[SUPER_MAX];     /* |c_k|^2 now */
    double comp_energy_ev[SUPER_MAX];
    double t_fs;                      /* simulated time */
    double period_fs;                 /* shortest beat period, 0 if static */
    double dipole[3];                 /* <r>(t) in a0 */
    int show_arrow;
    float arrow_scale;                /* drawn length = dipole * arrow_scale (a0) */
    const float *trace;               /* HUD_TRACE_N x 3 rolling history of <x>,<y>,<z> (a0) */
    int trace_head;                   /* index of the oldest sample */
    double trace_span_fs;
    float trace_amp;                  /* half-height of the plot in a0 */
    double progress;                  /* transition sweep 0..1 */
    char line[200];                   /* transition summary ("2p -> 1s · Lyman-α · 121.6 nm · allowed") */
    float line_rgb[3];
} SuperHud;

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
    const SuperHud *sup;            /* NULL for a stationary orbital */
    int iso_mode;                   /* 1 when the isosurface is being drawn (changes legend text) */
    int hide_stats;                 /* recording: no fps / point-count text */
} HudState;

/* Fills everything in `u` except the rolling trace (owned by the caller). mode: 1 superposition, 2 transition. */
void superhud_fill(SuperHud *u, const SuperSpec *s, const DipoleMatrix *D, double t_au, int mode, double progress);
/* Summary line for a transition: "2p → 1s · Lyman-α · 121.6 nm (10.20 eV) · A = 6.3e8 /s · E1 allowed". */
void superhud_transition_line(SuperHud *u, int ni, int li, int nf, int lf, double Z);

/* Energy-level diagram (sidebar widget). Left-click = initial state, right-click = final state. */
typedef struct {
    double Z;
    int n_max;
    int mode;                       /* 0 stationary, 1 superposition, 2 transition */
    int init_n, init_l;
    int final_n, final_l, has_final;
    int comp_count, comp_n[SUPER_MAX], comp_l[SUPER_MAX];
    float comp_w[SUPER_MAX];
    double progress;                /* transition sweep 0..1 (mode 2) */
    int hover;                      /* level index under the mouse or -1 */
    int dark;
} EnergyDiagram;

void hud_energy_diagram(cairo_t *cr, int w, int h, const EnergyDiagram *d, LevelLayout *layout_out);

void hud_draw(cairo_t *cr, const HudState *s);
void hud_radial_plot(cairo_t *cr, int w, int h, const OrbitalProps *p, const OrbitalSpec *spec, int dark);

#endif /* HUD_H */
