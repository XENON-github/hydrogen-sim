#include <math.h>
#include <stdio.h>
#include <string.h>
#include "hud.h"
#include "renderer.h"
#include "colors.h"

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */

typedef struct { Mat4 vp; double w, h; } Proj;

static int proj_pt(const Proj *P, Vec3 p, double *x, double *y) {
    Vec4 c = mat4_mul_vec4(P->vp, (Vec4){p.x, p.y, p.z, 1.0f});
    if (c.w <= 1e-4f) return 0;
    *x = (c.x / c.w * 0.5 + 0.5) * P->w;
    *y = (1.0 - (c.y / c.w * 0.5 + 0.5)) * P->h;
    return 1;
}

/* Projects a 3D segment, clipping it against the camera near plane. */
static int proj_seg(const Proj *P, Vec3 a, Vec3 b, double *x0, double *y0, double *x1, double *y1) {
    Vec4 ca = mat4_mul_vec4(P->vp, (Vec4){a.x, a.y, a.z, 1.0f});
    Vec4 cb = mat4_mul_vec4(P->vp, (Vec4){b.x, b.y, b.z, 1.0f});
    const float eps = 1e-3f;
    if (ca.w < eps && cb.w < eps) return 0;
    if (ca.w < eps) { float t = (eps - ca.w) / (cb.w - ca.w); ca = (Vec4){ca.x + (cb.x - ca.x) * t, ca.y + (cb.y - ca.y) * t, ca.z + (cb.z - ca.z) * t, eps}; }
    if (cb.w < eps) { float t = (eps - cb.w) / (ca.w - cb.w); cb = (Vec4){cb.x + (ca.x - cb.x) * t, cb.y + (ca.y - cb.y) * t, cb.z + (ca.z - cb.z) * t, eps}; }
    *x0 = (ca.x / ca.w * 0.5 + 0.5) * P->w; *y0 = (1.0 - (ca.y / ca.w * 0.5 + 0.5)) * P->h;
    *x1 = (cb.x / cb.w * 0.5 + 0.5) * P->w; *y1 = (1.0 - (cb.y / cb.w * 0.5 + 0.5)) * P->h;
    return 1;
}

static void line3d(cairo_t *cr, const Proj *P, Vec3 a, Vec3 b) {
    double x0, y0, x1, y1;
    if (!proj_seg(P, a, b, &x0, &y0, &x1, &y1)) return;
    cairo_move_to(cr, x0, y0);
    cairo_line_to(cr, x1, y1);
}

enum { ALIGN_LEFT = 0, ALIGN_CENTER = 1, ALIGN_RIGHT = 2 };

static void set_font(cairo_t *cr, double size, int bold) {
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, bold ? CAIRO_FONT_WEIGHT_BOLD : CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, size);
}

static double text_width(cairo_t *cr, const char *s) {
    cairo_text_extents_t e;
    cairo_text_extents(cr, s, &e);
    return e.x_advance;
}

/* Text with a soft dark outline so it stays legible on top of bright clouds. */
static void text(cairo_t *cr, const char *s, double x, double y, double size, int bold,
                 double r, double g, double b, double a, int align) {
    set_font(cr, size, bold);
    double w = text_width(cr, s);
    if (align == ALIGN_CENTER) x -= w * 0.5;
    else if (align == ALIGN_RIGHT) x -= w;
    cairo_save(cr);
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.55 * a);
    for (int dx = -1; dx <= 1; dx++)
        for (int dy = -1; dy <= 1; dy++) {
            if (!dx && !dy) continue;
            cairo_move_to(cr, x + dx, y + dy);
            cairo_show_text(cr, s);
        }
    cairo_set_source_rgba(cr, r, g, b, a);
    cairo_move_to(cr, x, y);
    cairo_show_text(cr, s);
    cairo_restore(cr);
}

static void round_rect(cairo_t *cr, double x, double y, double w, double h, double r) {
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r, r, -M_PI / 2, 0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI / 2);
    cairo_arc(cr, x + r, y + h - r, r, M_PI / 2, M_PI);
    cairo_arc(cr, x + r, y + r, r, M_PI, 1.5 * M_PI);
    cairo_close_path(cr);
}

static void panel(cairo_t *cr, double x, double y, double w, double h) {
    round_rect(cr, x, y, w, h, 9);
    cairo_set_source_rgba(cr, 0.03, 0.05, 0.09, 0.62);
    cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr, 0.55, 0.70, 0.95, 0.18);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);
}

static const double AX_COL[3][3] = {{0.96, 0.38, 0.38}, {0.45, 0.88, 0.45}, {0.42, 0.68, 1.00}};
static const char *AX_NAME[3] = {"x", "y", "z"};
static const Vec3 AX_DIR[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};

static void fmt_num(char *buf, size_t n, double v) {
    if (fabs(v - round(v)) < 1e-6) snprintf(buf, n, "%.0f", v);
    else if (fabs(v * 10 - round(v * 10)) < 1e-6) snprintf(buf, n, "%.1f", v);
    else snprintf(buf, n, "%.2f", v);
}

/* ------------------------------------------------------------------ */
/* scene annotations                                                   */
/* ------------------------------------------------------------------ */

