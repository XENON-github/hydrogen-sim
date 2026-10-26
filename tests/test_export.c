/* Exercises the GIF encoder and the recorder (GIF + optional ffmpeg MP4/WebM). Writes into the CWD /tmp dir given in argv[1]. */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "recorder.h"
#include "platform.h"
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

/* synthetic animated frame: dark background + a glowing blob orbiting the centre + colour ramp strip */
static void frame(uint8_t *px, int w, int h, int f, int nf) {
    double a = 2 * M_PI * f / nf, cx = w / 2 + 0.3 * w * cos(a), cy = h / 2 + 0.3 * h * sin(a);
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
        double d2 = ((x - cx) * (x - cx) + (y - cy) * (y - cy)) / (0.02 * w * w), g = exp(-d2);
        uint8_t *p = px + 4 * ((size_t)y * w + x);
        p[0] = (uint8_t)(255 * g);                         /* B (BGRA order) */
        p[1] = (uint8_t)(40 + 200 * g * 0.7);
        p[2] = (uint8_t)(10 + 240 * g * (0.5 + 0.5 * sin(a)));
        p[3] = 255;
        if (y > h - 12) { p[0] = (uint8_t)(255.0 * x / w); p[1] = (uint8_t)(255.0 * (w - x) / w); p[2] = 128; }
    }
}

int main(int argc, char **argv) {
    const char *dir = argc > 1 ? argv[1] : ".";
    const int W = 320, H = 240, NF = 24;
    uint8_t *px = malloc((size_t)W * H * 4);
    char path[512], err[128];
    for (int fmt = 0; fmt < 3; fmt++) {
        if (!recorder_format_available((RecFormat)fmt)) { printf("skip %s (no ffmpeg)\n", recorder_extension((RecFormat)fmt)); continue; }
        snprintf(path, sizeof path, "%s/out.%s", dir, recorder_extension((RecFormat)fmt));
        Recorder *r = recorder_open((RecFormat)fmt, path, W, H, 24, GIF_PIX_BGRA, err, sizeof err);
        CHECK(r != NULL, "open %s: %s", path, err);
        if (!r) continue;
        for (int f = 0; f < NF; f++) { frame(px, W, H, f, NF); CHECK(recorder_write(r, px, W * 4), "write frame %d", f); }
        CHECK(recorder_frames(r) == NF, "frames");
        CHECK(recorder_close(r), "close %s", path);
    }
    /* tricky GIF cases: flat colour, noise (forces LZW table resets) */
    { snprintf(path, sizeof path, "%s/flat.gif", dir); GifWriter *g = gif_open(path, 100, 80, 5, 0); memset(px, 77, 100 * 80 * 4); CHECK(gif_add_frame(g, px, 400, GIF_PIX_RGBA), "flat");
      for (int i = 0; i < 100 * 80 * 4; i++) { px[i] = (uint8_t)rand(); }
      CHECK(gif_add_frame(g, px, 400, GIF_PIX_RGBA), "noise"); CHECK(gif_close(g), "close"); }
    printf(fails ? "%d FAILURES\n" : "export tests passed\n", fails);
    free(px);
    return fails != 0;
}
