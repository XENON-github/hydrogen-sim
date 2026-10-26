/* Headless OpenGL test harness (EGL + Mesa llvmpipe or any GPU): renders scenes through the real
 * renderer/HUD code and writes PNGs. Needs libEGL + cairo only (no GTK, no display).
 *
 *   make gltest            build + run all scenes into build/gltest/
 *   ./build/gl_harness <scene> <out.png> [w h]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <dlfcn.h>
#include <cairo.h>
#include <glad/glad.h>
#include "renderer.h"
#include "hud.h"
#include "sampling.h"
#include "volume.h"
#include "transitions.h"
#include "scene.h"
#include "recorder.h"

/* ---- minimal EGL (declared by hand so no EGL headers are needed) ---- */
typedef void *EGLDisplay, *EGLConfig, *EGLContext, *EGLSurface;
typedef int EGLint; typedef unsigned int EGLBoolean, EGLenum;
#define EGL_PLATFORM_SURFACELESS_MESA 0x31DD
#define EGL_NONE 0x3038
#define EGL_SURFACE_TYPE 0x3033
#define EGL_PBUFFER_BIT 0x0001
#define EGL_RENDERABLE_TYPE 0x3040
#define EGL_OPENGL_BIT 0x0008
#define EGL_OPENGL_API 0x30A2
#define EGL_CONTEXT_MAJOR_VERSION 0x3098
#define EGL_CONTEXT_MINOR_VERSION 0x30FB
#define EGL_CONTEXT_OPENGL_PROFILE_MASK 0x30FD
#define EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT 0x1
typedef EGLDisplay (*PFN_getPlatformDisplay)(EGLenum, void *, const EGLint *);

static void *g_egl;
static void *eglsym(const char *n) { return dlsym(g_egl, n); }

