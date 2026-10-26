#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "renderer.h"
#include "colors.h"

/* ------------------------------------------------------------------ */
/* Shaders (embedded; GLSL 3.30 core)                                  */
/* ------------------------------------------------------------------ */
#define GLSL(...) "#version 330 core\n" #__VA_ARGS__

#define GLSL_RAW(...) #__VA_ARGS__
static const char *GLSL_VERSION = "#version 330 core\n";

/* Shared point-vertex code: colour mapping, slicing, sizing. The two mains below only differ in how they
 * obtain (a = normalised amplitude/density, ph = phase angle, wgt = importance weight). */
static const char *VS_PTS_COMMON = GLSL_RAW(
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

void emit(vec3 aPos, float a, float ph, float wgt) {
    if (uSliceMode != 0) {
        float s = dot(aPos, uSliceN) - uSliceOff;
        if ((uSliceMode == 1 && s > 0.0) || (uSliceMode == 2 && abs(s) > uSliceHalf)) {
            gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
            gl_PointSize = 0.0;
            vRGB = vec3(0.0); vAlpha = 0.0; vW = 0.0;
            return;
        }
    }
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
    vAlpha = uAlpha * w * wgt * energy * depthScale;
    vW = w;
}
);

static const char *VS_POINTS_MAIN = GLSL_RAW(
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aData;
void main() { emit(aPos, aData.x, aData.y * 6.2831853 - uPhase, 1.0); }
);