static void draw_axes(cairo_t *cr, const HudState *s, const Proj *P) {
    float step = s->grid_step > 0 ? s->grid_step : 1.0f;
    float L = ceilf(s->extent * 0.95f / step) * step;
    if (L < step) L = step;
    int nticks = (int)(L / step + 0.5f);
    int every = nticks > 10 ? 2 : 1;

    for (int a = 0; a < 3; a++) {
        Vec3 d = AX_DIR[a];
        const double *c = AX_COL[a];
        /* negative half, faint & dashed */
        cairo_save(cr);
        double dash[2] = {4.0, 5.0};
        cairo_set_dash(cr, dash, 2, 0);
        cairo_set_line_width(cr, 1.0);
        cairo_set_source_rgba(cr, c[0], c[1], c[2], 0.28);
        line3d(cr, P, vec3_scale(d, -L), vec3_create(0, 0, 0));
        cairo_stroke(cr);
        cairo_restore(cr);
        /* positive half */
        cairo_set_line_width(cr, 1.6);
        cairo_set_source_rgba(cr, c[0], c[1], c[2], 0.85);
        line3d(cr, P, vec3_create(0, 0, 0), vec3_scale(d, L));
        cairo_stroke(cr);

        /* tick marks + numeric labels (in a0) */
        for (int k = 1; k <= nticks; k++) {
            double x0, y0, x1, y1;
            Vec3 tp = vec3_scale(d, k * step);
            Vec3 tq = vec3_scale(d, (k + 0.001f) * step);
            if (!proj_pt(P, tp, &x0, &y0)) continue;
            if (!proj_pt(P, tq, &x1, &y1)) continue;
            double dx = x1 - x0, dy = y1 - y0, len = hypot(dx, dy);
            if (len < 1e-6) continue;
            double nx = -dy / len, ny = dx / len;
            cairo_set_line_width(cr, 1.2);
            cairo_set_source_rgba(cr, c[0], c[1], c[2], 0.9);
            cairo_move_to(cr, x0 - nx * 3.5, y0 - ny * 3.5);
            cairo_line_to(cr, x0 + nx * 3.5, y0 + ny * 3.5);
            cairo_stroke(cr);
            if (k % every == 0 && k != nticks) {
                char buf[16];
                fmt_num(buf, sizeof(buf), k * step);
                text(cr, buf, x0 + nx * 11, y0 + ny * 11 + 4, 10.5, 0, c[0], c[1], c[2], 0.95, ALIGN_CENTER);
            }
        }
        double lx, ly;
        if (proj_pt(P, vec3_scale(d, L * 1.07f), &lx, &ly))
            text(cr, AX_NAME[a], lx, ly + 5, 15, 1, c[0], c[1], c[2], 1.0, ALIGN_CENTER);
    }
}

static void draw_circle3d(cairo_t *cr, const Proj *P, int plane, float R) {
    const int N = 96;
    Vec3 prev = {0, 0, 0};
    for (int i = 0; i <= N; i++) {
        float t = 2.0f * (float)M_PI * (float)i / (float)N;
        float c = R * cosf(t), sn = R * sinf(t);
        Vec3 p = plane == 0 ? vec3_create(c, sn, 0) : plane == 1 ? vec3_create(c, 0, sn) : vec3_create(0, c, sn);
        if (i > 0) line3d(cr, P, prev, p);
        prev = p;
    }
}

static void draw_sphere90(cairo_t *cr, const HudState *s, const Proj *P) {
    cairo_save(cr);
    double dash[2] = {3.0, 5.0};
    cairo_set_dash(cr, dash, 2, 0);
    cairo_set_line_width(cr, 1.1);
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.38);
    for (int pl = 0; pl < 3; pl++) { draw_circle3d(cr, P, pl, (float)s->props->r_90); cairo_stroke(cr); }
    cairo_restore(cr);
}

static void draw_bbox(cairo_t *cr, const HudState *s, const Proj *P) {
    float e = s->extent;
    cairo_save(cr);
    cairo_set_line_width(cr, 1.0);
    cairo_set_source_rgba(cr, 0.7, 0.8, 1.0, 0.35);
    for (int i = 0; i < 8; i++) {
        Vec3 a = vec3_create((i & 1) ? e : -e, (i & 2) ? e : -e, (i & 4) ? e : -e);
        for (int b = 0; b < 3; b++) {
            if (i & (1 << b)) continue;
            Vec3 c = a;
            if (b == 0) c.x = e; else if (b == 1) c.y = e; else c.z = e;
            line3d(cr, P, a, c);
        }
    }
    cairo_stroke(cr);
    cairo_restore(cr);
}

static void draw_slice_plane(cairo_t *cr, const HudState *s, const Proj *P) {
    float e = s->extent, o = s->slice_pos;
    Vec3 c[4];
    for (int i = 0; i < 4; i++) {
        float u = (i == 0 || i == 3) ? -e : e, v = (i < 2) ? -e : e;
        c[i] = s->slice_axis == 0 ? vec3_create(o, u, v) : s->slice_axis == 1 ? vec3_create(u, o, v) : vec3_create(u, v, o);
    }
    double px[4], py[4];
    int ok = 1;
    for (int i = 0; i < 4; i++) ok &= proj_pt(P, c[i], &px[i], &py[i]);
    if (!ok) return;
    cairo_save(cr);
    cairo_move_to(cr, px[0], py[0]);
    cairo_line_to(cr, px[1], py[1]); cairo_line_to(cr, px[2], py[2]); cairo_line_to(cr, px[3], py[3]);
    cairo_close_path(cr);
    cairo_set_source_rgba(cr, 1.0, 0.85, 0.35, 0.05);
    cairo_fill_preserve(cr);
    double dash[2] = {6.0, 4.0};
    cairo_set_dash(cr, dash, 2, 0);
    cairo_set_source_rgba(cr, 1.0, 0.85, 0.35, 0.65);
    cairo_set_line_width(cr, 1.2);
    cairo_stroke(cr);
    cairo_restore(cr);
}

