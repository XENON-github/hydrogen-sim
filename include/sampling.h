#ifndef SAMPLING_H
#define SAMPLING_H

#include <stdint.h>
#include <stdatomic.h>
#include "hydrogen.h"

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

#endif /* SAMPLING_H */
