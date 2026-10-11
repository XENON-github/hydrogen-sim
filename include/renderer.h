#ifndef RENDERER_H
#define RENDERER_H

#include <glad/glad.h>
#include "camera.h"
#include "sampling.h"

enum { VIEW_DENSITY = 0, VIEW_WAVE = 1 };
enum { STYLE_GLOW = 0, STYLE_SPHERES = 1 };
enum { SLICE_OFF = 0, SLICE_CUT = 1, SLICE_SLAB = 2 };
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
} RenderParams;

typedef struct {
    int ready;
    GLuint p_points, p_grid, p_nuc, p_down, p_up, p_comp;
    GLuint vao_points, vbo_points, vao_empty;
    int point_count;
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
/* Renders the whole scene (points, grid, nucleus, bloom, tone-mapping) into `target_fbo`. */
void renderer_render(Renderer *r, const Camera *cam, const RenderParams *p,
                     GLuint target_fbo, int width, int height);

#endif /* RENDERER_H */
