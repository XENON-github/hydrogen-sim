#include "gifenc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct GifWriter {
    FILE *f;
    int w, h, delay, loop, frames, ok;
    uint8_t *idx;       /* w*h palette indices */
};

/* ------------------------------ palette (median cut on a 5-bit histogram) ------------------------------ */

typedef struct { uint32_t cnt; uint32_t sr, sg, sb; } Bin;
typedef struct { int lo, hi; uint32_t total; int axis_range; } Box;   /* range into sorted `ids` */

static int g_axis;   /* qsort context (single-threaded use inside one call) */
static int cmp_ids(const void *a, const void *b) {
    int x = *(const int *)a, y = *(const int *)b;
    int cx = g_axis == 0 ? (x >> 10) & 31 : g_axis == 1 ? (x >> 5) & 31 : x & 31;
    int cy = g_axis == 0 ? (y >> 10) & 31 : g_axis == 1 ? (y >> 5) & 31 : y & 31;
    return cx - cy;
}

static void box_stats(const Bin *hist, const int *ids, Box *b, int *axis) {
    int mn[3] = {31, 31, 31}, mx[3] = {0, 0, 0};
    uint32_t tot = 0;
    for (int i = b->lo; i < b->hi; i++) {
        int id = ids[i], c[3] = {(id >> 10) & 31, (id >> 5) & 31, id & 31};
        for (int a = 0; a < 3; a++) { if (c[a] < mn[a]) mn[a] = c[a]; if (c[a] > mx[a]) mx[a] = c[a]; }
        tot += hist[id].cnt;
    }
    int best = 0, br = -1;
    for (int a = 0; a < 3; a++) if (mx[a] - mn[a] > br) { br = mx[a] - mn[a]; best = a; }
    b->total = tot; b->axis_range = br; *axis = best;
}

