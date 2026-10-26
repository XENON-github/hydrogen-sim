#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "superposition.h"
#include "transitions.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

double super_energy(int n, double Z) { return -0.5 * Z * Z / ((double)n * (double)n); }

static Complex cmul(Complex a, Complex b) { return (Complex){a.r * b.r - a.i * b.i, a.r * b.i + a.i * b.r}; }
static Complex cconj(Complex a) { return (Complex){a.r, -a.i}; }

double super_reference_energy(const SuperSpec *s) {
    double e = 0.0;
    for (int k = 0; k < s->count; k++) {
        double ek = super_energy(s->c[k].n, s->Z);
        if (k == 0 || ek < e) e = ek;
    }
    return e;
}

void super_coeffs0(const SuperSpec *s, Complex out[SUPER_MAX]) {
    double norm = 0.0;
    for (int k = 0; k < SUPER_MAX; k++) out[k] = (Complex){0, 0};
    for (int k = 0; k < s->count; k++) norm += s->c[k].amp * s->c[k].amp;
    if (!(norm > 0.0)) { out[0] = (Complex){1, 0}; return; }
    norm = sqrt(norm);
    for (int k = 0; k < s->count; k++) {
        double a = s->c[k].amp / norm;
        out[k] = (Complex){a * cos(s->c[k].phase), a * sin(s->c[k].phase)};
    }
}

double super_mean_energy(const SuperSpec *s) {
    Complex c[SUPER_MAX];
    super_coeffs0(s, c);
    double e = 0.0;
    for (int k = 0; k < s->count; k++) e += (c[k].r * c[k].r + c[k].i * c[k].i) * super_energy(s->c[k].n, s->Z);
    return e;
}

void super_coeffs_at(const SuperSpec *s, double t, Complex out[SUPER_MAX]) {
    Complex c[SUPER_MAX];
    super_coeffs0(s, c);
    double Em = super_reference_energy(s);
    for (int k = 0; k < SUPER_MAX; k++) out[k] = (Complex){0, 0};
    for (int k = 0; k < s->count; k++) {
        double w = super_energy(s->c[k].n, s->Z) - Em;
        double ph = -fmod(w * t, 2.0 * M_PI);
        out[k] = cmul(c[k], (Complex){cos(ph), sin(ph)});
    }
}

static void omega_range(const SuperSpec *s, double *mx, double *mn) {
    *mx = 0.0; *mn = 0.0;
    for (int j = 0; j < s->count; j++)
        for (int k = j + 1; k < s->count; k++) {
            double d = fabs(super_energy(s->c[j].n, s->Z) - super_energy(s->c[k].n, s->Z));
            if (d < 1e-12) continue;
            if (d > *mx) *mx = d;
            if (*mn == 0.0 || d < *mn) *mn = d;
        }
}
double super_max_omega(const SuperSpec *s) { double a, b; omega_range(s, &a, &b); return a; }
double super_min_omega(const SuperSpec *s) { double a, b; omega_range(s, &a, &b); return b; }

OrbitalSpec super_component_spec(const SuperSpec *s, int k) {
    OrbitalSpec o = {s->c[k].n, s->c[k].l, s->c[k].m, s->Z, s->complex_basis};
    return o;
}

/* ---------------- Gauss-Legendre quadrature ---------------- */

static void gauss_legendre(int n, double *x, double *w) {
    for (int i = 0; i < (n + 1) / 2; i++) {
        double z = cos(M_PI * (i + 0.75) / (n + 0.5)), pp = 1.0;
        for (int it = 0; it < 100; it++) {
            double p1 = 1.0, p2 = 0.0;
            for (int j = 0; j < n; j++) {
                double p3 = p2; p2 = p1;
                p1 = ((2.0 * j + 1.0) * z * p2 - j * p3) / (j + 1.0);
            }
            pp = n * (z * p1 - p2) / (z * z - 1.0);
            double dz = p1 / pp;
            z -= dz;
            if (fabs(dz) < 1e-15) break;
        }
        x[i] = -z; x[n - 1 - i] = z;
        w[i] = w[n - 1 - i] = 2.0 / ((1.0 - z * z) * pp * pp);
    }
}

