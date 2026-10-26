#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include <math.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include "platform.h"
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

    int ncpu = plat_cpu_count();
    int nthreads = ncpu > 16 ? 16 : ncpu;
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

/* ================================================================================================ */
/* Superposition sampler                                                                            */
/* ================================================================================================ */

double orbital_l1_norm(const OrbitalSpec *s) {
    RadialTable t;
    if (!radial_table_build(&t, s, 1, 8192)) return 0.0;
    double rad = 0.0;
    for (int i = 1; i < t.size; i++) {
        double r0 = (i - 1) * t.dr, r1 = i * t.dr;
        rad += 0.5 * (r0 * r0 * fabs(t.R[i - 1]) + r1 * r1 * fabs(t.R[i])) * t.dr;
    }
    radial_table_free(&t);
    /* angular: int |Y| dOmega on a fine midpoint grid */
    YlmCtx y;
    ylm_init(&y, s->l, s->m, s->complex_basis);
    const int NT = 720, NP = (s->complex_basis || s->m == 0) ? 1 : 720;
    double ang = 0.0;
    for (int i = 0; i < NT; i++) {
        double ct = -1.0 + 2.0 * (i + 0.5) / NT;
        for (int j = 0; j < NP; j++) {
            double re, im;
            ylm_eval(&y, ct, 2.0 * M_PI * (j + 0.5) / NP, &re, &im);
            ang += sqrt(re * re + im * im);
        }
    }
    ang *= (2.0 / NT) * (2.0 * M_PI / NP);
    return rad * ang;
}

typedef struct {
    const SuperSamplingRequest *req;
    const RadialTable *tab;     /* [K] */
    const YlmCtx *ylm;          /* [K] */
    const double *ymax;         /* [K] */
    const double *norm;         /* N_k */
    int K;
    int begin, end;
    uint64_t seed;
    SuperVertex *out;
    float *raw;                 /* per-point un-normalised scalar */
    double raw_max;
    const atomic_int *latest;
    int my_id;
    atomic_int *progress;
    int cancelled;
} SWorker;

static void *super_worker(void *arg) {
    SWorker *w = arg;
    Rng rng = { 0 };
    uint64_t sm = w->seed;
    rng.s = splitmix64(&sm) | 1ULL;
    const int dens = (w->req->mode == SAMPLE_DENSITY);
    const int K = w->K;
    int batch = 0;
    double rmax = 0.0;

    for (int i = w->begin; i < w->end; i++) {
        if ((i & 2047) == 0 && w->latest && atomic_load(w->latest) != w->my_id) { w->cancelled = 1; return NULL; }
        int k = i % K;                                   /* stratified component choice */
        double ct, phi, yre, yim, yabs;
        for (;;) {
            ct = 2.0 * rng_u(&rng) - 1.0;
            phi = 2.0 * M_PI * rng_u(&rng);
            ylm_eval(&w->ylm[k], ct, phi, &yre, &yim);
            yabs = sqrt(yre * yre + yim * yim) / w->ymax[k];
            if (rng_u(&rng) < (dens ? yabs * yabs : yabs)) break;
        }
        double r = radial_table_sample(&w->tab[k], rng_u(&rng));
        double st = sqrt(fmax(0.0, 1.0 - ct * ct));

        double pr[SUPER_MAX], pi_[SUPER_MAX];
        double sum2 = 0.0, sum1 = 0.0;
        for (int j = 0; j < K; j++) {
            double re, im;
            ylm_eval(&w->ylm[j], ct, phi, &re, &im);
            double R = radial_table_R(&w->tab[j], r);
            pr[j] = R * re; pi_[j] = R * im;
            double m2 = pr[j] * pr[j] + pi_[j] * pi_[j];
            sum2 += m2;
            sum1 += sqrt(m2) / w->norm[j];
        }
        SuperVertex *v = &w->out[i];
        v->x = (float)(r * st * cos(phi)); v->y = (float)(r * st * sin(phi)); v->z = (float)(r * ct);
        for (int q = 0; q < 8; q++) v->b[q] = 0;
        double raw;
        if (dens) {
            double inv = sum2 > 0.0 ? 1.0 / sqrt(sum2) : 0.0;
            for (int j = 0; j < K; j++) {
                v->b[2 * j] = (int16_t)lrint(pr[j] * inv * 32767.0);
                v->b[2 * j + 1] = (int16_t)lrint(pi_[j] * inv * 32767.0);
            }
            raw = sum2;
        } else {
            double inv = sum1 > 0.0 ? 1.0 / sum1 : 0.0;
            for (int j = 0; j < K; j++) {
                v->b[2 * j] = (int16_t)lrint(pr[j] / w->norm[j] * inv * 32767.0);
                v->b[2 * j + 1] = (int16_t)lrint(pi_[j] / w->norm[j] * inv * 32767.0);
            }
            raw = sum1;     /* S_i = sum |psi_j| / N_j ; rescaled below */
        }
        w->raw[i] = (float)raw;
        /* bound used to normalise the colour ramp: max possible |psi|^2 (density) or |psi| (amplitude) */
        double s1 = 0.0;
        for (int j = 0; j < K; j++) s1 += sqrt(pr[j] * pr[j] + pi_[j] * pi_[j]);
        double bound = dens ? s1 * s1 / K : s1 / sqrt((double)K);   /* max |psi|^2 / |psi| for equal-weight coefficients */
        if (bound > rmax) rmax = bound;
        if (w->progress && ++batch == 4096) { atomic_fetch_add(w->progress, batch); batch = 0; }
    }
    w->raw_max = rmax;
    if (w->progress && batch) atomic_fetch_add(w->progress, batch);
    return NULL;
}