static void draw_gizmo(cairo_t *cr, const HudState *s) {
    Vec3 right, up, fwd;
    camera_get_basis(s->cam, &right, &up, &fwd);
    double cx = 58, cy = s->height - 58, L = 34;
    int order[3] = {0, 1, 2};
    float depth[3];
    for (int a = 0; a < 3; a++) depth[a] = vec3_dot(AX_DIR[a], fwd);
    for (int i = 0; i < 3; i++)
        for (int j = i + 1; j < 3; j++)
            if (depth[order[j]] > depth[order[i]]) { int t = order[i]; order[i] = order[j]; order[j] = t; }
    cairo_arc(cr, cx, cy, L + 14, 0, 2 * M_PI);
    cairo_set_source_rgba(cr, 0.03, 0.05, 0.09, 0.45);
    cairo_fill(cr);
    for (int i = 0; i < 3; i++) {
        int a = order[i];
        double ex = vec3_dot(AX_DIR[a], right) * L, ey = -vec3_dot(AX_DIR[a], up) * L;
        double fade = 0.55 + 0.45 * (1.0 - (depth[a] + 1.0) * 0.5);
        const double *c = AX_COL[a];
        cairo_set_line_width(cr, 2.2);
        cairo_set_source_rgba(cr, c[0], c[1], c[2], fade);
        cairo_move_to(cr, cx, cy);
        cairo_line_to(cr, cx + ex, cy + ey);
        cairo_stroke(cr);
        cairo_arc(cr, cx + ex, cy + ey, 8.5, 0, 2 * M_PI);
        cairo_set_source_rgba(cr, c[0], c[1], c[2], fade);
        cairo_fill(cr);
        set_font(cr, 12, 1);
        cairo_set_source_rgba(cr, 0.04, 0.05, 0.08, 1.0);
        cairo_move_to(cr, cx + ex - text_width(cr, AX_NAME[a]) * 0.5, cy + ey + 4.2);
        cairo_show_text(cr, AX_NAME[a]);
    }
}

/* ------------------------------------------------------------------ */
/* legends & info                                                      */
/* ------------------------------------------------------------------ */

static void draw_legend(cairo_t *cr, const HudState *s) {
    const ColorScheme *cs = color_scheme_get(s->scheme);
    double pw = 196, ph = 76;
    double x = s->width - pw - 14, y = s->height - ph - 14;
    int wheel = cs->wheel && (s->view_kind == VIEW_WAVE || s->complex_basis || s->sup);
    if (wheel) ph = 108;
    y = s->height - ph - 14;
    panel(cr, x, y, pw, ph);

    if (s->view_kind == VIEW_DENSITY && !wheel) {
        text(cr, "Probability density |ψ|²", x + 12, y + 20, 12, 1, 0.9, 0.95, 1.0, 0.95, ALIGN_LEFT);
        cairo_pattern_t *g = cairo_pattern_create_linear(x + 12, 0, x + pw - 12, 0);
        for (int i = 0; i < 5; i++)
            cairo_pattern_add_color_stop_rgb(g, i / 4.0, cs->ramp[i].x, cs->ramp[i].y, cs->ramp[i].z);
        round_rect(cr, x + 12, y + 32, pw - 24, 12, 4);
        cairo_set_source(cr, g);
        cairo_fill(cr);
        cairo_pattern_destroy(g);
        text(cr, "low", x + 12, y + 62, 10.5, 0, 0.75, 0.82, 0.95, 0.9, ALIGN_LEFT);
        text(cr, "high", x + pw - 12, y + 62, 10.5, 0, 0.75, 0.82, 0.95, 0.9, ALIGN_RIGHT);
    } else if (!wheel) {
        text(cr, "Wavefunction ψ  (sign)", x + 12, y + 20, 12, 1, 0.9, 0.95, 1.0, 0.95, ALIGN_LEFT);
        cairo_arc(cr, x + 22, y + 40, 6, 0, 2 * M_PI);
        cairo_set_source_rgb(cr, cs->pos.x, cs->pos.y, cs->pos.z);
        cairo_fill(cr);
        text(cr, "+ψ", x + 34, y + 45, 12, 1, cs->pos.x, cs->pos.y, cs->pos.z, 1.0, ALIGN_LEFT);
        cairo_arc(cr, x + 100, y + 40, 6, 0, 2 * M_PI);
        cairo_set_source_rgb(cr, cs->neg.x, cs->neg.y, cs->neg.z);
        cairo_fill(cr);
        text(cr, "−ψ", x + 112, y + 45, 12, 1, cs->neg.x, cs->neg.y, cs->neg.z, 1.0, ALIGN_LEFT);
        text(cr, "brightness ∝ |ψ|", x + 12, y + 65, 10.5, 0, 0.75, 0.82, 0.95, 0.9, ALIGN_LEFT);
    } else {
        text(cr, "Phase  arg ψ", x + 12, y + 20, 12, 1, 0.9, 0.95, 1.0, 0.95, ALIGN_LEFT);
        double cx = x + pw - 50, cy = y + 58, ro = 30, ri = 17;
        for (int i = 0; i < 72; i++) {
            double a0 = 2 * M_PI * i / 72.0 - s->phase * 0.0, a1 = 2 * M_PI * (i + 1.2) / 72.0;
            Vec3 c = color_hsv((float)i / 72.0f, 0.85f, 1.0f);
            cairo_new_path(cr);
            cairo_arc(cr, cx, cy, ro, -a1, -a0);
            cairo_arc_negative(cr, cx, cy, ri, -a0, -a1);
            cairo_close_path(cr);
            cairo_set_source_rgb(cr, c.x, c.y, c.z);
            cairo_fill(cr);
        }
        text(cr, "0", cx + ro + 5, cy + 4, 10.5, 0, 0.85, 0.9, 1, 0.9, ALIGN_LEFT);
        text(cr, "π/2", cx, cy - ro - 4, 10.5, 0, 0.85, 0.9, 1, 0.9, ALIGN_CENTER);
        text(cr, "π", cx - ro - 5, cy + 4, 10.5, 0, 0.85, 0.9, 1, 0.9, ALIGN_RIGHT);
        text(cr, "3π/2", cx, cy + ro + 13, 10.5, 0, 0.85, 0.9, 1, 0.9, ALIGN_CENTER);
        text(cr, "hue = phase", x + 12, y + 50, 10.5, 0, 0.75, 0.82, 0.95, 0.9, ALIGN_LEFT);
        text(cr, "bright = |ψ|", x + 12, y + 66, 10.5, 0, 0.75, 0.82, 0.95, 0.9, ALIGN_LEFT);
    }
}

