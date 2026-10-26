#include <math.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include "volume.h"
#include "platform.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

uint16_t float_to_half(float f) {
    union { float f; uint32_t u; } v = { f };
    uint32_t sign = (v.u >> 16) & 0x8000u;
    int32_t exp = (int32_t)((v.u >> 23) & 0xFF) - 127 + 15;
    uint32_t man = v.u & 0x7FFFFFu;
    if (((v.u >> 23) & 0xFF) == 0xFF) return (uint16_t)(sign | 0x7C00u | (man ? 0x200u : 0));   /* inf / nan */
    if (exp >= 31) return (uint16_t)(sign | 0x7BFFu);                                          /* saturate */
    if (exp <= 0) {
        if (exp < -10) return (uint16_t)sign;
        man |= 0x800000u;
        uint32_t shift = (uint32_t)(14 - exp);
        uint32_t h = man >> shift;
        uint32_t rem = man & ((1u << shift) - 1), half = 1u << (shift - 1);
        if (rem > half || (rem == half && (h & 1))) h++;
        return (uint16_t)(sign | h);
    }
    uint32_t h = ((uint32_t)exp << 10) | (man >> 13);
    uint32_t rem = man & 0x1FFFu;
    if (rem > 0x1000u || (rem == 0x1000u && (h & 1))) h++;      /* carry into exponent is correct */
    return (uint16_t)(sign | h);
}

typedef struct {
    const VolumeRequest *req;
    const RadialTable *tab;
    const YlmCtx *ylm;
    float *field;          /* res^3 * 2K floats: temporary */
    float *mean_rho;       /* res^3 */
    int K, res;
    double half;
    int z0, z1;
    const atomic_int *latest;
    int my_id;
    atomic_int *progress;
    int cancelled;
} VWorker;

static void *volume_worker(void *arg) {
    VWorker *w = arg;
    const int res = w->res, K = w->K;
    const double h = 2.0 * w->half / res;
    for (int iz = w->z0; iz < w->z1; iz++) {
        if (w->latest && atomic_load(w->latest) != w->my_id) { w->cancelled = 1; return NULL; }
        double z = -w->half + (iz + 0.5) * h;
        for (int iy = 0; iy < res; iy++) {
            double y = -w->half + (iy + 0.5) * h;
            for (int ix = 0; ix < res; ix++) {
                double x = -w->half + (ix + 0.5) * h;
                double r = sqrt(x * x + y * y + z * z);
                double ct = r > 1e-12 ? z / r : 0.0;
                double phi = atan2(y, x);
                size_t idx = ((size_t)iz * res + iy) * res + ix;
                double rho = 0.0;
                for (int k = 0; k < K; k++) {
                    double re, im;
                    ylm_eval(&w->ylm[k], ct, phi, &re, &im);
                    double R = radial_table_R(&w->tab[k], r);
                    w->field[idx * 2 * K + 2 * k] = (float)(R * re);
                    w->field[idx * 2 * K + 2 * k + 1] = (float)(R * im);
                    rho += R * R * (re * re + im * im);
                }
                w->mean_rho[idx] = (float)(rho / K);
            }
        }
        if (w->progress) atomic_fetch_add(w->progress, res * res);
    }
    return NULL;
}

