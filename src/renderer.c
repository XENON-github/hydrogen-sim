#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "renderer.h"
#include "colors.h"

/* ------------------------------------------------------------------ */
/* Shaders (embedded; GLSL 3.30 core)                                  */
/* ------------------------------------------------------------------ */
#define GLSL(...) "#version 330 core\n" #__VA_ARGS__

static const char *VS_POINTS = GLSL(
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aData;
uniform mat4 uViewProj;
uniform float uProjScale, uPointWorld, uAlpha, uCamDist, uMaxSize, uPhase;
uniform int uKind, uComplex, uWheel, uSliceMode;
uniform vec3 uRamp[5];
uniform vec3 uPos, uNeg, uSliceN;
uniform float uSliceOff, uSliceHalf;
out vec3 vRGB;
out float vAlpha;
out float vW;

vec3 hsv(float h, float s, float v) {
    vec3 p = abs(fract(vec3(h) + vec3(1.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0);
    return v * mix(vec3(1.0), clamp(p - 1.0, 0.0, 1.0), s);
}
vec3 ramp5(float t) {
    t = clamp(t, 0.0, 1.0) * 4.0;
    int i = int(min(floor(t), 3.0));
    return mix(uRamp[i], uRamp[i + 1], t - float(i));
}
vec3 lin(vec3 c) { return pow(max(c, vec3(0.0)), vec3(2.2)); }

void main() {
    if (uSliceMode != 0) {
        float s = dot(aPos, uSliceN) - uSliceOff;
        if ((uSliceMode == 1 && s > 0.0) || (uSliceMode == 2 && abs(s) > uSliceHalf)) {
            gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
            gl_PointSize = 0.0;
            vRGB = vec3(0.0); vAlpha = 0.0; vW = 0.0;
            return;
        }
    }
    float a = aData.x;
    float ph = aData.y * 6.2831853 - uPhase;
    vec3 col;
    float w;
    if (uKind == 0) {
        if (uWheel == 1 && uComplex == 1) {
            col = hsv(fract(ph / 6.2831853), 0.8, 1.0);
            w = mix(0.5, 1.0, pow(a, 0.4));
        } else {
            float t = pow(a, 0.5);
            col = ramp5(t);
            w = mix(0.55, 1.0, t);
        }
    } else {
        if (uWheel == 1) {
            col = hsv(fract(ph / 6.2831853), 0.85, 1.0);
            w = mix(0.4, 1.0, pow(a, 0.5));
        } else {
            float s = cos(ph);
            col = (s >= 0.0) ? uPos : uNeg;
            col = mix(col, vec3(1.0), 0.4 * a * a);
            w = (0.12 + 0.88 * pow(abs(s), 0.8)) * mix(0.55, 1.0, pow(a, 0.5));
        }
    }
    vec4 clip = uViewProj * vec4(aPos, 1.0);
    float dRaw = uPointWorld * uProjScale / max(clip.w, 1e-3);
    float d = clamp(dRaw, 2.0, uMaxSize);
    float energy = min((dRaw * dRaw) / (d * d), 4.0);
    float rel = clip.w / uCamDist;
    float depthScale = clamp(rel * rel, 0.05, 20.0);
    gl_Position = clip;
    gl_PointSize = d;
    vRGB = lin(col);
    vAlpha = uAlpha * w * energy * depthScale;
    vW = w;
}
);

static const char *FS_POINTS = GLSL(
in vec3 vRGB;
in float vAlpha;
in float vW;
uniform int uStyle;
out vec4 frag;
void main() {
    vec2 c = gl_PointCoord * 2.0 - 1.0;
    float r2 = dot(c, c);
    if (r2 > 1.0) discard;
    if (uStyle == 0) {
        float g = (exp(-3.2 * r2) - 0.0408) * 1.0425;
        frag = vec4(vRGB * vAlpha * g, 0.0);
    } else {
        float z = sqrt(1.0 - r2);
        vec3 n = vec3(c.x, -c.y, z);
        float diff = clamp(dot(n, normalize(vec3(-0.35, 0.55, 0.75))), 0.0, 1.0);
        float rim = pow(1.0 - z, 3.0);
        vec3 col = vRGB * (0.22 + 0.78 * diff) * (0.55 + 0.9 * vW) + vRGB * rim * 0.25;
        frag = vec4(col * 1.4, 1.0);
    }
}
);

static const char *VS_GRID = GLSL(
uniform mat4 uViewProj;
uniform float uHalf;
out vec2 vW;
void main() {
    vec2 p = vec2(float(gl_VertexID & 1), float((gl_VertexID >> 1) & 1)) * 2.0 - 1.0;
    vW = p * uHalf;
    gl_Position = uViewProj * vec4(vW, 0.0, 1.0);
}
);

static const char *FS_GRID = GLSL(
in vec2 vW;
uniform float uStep, uHalf;
out vec4 frag;
float gridLine(vec2 p, float s) {
    vec2 q = p / s;
    vec2 w = fwidth(q);
    vec2 g = abs(fract(q - 0.5) - 0.5) / max(w, vec2(1e-4));
    float l = 1.0 - min(min(g.x, g.y), 1.0);
    return l * (1.0 - smoothstep(0.35, 0.9, max(w.x, w.y)));
}
void main() {
    float minor = gridLine(vW, uStep);
    float major = gridLine(vW, uStep * 5.0);
    float fade = 1.0 - smoothstep(0.5, 1.0, length(vW) / uHalf);
    float a = (0.010 * minor + 0.030 * major) * fade;
    frag = vec4(vec3(0.45, 0.62, 0.95) * a, 0.0);
}
);

static const char *VS_NUC = GLSL(
uniform mat4 uViewProj;
uniform vec2 uViewport;
uniform float uHaloPx;
out vec2 vUV;
void main() {
    vec2 corner = vec2(float(gl_VertexID & 1), float((gl_VertexID >> 1) & 1)) * 2.0 - 1.0;
    vUV = corner;
    vec4 clip = uViewProj * vec4(0.0, 0.0, 0.0, 1.0);
    clip.xy += corner * (2.0 * uHaloPx / uViewport) * clip.w;
    gl_Position = clip;
}
);

static const char *FS_NUC = GLSL(
in vec2 vUV;
uniform float uGlow;
out vec4 frag;
void main() {
    float r = length(vUV);
    if (r > 1.0) discard;
    float fade = 1.0 - smoothstep(0.55, 1.0, r);
    float core = exp(-r * r * 260.0);
    float mid = exp(-r * r * 30.0);
    float wide = exp(-r * 6.0);
    vec3 col = vec3(1.0, 0.95, 0.85) * core * 5.0
             + vec3(1.0, 0.78, 0.45) * mid * 0.9
             + vec3(1.0, 0.60, 0.28) * wide * 0.18;
    frag = vec4(col * fade * uGlow, 0.0);
}
);

static const char *VS_FULL = GLSL(
out vec2 vUV;
void main() {
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    vUV = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
);

static const char *FS_DOWN = GLSL(
in vec2 vUV;
uniform sampler2D uSrc;
uniform vec2 uTexel;
uniform int uPrefilter;
uniform float uThreshold;
out vec4 frag;
vec3 P(vec2 o) {
    vec3 c = texture(uSrc, vUV + o * uTexel).rgb;
    if (uPrefilter == 1) {
        c = min(c, vec3(30.0));
        float br = max(max(c.r, c.g), c.b);
        c *= smoothstep(uThreshold * 0.5, uThreshold * 1.5, br);
    }
    return c;
}
void main() {
    vec3 a = P(vec2(-2, 2)), b = P(vec2(0, 2)), c = P(vec2(2, 2));
    vec3 d = P(vec2(-2, 0)), e = P(vec2(0, 0)), f = P(vec2(2, 0));
    vec3 g = P(vec2(-2, -2)), h = P(vec2(0, -2)), i = P(vec2(2, -2));
    vec3 j = P(vec2(-1, 1)), k = P(vec2(1, 1)), l = P(vec2(-1, -1)), m = P(vec2(1, -1));
    vec3 r = e * 0.125 + (a + c + g + i) * 0.03125 + (b + d + f + h) * 0.0625 + (j + k + l + m) * 0.125;
    frag = vec4(r, 1.0);
}
);

static const char *FS_UP = GLSL(
in vec2 vUV;
uniform sampler2D uSrc;
uniform vec2 uTexel;
out vec4 frag;
void main() {
    vec3 a = texture(uSrc, vUV + vec2(-1, 1) * uTexel).rgb;
    vec3 b = texture(uSrc, vUV + vec2(0, 1) * uTexel).rgb;
    vec3 c = texture(uSrc, vUV + vec2(1, 1) * uTexel).rgb;
    vec3 d = texture(uSrc, vUV + vec2(-1, 0) * uTexel).rgb;
    vec3 e = texture(uSrc, vUV).rgb;
    vec3 f = texture(uSrc, vUV + vec2(1, 0) * uTexel).rgb;
    vec3 g = texture(uSrc, vUV + vec2(-1, -1) * uTexel).rgb;
    vec3 h = texture(uSrc, vUV + vec2(0, -1) * uTexel).rgb;
    vec3 i = texture(uSrc, vUV + vec2(1, -1) * uTexel).rgb;
    frag = vec4((e * 4.0 + (b + d + f + h) * 2.0 + (a + c + g + i)) / 16.0, 1.0);
}
);

static const char *FS_COMP = GLSL(
in vec2 vUV;
uniform sampler2D uScene, uBloom;
uniform float uBloomStrength, uSeed;
uniform int uBloomOn;
out vec4 frag;
void main() {
    vec3 hdr = texture(uScene, vUV).rgb;
    if (uBloomOn == 1) hdr += texture(uBloom, vUV).rgb * uBloomStrength;
    vec2 q = (vUV - 0.5) * vec2(1.0, 0.85);
    float v = 1.0 - smoothstep(0.0, 0.9, length(q) * 1.6);
    vec3 bg = mix(vec3(0.0007, 0.0010, 0.0026), vec3(0.0040, 0.0056, 0.0120), v);
    vec3 c = 1.0 - exp(-(bg + hdr));
    c = pow(c, vec3(1.0 / 2.2));
    float n = fract(sin(dot(gl_FragCoord.xy + uSeed, vec2(12.9898, 78.233))) * 43758.5453);
    c += (n - 0.5) / 255.0;
    frag = vec4(c, 1.0);
}
);

/* ------------------------------------------------------------------ */

static GLuint compile(GLenum type, const char *src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetShaderInfoLog(s, sizeof(log), NULL, log);
        fprintf(stderr, "Shader compile error (%s):\n%s\n", type == GL_VERTEX_SHADER ? "vertex" : "fragment", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

static GLuint make_program(const char *vs, const char *fs) {
    GLuint v = compile(GL_VERTEX_SHADER, vs), f = compile(GL_FRAGMENT_SHADER, fs);
    if (!v || !f) { if (v) glDeleteShader(v); if (f) glDeleteShader(f); return 0; }
    GLuint p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    glDeleteShader(v);
    glDeleteShader(f);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetProgramInfoLog(p, sizeof(log), NULL, log);
        fprintf(stderr, "Program link error:\n%s\n", log);
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

#define UL(p, name) glGetUniformLocation((p), (name))

int renderer_init(Renderer *r) {
    memset(r, 0, sizeof(*r));
    r->p_points = make_program(VS_POINTS, FS_POINTS);
    r->p_grid = make_program(VS_GRID, FS_GRID);
    r->p_nuc = make_program(VS_NUC, FS_NUC);
    r->p_down = make_program(VS_FULL, FS_DOWN);
    r->p_up = make_program(VS_FULL, FS_UP);
    r->p_comp = make_program(VS_FULL, FS_COMP);
    if (!r->p_points || !r->p_grid || !r->p_nuc || !r->p_down || !r->p_up || !r->p_comp) {
        renderer_destroy(r);
        return 0;
    }
    glGenVertexArrays(1, &r->vao_empty);
    glGenVertexArrays(1, &r->vao_points);
    glGenBuffers(1, &r->vbo_points);
    GLfloat range[2] = {1.0f, 64.0f};
    glGetFloatv(GL_ALIASED_POINT_SIZE_RANGE, range);
    r->max_point_size = range[1] < 96.0f ? range[1] : 96.0f;
    if (r->max_point_size < 4.0f) r->max_point_size = 4.0f;
    r->ready = 1;
    return 1;
}

static void free_targets(Renderer *r) {
    if (r->scene_fbo) glDeleteFramebuffers(1, &r->scene_fbo);
    if (r->scene_tex) glDeleteTextures(1, &r->scene_tex);
    if (r->scene_depth) glDeleteRenderbuffers(1, &r->scene_depth);
    r->scene_fbo = r->scene_tex = r->scene_depth = 0;
    for (int i = 0; i < MAX_BLOOM_LEVELS; i++) {
        if (r->bloom_fbo[i]) glDeleteFramebuffers(1, &r->bloom_fbo[i]);
        if (r->bloom_tex[i]) glDeleteTextures(1, &r->bloom_tex[i]);
        r->bloom_fbo[i] = r->bloom_tex[i] = 0;
    }
    r->levels = 0;
}

void renderer_destroy(Renderer *r) {
    if (r->p_points) glDeleteProgram(r->p_points);
    if (r->p_grid) glDeleteProgram(r->p_grid);
    if (r->p_nuc) glDeleteProgram(r->p_nuc);
    if (r->p_down) glDeleteProgram(r->p_down);
    if (r->p_up) glDeleteProgram(r->p_up);
    if (r->p_comp) glDeleteProgram(r->p_comp);
    if (r->vbo_points) glDeleteBuffers(1, &r->vbo_points);
    if (r->vao_points) glDeleteVertexArrays(1, &r->vao_points);
    if (r->vao_empty) glDeleteVertexArrays(1, &r->vao_empty);
    free_targets(r);
    memset(r, 0, sizeof(*r));
}

void renderer_set_points(Renderer *r, const PointVertex *pts, int count) {
    glBindVertexArray(r->vao_points);
    glBindBuffer(GL_ARRAY_BUFFER, r->vbo_points);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)count * (GLsizeiptr)sizeof(PointVertex), pts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(PointVertex), (void *)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_UNSIGNED_SHORT, GL_TRUE, sizeof(PointVertex), (void *)(3 * sizeof(float)));
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    r->point_count = count;
}

static GLuint make_color_tex(int w, int h) {
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_HALF_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return t;
}

static void ensure_targets(Renderer *r, int w, int h) {
    if (r->scene_fbo && r->w == w && r->h == h) return;
    free_targets(r);
    r->w = w; r->h = h;
    r->scene_tex = make_color_tex(w, h);
    glGenRenderbuffers(1, &r->scene_depth);
    glBindRenderbuffer(GL_RENDERBUFFER, r->scene_depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
    glGenFramebuffers(1, &r->scene_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, r->scene_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, r->scene_tex, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, r->scene_depth);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        fprintf(stderr, "renderer: scene framebuffer incomplete\n");

    int bw = w / 2, bh = h / 2;
    r->levels = 0;
    for (int i = 0; i < MAX_BLOOM_LEVELS && bw >= 4 && bh >= 4; i++) {
        r->bw[i] = bw; r->bh[i] = bh;
        r->bloom_tex[i] = make_color_tex(bw, bh);
        glGenFramebuffers(1, &r->bloom_fbo[i]);
        glBindFramebuffer(GL_FRAMEBUFFER, r->bloom_fbo[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, r->bloom_tex[i], 0);
        r->levels++;
        bw /= 2; bh /= 2;
    }
}

static void draw_fullscreen(const Renderer *r) {
    glBindVertexArray(r->vao_empty);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

static void set_mat4(GLint loc, Mat4 m) { glUniformMatrix4fv(loc, 1, GL_TRUE, &m.m[0][0]); }

static void bloom_pass(Renderer *r, float threshold) {
    int n = r->levels;
    if (n < 2) return;
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glUseProgram(r->p_down);
    glUniform1i(UL(r->p_down, "uSrc"), 0);
    glUniform1f(UL(r->p_down, "uThreshold"), threshold);
    glActiveTexture(GL_TEXTURE0);
    for (int i = 0; i < n; i++) {
        GLuint src = (i == 0) ? r->scene_tex : r->bloom_tex[i - 1];
        int sw = (i == 0) ? r->w : r->bw[i - 1], sh = (i == 0) ? r->h : r->bh[i - 1];
        glBindFramebuffer(GL_FRAMEBUFFER, r->bloom_fbo[i]);
        glViewport(0, 0, r->bw[i], r->bh[i]);
        glBindTexture(GL_TEXTURE_2D, src);
        glUniform2f(UL(r->p_down, "uTexel"), 1.0f / sw, 1.0f / sh);
        glUniform1i(UL(r->p_down, "uPrefilter"), i == 0);
        draw_fullscreen(r);
    }
    glUseProgram(r->p_up);
    glUniform1i(UL(r->p_up, "uSrc"), 0);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    for (int i = n - 1; i >= 1; i--) {
        glBindFramebuffer(GL_FRAMEBUFFER, r->bloom_fbo[i - 1]);
        glViewport(0, 0, r->bw[i - 1], r->bh[i - 1]);
        glBindTexture(GL_TEXTURE_2D, r->bloom_tex[i]);
        glUniform2f(UL(r->p_up, "uTexel"), 1.0f / r->bw[i], 1.0f / r->bh[i]);
        draw_fullscreen(r);
    }
    glDisable(GL_BLEND);
}

void renderer_render(Renderer *r, const Camera *cam, const RenderParams *p,
                     GLuint target_fbo, int width, int height) {
    if (!r->ready || width < 8 || height < 8) return;
    ensure_targets(r, width, height);

    const float aspect = (float)width / (float)height;
    const Mat4 vp = camera_get_viewproj(cam, aspect);
    const ColorScheme *cs = color_scheme_get(p->scheme);

    /* ---- scene pass (HDR, additive) ---- */
    glBindFramebuffer(GL_FRAMEBUFFER, r->scene_fbo);
    glViewport(0, 0, width, height);
    glDisable(GL_SCISSOR_TEST);
    glDepthMask(GL_TRUE);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_PROGRAM_POINT_SIZE);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);

    if (p->show_grid) {
        glUseProgram(r->p_grid);
        set_mat4(UL(r->p_grid, "uViewProj"), vp);
        glUniform1f(UL(r->p_grid, "uStep"), p->grid_step);
        glUniform1f(UL(r->p_grid, "uHalf"), p->grid_half);
        glBindVertexArray(r->vao_empty);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }

    if (r->point_count > 0) {
        const float N = (float)r->point_count;
        const float proj_scale = (float)height / (2.0f * tanf(cam->fovy * 0.5f));
        const float point_world = p->particle_size * p->extent * 0.9f * powf(N, -1.0f / 3.0f);
        const float ratio = p->extent / point_world;
        float slice_gain = 1.0f;
        if (p->slice_mode == SLICE_SLAB && p->slice_half > 0.0f)
            slice_gain = powf(clampf(p->extent / (2.0f * p->slice_half), 1.0f, 40.0f), 0.8f);
        else if (p->slice_mode == SLICE_CUT)
            slice_gain = 1.5f;
        const float alpha = p->brightness * 3.0f * ratio * ratio / N * slice_gain;
        static const float axes[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
        const float *sn = axes[p->slice_axis < 0 || p->slice_axis > 2 ? 2 : p->slice_axis];

        glUseProgram(r->p_points);
        GLuint pr = r->p_points;
        set_mat4(UL(pr, "uViewProj"), vp);
        glUniform1f(UL(pr, "uProjScale"), proj_scale);
        glUniform1f(UL(pr, "uPointWorld"), point_world);
        glUniform1f(UL(pr, "uAlpha"), alpha);
        glUniform1f(UL(pr, "uCamDist"), cam->distance);
        glUniform1f(UL(pr, "uMaxSize"), r->max_point_size);
        glUniform1f(UL(pr, "uPhase"), p->phase);
        glUniform1i(UL(pr, "uKind"), p->view_kind);
        glUniform1i(UL(pr, "uComplex"), p->complex_basis);
        glUniform1i(UL(pr, "uWheel"), cs->wheel);
        glUniform1i(UL(pr, "uStyle"), p->style);
        glUniform3fv(UL(pr, "uRamp"), 5, &cs->ramp[0].x);
        glUniform3f(UL(pr, "uPos"), cs->pos.x, cs->pos.y, cs->pos.z);
        glUniform3f(UL(pr, "uNeg"), cs->neg.x, cs->neg.y, cs->neg.z);
        glUniform1i(UL(pr, "uSliceMode"), p->slice_mode);
        glUniform3f(UL(pr, "uSliceN"), sn[0], sn[1], sn[2]);
        glUniform1f(UL(pr, "uSliceOff"), p->slice_pos);
        glUniform1f(UL(pr, "uSliceHalf"), p->slice_half);
        if (p->style == STYLE_SPHERES) {
            glDisable(GL_BLEND);
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(GL_LESS);
        }
        glBindVertexArray(r->vao_points);
        glDrawArrays(GL_POINTS, 0, r->point_count);
        glBindVertexArray(0);
        if (p->style == STYLE_SPHERES) {
            glDisable(GL_DEPTH_TEST);
            glEnable(GL_BLEND);
            glBlendFunc(GL_ONE, GL_ONE);
        }
    }

    if (p->show_nucleus) {
        glUseProgram(r->p_nuc);
        set_mat4(UL(r->p_nuc, "uViewProj"), vp);
        glUniform2f(UL(r->p_nuc, "uViewport"), (float)width, (float)height);
        glUniform1f(UL(r->p_nuc, "uHaloPx"), 0.085f * (float)height);
        glUniform1f(UL(r->p_nuc, "uGlow"), p->nucleus_glow);
        glBindVertexArray(r->vao_empty);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }
    glDisable(GL_BLEND);

    /* ---- bloom ---- */
    if (p->bloom) bloom_pass(r, 0.9f);

    /* ---- composite + tone map into the target framebuffer ---- */
    glBindFramebuffer(GL_FRAMEBUFFER, target_fbo);
    glViewport(0, 0, width, height);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glUseProgram(r->p_comp);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, r->scene_tex);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, r->levels > 0 ? r->bloom_tex[0] : r->scene_tex);
    glUniform1i(UL(r->p_comp, "uScene"), 0);
    glUniform1i(UL(r->p_comp, "uBloom"), 1);
    glUniform1i(UL(r->p_comp, "uBloomOn"), p->bloom && r->levels >= 2);
    glUniform1f(UL(r->p_comp, "uBloomStrength"), p->bloom_strength * 0.35f);
    glUniform1f(UL(r->p_comp, "uSeed"), fmodf(p->time_s, 7.0f));
    draw_fullscreen(r);
    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(0);
    glUseProgram(0);
}