static int gl_init(void) {
    g_egl = dlopen("libEGL.so.1", RTLD_NOW);
    if (!g_egl) g_egl = dlopen("libEGL.so", RTLD_NOW);
    if (!g_egl) { fprintf(stderr, "libEGL not found\n"); return 0; }
    EGLDisplay (*getPD)(EGLenum, void *, const EGLint *) = eglsym("eglGetPlatformDisplay");
    EGLBoolean (*init)(EGLDisplay, EGLint *, EGLint *) = eglsym("eglInitialize");
    EGLBoolean (*choose)(EGLDisplay, const EGLint *, EGLConfig *, EGLint, EGLint *) = eglsym("eglChooseConfig");
    EGLBoolean (*bind)(EGLenum) = eglsym("eglBindAPI");
    EGLContext (*create)(EGLDisplay, EGLConfig, EGLContext, const EGLint *) = eglsym("eglCreateContext");
    EGLBoolean (*mk)(EGLDisplay, EGLSurface, EGLSurface, EGLContext) = eglsym("eglMakeCurrent");
    void *(*gpa)(const char *) = eglsym("eglGetProcAddress");
    EGLDisplay d = getPD(EGL_PLATFORM_SURFACELESS_MESA, NULL, NULL);
    EGLint a, b;
    if (!d || !init(d, &a, &b)) { fprintf(stderr, "EGL init failed\n"); return 0; }
    EGLint ca[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT, EGL_NONE};
    EGLConfig cfg; EGLint n;
    if (!choose(d, ca, &cfg, 1, &n) || n < 1) { fprintf(stderr, "no EGL config\n"); return 0; }
    bind(EGL_OPENGL_API);
    EGLint xa[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 3,
                   EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT, EGL_NONE};
    EGLContext c = create(d, cfg, NULL, xa);
    if (!c || !mk(d, NULL, NULL, c)) { fprintf(stderr, "cannot create a GL 3.3 core context\n"); return 0; }
    if (!gladLoadGLLoader((GLADloadproc)gpa)) { fprintf(stderr, "glad failed\n"); return 0; }
    return 1;
}

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: %s <scene> <out.png> [w h]\n", argv[0]); return 2; }
    int W = argc > 4 ? atoi(argv[3]) : 960, H = argc > 4 ? atoi(argv[4]) : 640;
    if (!gl_init()) return 1;
    printf("GL %s | %s\n", glGetString(GL_VERSION), glGetString(GL_RENDERER));
    Scene sc;
    if (!scene_setup(&sc, argv[1], W, H)) { fprintf(stderr, "unknown scene or setup failure: %s\n", argv[1]); return 1; }

    GLuint fbo, tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);

    if (strstr(argv[2], ".gif") || strstr(argv[2], ".mp4") || strstr(argv[2], ".webm")) {
        /* animation mode: same Recorder the app uses */
        int nf = getenv("HARNESS_FRAMES") ? atoi(getenv("HARNESS_FRAMES")) : 48;
        RecFormat fmt = strstr(argv[2], ".gif") ? REC_GIF : strstr(argv[2], ".mp4") ? REC_MP4 : REC_WEBM;
        char err[160];
        /* frames are rendered at W x H (HUD layout is pixel-based) and optionally downscaled by HARNESS_SCALE for small files */
        double sc_out = getenv("HARNESS_SCALE") ? atof(getenv("HARNESS_SCALE")) : 1.0;
        Recorder *rec = recorder_open(fmt, argv[2], (int)(W * sc_out), (int)(H * sc_out), 24, GIF_PIX_BGRA, err, sizeof err);
        if (!rec) { fprintf(stderr, "recorder: %s\n", err); return 1; }
        int OW = recorder_width(rec), OH = recorder_height(rec);
        uint8_t *px = malloc((size_t)W * H * 4);
        cairo_surface_t *sf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, W, H);
        cairo_surface_t *osf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, OW, OH);
        float yaw0 = sc.cam.yaw;
        double turns = getenv("HARNESS_ORBIT") ? atof(getenv("HARNESS_ORBIT")) : 0.0;     /* camera turns per loop */
        for (int f = 0; f < nf; f++) {
            int timed = scene_set_frac(&sc, (double)f / nf);
            if (!timed && turns == 0.0) { fprintf(stderr, "scene %s has no time dependence: set HARNESS_ORBIT=1 for a turntable\n", argv[1]); return 1; }
            sc.cam.yaw = yaw0 + (float)(turns * 2.0 * M_PI * f / nf);
            scene_render(&sc, fbo, W, H);
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px);
            uint8_t *dst = cairo_image_surface_get_data(sf);
            int stride = cairo_image_surface_get_stride(sf);
            for (int y = 0; y < H; y++) {
                const uint8_t *s2 = px + (size_t)(H - 1 - y) * W * 4;
                uint8_t *o = dst + (size_t)y * stride;
                for (int x = 0; x < W; x++) { o[4*x] = s2[4*x+2]; o[4*x+1] = s2[4*x+1]; o[4*x+2] = s2[4*x]; o[4*x+3] = 255; }
            }
            cairo_surface_mark_dirty(sf);
            cairo_t *cr = cairo_create(sf);
            scene_draw_hud(&sc, cr, W, H);
            cairo_destroy(cr);
            cairo_surface_flush(sf);
            cairo_t *oc = cairo_create(osf);
            cairo_scale(oc, (double)OW / W, (double)OH / H);
            cairo_set_source_surface(oc, sf, 0, 0);
            cairo_pattern_set_filter(cairo_get_source(oc), CAIRO_FILTER_BEST);
            cairo_paint(oc);
            cairo_destroy(oc);
            cairo_surface_flush(osf);
            if (!recorder_write(rec, cairo_image_surface_get_data(osf), cairo_image_surface_get_stride(osf))) { fprintf(stderr, "write failed\n"); return 1; }
        }
        int ok = recorder_close(rec);
        printf("%s %s (%d frames, %dx%d)\n", ok ? "wrote" : "FAILED", argv[2], nf, OW, OH);
        return !ok;
    }
    scene_render(&sc, fbo, W, H);
    { GLenum e = glGetError(); if (e) fprintf(stderr, "GL error 0x%x after render\n", e); }

    uint8_t *px = malloc((size_t)W * H * 4);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px);
    long lit = 0;                                /* sanity check: a scene must not render black */
    for (long i = 0; i < (long)W * H; i++) if (px[4*i] + px[4*i+1] + px[4*i+2] > 120) lit++;
    double lit_frac = (double)lit / ((double)W * H);
    cairo_surface_t *sf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, W, H);
    uint8_t *dst = cairo_image_surface_get_data(sf);
    int stride = cairo_image_surface_get_stride(sf);
    for (int y = 0; y < H; y++) {
        const uint8_t *s = px + (size_t)(H - 1 - y) * W * 4;
        uint8_t *o = dst + (size_t)y * stride;
        for (int x = 0; x < W; x++) { o[4*x] = s[4*x+2]; o[4*x+1] = s[4*x+1]; o[4*x+2] = s[4*x]; o[4*x+3] = 255; }
    }
    cairo_surface_mark_dirty(sf);
    if (sc.hud) {
        cairo_t *cr = cairo_create(sf);
        scene_draw_hud(&sc, cr, W, H);
        cairo_destroy(cr);
    }
    cairo_status_t st = cairo_surface_write_to_png(sf, argv[2]);
    printf("%s %s (%.0f ms render, %.1f%% lit)\n", st == CAIRO_STATUS_SUCCESS ? "wrote" : "FAILED", argv[2], sc.render_ms, 100.0 * lit_frac);
    if (lit_frac < 0.002) { fprintf(stderr, "FAIL: scene %s rendered (almost) black\n", argv[1]); return 1; }
    cairo_surface_destroy(sf);
    free(px);
    return st != CAIRO_STATUS_SUCCESS;
}
