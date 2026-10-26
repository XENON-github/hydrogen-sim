#ifndef SAMPLING_H
#define SAMPLING_H

#include <stdint.h>
#include <stdatomic.h>
#include "hydrogen.h"
#include "superposition.h"

/* 16-byte GPU vertex. Colour mapping happens on the GPU, so palettes can change live. */
typedef struct {
    float x, y, z;
    uint16_t amp;    /* normalised: |psi|^2/max (density mode) or |psi|/max (amplitude mode) */
    uint16_t phase;  /* arg(psi) as a fraction of a full turn, 0..65535 */
} PointVertex;

typedef enum { SAMPLE_DENSITY = 0, SAMPLE_AMPLITUDE = 1 } SampleMode;

typedef struct {
    OrbitalSpec spec;
    int count;
    SampleMode mode;            /* points distributed as |psi|^2 or as |psi| */
    unsigned long long seed;
} SamplingRequest;

typedef struct {
    int count;
    long n_pos, n_neg;          /* points with cos(phase) >= 0 / < 0 (diagnostic) */
} SamplingResult;

/* Exact Monte-Carlo sampling: radius by inverse CDF of r^2|R|^p, direction by rejection on |Y|^p.
 * Multi-threaded. Returns 0 if cancelled (latest != my_id) or on failure. Caller free()s *out.
 * `progress` (optional) receives the number of finished points. */
int sampling_generate(const SamplingRequest *req, PointVertex **out, SamplingResult *res,
                      const atomic_int *latest, int my_id, atomic_int *progress);

/* ---------------------------------------------------------------------------------------------
 * Superposition states: weighted particles.
 *
 * Points are drawn once from a FIXED proposal (equal-weight mixture of the component distributions),
 * so the cloud can evolve in time without resampling. Each point stores, per component k, a unit
 * vector entry b_k (complex, snorm16). The GPU forms  psi' = sum_k C_k b_k  with CPU-supplied complex
 * uniforms C_k(t) (coefficient x time phase), and the particle's brightness is its importance weight.
 *
 *  SAMPLE_DENSITY: component k ~ |psi_k|^2, b_k = psi_k / sqrt(sum_j |psi_j|^2)
 *                  weight = K |psi'|^2,        density a = scalar * |psi'|^2
 *                  C_k = c_k e^{-i w_k t}
 *  SAMPLE_AMPLITUDE: component k ~ |psi_k|,   b_k = (psi_k/N_k) / sum_j(|psi_j|/N_j)
 *                  weight = |psi'|,            amplitude a = scalar * |psi'|
 *                  C_k = c_k N_k e^{-i w_k t} / gmax   (N_k = int |psi_k|, gmax = max N_k)
 * --------------------------------------------------------------------------------------------- */
typedef struct {
    float x, y, z;
    float scalar;       /* normalising factor described above (<= ~1) */
    int16_t b[8];       /* b_k re/im interleaved, snorm16 */
} SuperVertex;          /* 32 bytes */

typedef struct {
    SuperSpec spec;
    int count;
    SampleMode mode;
    unsigned long long seed;
} SuperSamplingRequest;

typedef struct {
    int count;
    int K;
    double comp_norm[SUPER_MAX];   /* N_k = int |psi_k| dV (amplitude mode) */
    double gmax;                   /* max_k N_k */
    double dens_bound;             /* normalisation used for `scalar` (diagnostic) */
} SuperSamplingResult;

int sampling_generate_super(const SuperSamplingRequest *req, SuperVertex **out, SuperSamplingResult *res,
                            const atomic_int *latest, int my_id, atomic_int *progress);

/* int |psi_k| dV for one orbital (the L1 norm used by the amplitude-mode proposal). */
double orbital_l1_norm(const OrbitalSpec *s);

#endif /* SAMPLING_H */
