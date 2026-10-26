#ifndef VOLUME_H
#define VOLUME_H

#include <stdint.h>
#include <stdatomic.h>
#include "superposition.h"

/* A 3D grid of the component wavefunctions psi_k(x) over the cube [-half, half]^3, stored as two
 * RGBA16F volumes (component k lives in volume k/2, channels 2*(k%2) and 2*(k%2)+1 = Re, Im).
 * The grid is independent of the coefficients, so changing amplitudes/phases/time never needs a rebuild.
 * Used by the GPU ray-marcher to draw isosurfaces of psi(x, t) = sum_k C_k(t) psi_k(x). */

typedef struct {
    int res, K;
    float half;
    float vscale;                /* stored = psi / vscale (so values are O(1) in half precision) */
    uint16_t *tex[2];            /* res^3 * 4 half floats each (tex[1] NULL when K <= 2) */
    double rho_level[101];       /* rho_level[p] = |psi|^2 threshold enclosing p % of the probability (mean density of components) */
    double total_mass;           /* grid integral of the mean density (should be ~1) */
} Volume;

typedef struct {
    SuperSpec spec;
    int res;
    double half;
} VolumeRequest;

/* Builds the grid with all CPU cores. Returns 0 if cancelled (latest != my_id) or on failure. */
int volume_generate(const VolumeRequest *req, Volume *out, const atomic_int *latest, int my_id, atomic_int *progress);
void volume_free(Volume *v);

/* |psi|^2 threshold whose super-level set holds probability p (0..1), interpolated from rho_level. */
double volume_level_for_probability(const Volume *v, double p);

/* float -> IEEE half (round to nearest even, saturating) */
uint16_t float_to_half(float f);

#endif /* VOLUME_H */
