#ifndef COLORS_H
#define COLORS_H

#include "math_utils.h"

/* A colour scheme drives both the GPU shaders and the HUD legends (single source of truth).
 * Colours are sRGB in 0..1. */
typedef struct {
    const char *name;
    Vec3 ramp[5];   /* density colour ramp, low -> high |psi|^2 */
    Vec3 pos, neg;  /* wavefunction sign colours (+psi / -psi) */
    int wheel;      /* 1: colour by complex phase (hue wheel) */
} ColorScheme;

int color_scheme_count(void);
const ColorScheme *color_scheme_get(int idx);
Vec3 color_hsv(float h, float s, float v);       /* h in turns [0,1) */
Vec3 color_ramp(const ColorScheme *s, float t);  /* t in 0..1 */

#endif /* COLORS_H */