static const char *k_l_letters = "spdfghik";

static void draw_info_super(cairo_t *cr, const HudState *s) {
    const SuperHud *u = s->sup;
    double pw = 350, ph = 112 + 20 * u->count + (u->mode == 2 ? 20 : 0);
    panel(cr, 12, 12, pw, ph);
    text(cr, s->view_kind == VIEW_DENSITY ? "SUPERPOSITION  |ψ(t)|²" : "SUPERPOSITION  ψ(t)  —  AMPLITUDE & PHASE",
         24, 32, 10.5, 1, 0.55, 0.72, 1.0, 0.95, ALIGN_LEFT);
    text(cr, u->mode == 2 ? "Transition" : "Coherent mix", 24, 62, 24, 1, 1, 1, 1, 1, ALIGN_LEFT);
    double y = 86;
    char buf[200];
    for (int k = 0; k < u->count; k++) {
        Vec3 c = color_hsv((float)k / 5.0f + 0.02f, 0.55f, 1.0f);
        cairo_arc(cr, 30, y - 4, 4.5, 0, 2 * M_PI);
        cairo_set_source_rgb(cr, c.x, c.y, c.z);
        cairo_fill(cr);
        snprintf(buf, sizeof(buf), "%-6s  E = %.2f eV", u->comp_label[k], u->comp_energy_ev[k]);
        text(cr, buf, 42, y, 12, 0, 0.88, 0.92, 1, 0.95, ALIGN_LEFT);
        double bw = 90 * fmin(1.0, u->comp_weight[k]);
        round_rect(cr, 236, y - 10, 90, 9, 3);
        cairo_set_source_rgba(cr, 1, 1, 1, 0.12);
        cairo_fill(cr);
        if (bw > 1) { round_rect(cr, 236, y - 10, bw, 9, 3); cairo_set_source_rgb(cr, c.x, c.y, c.z); cairo_fill(cr); }
        snprintf(buf, sizeof(buf), "%3.0f%%", 100.0 * u->comp_weight[k]);
        text(cr, buf, pw - 6, y, 11, 0, 0.8, 0.88, 1, 0.9, ALIGN_RIGHT);
        y += 20;
    }
    if (u->mode == 2) {
        snprintf(buf, sizeof(buf), "sweep  %.0f %%", 100.0 * u->progress);
        text(cr, buf, 24, y, 12, 0, 1.0, 0.85, 0.5, 0.95, ALIGN_LEFT);
        y += 20;
    }
    if (u->period_fs > 0.0) snprintf(buf, sizeof(buf), "t = %.3f fs      beat period = %.3f fs", u->t_fs, u->period_fs);
    else snprintf(buf, sizeof(buf), "t = %.3f fs      (degenerate: static density)", u->t_fs);
    text(cr, buf, 24, y, 12, 0, 0.88, 0.92, 1, 0.95, ALIGN_LEFT); y += 18;
    snprintf(buf, sizeof(buf), "<r> = (%+.2f, %+.2f, %+.2f) a₀", u->dipole[0], u->dipole[1], u->dipole[2]);
    text(cr, buf, 24, y, 12, 0, 0.88, 0.92, 1, 0.95, ALIGN_LEFT);
    if (u->line[0]) {
        double ly = 12 + ph + 8;
        set_font(cr, 11.5, 0);
        double tw = text_width(cr, u->line);
        panel(cr, 12, ly, fmax(pw, tw + 40), 28);
        cairo_arc(cr, 28, ly + 14, 5, 0, 2 * M_PI);
        cairo_set_source_rgb(cr, u->line_rgb[0], u->line_rgb[1], u->line_rgb[2]);
        cairo_fill(cr);
        text(cr, u->line, 40, ly + 19, 11.5, 0, 0.9, 0.94, 1, 0.95, ALIGN_LEFT);
    }
}

