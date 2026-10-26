#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "transitions.h"

#define ALPHA_FS 7.2973525693e-3

double radial_dipole_integral(int n1, int l1, int n2, int l2, double Z) {
    double a = 1.0 / Z;
    int nm = n1 > n2 ? n1 : n2;
    double rmax = a * (2.0 * nm * nm + 10.0 * nm + 30.0);
    const int N = 40000;                    /* even, Simpson */
    double h = rmax / N, sum = 0.0;
    for (int i = 0; i <= N; i++) {
        double r = i * h;
        double f = radial_wave_function(n1, l1, r, a) * radial_wave_function(n2, l2, r, a) * r * r * r;
        sum += f * ((i == 0 || i == N) ? 1.0 : (i & 1) ? 4.0 : 2.0);
    }
    return sum * h / 3.0;
}

int transition_l_allowed(int l1, int l2) { return abs(l1 - l2) == 1; }

double transition_energy_ev(int ni, int nf, double Z) {
    double e = 13.605693122994 * Z * Z * fabs(1.0 / ((double)nf * nf) - 1.0 / ((double)ni * ni));
    return e;
}

double transition_wavelength_nm(int ni, int nf, double Z) {
    double e = transition_energy_ev(ni, nf, Z);
    return e > 0.0 ? HC_EV_NM / e : INFINITY;
}

const char *transition_series(int nl) {
    switch (nl) {
    case 1: return "Lyman";
    case 2: return "Balmer";
    case 3: return "Paschen";
    case 4: return "Brackett";
    case 5: return "Pfund";
    case 6: return "Humphreys";
    default: return "";
    }
}

void transition_line_label(int nu, int nl, char *buf, size_t size) {
    static const char *greek[] = {"α", "β", "γ", "δ", "ε", "ζ", "η"};
    const char *s = transition_series(nl);
    int k = nu - nl - 1;
    if (s[0] && k >= 0 && k < 7) snprintf(buf, size, "%s-%s", s, greek[k]);
    else snprintf(buf, size, "n=%d → n=%d", nu, nl);
}

double transition_rate(int ni, int li, int nf, int lf, double Z) {
    if (nf >= ni || !transition_l_allowed(li, lf)) return 0.0;
    double I = radial_dipole_integral(ni, li, nf, lf, Z);
    int lg = li > lf ? li : lf;
    double d2 = (double)lg / (2.0 * li + 1.0) * I * I;           /* sum over final m, in a0^2 */
    double w = Z * Z * (0.5 / ((double)nf * nf) - 0.5 / ((double)ni * ni));   /* Hartree */
    double A_au = (4.0 / 3.0) * ALPHA_FS * ALPHA_FS * ALPHA_FS * w * w * w * d2;
    return A_au / AU_TIME_S;
}

int transition_decay_channels(int ni, int li, double Z, DecayChannel *out, int max, double *total_A) {
    int c = 0;
    double tot = 0.0;
    for (int nf = 1; nf < ni; nf++)
        for (int lf = li - 1; lf <= li + 1; lf += 2) {
            if (lf < 0 || lf >= nf || c >= max) continue;
            double A = transition_rate(ni, li, nf, lf, Z);
            if (A <= 0.0) continue;
            out[c].nf = nf; out[c].lf = lf; out[c].A = A;
            out[c].energy_ev = transition_energy_ev(ni, nf, Z);
            out[c].wavelength_nm = transition_wavelength_nm(ni, nf, Z);
            tot += A;
            c++;
        }
    for (int i = 0; i < c; i++) out[i].branching = out[i].A / tot;
    if (total_A) *total_A = tot;
    return c;
}

void wavelength_to_rgb(double nm, float rgb[3]) {
    double r = 0, g = 0, b = 0;
    if (nm >= 380 && nm < 440)      { r = -(nm - 440) / 60.0; b = 1.0; }
    else if (nm < 490)              { g = (nm - 440) / 50.0; b = 1.0; }
    else if (nm < 510)              { g = 1.0; b = -(nm - 510) / 20.0; }
    else if (nm < 580)              { r = (nm - 510) / 70.0; g = 1.0; }
    else if (nm < 645)              { r = 1.0; g = -(nm - 645) / 65.0; }
    else if (nm <= 780)             { r = 1.0; }
    else if (nm > 780)              { rgb[0] = 0.55f; rgb[1] = 0.25f; rgb[2] = 0.25f; return; }     /* infrared */
    else                            { rgb[0] = 0.55f; rgb[1] = 0.45f; rgb[2] = 0.90f; return; }     /* ultraviolet */
    double f = 1.0;
    if (nm < 420) f = 0.3 + 0.7 * (nm - 380) / 40.0;
    else if (nm > 700) f = 0.3 + 0.7 * (780 - nm) / 80.0;
    rgb[0] = (float)pow(r * f, 0.8); rgb[1] = (float)pow(g * f, 0.8); rgb[2] = (float)pow(b * f, 0.8);
}

/* ---------------- energy-level diagram layout ---------------- */

/* Energy axis is compressed (y ~ sqrt|E| ~ 1/n) so the crowded high-n levels stay separable.
 * The ionisation limit E = 0 is at the top of the plot, n = 1 at the bottom. */
double level_layout_y(const LevelLayout *L, int n) {
    double top = L->top + 6.0, bot = L->h - L->bottom;
    return top + (bot - top) / (double)n;
}

void level_layout_build(LevelLayout *L, int n_max, int l_cols, double w, double h, double Z) {
    (void)Z;
    memset(L, 0, sizeof(*L));
    if (n_max > HYDROGEN_MAX_N) n_max = HYDROGEN_MAX_N;
    if (n_max < 2) n_max = 2;
    if (l_cols > n_max) l_cols = n_max;
    if (l_cols < 1) l_cols = 1;
    L->n_max = n_max; L->l_cols = l_cols;
    L->w = w; L->h = h;
    L->left = 62; L->right = 12; L->top = 22; L->bottom = 8;
    double cw = (w - L->left - L->right) / l_cols;
    for (int n = 1; n <= n_max; n++)
        for (int l = 0; l < n && l < l_cols; l++) {
            LevelRect *r = &L->lv[L->count++];
            r->n = n; r->l = l;
            r->x0 = L->left + l * cw + cw * 0.14;
            r->x1 = L->left + (l + 1) * cw - cw * 0.14;
            r->y = level_layout_y(L, n);
        }
}

int level_layout_hit(const LevelLayout *L, double x, double y) {
    int best = -1;
    double bd = 7.0;
    for (int i = 0; i < L->count; i++) {
        const LevelRect *r = &L->lv[i];
        if (x < r->x0 - 3 || x > r->x1 + 3) continue;
        double d = fabs(y - r->y);
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}
