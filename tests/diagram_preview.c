/* Draws the energy-level diagram to a PNG (cairo only). */
#include <stdio.h>
#include <cairo.h>
#include "hud.h"
int main(int argc, char **argv) {
    int W = 400, H = 280;
    cairo_surface_t *sf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, W * 2, H);
    cairo_t *cr = cairo_create(sf);
    cairo_set_source_rgb(cr, 0.11, 0.11, 0.13); cairo_paint(cr);
    EnergyDiagram d = { .Z = 1.0, .n_max = 6, .progress = 0.5, .hover = -1, .dark = 1 };
    d.mode = 2; d.init_n = 3; d.init_l = 1; d.final_n = 2; d.final_l = 0; d.has_final = 1; d.hover = -1;
    hud_energy_diagram(cr, W, H, &d, NULL);
    cairo_translate(cr, W, 0);
    EnergyDiagram e = d; e.mode = 1; e.comp_count = 3; e.comp_n[0] = 1; e.comp_l[0] = 0; e.comp_n[1] = 2; e.comp_l[1] = 1; e.comp_n[2] = 3; e.comp_l[2] = 2;
    e.comp_w[0] = .33f; e.comp_w[1] = .33f; e.comp_w[2] = .34f; e.hover = 9;
    hud_energy_diagram(cr, W, H, &e, NULL);
    cairo_surface_write_to_png(sf, argc > 1 ? argv[1] : "diagram.png");
    return 0;
}
