#define _POSIX_C_SOURCE 200809L
#include <math.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "sampling.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef struct { uint64_t s; } Rng;

static uint64_t splitmix64(uint64_t *x) {
    uint64_t z = (*x += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}
static inline double rng_u(Rng *r) {   /* [0,1) */
    uint64_t x = r->s;
    x ^= x >> 12; x ^= x << 25; x ^= x >> 27;
    r->s = x;
    return (double)((x * 2685821657736338717ULL) >> 11) * (1.0 / 9007199254740992.0);
}

typedef struct {
    const SamplingRequest *req;
    const RadialTable *tab;
    YlmCtx ylm;
    double ymax;
    int begin, end;
    uint64_t seed;
    PointVertex *out;
    const atomic_int *latest;
    int my_id;
    atomic_int *progress;
    long n_pos, n_neg;
    int cancelled;
} Worker;

static void *worker_main(void *arg) {
    Worker *w = arg;
    Rng rng = { 0 };
    uint64_t sm = w->seed;
    rng.s = splitmix64(&sm) | 1ULL;
    const int p2 = (w->req->mode == SAMPLE_DENSITY);
    const double inv_ymax = 1.0 / w->ymax;
    const double inv_rmax = 1.0 / w->tab->r_abs_max;
    int batch = 0;

    for (int i = w->begin; i < w->end; i++) {
        if ((i & 4095) == 0 && w->latest && atomic_load(w->latest) != w->my_id) { w->cancelled = 1; return NULL; }
        double ct, phi, yre, yim, yabs;
        for (;;) {
            ct = 2.0 * rng_u(&rng) - 1.0;
            phi = 2.0 * M_PI * rng_u(&rng);
            ylm_eval(&w->ylm, ct, phi, &yre, &yim);
            yabs = sqrt(yre * yre + yim * yim) * inv_ymax;
            double acc = p2 ? yabs * yabs : yabs;
            if (rng_u(&rng) < acc) break;
        }
        double r = radial_table_sample(w->tab, rng_u(&rng));
        double R = radial_table_R(w->tab, r);
        double st = sqrt(fmax(0.0, 1.0 - ct * ct));
        double sp, cp;
        sp = sin(phi); cp = cos(phi);

        double t = fmin(1.0, fabs(R) * inv_rmax * yabs);
        double amp = p2 ? t * t : t;

        double ph;
        if (w->ylm.cplx) {
            double re = R * yre, im = R * yim;
            ph = atan2(im, re);
        } else {
            ph = (R * yre < 0.0) ? M_PI : 0.0;
        }
        double turn = ph / (2.0 * M_PI);
        turn -= floor(turn);
        PointVertex *v = &w->out[i];
        v->x = (float)(r * st * cp);
        v->y = (float)(r * st * sp);
        v->z = (float)(r * ct);
        v->amp = (uint16_t)(amp * 65535.0 + 0.5);
        uint32_t pq = (uint32_t)(turn * 65536.0 + 0.5);
        v->phase = (uint16_t)(pq & 0xFFFF);
        double c = cos(ph);
        if (c >= 0.0) w->n_pos++; else w->n_neg++;

        if (w->progress && ++batch == 8192) { atomic_fetch_add(w->progress, batch); batch = 0; }
    }
    if (w->progress && batch) atomic_fetch_add(w->progress, batch);
    return NULL;
}

int sampling_generate(const SamplingRequest *req, PointVertex **out, SamplingResult *res,
                      const atomic_int *latest, int my_id, atomic_int *progress) {
    *out = NULL;
    if (res) memset(res, 0, sizeof(*res));
    if (req->count <= 0) return 0;

    RadialTable tab;
    if (!radial_table_build(&tab, &req->spec, req->mode == SAMPLE_DENSITY ? 2 : 1, 16384)) return 0;

    YlmCtx ylm;
    ylm_init(&ylm, req->spec.l, req->spec.m, req->spec.complex_basis);
    double ymax = ylm_max_abs(&ylm);
    if (!(ymax > 0.0)) { radial_table_free(&tab); return 0; }

    PointVertex *verts = malloc((size_t)req->count * sizeof(PointVertex));
    if (!verts) { radial_table_free(&tab); return 0; }

    long ncpu = sysconf(_SC_NPROCESSORS_ONLN);
    int nthreads = (int)(ncpu < 1 ? 1 : (ncpu > 16 ? 16 : ncpu));
    if (req->count < 20000) nthreads = 1;

    Worker workers[16];
    pthread_t threads[16];
    int started[16] = {0};
    uint64_t sm = req->seed ? req->seed : 0x1234567ULL;
    int chunk = (req->count + nthreads - 1) / nthreads;
    for (int t = 0; t < nthreads; t++) {
        Worker *w = &workers[t];
        memset(w, 0, sizeof(*w));
        w->req = req; w->tab = &tab; w->ylm = ylm; w->ymax = ymax;
        w->begin = t * chunk;
        w->end = (t + 1) * chunk > req->count ? req->count : (t + 1) * chunk;
        w->seed = splitmix64(&sm);
        w->out = verts; w->latest = latest; w->my_id = my_id; w->progress = progress;
    }
    /* Run worker 0 on this thread, the rest on pthreads. */
    for (int t = 1; t < nthreads; t++)
        started[t] = (pthread_create(&threads[t], NULL, worker_main, &workers[t]) == 0);
    for (int t = 1; t < nthreads; t++)
        if (!started[t]) worker_main(&workers[t]);   /* fall back to running inline */
    worker_main(&workers[0]);
    for (int t = 1; t < nthreads; t++)
        if (started[t]) pthread_join(threads[t], NULL);

    int cancelled = 0;
    long npos = 0, nneg = 0;
    for (int t = 0; t < nthreads; t++) {
        cancelled |= workers[t].cancelled;
        npos += workers[t].n_pos;
        nneg += workers[t].n_neg;
    }
    radial_table_free(&tab);
    if (cancelled) { free(verts); return 0; }
    *out = verts;
    if (res) { res->count = req->count; res->n_pos = npos; res->n_neg = nneg; }
    return req->count;
}
