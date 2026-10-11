#include "colors.h"

#define V(r, g, b) {r, g, b}
static const ColorScheme k_schemes[] = {
    { "Aurora  (cyan cloud · gold / violet wave)",
      { V(0.03f, 0.12f, 0.40f), V(0.05f, 0.40f, 0.85f), V(0.12f, 0.75f, 0.98f), V(0.55f, 0.95f, 1.0f), V(0.95f, 1.0f, 1.0f) },
      V(1.00f, 0.70f, 0.18f), V(0.55f, 0.36f, 1.00f), 0 },
    { "Classic  (cyan cloud · red / blue wave)",
      { V(0.03f, 0.12f, 0.40f), V(0.05f, 0.40f, 0.85f), V(0.12f, 0.75f, 0.98f), V(0.55f, 0.95f, 1.0f), V(0.95f, 1.0f, 1.0f) },
      V(1.00f, 0.26f, 0.22f), V(0.20f, 0.52f, 1.00f), 0 },
    { "Colour-blind safe  (orange / blue)",
      { V(0.02f, 0.10f, 0.30f), V(0.00f, 0.32f, 0.62f), V(0.20f, 0.60f, 0.90f), V(0.60f, 0.85f, 1.0f), V(0.95f, 0.98f, 1.0f) },
      V(1.00f, 0.62f, 0.05f), V(0.00f, 0.45f, 0.90f), 0 },
    { "Inferno  (heat map)",
      { V(0.04f, 0.01f, 0.20f), V(0.45f, 0.10f, 0.45f), V(0.85f, 0.25f, 0.30f), V(0.98f, 0.62f, 0.10f), V(1.0f, 0.96f, 0.65f) },
      V(1.00f, 0.80f, 0.25f), V(0.66f, 0.16f, 0.88f), 0 },
    { "Phase wheel  (colour = arg ψ)",
      { V(0.03f, 0.12f, 0.40f), V(0.05f, 0.40f, 0.85f), V(0.12f, 0.75f, 0.98f), V(0.55f, 0.95f, 1.0f), V(0.95f, 1.0f, 1.0f) },
      V(1.00f, 0.70f, 0.18f), V(0.55f, 0.36f, 1.00f), 1 },
};

int color_scheme_count(void) { return (int)(sizeof(k_schemes) / sizeof(k_schemes[0])); }

const ColorScheme *color_scheme_get(int idx) {
    if (idx < 0 || idx >= color_scheme_count()) idx = 0;
    return &k_schemes[idx];
}

Vec3 color_hsv(float h, float s, float v) {
    h -= floorf(h);
    float r = fabsf(h * 6.0f - 3.0f) - 1.0f;
    float g = 2.0f - fabsf(h * 6.0f - 2.0f);
    float b = 2.0f - fabsf(h * 6.0f - 4.0f);
    r = clampf(r, 0, 1); g = clampf(g, 0, 1); b = clampf(b, 0, 1);
    return vec3_create(v * (1 - s + s * r), v * (1 - s + s * g), v * (1 - s + s * b));
}

Vec3 color_ramp(const ColorScheme *s, float t) {
    t = clampf(t, 0.0f, 1.0f) * 4.0f;
    int i = (int)t;
    if (i > 3) i = 3;
    float f = t - (float)i;
    Vec3 a = s->ramp[i], b = s->ramp[i + 1];
    return vec3_create(a.x + (b.x - a.x) * f, a.y + (b.y - a.y) * f, a.z + (b.z - a.z) * f);
}