/* <r>(t): rolling trace + arrow from the nucleus */
static void draw_dipole_trace(cairo_t *cr, const HudState *s) {
    const SuperHud *u = s->sup;
    if (!u->trace || s->height < 540) return;
    double pw = 232, ph = 108, x = 12, y = s->height - ph - 112;
    panel(cr, x, y, pw, ph);
    text(cr, "dipole  <r>(t)", x + 10, y + 17, 11, 1, 0.85, 0.92, 1, 0.95, ALIGN_LEFT);
    double px = x + 10, py = y + 26, pwid = pw - 20, phei = ph - 36, mid = py + phei * 0.5;
    cairo_set_line_width(cr, 1.0);
    cairo_set_source_rgba(cr, 1, 1, 1, 0.18);
    cairo_move_to(cr, px, mid); cairo_line_to(cr, px + pwid, mid); cairo_stroke(cr);
    static const double col[3][3] = {{0.96, 0.38, 0.38}, {0.45, 0.88, 0.45}, {0.42, 0.68, 1.00}};
    double amp = u->trace_amp > 1e-6 ? u->trace_amp : 1.0;
    for (int a = 0; a < 3; a++) {
        double peak = 0;
        for (int i = 0; i < HUD_TRACE_N; i++) peak = fmax(peak, fabs(u->trace[3 * i + a]));
        if (peak < 0.02 * amp) continue;                       /* skip flat components */
        cairo_set_source_rgba(cr, col[a][0], col[a][1], col[a][2], 0.95);
        cairo_set_line_width(cr, 1.5);
        for (int i = 0; i < HUD_TRACE_N; i++) {
            int idx = (u->trace_head + i) % HUD_TRACE_N;
            double X = px + pwid * i / (HUD_TRACE_N - 1.0);
            double Y = mid - 0.5 * phei * clampf(u->trace[3 * idx + a] / amp, -1.0f, 1.0f);
            if (i == 0) cairo_move_to(cr, X, Y); else cairo_line_to(cr, X, Y);
        }
        cairo_stroke(cr);
    }
    char b[64];
    snprintf(b, sizeof(b), "±%.2f a₀", amp);
    text(cr, b, x + pw - 10, y + 17, 10, 0, 0.75, 0.82, 0.95, 0.9, ALIGN_RIGHT);
}

static void draw_dipole_arrow(cairo_t *cr, const HudState *s, const Proj *P) {
    const SuperHud *u = s->sup;
    if (!u->show_arrow) return;
    Vec3 tip = {(float)(u->dipole[0] * u->arrow_scale), (float)(u->dipole[1] * u->arrow_scale), (float)(u->dipole[2] * u->arrow_scale)};
    double len = sqrt((double)tip.x * tip.x + (double)tip.y * tip.y + (double)tip.z * tip.z);
    if (len < 1e-4) return;
    double x0, y0, x1, y1;
    if (!proj_seg(P, (Vec3){0, 0, 0}, tip, &x0, &y0, &x1, &y1)) return;
    cairo_set_source_rgba(cr, 1.0, 0.85, 0.4, 0.95);
    cairo_set_line_width(cr, 2.5);
    cairo_move_to(cr, x0, y0); cairo_line_to(cr, x1, y1); cairo_stroke(cr);
    double ang = atan2(y1 - y0, x1 - x0);
    cairo_move_to(cr, x1, y1);
    cairo_line_to(cr, x1 - 11 * cos(ang - 0.4), y1 - 11 * sin(ang - 0.4));
    cairo_line_to(cr, x1 - 11 * cos(ang + 0.4), y1 - 11 * sin(ang + 0.4));
    cairo_close_path(cr);
    cairo_fill(cr);
    char b[64];
    snprintf(b, sizeof(b), "<r> × %.0f", u->arrow_scale);
    text(cr, b, x1 + 8, y1 - 8, 11, 1, 1.0, 0.88, 0.5, 0.95, ALIGN_LEFT);
}

static void draw_info(cairo_t *cr, const HudState *s) {
    if (s->sup) { draw_info_super(cr, s); return; }
    const OrbitalProps *p = s->props;
    char l1[160], l2[160], l3[160], l4[160];
    snprintf(l1, sizeof(l1), "n = %d     l = %d     m = %+d%s", s->spec->n, s->spec->l, s->spec->m,
             s->spec->complex_basis ? "     (complex Yₗᵐ)" : "     (real orbital)");
    snprintf(l2, sizeof(l2), "E = %.3f eV      <r> = %.2f a₀      r_peak = %.2f a₀", p->energy_ev, p->r_expect, p->r_peak);
    snprintf(l3, sizeof(l3), "Nodes: %d radial · %d angular · %d total", p->radial_nodes, p->angular_nodes, p->total_nodes);
    snprintf(l4, sizeof(l4), "Z = %.0f  (%s)", s->spec->Z,
             s->spec->Z < 1.5 ? "hydrogen" : s->spec->Z < 2.5 ? "He⁺" : s->spec->Z < 3.5 ? "Li²⁺" : "hydrogen-like ion");
    int lines = 3 + (s->spec->Z > 1.5 ? 1 : 0);
    double pw = 350, ph = 62 + 18 * lines;
    panel(cr, 12, 12, pw, ph);
    text(cr, s->view_kind == VIEW_DENSITY ? "ELECTRON PROBABILITY DENSITY  |ψ|²" : "WAVEFUNCTION  ψ  —  AMPLITUDE & PHASE",
         24, 32, 10.5, 1, 0.55, 0.72, 1.0, 0.95, ALIGN_LEFT);
    text(cr, s->orbital_name, 24, 66, 28, 1, 1, 1, 1, 1, ALIGN_LEFT);
    double y = 90;
    text(cr, l1, 24, y, 12, 0, 0.88, 0.92, 1, 0.95, ALIGN_LEFT); y += 18;
    text(cr, l2, 24, y, 12, 0, 0.88, 0.92, 1, 0.95, ALIGN_LEFT); y += 18;
    text(cr, l3, 24, y, 12, 0, 0.88, 0.92, 1, 0.95, ALIGN_LEFT); y += 18;
    if (s->spec->Z > 1.5) text(cr, l4, 24, y, 12, 0, 0.88, 0.92, 1, 0.95, ALIGN_LEFT);
}

