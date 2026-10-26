/* Physics checks for transitions + superpositions (GTK-free). */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "transitions.h"
#include "superposition.h"
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)
#define NEAR(a, b, rel) (fabs((a) - (b)) <= (rel) * fabs(b))

int main(void) {
    /* radial dipole integral <1s|r|2p> = 1.2903 a0  (= 2^7 sqrt(6)/3^5... = 1.29027) */
    CHECK(NEAR(radial_dipole_integral(1, 0, 2, 1, 1.0), 1.29027, 1e-4), "I(1s,2p) = %.5f", radial_dipole_integral(1, 0, 2, 1, 1.0));
    /* Einstein A coefficients (NIST): Ly-alpha 6.2649e8, H-alpha(3d->2p) 6.4651e7, 3p->1s 1.6725e8, 3p->2s 2.2449e7 */
    double A = transition_rate(2, 1, 1, 0, 1.0);   CHECK(NEAR(A, 6.2649e8, 2e-3), "A(2p->1s) = %.4e", A);
    A = transition_rate(3, 2, 2, 1, 1.0);           CHECK(NEAR(A, 6.4651e7, 2e-3), "A(3d->2p) = %.4e", A);
    A = transition_rate(3, 1, 1, 0, 1.0);           CHECK(NEAR(A, 1.6725e8, 2e-3), "A(3p->1s) = %.4e", A);
    A = transition_rate(3, 1, 2, 0, 1.0);           CHECK(NEAR(A, 2.2449e7, 2e-3), "A(3p->2s) = %.4e", A);
    A = transition_rate(3, 0, 2, 1, 1.0);           CHECK(NEAR(A, 6.3135e6, 3e-3), "A(3s->2p) = %.4e", A);
    CHECK(transition_rate(2, 0, 1, 0, 1.0) == 0.0, "2s->1s must be E1-forbidden");
    CHECK(transition_rate(3, 2, 1, 0, 1.0) == 0.0, "3d->1s must be E1-forbidden");
    /* Z^4 scaling */
    A = transition_rate(2, 1, 1, 0, 2.0);           CHECK(NEAR(A, 16.0 * 6.2649e8, 2e-3), "Z^4 scaling: %.4e", A);
    /* lifetimes */
    DecayChannel ch[MAX_DECAY_CHANNELS]; double tot;
    int n = transition_decay_channels(3, 1, 1.0, ch, MAX_DECAY_CHANNELS, &tot);
    CHECK(n == 2 && NEAR(1.0 / tot, 5.27e-9, 5e-3), "tau(3p) = %.3e ns (n=%d)", 1e9 / tot, n);
    double bsum = 0; for (int i = 0; i < n; i++) bsum += ch[i].branching; CHECK(NEAR(bsum, 1.0, 1e-12), "branching sum");
    n = transition_decay_channels(2, 0, 1.0, ch, MAX_DECAY_CHANNELS, &tot); CHECK(n == 0, "2s is metastable");
    /* wavelengths: Ly-a 121.57, H-alpha 656.1 (vacuum, infinite nuclear mass: 656.11) */
    CHECK(NEAR(transition_wavelength_nm(2, 1, 1.0), 121.57, 1e-3), "Ly-a %.3f", transition_wavelength_nm(2, 1, 1.0));
    CHECK(NEAR(transition_wavelength_nm(3, 2, 1.0), 656.11, 1e-3), "H-a %.3f", transition_wavelength_nm(3, 2, 1.0));
    char b[40]; transition_line_label(3, 2, b, sizeof b); CHECK(strcmp(b, "Balmer-α") == 0, "label '%s'", b);

    /* dipole matrix <1s|z|2p0> = 128 sqrt(2)/243 = 0.74492 a0 (both bases: m = 0) */
    for (int cb = 0; cb < 2; cb++) {
        SuperSpec s = super_preset(0, 1.0, cb);
        DipoleMatrix D; super_dipole_matrix(&s, &D);
        CHECK(NEAR(D.d[2][0][1].r, 0.74492, 2e-4) && fabs(D.d[2][0][1].i) < 1e-9, "<1s|z|2pz> = %.5f%+.1ei (basis %d)", D.d[2][0][1].r, D.d[2][0][1].i, cb);
        CHECK(fabs(D.d[0][0][1].r) < 1e-9 && fabs(D.d[1][0][1].r) < 1e-9, "x,y elements of 1s-2pz must vanish");
        CHECK(fabs(D.d[2][0][0].r) < 1e-12, "diagonal (parity) must vanish");
        /* <z>(t) = 2 c1 c2 D cos(w t): amplitude 0.74492 at equal weights, oscillating at w = 3/8 Eh */
        double w = super_max_omega(&s); CHECK(NEAR(w, 0.375, 1e-12), "omega = %.6f", w);
        double mx = 0, z[3];
        for (int i = 0; i < 200; i++) { Complex c[SUPER_MAX]; super_coeffs_at(&s, i * 2 * M_PI / w / 200, c); super_dipole_expect(&D, c, z); if (fabs(z[2]) > mx) mx = fabs(z[2]); }
        CHECK(NEAR(mx, 0.74492, 2e-3), "max <z>(t) = %.5f", mx);
        Complex c0[SUPER_MAX]; super_coeffs_at(&s, 0, c0); double nrm = 0; for (int k = 0; k < 2; k++) nrm += c0[k].r * c0[k].r + c0[k].i * c0[k].i;
        CHECK(NEAR(nrm, 1.0, 1e-12), "norm");
    }
    /* complex basis: 1s + 2p(m=+1) gives a rotating dipole: <x>^2+<y>^2 constant */
    { SuperSpec s = super_preset(4, 1.0, 1); DipoleMatrix D; super_dipole_matrix(&s, &D);
      double w = super_max_omega(&s), r0 = -1, z[3];
      for (int i = 0; i < 50; i++) { Complex c[SUPER_MAX]; super_coeffs_at(&s, i * 0.37, c); super_dipole_expect(&D, c, z);
          double r = hypot(z[0], z[1]); if (r0 < 0) r0 = r; CHECK(NEAR(r, r0, 1e-9) && fabs(z[2]) < 1e-9, "rotating dipole radius %.6f vs %.6f", r, r0); }
      CHECK(w > 0, "w"); }
    /* degenerate 2s+2p: no time dependence in the dipole, static hybrid with <z> = -3 a0 * c1c2 * 2 ... magnitude 3 at equal weights */
    { SuperSpec s = super_preset(3, 1.0, 0); DipoleMatrix D; super_dipole_matrix(&s, &D); Complex c[SUPER_MAX]; double z[3];
      CHECK(super_max_omega(&s) == 0.0, "degenerate");
      super_coeffs_at(&s, 5.0, c); super_dipole_expect(&D, c, z); CHECK(NEAR(fabs(z[2]), 3.0, 1e-3), "<z> of 2s+2pz = %.5f (want |3|)", z[2]); }
    /* level layout hit testing */
    { LevelLayout L; level_layout_build(&L, 6, 5, 400, 240, 1.0);
      CHECK(L.count == 1 + 2 + 3 + 4 + 5 + 5, "level count %d", L.count);
      for (int i = 0; i < L.count; i++) { int h = level_layout_hit(&L, 0.5 * (L.lv[i].x0 + L.lv[i].x1), L.lv[i].y); CHECK(h == i, "hit %d != %d", h, i); }
      CHECK(level_layout_hit(&L, 2, 2) == -1, "miss"); }
    printf(fails ? "%d FAILURES\n" : "all transition/superposition tests passed\n", fails);
    return fails != 0;
}
