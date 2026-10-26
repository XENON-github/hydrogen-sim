/* Superposition sampler + volume tests (GTK-free). */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "sampling.h"
#include "volume.h"
#include "transitions.h"
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static void cmul_acc(double *pr, double *pi, Complex c, double br, double bi) { *pr += c.r * br - c.i * bi; *pi += c.r * bi + c.i * br; }

int main(void) {
    /* 1. half-float conversion */
    CHECK(float_to_half(1.0f) == 0x3C00, "half 1.0 = %04x", float_to_half(1.0f));
    CHECK(float_to_half(-2.0f) == 0xC000, "half -2");
    CHECK(float_to_half(0.0f) == 0, "half 0");
    CHECK(float_to_half(65504.0f) == 0x7BFF, "half max");
    CHECK(float_to_half(1e-8f) == 0, "half underflow");
    for (float f = -1.0f; f <= 1.0f; f += 0.0137f) {   /* round-trip accuracy */
        uint16_t h = float_to_half(f);
        int e = (h >> 10) & 31; float m = (h & 1023) / 1024.0f, sg = (h & 0x8000) ? -1.f : 1.f;
        float back = e == 0 ? sg * ldexpf(m, -14) : sg * ldexpf(1.0f + m, e - 15);
        CHECK(fabsf(back - f) <= fabsf(f) * 1.0e-3f + 1e-6f, "half roundtrip %g -> %g", f, back);
    }
    /* 2. L1 norm of 1s: int |psi| dV = 4 pi int r^2 e^{-r}/sqrt(pi) dr = 8 sqrt(pi) */
    { OrbitalSpec o = {1, 0, 0, 1.0, 0}; double n1 = orbital_l1_norm(&o);
      CHECK(fabs(n1 - 8.0 * sqrt(M_PI)) < 2e-3 * n1, "L1 norm(1s) = %.5f want %.5f", n1, 8.0 * sqrt(M_PI)); }

    /* 3. weighted MC reproduces <z>(t) for 1s + 2pz, both bases; density estimator integrates to 1 */
    for (int cb = 0; cb < 2; cb++) {
        SuperSpec sp = super_preset(0, 1.0, cb); sp.c[1].amp = 0.8;
        SuperSamplingRequest rq = { sp, 800000, SAMPLE_DENSITY, 99 };
        SuperVertex *v; SuperSamplingResult res;
        CHECK(sampling_generate_super(&rq, &v, &res, NULL, 0, NULL) == 800000, "super gen");
        DipoleMatrix D; super_dipole_matrix(&sp, &D);
        for (int ti = 0; ti < 4; ti++) {
            double t = ti * 5.3; Complex c[SUPER_MAX]; super_coeffs_at(&sp, t, c);
            double zex[3]; super_dipole_expect(&D, c, zex);
            double sw = 0, swz = 0;
            for (int i = 0; i < rq.count; i++) {
                double pr = 0, pi = 0;
                for (int k = 0; k < 2; k++) cmul_acc(&pr, &pi, c[k], v[i].b[2 * k] / 32767.0, v[i].b[2 * k + 1] / 32767.0);
                double w = 2.0 * (pr * pr + pi * pi);
                sw += w; swz += w * v[i].z;
            }
            CHECK(fabs(sw / rq.count - 1.0) < 0.01, "norm estimate %.4f (basis %d, t=%.1f)", sw / rq.count, cb, t);
            CHECK(fabs(swz / rq.count - zex[2]) < 0.02, "<z> MC %.4f vs exact %.4f (basis %d, t=%.1f)", swz / rq.count, zex[2], cb, t);
        }
        for (int i = 0; i < 1000; i++) CHECK(v[i].scalar >= 0 && isfinite(v[i].scalar), "scalar %g", v[i].scalar);
        free(v);
    }
    /* 4. amplitude proposal: MC estimate of int |psi| dV vs the volume-grid integral */
    { SuperSpec sp = super_preset(0, 1.0, 0); sp.c[1].amp = 1.3; sp.c[1].phase = 0.7;
      SuperSamplingRequest rq = { sp, 800000, SAMPLE_AMPLITUDE, 5 }; SuperVertex *v; SuperSamplingResult res;
      CHECK(sampling_generate_super(&rq, &v, &res, NULL, 0, NULL) > 0, "amp gen");
      Complex c0[SUPER_MAX]; super_coeffs0(&sp, c0); double t = 2.2; Complex c[SUPER_MAX]; super_coeffs_at(&sp, t, c);
      double acc = 0;
      for (int i = 0; i < rq.count; i++) { double pr = 0, pi = 0;
          for (int k = 0; k < 2; k++) { Complex g = { c[k].r * res.comp_norm[k] / res.gmax, c[k].i * res.comp_norm[k] / res.gmax }; cmul_acc(&pr, &pi, g, v[i].b[2 * k] / 32767.0, v[i].b[2 * k + 1] / 32767.0); }
          acc += sqrt(pr * pr + pi * pi); }
      double mc = 2.0 * res.gmax * acc / rq.count;
      VolumeRequest vr = { sp, 128, 30.0 }; Volume vol; CHECK(volume_generate(&vr, &vol, NULL, 0, NULL), "volume");
      double h = 2 * vol.half / vol.res, I = 0; size_t nv = (size_t)vol.res * vol.res * vol.res;
      /* decode half floats */
      #define H2F(h_) ({ uint16_t q = (h_); int e = (q >> 10) & 31; float m = (q & 1023) / 1024.0f, sg = (q & 0x8000) ? -1.f : 1.f; e == 0 ? sg * ldexpf(m, -14) : sg * ldexpf(1.0f + m, e - 15); })
      for (size_t i = 0; i < nv; i++) { double pr = 0, pi = 0;
          for (int k = 0; k < 2; k++) { double re = H2F(vol.tex[0][i * 4 + 2 * k]) * vol.vscale, im = H2F(vol.tex[0][i * 4 + 2 * k + 1]) * vol.vscale; cmul_acc(&pr, &pi, c[k], re, im); }
          I += sqrt(pr * pr + pi * pi); }
      I *= h * h * h;
      CHECK(fabs(mc - I) < 0.02 * I, "int|psi| MC %.4f vs grid %.4f", mc, I);
      /* density-level table for the 1s: radius holding 90 % is 2.6741 a0 */
      SuperSpec s1 = {0}; s1.count = 1; s1.Z = 1; s1.c[0] = (SuperComponent){1, 0, 0, 1, 0};
      VolumeRequest v1 = { s1, 96, 14.0 }; Volume vol1; CHECK(volume_generate(&v1, &vol1, NULL, 0, NULL), "volume 1s");
      double rho90 = volume_level_for_probability(&vol1, 0.90), want = exp(-2.0 * 2.6741) / M_PI;
      CHECK(fabs(rho90 - want) < 0.05 * want, "1s 90%% level %.5g vs %.5g", rho90, want);
      CHECK(fabs(vol1.total_mass - 1.0) < 0.01, "volume mass %.4f", vol1.total_mass);
      volume_free(&vol); volume_free(&vol1); free(v); }
    /* 5. cancellation */
    { atomic_int latest = 2; SuperSpec sp = super_preset(0, 1.0, 0); SuperSamplingRequest rq = { sp, 400000, SAMPLE_DENSITY, 1 }; SuperVertex *v;
      CHECK(sampling_generate_super(&rq, &v, NULL, &latest, 1, NULL) == 0 && v == NULL, "cancel"); }
    printf(fails ? "%d FAILURES\n" : "all superposition sampler/volume tests passed\n", fails);
    return fails != 0;
}