static void draw_stats(cairo_t *cr, const HudState *s) {
    char buf[200];
    double y = 24;
    if (s->generating) snprintf(buf, sizeof(buf), "sampling…");
    else if (s->animating && s->iso_mode == 1) snprintf(buf, sizeof(buf), "%.0f fps  ·  isosurface", s->fps);
    else if (s->animating) snprintf(buf, sizeof(buf), "%.0f fps  ·  %d points", s->fps, s->point_count);
    else if (s->iso_mode == 1) snprintf(buf, sizeof(buf), "idle (GPU asleep)  ·  isosurface");
    else snprintf(buf, sizeof(buf), "idle (GPU asleep)  ·  %d points", s->point_count);
    text(cr, buf, s->width - 16, y, 11.5, 0, 0.8, 0.88, 1, 0.9, ALIGN_RIGHT);
    y += 17;
    if (s->view_kind == VIEW_WAVE && !s->generating && !s->sup && s->iso_mode != 1) {
        snprintf(buf, sizeof(buf), "+ψ: %ld   −ψ: %ld", s->n_pos, s->n_neg);
        text(cr, buf, s->width - 16, y, 11, 0, 0.8, 0.88, 1, 0.8, ALIGN_RIGHT);
        y += 17;
    }
    if (s->phase_anim && !s->sup) {
        snprintf(buf, sizeof(buf), "phase  ωt = %.2f rad", fmod(s->phase, 2 * M_PI));
        text(cr, buf, s->width - 16, y, 11, 0, 1.0, 0.85, 0.5, 0.9, ALIGN_RIGHT);
    }
}

static void draw_footer(cairo_t *cr, const HudState *s) {
    char buf[200];
    double y = s->height - 16;
    if (s->slice_mode != SLICE_OFF) {
        snprintf(buf, sizeof(buf), "Cross-section: %c = %.1f a₀ (%s)", "xyz"[s->slice_axis], s->slice_pos,
                 s->slice_mode == SLICE_CUT ? "half-space cut" : "thin slab");
        text(cr, buf, s->width * 0.5, y, 11.5, 0, 1.0, 0.88, 0.5, 0.95, ALIGN_CENTER);
        y -= 17;
    }
    if (s->show_sphere90) {
        snprintf(buf, sizeof(buf), "dashed sphere: 90%% of the probability inside r = %.2f a₀", s->props->r_90);
        text(cr, buf, s->width * 0.5, y, 11.5, 0, 0.9, 0.93, 1, 0.85, ALIGN_CENTER);
        y -= 17;
    }
    if (s->show_axes || s->show_grid) {
        snprintf(buf, sizeof(buf), "axis ticks & grid lines: %g a₀  (a₀ = Bohr radius)", s->grid_step);
        text(cr, buf, s->width * 0.5, y, 11.5, 0, 0.7, 0.8, 0.95, 0.8, ALIGN_CENTER);
    }
}

void hud_draw(cairo_t *cr, const HudState *s) {
    if (!s->cam || s->width < 50 || s->height < 50) return;
    Proj P;
    P.vp = camera_get_viewproj(s->cam, (float)s->width / (float)s->height);
    P.w = s->width;
    P.h = s->height;
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);

    if (s->show_bbox) draw_bbox(cr, s, &P);
    if (s->show_sphere90) draw_sphere90(cr, s, &P);
    if (s->slice_mode != SLICE_OFF) draw_slice_plane(cr, s, &P);
    if (s->show_axes) draw_axes(cr, s, &P);
    if (s->sup) draw_dipole_arrow(cr, s, &P);
    if (s->show_info) draw_info(cr, s);
    if (s->sup && s->show_info) draw_dipole_trace(cr, s);
    if (s->show_info && !s->hide_stats) draw_stats(cr, s);
    if (s->show_gizmo) draw_gizmo(cr, s);
    if (s->show_legend) draw_legend(cr, s);
    if (s->show_info) draw_footer(cr, s);
}

/* ------------------------------------------------------------------ */
/* radial distribution plot for the sidebar                            */
/* ------------------------------------------------------------------ */

void hud_radial_plot(cairo_t *cr, int w, int h, const OrbitalProps *p, const OrbitalSpec *spec, int dark) {
    (void)spec;
    double fg = dark ? 1.0 : 0.0;
    double ml = 34, mr = 10, mt = 10, mb = 24;
    double pw = w - ml - mr, ph = h - mt - mb;
    cairo_set_source_rgba(cr, fg, fg, fg, 0.05);
    cairo_rectangle(cr, ml, mt, pw, ph);
    cairo_fill(cr);

    /* grid + x labels */
    float step = nice_step((float)p->plot_rmax / 5.0f);
    set_font(cr, 10, 0);
    for (double r = 0; r <= p->plot_rmax + 1e-9; r += step) {
        double x = ml + pw * r / p->plot_rmax;
        cairo_set_source_rgba(cr, fg, fg, fg, 0.12);
        cairo_set_line_width(cr, 1);
        cairo_move_to(cr, x, mt);
        cairo_line_to(cr, x, mt + ph);
        cairo_stroke(cr);
        char b[16];
        fmt_num(b, sizeof(b), r);
        cairo_set_source_rgba(cr, fg, fg, fg, 0.75);
        cairo_move_to(cr, x - text_width(cr, b) * 0.5, h - 10);
        cairo_show_text(cr, b);
    }
    cairo_set_source_rgba(cr, fg, fg, fg, 0.75);
    cairo_move_to(cr, ml + pw - 40, h - 1);
    cairo_show_text(cr, "r (a₀)");
    cairo_save(cr);
    cairo_translate(cr, 11, mt + ph * 0.5 + 24);
    cairo_rotate(cr, -M_PI / 2);
    cairo_move_to(cr, 0, 0);
    cairo_show_text(cr, "r²|R|²");
    cairo_restore(cr);

    /* nodes */
    cairo_save(cr);
    double dash[2] = {3, 3};
    cairo_set_dash(cr, dash, 2, 0);
    cairo_set_source_rgba(cr, 1.0, 0.55, 0.35, 0.8);
    for (int i = 0; i < p->node_count; i++) {
        double x = ml + pw * p->node_r[i] / p->plot_rmax;
        cairo_move_to(cr, x, mt);
        cairo_line_to(cr, x, mt + ph);
        cairo_stroke(cr);
    }
    /* <r> marker */
    cairo_set_source_rgba(cr, 0.45, 1.0, 0.65, 0.85);
    double xe = ml + pw * p->r_expect / p->plot_rmax;
    if (xe < ml + pw) { cairo_move_to(cr, xe, mt); cairo_line_to(cr, xe, mt + ph); cairo_stroke(cr); }
    cairo_restore(cr);

    /* curve (filled) */
    cairo_new_path(cr);
    cairo_move_to(cr, ml, mt + ph);
    for (int i = 0; i < PROPS_PLOT_N; i++)
        cairo_line_to(cr, ml + pw * i / (PROPS_PLOT_N - 1.0), mt + ph - ph * 0.95 * p->plot_p[i]);
    cairo_line_to(cr, ml + pw, mt + ph);
    cairo_close_path(cr);
    cairo_set_source_rgba(cr, 0.25, 0.65, 1.0, 0.25);
    cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr, 0.35, 0.78, 1.0, 1.0);
    cairo_set_line_width(cr, 1.8);
    cairo_stroke(cr);
}