static const char *VS_SUPER_MAIN = GLSL_RAW(
layout(location = 0) in vec3 aPos;
layout(location = 1) in float aScalar;
layout(location = 2) in vec4 aB0;
layout(location = 3) in vec4 aB1;
uniform vec2 uC[4];
uniform int uSuperAmp;
uniform float uWeightGain, uScalarGain;
vec2 cmul(vec2 a, vec2 b) { return vec2(a.x * b.x - a.y * b.y, a.x * b.y + a.y * b.x); }
void main() {
    vec2 psi = cmul(uC[0], aB0.xy) + cmul(uC[1], aB0.zw) + cmul(uC[2], aB1.xy) + cmul(uC[3], aB1.zw);
    float m2 = dot(psi, psi);
    float mag = (uSuperAmp == 1) ? sqrt(m2) : m2;
    float a = clamp(aScalar * mag * uScalarGain, 0.0, 1.0);
    float ph = (m2 > 1e-14) ? atan(psi.y, psi.x) : 0.0;
    emit(aPos, a, ph, mag * uWeightGain);
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


/* Ray-marched isosurface through the component volume:  psi(x) = sum_k C_k tex_k(x). */
static const char *FS_ISO = GLSL(
in vec2 vUV;
uniform sampler3D uVol0, uVol1;
uniform vec3 uCamPos, uRight, uUp, uFwd;
uniform float uTanHalf, uAspect, uHalf, uLevel, uOpacity, uDt, uEps, uBright;
uniform int uK, uSigned, uWheel, uMaxSteps, uSliceMode;
uniform vec2 uC[4];
uniform vec3 uPos, uNeg, uSliceN, uRamp[5];
uniform float uSliceOff, uSliceHalf;
out vec4 frag;

vec2 cmul(vec2 a, vec2 b) { return vec2(a.x * b.x - a.y * b.y, a.x * b.y + a.y * b.x); }
vec3 hsv(float h, float s, float v) {
    vec3 p = abs(fract(vec3(h) + vec3(1.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0);
    return v * mix(vec3(1.0), clamp(p - 1.0, 0.0, 1.0), s);
}
vec3 lin(vec3 c) { return pow(max(c, vec3(0.0)), vec3(2.2)); }

vec2 psiAt(vec3 p) {
    vec3 uvw = (p + vec3(uHalf)) / (2.0 * uHalf);
    vec4 A = texture(uVol0, uvw);
    vec2 s = cmul(uC[0], A.xy) + cmul(uC[1], A.zw);
    if (uK > 2) {
        vec4 B = texture(uVol1, uvw);
        s += cmul(uC[2], B.xy) + cmul(uC[3], B.zw);
    }
    return s;
}
float fld(vec3 p) { vec2 s = psiAt(p); return (uSigned == 1) ? s.x : length(s); }
float dfun(vec3 p) {
    vec2 s = psiAt(p);
    return ((uSigned == 1) ? abs(s.x) : length(s)) - uLevel;
}

vec3 shade(vec3 p, vec3 rd) {
    float e = uEps;
    vec3 g = vec3(fld(p + vec3(e, 0.0, 0.0)) - fld(p - vec3(e, 0.0, 0.0)),
                  fld(p + vec3(0.0, e, 0.0)) - fld(p - vec3(0.0, e, 0.0)),
                  fld(p + vec3(0.0, 0.0, e)) - fld(p - vec3(0.0, 0.0, e)));
    vec3 n = g / max(length(g), 1e-20);
    if (dot(n, rd) > 0.0) n = -n;
    vec2 s = psiAt(p);
    vec3 base;
    if (uSigned == 1) base = (s.x >= 0.0) ? uPos : uNeg;
    else if (uWheel == 1) base = hsv(fract(atan(s.y, s.x) / 6.2831853), 0.8, 1.0);
    else base = uRamp[3];
    vec3 L = normalize(-rd * 0.65 + uUp * 0.55 - uRight * 0.35);
    float diff = max(dot(n, L), 0.0);
    float spec = pow(max(dot(reflect(-L, n), -rd), 0.0), 36.0);
    float rim = pow(1.0 - max(dot(n, -rd), 0.0), 3.0);
    vec3 b = lin(base);
    return (b * (0.16 + 0.95 * diff) + vec3(0.30) * spec + b * rim * 0.55) * uBright;
}

void main() {
    vec2 ndc = vUV * 2.0 - 1.0;
    vec3 ro = uCamPos;
    vec3 rd = normalize(uFwd + ndc.x * uAspect * uTanHalf * uRight + ndc.y * uTanHalf * uUp);
    vec3 inv = 1.0 / rd;
    vec3 t0 = (-vec3(uHalf) - ro) * inv, t1 = (vec3(uHalf) - ro) * inv;
    vec3 tmn = min(t0, t1), tmx = max(t0, t1);
    float tn = max(max(tmn.x, tmn.y), max(tmn.z, 0.0));
    float tf = min(min(tmx.x, tmx.y), tmx.z);
    if (tf <= tn) discard;
    vec3 acc = vec3(0.0);
    float A = 0.0, prevD = 0.0;
    bool havePrev = false;
    float t = tn;
    for (int i = 0; i < uMaxSteps; i++) {
        if (t > tf) break;
        vec3 p = ro + rd * t;
        if (uSliceMode != 0) {
            float sd = dot(p, uSliceN) - uSliceOff;
            if ((uSliceMode == 1 && sd > 0.0) || (uSliceMode == 2 && abs(sd) > uSliceHalf)) {
                havePrev = false; t += uDt; continue;
            }
        }
        float d = dfun(p);
        if (havePrev && ((prevD < 0.0) != (d < 0.0))) {
            float ta = t - uDt, tb = t, da = prevD;
            for (int k = 0; k < 6; k++) {
                float tm = 0.5 * (ta + tb);
                float dm = dfun(ro + rd * tm);
                if ((dm < 0.0) == (da < 0.0)) { ta = tm; da = dm; } else { tb = tm; }
            }
            vec3 col = shade(ro + rd * (0.5 * (ta + tb)), rd);
            acc += (1.0 - A) * uOpacity * col;
            A += (1.0 - A) * uOpacity;
            if (A > 0.985) break;
        }
        prevD = d; havePrev = true;
        t += uDt;
    }
    frag = vec4(acc, A);
}
);

/* ------------------------------------------------------------------ */

static GLuint compile_multi(GLenum type, const char *const *srcs, int n) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, n, srcs, NULL);
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

static GLuint compile(GLenum type, const char *src) { return compile_multi(type, &src, 1); }

static GLuint make_program_multi(const char *const *vs, int nvs, const char *fs) {
    GLuint v = compile_multi(GL_VERTEX_SHADER, vs, nvs), f = compile(GL_FRAGMENT_SHADER, fs);
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

static GLuint make_program(const char *vs, const char *fs) { return make_program_multi(&vs, 1, fs); }

#define UL(p, name) glGetUniformLocation((p), (name))

int renderer_init(Renderer *r) {
    memset(r, 0, sizeof(*r));
    const char *vs_pts[3] = {GLSL_VERSION, VS_PTS_COMMON, VS_POINTS_MAIN};
    const char *vs_sup[3] = {GLSL_VERSION, VS_PTS_COMMON, VS_SUPER_MAIN};
    r->p_points = make_program_multi(vs_pts, 3, FS_POINTS);
    r->p_super = make_program_multi(vs_sup, 3, FS_POINTS);
    r->p_iso = make_program(VS_FULL, FS_ISO);
    r->p_grid = make_program(VS_GRID, FS_GRID);
    r->p_nuc = make_program(VS_NUC, FS_NUC);
    r->p_down = make_program(VS_FULL, FS_DOWN);
    r->p_up = make_program(VS_FULL, FS_UP);
    r->p_comp = make_program(VS_FULL, FS_COMP);
    if (!r->p_points || !r->p_super || !r->p_iso || !r->p_grid || !r->p_nuc || !r->p_down || !r->p_up || !r->p_comp) {
        renderer_destroy(r);
        return 0;
    }
    glGenVertexArrays(1, &r->vao_empty);
    glGenVertexArrays(1, &r->vao_points);
    glGenBuffers(1, &r->vbo_points);
    glGenVertexArrays(1, &r->vao_super);
    glGenBuffers(1, &r->vbo_super);
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
    if (r->p_super) glDeleteProgram(r->p_super);
    if (r->p_iso) glDeleteProgram(r->p_iso);
    if (r->vbo_super) glDeleteBuffers(1, &r->vbo_super);
    if (r->vao_super) glDeleteVertexArrays(1, &r->vao_super);
    if (r->vol_tex[0]) glDeleteTextures(2, r->vol_tex);
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

void renderer_set_super_points(Renderer *r, const SuperVertex *pts, int count) {
    glBindVertexArray(r->vao_super);
    glBindBuffer(GL_ARRAY_BUFFER, r->vbo_super);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)count * (GLsizeiptr)sizeof(SuperVertex), pts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(SuperVertex), (void *)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, sizeof(SuperVertex), (void *)(3 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_SHORT, GL_TRUE, sizeof(SuperVertex), (void *)(4 * sizeof(float)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_SHORT, GL_TRUE, sizeof(SuperVertex), (void *)(4 * sizeof(float) + 4 * sizeof(int16_t)));
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    r->super_count = count;
}

void renderer_set_volume(Renderer *r, const Volume *v) {
    if (!r->vol_tex[0]) glGenTextures(2, r->vol_tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    for (int t = 0; t < 2; t++) {
        glBindTexture(GL_TEXTURE_3D, r->vol_tex[t]);
        const uint16_t *src = v->tex[t] ? v->tex[t] : v->tex[0];   /* unused 2nd volume mirrors the 1st */
        glTexImage3D(GL_TEXTURE_3D, 0, GL_RGBA16F, v->res, v->res, v->res, 0, GL_RGBA, GL_HALF_FLOAT, src);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    }
    glBindTexture(GL_TEXTURE_3D, 0);
    r->vol_ready = 1; r->vol_k = v->K; r->vol_res = v->res; r->vol_half = v->half; r->vol_vscale = v->vscale;
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

    const int use_super = p->super;
    const int n_pts = use_super ? r->super_count : r->point_count;
    if (p->render_mode != RENDER_ISO && n_pts > 0) {
        const float N = (float)n_pts;
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

        GLuint pr = use_super ? r->p_super : r->p_points;
        glUseProgram(pr);
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
        if (use_super) {
            glUniform2fv(UL(pr, "uC"), SUPER_MAX, &p->coef[0][0]);
            glUniform1i(UL(pr, "uSuperAmp"), p->super_amp);
            glUniform1f(UL(pr, "uWeightGain"), p->weight_gain);
            glUniform1f(UL(pr, "uScalarGain"), p->scalar_gain);
        }
        if (p->style == STYLE_SPHERES) {
            glDisable(GL_BLEND);
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(GL_LESS);
        }
        glBindVertexArray(use_super ? r->vao_super : r->vao_points);
        glDrawArrays(GL_POINTS, 0, n_pts);
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

    if (p->render_mode != RENDER_PARTICLES && r->vol_ready) {
        Vec3 right, up, fwd;
        camera_get_basis(cam, &right, &up, &fwd);
        Vec3 eye = camera_get_position(cam);
        static const float axes[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
        const float *sn = axes[p->slice_axis < 0 || p->slice_axis > 2 ? 2 : p->slice_axis];
        GLuint pr = r->p_iso;
        glUseProgram(pr);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_3D, r->vol_tex[0]);
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_3D, r->vol_tex[1]);
        glActiveTexture(GL_TEXTURE0);
        glUniform1i(UL(pr, "uVol0"), 2);
        glUniform1i(UL(pr, "uVol1"), 3);
        glUniform3f(UL(pr, "uCamPos"), eye.x, eye.y, eye.z);
        glUniform3f(UL(pr, "uRight"), right.x, right.y, right.z);
        glUniform3f(UL(pr, "uUp"), up.x, up.y, up.z);
        glUniform3f(UL(pr, "uFwd"), fwd.x, fwd.y, fwd.z);
        glUniform1f(UL(pr, "uTanHalf"), tanf(cam->fovy * 0.5f));
        glUniform1f(UL(pr, "uAspect"), aspect);
        glUniform1f(UL(pr, "uHalf"), r->vol_half);
        glUniform1f(UL(pr, "uLevel"), p->iso_level);
        glUniform1f(UL(pr, "uOpacity"), clampf(p->iso_opacity, 0.02f, 1.0f));
        const float voxel = 2.0f * r->vol_half / (float)r->vol_res;
        glUniform1f(UL(pr, "uDt"), voxel * 0.8f);
        glUniform1f(UL(pr, "uEps"), voxel * 0.7f);
        glUniform1f(UL(pr, "uBright"), p->brightness);
        glUniform1i(UL(pr, "uMaxSteps"), (int)(2.0f * 1.7321f * (float)r->vol_res / 0.8f) + 8);
        glUniform1i(UL(pr, "uK"), r->vol_k);
        glUniform1i(UL(pr, "uSigned"), p->iso_signed);
        glUniform1i(UL(pr, "uWheel"), cs->wheel);
        glUniform2fv(UL(pr, "uC"), SUPER_MAX, &p->iso_coef[0][0]);
        glUniform3f(UL(pr, "uPos"), cs->pos.x, cs->pos.y, cs->pos.z);
        glUniform3f(UL(pr, "uNeg"), cs->neg.x, cs->neg.y, cs->neg.z);
        glUniform3fv(UL(pr, "uRamp"), 5, &cs->ramp[0].x);
        glUniform1i(UL(pr, "uSliceMode"), p->slice_mode);
        glUniform3f(UL(pr, "uSliceN"), sn[0], sn[1], sn[2]);
        glUniform1f(UL(pr, "uSliceOff"), p->slice_pos);
        glUniform1f(UL(pr, "uSliceHalf"), p->slice_half);
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        draw_fullscreen(r);
        glBlendFunc(GL_ONE, GL_ONE);
        glBindTexture(GL_TEXTURE_3D, 0);
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

/* ------------------------------------------------------------------ */
/* parameter helpers shared by the app and the tests                   */
/* ------------------------------------------------------------------ */

void render_params_set_super(RenderParams *p, const SuperSpec *s, const SuperSamplingResult *res,
                             double t_au, double global_phase, int amp_mode) {
    Complex c[SUPER_MAX];
    super_coeffs_at(s, t_au, c);
    const double cg = cos(-global_phase), sg = sin(-global_phase);
    p->super = 1;
    p->super_amp = amp_mode;
    for (int k = 0; k < SUPER_MAX; k++) {
        double re = c[k].r * cg - c[k].i * sg, im = c[k].r * sg + c[k].i * cg;
        double f = 1.0;
        if (amp_mode && res && res->gmax > 0.0) f = res->comp_norm[k] / res->gmax;
        p->coef[k][0] = (float)(re * f);
        p->coef[k][1] = (float)(im * f);
    }
    p->weight_gain = amp_mode ? 1.0f : (float)s->count;
    p->scalar_gain = amp_mode ? (float)(res && res->gmax > 0.0 ? res->gmax : 1.0) : 1.0f;
}

void render_params_set_iso(RenderParams *p, const SuperSpec *s, const Volume *vol, double t_au,
                           double global_phase, double probability, int signed_mode, float opacity) {
    Complex c[SUPER_MAX];
    super_coeffs_at(s, t_au, c);
    const double cg = cos(-global_phase), sg = sin(-global_phase);
    for (int k = 0; k < SUPER_MAX; k++) {
        double re = c[k].r * cg - c[k].i * sg, im = c[k].r * sg + c[k].i * cg;
        p->iso_coef[k][0] = (float)(re * vol->vscale);
        p->iso_coef[k][1] = (float)(im * vol->vscale);
    }
    p->iso_level = (float)sqrt(volume_level_for_probability(vol, probability));
    p->iso_opacity = opacity;
    p->iso_signed = signed_mode;
}
