#ifndef RENDERER_H
#define RENDERER_H

#include <glad/glad.h>
#include "camera.h"
#include "sampling.h"
#include "volume.h"

enum { VIEW_DENSITY = 0, VIEW_WAVE = 1 };
enum { STYLE_GLOW = 0, STYLE_SPHERES = 1 };
enum { SLICE_OFF = 0, SLICE_CUT = 1, SLICE_SLAB = 2 };
enum { RENDER_PARTICLES = 0, RENDER_ISO = 1, RENDER_BOTH = 2 };
#define MAX_BLOOM_LEVELS 6

typedef struct {
    int view_kind;          /* VIEW_DENSITY or VIEW_WAVE */
    int scheme;
    int complex_basis;
    int style;              /* STYLE_GLOW / STYLE_SPHERES */
    float extent;           /* orbital radius used to scale particles (a0) */
    float particle_size;    /* relative, 1 = default */
    float brightness;       /* relative, 1 = default */
    int bloom;
    float bloom_strength;
    int show_grid;
    float grid_step, grid_half;
    int show_nucleus;
    float nucleus_glow;
    float phase;            /* animated global phase, radians */
    int slice_mode, slice_axis;
    float slice_pos, slice_half;   /* world units */
    float time_s;

    /* --- render mode: particles, isosurface, or both --- */
    int render_mode;

    /* --- superposition particles (weighted SuperVertex cloud); see sampling.h --- */
    int super;                      /* draw the SuperVertex cloud instead of the stationary one */
    int super_amp;                  /* 1: amplitude-mode weighting (wave view) */
    float coef[SUPER_MAX][2];       /* complex coefficients C_k(t) for the particle shader */
    float weight_gain, scalar_gain;

    /* --- isosurface (ray-marched through the volume textures) --- */
    float iso_coef[SUPER_MAX][2];   /* complex coefficients, volume scale already folded in */
    float iso_level;                /* |psi| (or |Re psi|) threshold, absolute amplitude */
    float iso_opacity;              /* 0..1 per surface crossing */
    int iso_signed;                 /* 1: surfaces of Re(psi) = +-level coloured by sign */
} RenderParams;

typedef struct {
    int ready;
    GLuint p_points, p_grid, p_nuc, p_down, p_up, p_comp, p_super, p_iso;
    GLuint vao_points, vbo_points, vao_empty;
    int point_count;
    GLuint vao_super, vbo_super;
    int super_count;
    GLuint vol_tex[2];
    int vol_ready, vol_k, vol_res;
    float vol_half, vol_vscale;
    float max_point_size;
    int w, h;
    GLuint scene_fbo, scene_tex, scene_depth;
    int levels;
    GLuint bloom_fbo[MAX_BLOOM_LEVELS], bloom_tex[MAX_BLOOM_LEVELS];
    int bw[MAX_BLOOM_LEVELS], bh[MAX_BLOOM_LEVELS];
} Renderer;

/* Requires a current OpenGL 3.3+ core context and loaded GLAD. Returns 0 on failure. */
int renderer_init(Renderer *r);
void renderer_destroy(Renderer *r);
void renderer_set_points(Renderer *r, const PointVertex *pts, int count);
void renderer_set_super_points(Renderer *r, const SuperVertex *pts, int count);
/* Uploads the component volume (RGBA16F 3D textures) used by the isosurface ray-marcher. */
void renderer_set_volume(Renderer *r, const Volume *v);
/* Renders the whole scene (points, grid, nucleus, bloom, tone-mapping) into `target_fbo`. */
void renderer_render(Renderer *r, const Camera *cam, const RenderParams *p,
                     GLuint target_fbo, int width, int height);

/* Fills the superposition fields of `p` for simulation time t_au. `global_phase` (radians) is an extra
 * overall e^{-i theta}. `res` comes from sampling_generate_super() for the same mode. */
void render_params_set_super(RenderParams *p, const SuperSpec *s, const SuperSamplingResult *res,
                             double t_au, double global_phase, int amp_mode);
/* Fills the isosurface fields: coefficients (volume scale folded in) and the |psi| level that encloses
 * `probability` of the mean density. */
void render_params_set_iso(RenderParams *p, const SuperSpec *s, const Volume *vol, double t_au,
                           double global_phase, double probability, int signed_mode, float opacity);

#endif /* RENDERER_H */
