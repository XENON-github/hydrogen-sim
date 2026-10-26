#ifndef SUPERPOSITION_H
#define SUPERPOSITION_H

#include "hydrogen.h"

/* A coherent superposition  psi(t) = sum_k c_k exp(-i E_k t) psi_k  of hydrogen-like eigenstates that
 * share the same nuclear charge Z and orbital basis. Atomic units (hbar = m = e = 1). */

#define SUPER_MAX 4

typedef struct {
    int n, l, m;
    double amp;      /* relative amplitude >= 0 (normalised internally) */
    double phase;    /* relative phase, radians */
} SuperComponent;

typedef struct {
    int count;       /* 1 .. SUPER_MAX */
    SuperComponent c[SUPER_MAX];
    double Z;
    int complex_basis;
} SuperSpec;

/* Eigen-energy in Hartree (-Z^2 / 2n^2). */
double super_energy(int n, double Z);

/* Normalised complex coefficients at t = 0 (sum |c|^2 = 1). */
void super_coeffs0(const SuperSpec *s, Complex out[SUPER_MAX]);

/* Coefficients at simulation time t (atomic units). Phases are measured against the LOWEST component
 * energy, which removes the unobservable global phase (the cloud would otherwise spin at ~E/hbar) and
 * keeps every phase independent of the amplitudes, so beat patterns loop exactly after 2 pi / dE. */
double super_reference_energy(const SuperSpec *s);
void super_coeffs_at(const SuperSpec *s, double t_au, Complex out[SUPER_MAX]);

double super_mean_energy(const SuperSpec *s);
/* Largest and smallest non-zero |E_j - E_k| among components with amplitude; 0 if all degenerate. */
double super_max_omega(const SuperSpec *s);
double super_min_omega(const SuperSpec *s);

/* OrbitalSpec of component k. */
OrbitalSpec super_component_spec(const SuperSpec *s, int k);

/* Position matrix elements  D[a][j][k] = < psi_j | x_a | psi_k >  in a0  (a = 0,1,2 = x,y,z). */
typedef struct { Complex d[3][SUPER_MAX][SUPER_MAX]; int count; } DipoleMatrix;
void super_dipole_matrix(const SuperSpec *s, DipoleMatrix *D);
/* <r>(t) for the coefficient vector c (need not be time-evolved by the caller: pass super_coeffs_at). */
void super_dipole_expect(const DipoleMatrix *D, const Complex *c, double out[3]);

/* Angular matrix element  int Y_a^* n_axis Y_b dOmega  (axis 0..2). */
Complex angular_position_element(const YlmCtx *a, const YlmCtx *b, int axis);

/* Built-in example superpositions. */
int super_preset_count(void);
const char *super_preset_name(int idx);
SuperSpec super_preset(int idx, double Z, int complex_basis);

/* Transition sweep: c_i = cos(theta), c_f = sin(theta) with a smooth theta(tau), tau in [0,1]. */
double transition_sweep_theta(double tau);

#endif /* SUPERPOSITION_H */