static int build_palette(const uint8_t *px, int w, int h, int stride, GifPixelOrder order, uint8_t pal[256][3], uint8_t *lut) {
    static Bin hist[32768];
    memset(hist, 0, sizeof(hist));
    int ro = order == GIF_PIX_RGBA ? 0 : 2, bo = order == GIF_PIX_RGBA ? 2 : 0;
    int step = (w * h > 200000) ? 2 : 1;           /* subsample big frames for the histogram */
    for (int y = 0; y < h; y += step) {
        const uint8_t *row = px + (size_t)y * stride;
        for (int x = 0; x < w; x += step) {
            int r = row[4 * x + ro], g = row[4 * x + 1], b = row[4 * x + bo];
            Bin *B = &hist[((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3)];
            B->cnt++; B->sr += r; B->sg += g; B->sb += b;
        }
    }
    int *ids = malloc(32768 * sizeof(int)), n = 0;
    if (!ids) return 0;
    for (int i = 0; i < 32768; i++) if (hist[i].cnt) ids[n++] = i;
    Box boxes[256];
    int nb = 1;
    boxes[0] = (Box){0, n, 0, 0};
    int axis[256];
    if (n == 0) { free(ids); memset(pal, 0, 768); memset(lut, 0, 32768); return 1; }
    box_stats(hist, ids, &boxes[0], &axis[0]);
    while (nb < 256) {
        int pick = -1; double bs = -1.0;
        for (int i = 0; i < nb; i++) {
            if (boxes[i].hi - boxes[i].lo < 2) continue;
            double score = (double)boxes[i].total * (boxes[i].axis_range + 1);   /* popularity x extent */
            if (score > bs) { bs = score; pick = i; }
        }
        if (pick < 0) break;
        Box *b = &boxes[pick];
        g_axis = axis[pick];
        qsort(ids + b->lo, (size_t)(b->hi - b->lo), sizeof(int), cmp_ids);
        uint32_t half = b->total / 2, acc = 0;
        int cut = b->lo + 1;
        for (int i = b->lo; i < b->hi - 1; i++) { acc += hist[ids[i]].cnt; cut = i + 1; if (acc >= half) break; }
        Box nbx = {cut, b->hi, 0, 0};
        b->hi = cut;
        box_stats(hist, ids, b, &axis[pick]);
        box_stats(hist, ids, &nbx, &axis[nb]);
        boxes[nb++] = nbx;
    }
    for (int i = 0; i < 256; i++) {
        if (i >= nb) { pal[i][0] = pal[i][1] = pal[i][2] = 0; continue; }
        uint64_t sr = 0, sg = 0, sb = 0, c = 0;
        for (int k = boxes[i].lo; k < boxes[i].hi; k++) { Bin *B = &hist[ids[k]]; sr += B->sr; sg += B->sg; sb += B->sb; c += B->cnt; }
        if (!c) c = 1;
        pal[i][0] = (uint8_t)(sr / c); pal[i][1] = (uint8_t)(sg / c); pal[i][2] = (uint8_t)(sb / c);
    }
    free(ids);
    /* nearest palette entry for every 5-bit cell */
    for (int cell = 0; cell < 32768; cell++) {
        int r = ((cell >> 10) & 31) * 8 + 4, g = ((cell >> 5) & 31) * 8 + 4, b = (cell & 31) * 8 + 4;
        int best = 0, bd = 1 << 30;
        for (int i = 0; i < nb; i++) {
            int dr = r - pal[i][0], dg = g - pal[i][1], db = b - pal[i][2];
            int d = 2 * dr * dr + 4 * dg * dg + 3 * db * db;     /* perceptual-ish weights */
            if (d < bd) { bd = d; best = i; }
        }
        lut[cell] = (uint8_t)best;
    }
    return 1;
}

/* ------------------------------ LZW ------------------------------ */

typedef struct { FILE *f; uint8_t buf[255]; int n; uint32_t acc; int nbits; int ok; } BitOut;

static void flush_block(BitOut *o) {
    if (!o->n) return;
    uint8_t len = (uint8_t)o->n;
    if (fwrite(&len, 1, 1, o->f) != 1 || fwrite(o->buf, 1, (size_t)o->n, o->f) != (size_t)o->n) o->ok = 0;
    o->n = 0;
}
static void put_byte(BitOut *o, uint8_t b) { o->buf[o->n++] = b; if (o->n == 255) flush_block(o); }
static void put_code(BitOut *o, int code, int bits) {
    o->acc |= (uint32_t)code << o->nbits;
    o->nbits += bits;
    while (o->nbits >= 8) { put_byte(o, (uint8_t)(o->acc & 0xFF)); o->acc >>= 8; o->nbits -= 8; }
}

static int lzw_encode(FILE *f, const uint8_t *idx, size_t n, int min_code_size) {
    BitOut o = {f, {0}, 0, 0, 0, 1};
    uint8_t mcs = (uint8_t)min_code_size;
    if (fwrite(&mcs, 1, 1, f) != 1) return 0;
    const int clear = 1 << min_code_size, eoi = clear + 1;
    enum { HSIZE = 8192 };
    static int32_t hkey[HSIZE];
    static int16_t hval[HSIZE];
    int next, bits;
#define RESET() do { for (int i_ = 0; i_ < HSIZE; i_++) hkey[i_] = -1; next = eoi + 1; bits = min_code_size + 1; } while (0)
    RESET();
    put_code(&o, clear, bits);
    int prefix = idx[0];
    for (size_t i = 1; i < n; i++) {
        int c = idx[i];
        int32_t key = (prefix << 8) | c;
        uint32_t h = ((uint32_t)key * 2654435761u) >> 19;     /* 13-bit hash */
        int found = -1;
        while (hkey[h] != -1) { if (hkey[h] == key) { found = hval[h]; break; } h = (h + 1) & (HSIZE - 1); }
        if (found >= 0) { prefix = found; continue; }
        put_code(&o, prefix, bits);
        if (next < 4096) {
            hkey[h] = key; hval[h] = (int16_t)next;
            if (next == (1 << bits) && bits < 12) bits++;
            else if (next == (1 << bits)) { /* unreachable: bits capped below */ }
            next++;
        } else {
            put_code(&o, clear, bits);
            RESET();
        }
        prefix = c;
    }
    put_code(&o, prefix, bits);
    put_code(&o, eoi, bits);
    if (o.nbits > 0) put_byte(&o, (uint8_t)(o.acc & 0xFF));
    flush_block(&o);
    uint8_t z = 0;
    if (fwrite(&z, 1, 1, f) != 1) o.ok = 0;
    return o.ok;
#undef RESET
}

/* ------------------------------ public API ------------------------------ */

static void w16(FILE *f, int v) { fputc(v & 0xFF, f); fputc((v >> 8) & 0xFF, f); }

GifWriter *gif_open(const char *path, int w, int h, int delay_cs, int loop) {
    if (w < 1 || h < 1 || w > 65535 || h > 65535) return NULL;
    GifWriter *g = calloc(1, sizeof(*g));
    if (!g) return NULL;
    g->f = fopen(path, "wb");
    g->idx = malloc((size_t)w * h);
    if (!g->f || !g->idx) { if (g->f) fclose(g->f); free(g->idx); free(g); return NULL; }
    g->w = w; g->h = h; g->delay = delay_cs < 2 ? 2 : delay_cs; g->loop = loop; g->ok = 1;
    fwrite("GIF89a", 1, 6, g->f);
    w16(g->f, w); w16(g->f, h);
    fputc(0x00, g->f);          /* no global palette */
    fputc(0, g->f); fputc(0, g->f);
    if (loop) {
        fwrite("\x21\xFF\x0BNETSCAPE2.0\x03\x01\x00\x00\x00", 1, 19, g->f);
    }
    return g;
}

static const uint8_t k_bayer8[8][8] = {
    { 0, 32,  8, 40,  2, 34, 10, 42}, {48, 16, 56, 24, 50, 18, 58, 26},
    {12, 44,  4, 36, 14, 46,  6, 38}, {60, 28, 52, 20, 62, 30, 54, 22},
    { 3, 35, 11, 43,  1, 33,  9, 41}, {51, 19, 59, 27, 49, 17, 57, 25},
    {15, 47,  7, 39, 13, 45,  5, 37}, {63, 31, 55, 23, 61, 29, 53, 21}};

int gif_add_frame(GifWriter *g, const uint8_t *px, int stride, GifPixelOrder order) {
    if (!g || !g->ok) return 0;
    uint8_t pal[256][3], lut[32768];
    if (!build_palette(px, g->w, g->h, stride, order, pal, lut)) { g->ok = 0; return 0; }
    const int ro = order == GIF_PIX_RGBA ? 0 : 2, bo = order == GIF_PIX_RGBA ? 2 : 0;
    for (int y = 0; y < g->h; y++) {
        const uint8_t *row = px + (size_t)y * stride;
        uint8_t *o = g->idx + (size_t)y * g->w;
        for (int x = 0; x < g->w; x++) {
            int d = ((int)k_bayer8[y & 7][x & 7] - 32) / 5;          /* +-6 levels of ordered dither */
            int r = row[4 * x + ro] + d, gg = row[4 * x + 1] + d, b = row[4 * x + bo] + d;
            r = r < 0 ? 0 : r > 255 ? 255 : r; gg = gg < 0 ? 0 : gg > 255 ? 255 : gg; b = b < 0 ? 0 : b > 255 ? 255 : b;
            o[x] = lut[((r >> 3) << 10) | ((gg >> 3) << 5) | (b >> 3)];
        }
    }
    FILE *f = g->f;
    fwrite("\x21\xF9\x04\x04", 1, 4, f);      /* graphic control: disposal 1 */
    w16(f, g->delay); fputc(0, f); fputc(0, f);
    fputc(0x2C, f);
    w16(f, 0); w16(f, 0); w16(f, g->w); w16(f, g->h);
    fputc(0x87, f);                           /* local colour table, 256 entries */
    fwrite(pal, 1, 768, f);
    if (!lzw_encode(f, g->idx, (size_t)g->w * g->h, 8)) g->ok = 0;
    g->frames++;
    return g->ok;
}

int gif_close(GifWriter *g) {
    if (!g) return 0;
    fputc(0x3B, g->f);
    int ok = g->ok && fflush(g->f) == 0;
    if (fclose(g->f) != 0) ok = 0;
    free(g->idx);
    free(g);
    return ok;
}
