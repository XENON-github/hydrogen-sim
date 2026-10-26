#ifndef TRANSITIONS_H
#define TRANSITIONS_H

#include "hydrogen.h"

/* Electric-dipole transitions between hydrogen-like levels. Atomic units unless stated. */

#define HARTREE_EV 27.211386245988
#define AU_TIME_S 2.4188843265857e-17
#define AU_TIME_FS 0.024188843265857
#define HC_EV_NM 1239.84198

#define MAX_DECAY_CHANNELS 40

/* Radial dipole integral  I = int R_{n1 l1}(r) R_{n2 l2}(r) r^3 dr   (in a0). */
double radial_dipole_integral(int n1, int l1, int n2, int l2, double Z);

/* E1 selection rule on l only (|dl| = 1). */
int transition_l_allowed(int l1, int l2);

/* Photon energy (eV) and vacuum wavelength (nm) for a transition between shells ni and nf. */
double transition_energy_ev(int ni, int nf, double Z);
double transition_wavelength_nm(int ni, int nf, double Z);

/* Series name for the lower shell ("Lyman", "Balmer", ...) and a line label like "Lyman-α". */
const char *transition_series(int n_lower);
void transition_line_label(int n_upper, int n_lower, char *buf, size_t size);

/* Spontaneous-emission (Einstein A) rate in 1/s for the decay (ni, li) -> (nf, lf), nf < ni.
 * Summed over the final m states, so it does not depend on the initial m. 0 if forbidden. */
double transition_rate(int ni, int li, int nf, int lf, double Z);

typedef struct {
    int nf, lf;
    double A;            /* 1/s */
    double energy_ev, wavelength_nm;
    double branching;    /* A / total */
} DecayChannel;

/* All E1-allowed decays of (ni, li). Returns the number of channels; *total_A = sum of rates (1/s). */
int transition_decay_channels(int ni, int li, double Z, DecayChannel *out, int max, double *total_A);

/* Rough visible-spectrum colour (sRGB 0..1); non-visible wavelengths give a dim grey-violet / grey-red. */
void wavelength_to_rgb(double nm, float rgb[3]);

/* Layout of the energy-level diagram (also used for mouse hit-testing). */
typedef struct { double x0, x1, y; int n, l; } LevelRect;

typedef struct {
    int n_max, l_cols;
    double w, h, left, right, top, bottom;
    LevelRect lv[HYDROGEN_MAX_N * HYDROGEN_MAX_N];
    int count;
} LevelLayout;

void level_layout_build(LevelLayout *L, int n_max, int l_cols, double w, double h, double Z);
/* y-position of an energy given as shell number n (same compressed scale as the layout). */
double level_layout_y(const LevelLayout *L, int n);
/* Index into L->lv of the level under (x, y), or -1. */
int level_layout_hit(const LevelLayout *L, double x, double y);

#endif /* TRANSITIONS_H */