int sampling_generate_super(const SuperSamplingRequest *req, SuperVertex **out, SuperSamplingResult *res,
                            const atomic_int *latest, int my_id, atomic_int *progress) {
    *out = NULL;
    if (res) memset(res, 0, sizeof(*res));
    const SuperSpec *sp = &req->spec;
    const int K = sp->count;
    if (req->count <= 0 || K < 1 || K > SUPER_MAX) return 0;

    RadialTable tab[SUPER_MAX];
    YlmCtx ylm[SUPER_MAX];
    double ymax[SUPER_MAX], norm[SUPER_MAX], gmax = 0.0;
    int built = 0, ok = 1;
    for (int k = 0; k < K; k++) {
        OrbitalSpec o = super_component_spec(sp, k);
        if (!radial_table_build(&tab[k], &o, req->mode == SAMPLE_DENSITY ? 2 : 1, 16384)) { ok = 0; break; }
        built++;
        ylm_init(&ylm[k], o.l, o.m, o.complex_basis);
        ymax[k] = ylm_max_abs(&ylm[k]);
        norm[k] = req->mode == SAMPLE_AMPLITUDE ? orbital_l1_norm(&o) : 1.0;
        if (!(ymax[k] > 0.0) || !(norm[k] > 0.0)) { ok = 0; break; }
        if (norm[k] > gmax) gmax = norm[k];
    }
    SuperVertex *verts = NULL;
    float *raw = NULL;
    if (ok) { verts = malloc((size_t)req->count * sizeof(SuperVertex)); raw = malloc((size_t)req->count * sizeof(float)); }
    if (!ok || !verts || !raw) {
        for (int k = 0; k < built; k++) radial_table_free(&tab[k]);
        free(verts); free(raw);
        return 0;
    }

    int nthreads = plat_cpu_count();
    if (nthreads > 16) nthreads = 16;
    if (req->count < 20000) nthreads = 1;
    SWorker workers[16];
    pthread_t threads[16];
    int started[16] = {0};
    uint64_t sm = req->seed ? req->seed : 0x7654321ULL;
    int chunk = (req->count + nthreads - 1) / nthreads;
    for (int t = 0; t < nthreads; t++) {
        SWorker *w = &workers[t];
        memset(w, 0, sizeof(*w));
        w->req = req; w->tab = tab; w->ylm = ylm; w->ymax = ymax; w->norm = norm; w->K = K;
        w->begin = t * chunk;
        w->end = (t + 1) * chunk > req->count ? req->count : (t + 1) * chunk;
        w->seed = splitmix64(&sm);
        w->out = verts; w->raw = raw; w->latest = latest; w->my_id = my_id; w->progress = progress;
    }
    for (int t = 1; t < nthreads; t++) started[t] = (pthread_create(&threads[t], NULL, super_worker, &workers[t]) == 0);
    for (int t = 1; t < nthreads; t++) if (!started[t]) super_worker(&workers[t]);
    super_worker(&workers[0]);
    for (int t = 1; t < nthreads; t++) if (started[t]) pthread_join(threads[t], NULL);

    int cancelled = 0;
    double bound = 0.0;
    for (int t = 0; t < nthreads; t++) { cancelled |= workers[t].cancelled; if (workers[t].raw_max > bound) bound = workers[t].raw_max; }
    for (int k = 0; k < K; k++) radial_table_free(&tab[k]);
    if (cancelled || !(bound > 0.0)) { free(verts); free(raw); return 0; }

    /* Per-point scalar. density: sum_j|psi_j|^2 / bound.  amplitude: S_i / bound, where |psi| = S_i |sum_k g_k b_k|. */
    for (int i = 0; i < req->count; i++) verts[i].scalar = (float)(raw[i] / bound);
    free(raw);
    *out = verts;
    if (res) {
        res->count = req->count; res->K = K; res->gmax = gmax; res->dens_bound = bound;
        for (int k = 0; k < K; k++) res->comp_norm[k] = norm[k];
    }
    return req->count;
}
