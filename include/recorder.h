#ifndef RECORDER_H
#define RECORDER_H

#include <stddef.h>
#include <stdint.h>
#include "gifenc.h"

/* Video / animation sink. GIF is encoded in-process; MP4 and WebM are piped to the `ffmpeg` executable
 * (used only if it is found on PATH, see plat_have_ffmpeg()). */

typedef enum { REC_GIF = 0, REC_MP4 = 1, REC_WEBM = 2 } RecFormat;

typedef struct Recorder Recorder;

const char *recorder_extension(RecFormat f);          /* "gif", "mp4", "webm" */
int recorder_format_available(RecFormat f);           /* GIF always; MP4/WebM need ffmpeg */

/* w and h are rounded down to even numbers for the ffmpeg formats (yuv420p); query with
 * recorder_width()/recorder_height(). On failure returns NULL and writes a message into err. */
Recorder *recorder_open(RecFormat fmt, const char *path, int w, int h, int fps, GifPixelOrder order,
                        char *err, size_t err_size);
int recorder_width(const Recorder *r);
int recorder_height(const Recorder *r);
int recorder_frames(const Recorder *r);
/* Frame is the full w*h image (top row first). The source may be larger than the recorder
 * (extra columns/rows are ignored); `stride` is bytes per row. */
int recorder_write(Recorder *r, const uint8_t *px, int stride);
/* Finish and close. Returns 1 on success. */
int recorder_close(Recorder *r);

#endif /* RECORDER_H */
