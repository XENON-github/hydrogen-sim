#ifndef HYDROGEN_H
#define HYDROGEN_H

#include <stddef.h>

/* All lengths are in Bohr radii (a0) unless stated otherwise. */

typedef struct { double r, i; } Complex;

typedef struct {
    int n, l, m;
    double Z;          /* nuclear charge: 1 = H, 2 = He+, 3 = Li2+ ... */
    int complex_basis; /* 0: real (chemist's) orbitals, 1: complex Y_l^m eigenstates */
} OrbitalSpec;

#define HYDROGEN_MAX_N 8
#define PROPS_PLOT_N 256
#define PROPS_MAX_NODES 8

typedef struct {
    double a;                 /* length unit a0/Z */
    double energy_ev;         /* E_n = -13.6057 Z^2 / n^2 */
    double r_expect;          /* <r> */
    double r_peak;            /* most probable radius of the radial distribution */
    double r_90;              /* sphere that contains 90% of the probability */
    double r_extent;          /* sphere that contains 99.9% (used for framing / grid) */
    int radial_nodes, angular_nodes, total_nodes;
    int node_count;
    double node_r[PROPS_MAX_NODES];
    double L_hbar;            /* |L| = sqrt(l(l+1)) hbar */
    double Lz_hbar;           /* L_z = m hbar (meaningful for the complex basis) */
    int degeneracy;           /* n^2 (spin neglected) */
    double plot_rmax;
    double plot_p[PROPS_PLOT_N]; /* r^2 R^2, normalised to max = 1, over [0, plot_rmax] */
} OrbitalProps;

typedef struct { int n, l, m; } OrbitalPreset;

/* ---- special functions ---- */
double factorial(int n);
double assoc_legendre(int l, int m, double x);   /* includes the Condon-Shortley phase */
double assoc_laguerre(int k, int alpha, double x);

/* ---- wave function pieces ---- */
double radial_wave_function(int n, int l, double r, double a);

typedef struct { int l, m, abs_m, cplx; double norm; } YlmCtx;
void ylm_init(YlmCtx *c, int l, int m, int complex_basis);
/* Real basis: *im = 0 and *re is the real spherical harmonic. Complex basis: Y_l^m. */
void ylm_eval(const YlmCtx *c, double cos_theta, double phi, double *re, double *im);
double ylm_max_abs(const YlmCtx *c);

/* Full wave function psi(r, theta, phi). */
Complex hydrogen_psi(const OrbitalSpec *s, double r, double cos_theta, double phi);

/* ---- radial sampling tables ---- */
typedef struct {
    int size;
    double dr, r_hi;
    double *R;       /* signed radial function at each grid point */
    double *cdf;     /* cumulative distribution of r^2 |R|^power, normalised */
    double r_abs_max;/* max |R| */
} RadialTable;

int radial_table_build(RadialTable *t, const OrbitalSpec *s, int power, int size);
void radial_table_free(RadialTable *t);
double radial_table_sample(const RadialTable *t, double u);   /* inverse CDF, u in [0,1) */
double radial_table_R(const RadialTable *t, double r);        /* interpolated signed R */
double radial_table_quantile(const RadialTable *t, double p); /* radius containing probability p */

/* ---- metadata ---- */
OrbitalProps hydrogen_compute_props(const OrbitalSpec *s);
void hydrogen_orbital_name(const OrbitalSpec *s, char *buf, size_t size);

int hydrogen_preset_count(void);
OrbitalPreset hydrogen_preset(int idx);
int hydrogen_find_preset(int n, int l, int m); /* -1 if none */

#endif /* HYDROGEN_H */
