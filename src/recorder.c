#include "recorder.h"
#include "platform.h"
#include <stdlib.h>
#include <string.h>

struct Recorder {
    RecFormat fmt;
    int w, h, fps, frames, ok;
    GifPixelOrder order;
    GifWriter *gif;
    FILE *pipe;
};

const char *recorder_extension(RecFormat f) { return f == REC_GIF ? "gif" : f == REC_MP4 ? "mp4" : "webm"; }
int recorder_format_available(RecFormat f) { return f == REC_GIF ? 1 : plat_have_ffmpeg(); }

/* Quote a path for the platform shell. */
static int quote_path(const char *in, char *out, size_t n) {
    size_t o = 0;
#ifdef _WIN32
    if (o + 1 < n) out[o++] = '"';
    for (; *in; in++) { if (*in == '"') return 0; if (o + 2 >= n) return 0; out[o++] = *in; }
    if (o + 2 > n) return 0;
    out[o++] = '"';
#else
    if (o + 1 < n) out[o++] = '\'';
    for (; *in; in++) {
        if (*in == '\'') { if (o + 4 >= n) return 0; memcpy(out + o, "'\\''", 4); o += 4; }
        else { if (o + 2 >= n) return 0; out[o++] = *in; }
    }
    if (o + 2 > n) return 0;
    out[o++] = '\'';
#endif
    out[o] = 0;
    return 1;
}

Recorder *recorder_open(RecFormat fmt, const char *path, int w, int h, int fps, GifPixelOrder order,
                        char *err, size_t es) {
    if (err && es) err[0] = 0;
    if (fps < 1) fps = 1;
    if (fps > 120) fps = 120;
    if (fmt != REC_GIF) { w &= ~1; h &= ~1; }
    if (w < 16 || h < 16) { if (err) snprintf(err, es, "frame size too small"); return NULL; }
    Recorder *r = calloc(1, sizeof(*r));
    if (!r) return NULL;
    r->fmt = fmt; r->w = w; r->h = h; r->fps = fps; r->order = order; r->ok = 1;
    if (fmt == REC_GIF) {
        r->gif = gif_open(path, w, h, (100 + fps / 2) / fps, 1);
        if (!r->gif) { if (err) snprintf(err, es, "cannot create %s", path); free(r); return NULL; }
        return r;
    }
    if (!plat_have_ffmpeg()) { if (err) snprintf(err, es, "ffmpeg was not found on PATH"); free(r); return NULL; }
    char q[1024], cmd[2048];
    if (!quote_path(path, q, sizeof(q))) { if (err) snprintf(err, es, "bad output path"); free(r); return NULL; }
    const char *pf = order == GIF_PIX_RGBA ? "rgba" : "bgra";
    const char *codec = fmt == REC_MP4
        ? "-c:v libx264 -preset medium -crf 18 -pix_fmt yuv420p -movflags +faststart"
        : "-c:v libvpx-vp9 -crf 30 -b:v 0 -pix_fmt yuv420p";
    snprintf(cmd, sizeof(cmd),
             "ffmpeg -y -hide_banner -loglevel error -f rawvideo -pix_fmt %s -s %dx%d -r %d -i - -an %s %s",
             pf, w, h, fps, codec, q);
    r->pipe = PLAT_POPEN(cmd, PLAT_POPEN_WRITE);
    if (!r->pipe) { if (err) snprintf(err, es, "could not start ffmpeg"); free(r); return NULL; }
    return r;
}

int recorder_width(const Recorder *r) { return r->w; }
int recorder_height(const Recorder *r) { return r->h; }
int recorder_frames(const Recorder *r) { return r->frames; }

int recorder_write(Recorder *r, const uint8_t *px, int stride) {
    if (!r || !r->ok) return 0;
    if (r->fmt == REC_GIF) {
        r->ok = gif_add_frame(r->gif, px, stride, r->order);
    } else {
        for (int y = 0; y < r->h && r->ok; y++)
            if (fwrite(px + (size_t)y * stride, 4, (size_t)r->w, r->pipe) != (size_t)r->w) r->ok = 0;
    }
    if (r->ok) r->frames++;
    return r->ok;
}

int recorder_close(Recorder *r) {
    if (!r) return 0;
    int ok = r->ok;
    if (r->fmt == REC_GIF) ok = gif_close(r->gif) && ok;
    else if (r->pipe) { fflush(r->pipe); ok = (PLAT_PCLOSE(r->pipe) == 0) && ok; }
    free(r);
    return ok;
}
