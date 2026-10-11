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
    int wheel = cs->wheel && (s->view_kind == VIEW_WAVE || s->complex_basis);
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

static void draw_info(cairo_t *cr, const HudState *s) {
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
    else if (s->animating) snprintf(buf, sizeof(buf), "%.0f fps  ·  %d points", s->fps, s->point_count);
    else snprintf(buf, sizeof(buf), "idle (GPU asleep)  ·  %d points", s->point_count);
    text(cr, buf, s->width - 16, y, 11.5, 0, 0.8, 0.88, 1, 0.9, ALIGN_RIGHT);
    y += 17;
    if (s->view_kind == VIEW_WAVE && !s->generating) {
        snprintf(buf, sizeof(buf), "+ψ: %ld   −ψ: %ld", s->n_pos, s->n_neg);
        text(cr, buf, s->width - 16, y, 11, 0, 0.8, 0.88, 1, 0.8, ALIGN_RIGHT);
        y += 17;
    }
    if (s->phase_anim) {
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
    if (s->show_info) draw_info(cr, s);
    if (s->show_info) draw_stats(cr, s);
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
