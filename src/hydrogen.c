#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hydrogen.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define RYDBERG_EV 13.605693122994

double factorial(int n) {
    double f = 1.0;
    for (int i = 2; i <= n; i++) f *= i;
    return f;
}

/* Associated Legendre P_l^m(x), m >= 0, with the Condon-Shortley phase. */
double assoc_legendre(int l, int m, double x) {
    if (m < 0 || m > l || fabs(x) > 1.0) return 0.0;
    double pmm = 1.0;
    if (m > 0) {
        double somx2 = sqrt((1.0 - x) * (1.0 + x));
        double fact = 1.0;
        for (int i = 1; i <= m; i++) {
            pmm *= -fact * somx2;
            fact += 2.0;
        }
    }
    if (l == m) return pmm;
    double pmmp1 = x * (2 * m + 1) * pmm;
    if (l == m + 1) return pmmp1;
    double pll = 0.0;
    for (int ll = m + 2; ll <= l; ll++) {
        pll = (x * (2 * ll - 1) * pmmp1 - (ll + m - 1) * pmm) / (ll - m);
        pmm = pmmp1;
        pmmp1 = pll;
    }
    return pmmp1;
}

double assoc_laguerre(int k, int alpha, double x) {
    if (k < 0) return 0.0;
    double l0 = 1.0;
    if (k == 0) return l0;
    double l1 = 1.0 + alpha - x;
    for (int j = 1; j < k; j++) {
        double lj = ((2.0 * j + 1.0 + alpha - x) * l1 - (j + alpha) * l0) / (j + 1.0);
        l0 = l1;
        l1 = lj;
    }
    return l1;
}

/* R_{nl}(r) for a hydrogen-like ion; `a` is the length unit a0/Z. */
double radial_wave_function(int n, int l, double r, double a) {
    if (r < 0.0) r = 0.0;
    double nd = (double)n;
    double norm = sqrt(pow(2.0 / (nd * a), 3.0) * factorial(n - l - 1) / (2.0 * nd * factorial(n + l)));
    double rho = 2.0 * r / (nd * a);
    return norm * exp(-0.5 * rho) * pow(rho, (double)l) * assoc_laguerre(n - l - 1, 2 * l + 1, rho);
}

/* ---------------------------------------------------------------- */
/* Spherical harmonics                                              */
/* ---------------------------------------------------------------- */

void ylm_init(YlmCtx *c, int l, int m, int complex_basis) {
    c->l = l;
    c->m = m;
    c->abs_m = m < 0 ? -m : m;
    c->cplx = complex_basis;
    c->norm = sqrt((2.0 * l + 1.0) / (4.0 * M_PI) * factorial(l - c->abs_m) / factorial(l + c->abs_m));
}

void ylm_eval(const YlmCtx *c, double ct, double phi, double *re, double *im) {
    double p = c->norm * assoc_legendre(c->l, c->abs_m, ct);   /* includes (-1)^|m| */
    double sgn = (c->abs_m & 1) ? -1.0 : 1.0;
    if (c->cplx) {
        if (c->m == 0) { *re = p; *im = 0.0; return; }
        double s, co;
        s = sin(c->abs_m * phi); co = cos(c->abs_m * phi);
        if (c->m > 0) { *re = p * co; *im = p * s; }
        else          { *re = sgn * p * co; *im = -sgn * p * s; }   /* Y_l^-m = (-1)^m conj(Y_l^m) */
        return;
    }
    *im = 0.0;
    if (c->m == 0) { *re = p; return; }
    /* Real harmonics without the Condon-Shortley phase, so p_x is positive along +x. */
    double s, co;
    s = sin(c->abs_m * phi); co = cos(c->abs_m * phi);
    double base = 1.4142135623730951 * sgn * p;
    *re = (c->m > 0) ? base * co : base * s;
}

double ylm_max_abs(const YlmCtx *c) {
    const int NT = 721, NP = (c->cplx || c->m == 0) ? 1 : 721;
    double best = 0.0;
    for (int i = 0; i < NT; i++) {
        double ct = -1.0 + 2.0 * (double)i / (double)(NT - 1);
        for (int j = 0; j < NP; j++) {
            double phi = 2.0 * M_PI * (double)j / (double)NP;
            double re, im;
            ylm_eval(c, ct, phi, &re, &im);
            double v = sqrt(re * re + im * im);
            if (v > best) best = v;
        }
    }
    return best * 1.01; /* tiny safety margin for the rejection sampler */
}