int volume_generate(const VolumeRequest *req, Volume *out, const atomic_int *latest, int my_id, atomic_int *progress) {
    memset(out, 0, sizeof(*out));
    const SuperSpec *sp = &req->spec;
    const int K = sp->count, res = req->res;
    if (K < 1 || K > SUPER_MAX || res < 8 || res > 256) return 0;

    RadialTable tab[SUPER_MAX];
    YlmCtx ylm[SUPER_MAX];
    int built = 0, ok = 1;
    for (int k = 0; k < K; k++) {
        OrbitalSpec o = super_component_spec(sp, k);
        if (!radial_table_build(&tab[k], &o, 2, 16384)) { ok = 0; break; }
        built++;
        ylm_init(&ylm[k], o.l, o.m, o.complex_basis);
    }
    size_t nvox = (size_t)res * res * res;
    float *field = ok ? malloc(nvox * 2 * K * sizeof(float)) : NULL;
    float *rho = ok ? malloc(nvox * sizeof(float)) : NULL;
    if (!ok || !field || !rho) {
        for (int k = 0; k < built; k++) radial_table_free(&tab[k]);
        free(field); free(rho);
        return 0;
    }

    int nt = plat_cpu_count();
    if (nt > 16) nt = 16;
    if (nt > res) nt = res;
    VWorker W[16];
    pthread_t th[16];
    int started[16] = {0};
    for (int t = 0; t < nt; t++) {
        memset(&W[t], 0, sizeof(W[t]));
        W[t].req = req; W[t].tab = tab; W[t].ylm = ylm; W[t].field = field; W[t].mean_rho = rho;
        W[t].K = K; W[t].res = res; W[t].half = req->half;
        W[t].z0 = (int)((long)res * t / nt); W[t].z1 = (int)((long)res * (t + 1) / nt);
        W[t].latest = latest; W[t].my_id = my_id; W[t].progress = progress;
    }
    for (int t = 1; t < nt; t++) started[t] = (pthread_create(&th[t], NULL, volume_worker, &W[t]) == 0);
    for (int t = 1; t < nt; t++) if (!started[t]) volume_worker(&W[t]);
    volume_worker(&W[0]);
    for (int t = 1; t < nt; t++) if (started[t]) pthread_join(th[t], NULL);
    int cancelled = 0;
    for (int t = 0; t < nt; t++) cancelled |= W[t].cancelled;
    for (int k = 0; k < K; k++) radial_table_free(&tab[k]);
    if (cancelled) { free(field); free(rho); return 0; }

    /* scale + pack to half floats */
    float vmax = 0.0f;
    for (size_t i = 0; i < nvox * 2 * K; i++) { float a = fabsf(field[i]); if (a > vmax) vmax = a; }
    if (!(vmax > 0.0f)) { free(field); free(rho); return 0; }
    out->res = res; out->K = K; out->half = (float)req->half; out->vscale = vmax;
    int ntex = K > 2 ? 2 : 1;
    for (int t = 0; t < ntex; t++) {
        out->tex[t] = calloc(nvox * 4, sizeof(uint16_t));
        if (!out->tex[t]) { volume_free(out); free(field); free(rho); return 0; }
    }
    const float inv = 1.0f / vmax;
    for (size_t i = 0; i < nvox; i++)
        for (int k = 0; k < K; k++) {
            uint16_t *t = out->tex[k / 2] + i * 4 + 2 * (k % 2);
            t[0] = float_to_half(field[i * 2 * K + 2 * k] * inv);
            t[1] = float_to_half(field[i * 2 * K + 2 * k + 1] * inv);
        }

    /* probability -> density level table (log histogram of the mean density) */
    double dv = pow(2.0 * req->half / res, 3.0), rmax = 0.0;
    for (size_t i = 0; i < nvox; i++) if (rho[i] > rmax) rmax = rho[i];
    enum { NB = 4096 };
    static const double SPAN = 14.0;                 /* decades below the peak */
    double *mass = calloc(NB, sizeof(double));
    double total = 0.0;
    if (mass && rmax > 0.0) {
        for (size_t i = 0; i < nvox; i++) {
            if (rho[i] <= 0.0f) continue;
            double d = -log10((double)rho[i] / rmax);
            int b = (int)(d / SPAN * NB);
            if (b < 0) b = 0;
            if (b >= NB) continue;
            mass[b] += (double)rho[i] * dv;
            total += (double)rho[i] * dv;
        }
        out->total_mass = total;
        double acc = 0.0;
        int p = 1;
        for (int b = 0; b < NB && p <= 100; b++) {
            double prev = acc;
            acc += mass[b];
            while (p <= 100 && acc >= total * p / 100.0) {
                double f = mass[b] > 0 ? (total * p / 100.0 - prev) / mass[b] : 0.0;
                double d = ((double)b + f) / NB * SPAN;
                out->rho_level[p] = rmax * pow(10.0, -d);
                p++;
            }
        }
        out->rho_level[0] = rmax;
    }
    free(mass); free(field); free(rho);
    return 1;
}

void volume_free(Volume *v) {
    free(v->tex[0]); free(v->tex[1]);
    memset(v, 0, sizeof(*v));
}

double volume_level_for_probability(const Volume *v, double p) {
    if (p < 0.01) p = 0.01;
    if (p > 0.99) p = 0.99;
    double x = p * 100.0;
    int i = (int)x;
    double f = x - i;
    double a = v->rho_level[i], b = v->rho_level[i + 1];
    return a > 0 && b > 0 ? exp(log(a) * (1 - f) + log(b) * f) : a;
}