/* ------------------------------------------------------------------ */
/* energy-level diagram                                                */
/* ------------------------------------------------------------------ */

static const double k_l_col[5][3] = {{0.45, 0.78, 1.00}, {1.00, 0.62, 0.40}, {0.55, 0.92, 0.55}, {0.90, 0.55, 0.90}, {0.95, 0.88, 0.45}};

static void arrow_head(cairo_t *cr, double x, double y, double dx, double dy) {
    double L = hypot(dx, dy);
    if (L < 1e-6) return;
    dx /= L; dy /= L;
    cairo_move_to(cr, x, y);
    cairo_line_to(cr, x - 9 * dx + 4.5 * dy, y - 9 * dy - 4.5 * dx);
    cairo_line_to(cr, x - 9 * dx - 4.5 * dy, y - 9 * dy + 4.5 * dx);
    cairo_close_path(cr);
    cairo_fill(cr);
}

void hud_energy_diagram(cairo_t *cr, int w, int h, const EnergyDiagram *d, LevelLayout *layout_out) {
    LevelLayout L;
    int nmax = d->n_max;
    level_layout_build(&L, nmax, 5, w, h, d->Z);
    if (layout_out) *layout_out = L;
    const double fg = d->dark ? 1.0 : 0.0;
    double tr = d->dark ? 0.86 : 0.15;
    (void)fg;

    /* column headers */
    double cw = (w - L.left - L.right) / L.l_cols;
    for (int l = 0; l < L.l_cols; l++) {
        char t[4] = {k_l_letters[l], 0, 0, 0};
        text(cr, t, L.left + (l + 0.5) * cw, 15, 11.5, 1, k_l_col[l][0], k_l_col[l][1], k_l_col[l][2], 0.95, ALIGN_CENTER);
    }
    /* ionisation limit and energy ticks */
    double ytop = level_layout_y(&L, 1000000);
    cairo_set_line_width(cr, 1.0);
    cairo_set_source_rgba(cr, tr, tr, tr, 0.35);
    double dash[2] = {3, 3};
    cairo_set_dash(cr, dash, 2, 0);
    cairo_move_to(cr, L.left - 4, ytop); cairo_line_to(cr, w - L.right, ytop); cairo_stroke(cr);
    cairo_set_dash(cr, NULL, 0, 0);
    text(cr, "0 eV", L.left - 8, ytop + 4, 10, 0, tr, tr, tr, 0.8, ALIGN_RIGHT);
    for (int n = 1; n <= nmax; n++) {
        char t[32];
        snprintf(t, sizeof(t), "%.2f", -13.605693 * d->Z * d->Z / ((double)n * n));
        text(cr, t, L.left - 8, level_layout_y(&L, n) + 4, 10, 0, tr, tr, tr, 0.8, ALIGN_RIGHT);
    }

    /* levels */
    for (int i = 0; i < L.count; i++) {
        const LevelRect *r = &L.lv[i];
        int is_init = (d->mode != 1 && r->n == d->init_n && r->l == d->init_l);
        int is_final = (d->mode == 2 && d->has_final && r->n == d->final_n && r->l == d->final_l);
        double wgt = 0; int in_comp = 0;
        for (int k = 0; k < d->comp_count && d->mode != 0; k++)
            if (d->comp_n[k] == r->n && d->comp_l[k] == r->l) { in_comp = 1; wgt += d->comp_w[k]; }
        const double *c = k_l_col[r->l];
        double a = 0.55;
        double lw = 2.0;
        if (is_init || is_final || in_comp) { a = 1.0; lw = 3.5; }
        if (i == d->hover) { a = 1.0; lw = lw < 3.0 ? 3.0 : lw; }
        if (d->mode == 0 && is_init) { lw = 3.5; }
        cairo_set_line_width(cr, lw);
        cairo_set_source_rgba(cr, c[0], c[1], c[2], a);
        cairo_move_to(cr, r->x0, r->y); cairo_line_to(cr, r->x1, r->y); cairo_stroke(cr);
        if (in_comp || is_init || is_final) {
            double m = 0.5 * (r->x0 + r->x1);
            double rad = 3.5 + (in_comp ? 5.0 * wgt : (is_init ? 4.0 : 2.5));
            cairo_arc(cr, m, r->y, rad, 0, 2 * M_PI);
            cairo_set_source_rgba(cr, c[0], c[1], c[2], 0.35);
            cairo_fill(cr);
            cairo_arc(cr, m, r->y, 3.2, 0, 2 * M_PI);
            cairo_set_source_rgba(cr, c[0], c[1], c[2], 1.0);
            cairo_fill(cr);
        }
        /* label: "3d" at the right end of the level, only where there is room */
        char t[8];
        snprintf(t, sizeof(t), "%d%c", r->n, k_l_letters[r->l]);
        if (r->n <= 4 || i == d->hover || is_init || is_final || in_comp)
            text(cr, t, r->x1 - 1, r->y - 4, 9.5, 0, c[0], c[1], c[2], 0.9, ALIGN_RIGHT);
    }

    /* transition arrow */
    if (d->mode == 2 && d->has_final && (d->final_n != d->init_n)) {
        int ia = -1, fa = -1;
        for (int i = 0; i < L.count; i++) {
            if (L.lv[i].n == d->init_n && L.lv[i].l == d->init_l) ia = i;
            if (L.lv[i].n == d->final_n && L.lv[i].l == d->final_l) fa = i;
        }
        if (ia >= 0 && fa >= 0) {
            double x0 = 0.5 * (L.lv[ia].x0 + L.lv[ia].x1), y0 = L.lv[ia].y;
            double x1 = 0.5 * (L.lv[fa].x0 + L.lv[fa].x1), y1 = L.lv[fa].y;
            if (fabs(x1 - x0) < 6) x1 = x0 + 6;
            float rgb[3];
            double nm = transition_wavelength_nm(d->init_n, d->final_n, d->Z);
            wavelength_to_rgb(nm, rgb);
            cairo_set_source_rgba(cr, rgb[0], rgb[1], rgb[2], 0.95);
            cairo_set_line_width(cr, 2.2);
            double dx = x1 - x0, dy = y1 - y0, len = hypot(dx, dy);
            double ux = dx / len, uy = dy / len;
            cairo_move_to(cr, x0, y0);
            cairo_line_to(cr, x1 - ux * 8, y1 - uy * 8);
            cairo_stroke(cr);
            arrow_head(cr, x1, y1, dx, dy);
            /* electron marker travelling along the arrow with the sweep */
            double f = clampf((float)d->progress, 0.0f, 1.0f);
            cairo_arc(cr, x0 + dx * f, y0 + dy * f, 4.5, 0, 2 * M_PI);
            cairo_set_source_rgb(cr, 1, 1, 1);
            cairo_fill(cr);
            char t[48];
            if (nm < 10000) snprintf(t, sizeof(t), "%.1f nm", nm); else snprintf(t, sizeof(t), "%.2f µm", nm / 1000.0);
            text(cr, t, 0.5 * (x0 + x1) + 6, 0.5 * (y0 + y1), 10.5, 1, rgb[0], rgb[1], rgb[2], 1.0, ALIGN_LEFT);
        }
    }
}