Complex hydrogen_psi(const OrbitalSpec *s, double r, double ct, double phi) {
    YlmCtx y;
    ylm_init(&y, s->l, s->m, s->complex_basis);
    double re, im;
    ylm_eval(&y, ct, phi, &re, &im);
    double R = radial_wave_function(s->n, s->l, r, 1.0 / s->Z);
    return (Complex){R * re, R * im};
}

/* ---------------------------------------------------------------- */
/* Radial tables                                                    */
/* ---------------------------------------------------------------- */

int radial_table_build(RadialTable *t, const OrbitalSpec *s, int power, int size) {
    memset(t, 0, sizeof(*t));
    if (size < 16) size = 16;
    double a = 1.0 / s->Z;
    double n = (double)s->n;
    t->size = size;
    t->r_hi = a * (2.0 * n * n + 10.0 * n + 20.0);
    t->dr = t->r_hi / (double)(size - 1);
    t->R = malloc((size_t)size * sizeof(double));
    t->cdf = malloc((size_t)size * sizeof(double));
    if (!t->R || !t->cdf) { radial_table_free(t); return 0; }

    double amax = 0.0;
    for (int i = 0; i < size; i++) {
        double r = i * t->dr;
        t->R[i] = radial_wave_function(s->n, s->l, r, a);
        double ab = fabs(t->R[i]);
        if (ab > amax) amax = ab;
    }
    t->r_abs_max = amax;

    double acc = 0.0;
    double prev = 0.0;
    t->cdf[0] = 0.0;
    for (int i = 1; i < size; i++) {
        double r = i * t->dr;
        double v = r * r * (power == 1 ? fabs(t->R[i]) : t->R[i] * t->R[i]);
        acc += 0.5 * (prev + v) * t->dr;
        t->cdf[i] = acc;
        prev = v;
    }
    if (acc <= 0.0) { radial_table_free(t); return 0; }
    for (int i = 0; i < size; i++) t->cdf[i] /= acc;
    t->cdf[size - 1] = 1.0;
    return 1;
}

void radial_table_free(RadialTable *t) {
    free(t->R);
    free(t->cdf);
    t->R = t->cdf = NULL;
    t->size = 0;
}

double radial_table_quantile(const RadialTable *t, double p) {
    int lo = 0, hi = t->size - 1;
    while (hi - lo > 1) {
        int mid = (lo + hi) >> 1;
        if (t->cdf[mid] <= p) lo = mid; else hi = mid;
    }
    double span = t->cdf[hi] - t->cdf[lo];
    double f = span > 0.0 ? (p - t->cdf[lo]) / span : 0.0;
    return ((double)lo + f) * t->dr;
}

double radial_table_sample(const RadialTable *t, double u) { return radial_table_quantile(t, u); }

double radial_table_R(const RadialTable *t, double r) {
    double x = r / t->dr;
    int i = (int)x;
    if (i < 0) return t->R[0];
    if (i >= t->size - 1) return t->R[t->size - 1];
    double f = x - (double)i;
    return t->R[i] * (1.0 - f) + t->R[i + 1] * f;
}

/* ---------------------------------------------------------------- */
/* Derived properties                                               */
/* ---------------------------------------------------------------- */