Complex angular_position_element(const YlmCtx *a, const YlmCtx *b, int axis) {
    enum { NG = 40, NP = 64 };
    static double gx[NG], gw[NG];
    static int init = 0;
    if (!init) { gauss_legendre(NG, gx, gw); init = 1; }
    double sr = 0.0, si = 0.0;
    for (int i = 0; i < NG; i++) {
        double ct = gx[i], st = sqrt(fmax(0.0, 1.0 - ct * ct));
        for (int j = 0; j < NP; j++) {
            double phi = 2.0 * M_PI * (j + 0.5) / NP;
            double nx = axis == 0 ? st * cos(phi) : axis == 1 ? st * sin(phi) : ct;
            double ar, ai, br, bi;
            ylm_eval(a, ct, phi, &ar, &ai);
            ylm_eval(b, ct, phi, &br, &bi);
            Complex p = cmul(cconj((Complex){ar, ai}), (Complex){br, bi});
            sr += gw[i] * nx * p.r; si += gw[i] * nx * p.i;
        }
    }
    double f = 2.0 * M_PI / NP;
    return (Complex){sr * f, si * f};
}

void super_dipole_matrix(const SuperSpec *s, DipoleMatrix *D) {
    memset(D, 0, sizeof(*D));
    D->count = s->count;
    YlmCtx y[SUPER_MAX];
    for (int k = 0; k < s->count; k++) ylm_init(&y[k], s->c[k].l, s->c[k].m, s->complex_basis);
    for (int j = 0; j < s->count; j++)
        for (int k = 0; k < s->count; k++) {
            if (abs(s->c[j].l - s->c[k].l) != 1) continue;     /* parity: only dl = +-1 couples */
            double R = radial_dipole_integral(s->c[j].n, s->c[j].l, s->c[k].n, s->c[k].l, s->Z);
            for (int a = 0; a < 3; a++) {
                Complex A = angular_position_element(&y[j], &y[k], a);
                D->d[a][j][k] = (Complex){A.r * R, A.i * R};
            }
        }
}

void super_dipole_expect(const DipoleMatrix *D, const Complex *c, double out[3]) {
    for (int a = 0; a < 3; a++) {
        double sum = 0.0;
        for (int j = 0; j < D->count; j++)
            for (int k = 0; k < D->count; k++) {
                Complex t = cmul(cconj(c[j]), cmul(D->d[a][j][k], c[k]));
                sum += t.r;
            }
        out[a] = sum;
    }
}

/* ---------------- presets ---------------- */

typedef struct { const char *name; int count; int n[3], l[3], m[3]; } PresetDef;
static const PresetDef k_presets[] = {
    {"1s + 2p_z   (dipole oscillation)",             2, {1, 2, 0}, {0, 1, 0}, {0, 0, 0}},
    {"1s + 2s   (breathing mode)",                    2, {1, 2, 0}, {0, 0, 0}, {0, 0, 0}},
    {"2p_z + 3d_z²   (dipole oscillation)",           2, {2, 3, 0}, {1, 2, 0}, {0, 0, 0}},
    {"2s + 2p_z   (degenerate sp hybrid, static)",    2, {2, 2, 0}, {0, 1, 0}, {0, 0, 0}},
    {"1s + 2p, m = +1   (rotating / in-plane dipole)", 2, {1, 2, 0}, {0, 1, 0}, {0, 1, 0}},
    {"1s + 2p_z + 3d_z²   (three levels)",            3, {1, 2, 3}, {0, 1, 2}, {0, 0, 0}},
};

int super_preset_count(void) { return (int)(sizeof(k_presets) / sizeof(k_presets[0])); }
const char *super_preset_name(int i) { return k_presets[i < 0 || i >= super_preset_count() ? 0 : i].name; }

SuperSpec super_preset(int idx, double Z, int complex_basis) {
    const PresetDef *d = &k_presets[idx < 0 || idx >= super_preset_count() ? 0 : idx];
    SuperSpec s;
    memset(&s, 0, sizeof(s));
    s.count = d->count; s.Z = Z; s.complex_basis = complex_basis;
    for (int k = 0; k < d->count; k++) s.c[k] = (SuperComponent){d->n[k], d->l[k], d->m[k], 1.0, 0.0};
    return s;
}

double transition_sweep_theta(double tau) {
    if (tau < 0.0) tau = 0.0;
    if (tau > 1.0) tau = 1.0;
    double s = tau * tau * (3.0 - 2.0 * tau);   /* smoothstep */
    return s * 0.5 * M_PI;
}