/* ------------------------------------------------------------------ */
/* superposition HUD data                                              */
/* ------------------------------------------------------------------ */

void superhud_fill(SuperHud *u, const SuperSpec *s, const DipoleMatrix *D, double t_au, int mode, double progress) {
    Complex c[SUPER_MAX];
    super_coeffs_at(s, t_au, c);
    u->mode = mode;
    u->count = s->count;
    u->progress = progress;
    for (int k = 0; k < s->count; k++) {
        OrbitalSpec o = super_component_spec(s, k);
        hydrogen_orbital_name(&o, u->comp_label[k], sizeof(u->comp_label[k]));
        u->comp_weight[k] = (float)(c[k].r * c[k].r + c[k].i * c[k].i);
        u->comp_energy_ev[k] = super_energy(s->c[k].n, s->Z) * HARTREE_EV;
    }
    u->t_fs = t_au * AU_TIME_FS;
    double w = super_max_omega(s);
    u->period_fs = w > 0.0 ? 2.0 * M_PI / w * AU_TIME_FS : 0.0;
    if (D) super_dipole_expect(D, c, u->dipole);
}

void superhud_transition_line(SuperHud *u, int ni, int li, int nf, int lf, double Z) {
    static const char *L = "spdfghik";
    char name[40] = "", extra[96] = "";
    if (ni != nf) {
        double nm = transition_wavelength_nm(ni, nf, Z);
        double ev = transition_energy_ev(ni, nf, Z);
        wavelength_to_rgb(nm, u->line_rgb);
        int hi = ni > nf ? ni : nf, lo = ni > nf ? nf : ni;
        if (Z < 1.5) transition_line_label(hi, lo, name, sizeof(name));
        if (nm < 10000.0) snprintf(extra, sizeof(extra), "%.1f nm (%.2f eV)", nm, ev);
        else snprintf(extra, sizeof(extra), "%.2f µm (%.3f eV)", nm / 1000.0, ev);
    } else {
        u->line_rgb[0] = u->line_rgb[1] = u->line_rgb[2] = 0.7f;
        snprintf(extra, sizeof(extra), "same shell (no photon)");
    }
    int allowed = transition_l_allowed(li, lf);
    char rate[48] = "";
    if (allowed && ni > nf) snprintf(rate, sizeof(rate), " · A = %.2e /s", transition_rate(ni, li, nf, lf, Z));
    snprintf(u->line, sizeof(u->line), "%d%c → %d%c%s%s · %s%s · %s", ni, L[li], nf, L[lf],
             name[0] ? " · " : "", name, extra, rate, allowed ? "E1 allowed" : "E1 forbidden");
}