OrbitalProps hydrogen_compute_props(const OrbitalSpec *s) {
    OrbitalProps p;
    memset(&p, 0, sizeof(p));
    double Z = s->Z > 0.0 ? s->Z : 1.0;
    double n = (double)s->n, l = (double)s->l;
    p.a = 1.0 / Z;
    p.energy_ev = -RYDBERG_EV * Z * Z / (n * n);
    p.r_expect = 0.5 * p.a * (3.0 * n * n - l * (l + 1.0));
    p.radial_nodes = s->n - s->l - 1;
    p.angular_nodes = s->l;
    p.total_nodes = s->n - 1;
    p.L_hbar = sqrt(l * (l + 1.0));
    p.Lz_hbar = (double)s->m;
    p.degeneracy = s->n * s->n;

    RadialTable t;
    if (!radial_table_build(&t, s, 2, 16384)) {
        p.r_90 = p.r_extent = p.r_peak = p.r_expect;
        p.plot_rmax = p.r_extent * 1.1;
        return p;
    }
    p.r_90 = radial_table_quantile(&t, 0.90);
    p.r_extent = radial_table_quantile(&t, 0.999);
    p.plot_rmax = p.r_extent * 1.08;

    double best = -1.0;
    for (int i = 1; i < t.size; i++) {
        double r = i * t.dr;
        double v = r * r * t.R[i] * t.R[i];
        if (v > best) { best = v; p.r_peak = r; }
    }
    /* Radial nodes: sign changes of R(r) for r > 0. */
    for (int i = 2; i < t.size && p.node_count < PROPS_MAX_NODES; i++) {
        if (t.R[i - 1] != 0.0 && t.R[i - 1] * t.R[i] < 0.0) {
            double f = t.R[i - 1] / (t.R[i - 1] - t.R[i]);
            p.node_r[p.node_count++] = ((double)(i - 1) + f) * t.dr;
        }
    }
    /* Radial distribution for the sidebar plot. */
    double pmax = 0.0;
    for (int k = 0; k < PROPS_PLOT_N; k++) {
        double r = p.plot_rmax * (double)k / (double)(PROPS_PLOT_N - 1);
        double R = radial_table_R(&t, r);
        p.plot_p[k] = r * r * R * R;
        if (p.plot_p[k] > pmax) pmax = p.plot_p[k];
    }
    if (pmax > 0.0)
        for (int k = 0; k < PROPS_PLOT_N; k++) p.plot_p[k] /= pmax;
    radial_table_free(&t);
    return p;
}

/* ---------------------------------------------------------------- */
/* Names & presets                                                  */
/* ---------------------------------------------------------------- */

void hydrogen_orbital_name(const OrbitalSpec *s, char *buf, size_t size) {
    static const char letters[] = "spdfghiklmno";
    char L = (s->l >= 0 && s->l < 12) ? letters[s->l] : '?';
    if (s->complex_basis) {
        if (s->l == 0) snprintf(buf, size, "%d%c", s->n, L);
        else snprintf(buf, size, "%d%c, m = %+d", s->n, L, s->m);
        return;
    }
    const char *suffix = NULL;
    switch (s->l) {
    case 0: suffix = ""; break;
    case 1: suffix = s->m == 0 ? "_z" : s->m == 1 ? "_x" : s->m == -1 ? "_y" : NULL; break;
    case 2:
        suffix = s->m == 0 ? "_z²" : s->m == 1 ? "_xz" : s->m == -1 ? "_yz" :
                 s->m == 2 ? "_x²−y²" : s->m == -2 ? "_xy" : NULL; break;
    case 3:
        suffix = s->m == 0 ? "_z³" : s->m == 1 ? "_xz²" : s->m == -1 ? "_yz²" :
                 s->m == 2 ? "_z(x²−y²)" : s->m == -2 ? "_xyz" :
                 s->m == 3 ? "_x(x²−3y²)" : s->m == -3 ? "_y(3x²−y²)" : NULL; break;
    default: break;
    }
    if (suffix) snprintf(buf, size, "%d%c%s", s->n, L, suffix);
    else snprintf(buf, size, "%d%c (real, m = %+d)", s->n, L, s->m);
}

static const OrbitalPreset k_presets[] = {
    {1, 0, 0},
    {2, 0, 0}, {2, 1, 0}, {2, 1, 1}, {2, 1, -1},
    {3, 0, 0}, {3, 1, 0}, {3, 1, 1}, {3, 1, -1},
    {3, 2, 0}, {3, 2, 1}, {3, 2, -1}, {3, 2, 2}, {3, 2, -2},
    {4, 0, 0}, {4, 1, 0}, {4, 2, 0}, {4, 3, 0}, {4, 3, 1}, {4, 3, -2},
    {5, 0, 0}, {5, 1, 0}, {5, 2, 0}, {5, 3, 0}, {5, 4, 0},
    {6, 0, 0}, {6, 5, 0}, {7, 6, 0},
};

int hydrogen_preset_count(void) { return (int)(sizeof(k_presets) / sizeof(k_presets[0])); }

OrbitalPreset hydrogen_preset(int idx) {
    int n = hydrogen_preset_count();
    if (idx < 0 || idx >= n) idx = 2;
    return k_presets[idx];
}

int hydrogen_find_preset(int n, int l, int m) {
    for (int i = 0; i < hydrogen_preset_count(); i++)
        if (k_presets[i].n == n && k_presets[i].l == l && k_presets[i].m == m) return i;
    return -1;
}
