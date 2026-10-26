#ifndef GIFENC_H
#define GIFENC_H

#include <stdint.h>

/* Minimal animated-GIF (GIF89a) writer: per-frame median-cut palette, ordered dithering, LZW.
 * No dependencies, so GIF export works on every platform. */

typedef enum { GIF_PIX_RGBA = 0, GIF_PIX_BGRA = 1 } GifPixelOrder;

typedef struct GifWriter GifWriter;

/* delay_cs: frame delay in 1/100 s. loop: 1 = repeat forever. Returns NULL on failure. */
GifWriter *gif_open(const char *path, int width, int height, int delay_cs, int loop);
/* `px` is width*height 4-byte pixels, top row first, `stride` bytes per row (alpha ignored). */
int gif_add_frame(GifWriter *g, const uint8_t *px, int stride, GifPixelOrder order);
/* Finishes the file. Returns 1 if every write succeeded. */
int gif_close(GifWriter *g);

#endif /* GIFENC_H */
