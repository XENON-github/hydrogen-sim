//test file, is not mandatory, just incase
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "hydrogen.h"
#include "sampling.h"
static int fails = 0;
#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

int main(void) {
    /* 1. radial normalisation  int r^2 R^2 dr = 1 and <r> formula */
    for (int n = 1; n <= 7; n++) for (int l = 0; l < n; l++) {
        double Z = (n % 2) ? 1.0 : 2.0, a = 1.0 / Z, I = 0, Ir = 0, h = 0.002 * a;
        for (double r = h / 2; r < 400 * a; r += h) { double R = radial_wave_function(n, l, r, a); I += r * r * R * R * h; Ir += r * r * r * R * R * h; }
        double er = 0.5 * a * (3.0 * n * n - l * (l + 1.0));
        CHECK(fabs(I - 1) < 1e-3, "norm n=%d l=%d got %.5f", n, l, I);
        CHECK(fabs(Ir - er) / er < 2e-3, "<r> n=%d l=%d got %.4f want %.4f", n, l, Ir, er);
    }
    /* 2. angular normalisation (real and complex) */
    for (int cplx = 0; cplx < 2; cplx++) for (int l = 0; l <= 4; l++) for (int m = -l; m <= l; m++) {
        YlmCtx y; ylm_init(&y, l, m, cplx); double s = 0; int NT = 400, NP = 400;
        for (int i = 0; i < NT; i++) { double ct = -1 + 2 * (i + 0.5) / NT;
            for (int j = 0; j < NP; j++) { double re, im; ylm_eval(&y, ct, 2 * M_PI * (j + 0.5) / NP, &re, &im); s += re * re + im * im; } }
        s *= (2.0 / NT) * (2 * M_PI / NP);
        CHECK(fabs(s - 1) < 1e-3, "Y norm cplx=%d l=%d m=%d: %.5f", cplx, l, m, s);
    }
    /* 3. real orbital orientation: p_x > 0 on +x, p_y > 0 on +y, p_z > 0 on +z */
    { YlmCtx px, py, pz; double re, im;
      ylm_init(&px, 1, 1, 0); ylm_init(&py, 1, -1, 0); ylm_init(&pz, 1, 0, 0);
      ylm_eval(&px, 0.0, 0.0, &re, &im); CHECK(re > 0, "px sign");
      ylm_eval(&py, 0.0, M_PI / 2, &re, &im); CHECK(re > 0, "py sign");
      ylm_eval(&pz, 1.0, 0.0, &re, &im); CHECK(re > 0, "pz sign"); }
    /* 4. sampler: mean radius, lobe balance, determinism of count */
    OrbitalSpec specs[] = {{1,0,0,1,0},{2,1,0,1,0},{3,2,-2,1,0},{4,3,1,2,0},{3,1,1,1,1},{5,2,0,1,0}};
    for (int k = 0; k < 6; k++) for (int mode = 0; mode < 2; mode++) {
        SamplingRequest rq = { specs[k], 200000, mode, 42 + k }; PointVertex *v; SamplingResult res;
        int n = sampling_generate(&rq, &v, &res, NULL, 0, NULL);
        CHECK(n == 200000, "count %d", n);
        if (!mode) { double sr = 0, sz = 0; for (int i = 0; i < n; i++) { sr += sqrt(v[i].x*v[i].x + v[i].y*v[i].y + v[i].z*v[i].z); sz += v[i].z; }
            OrbitalProps p = hydrogen_compute_props(&specs[k]);
            CHECK(fabs(sr / n - p.r_expect) / p.r_expect < 0.01, "mean r spec %d: %.3f vs %.3f", k, sr / n, p.r_expect);
            if (!specs[k].complex_basis && specs[k].m == 0) CHECK(fabs(sz / n) < 0.05 * p.r_expect, "z balance spec %d", k);
            if (k == 1) CHECK(fabs((double)res.n_pos / n - 0.5) < 0.01, "2pz +/- balance %ld %ld", res.n_pos, res.n_neg); }
        free(v);
    }
    printf(fails ? "%d FAILURES\n" : "all physics tests passed\n", fails);
    return fails != 0;
}
