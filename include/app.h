#ifndef APP_H
#define APP_H

#include <glad/glad.h>
#include <gtk/gtk.h>
#include <stdatomic.h>
#include "camera.h"
#include "hydrogen.h"
#include "renderer.h"
#include "hud.h"

typedef struct AppState AppState;

/* One 3D viewport (GtkGLArea + Cairo HUD overlay). Two exist: |psi|^2 density and psi wave. */
typedef struct {
    AppState *app;
    int kind;                       /* VIEW_DENSITY / VIEW_WAVE */
    GtkWidget *window;              /* separate top-level window (wave view only) */
    GtkWidget *overlay, *gl_area, *hud_area;
    GtkWidget *load_box, *load_title, *load_bar;
    Renderer renderer;
    Camera cam;

    int point_count;
    long n_pos, n_neg;
    double gen_seconds;
    atomic_int gen_id, progress;
    int generating, gen_total;

    int drag_mode;                  /* -1 none, 0 orbit, 1 pan */
    float drag_yaw, drag_pitch, zoom_start;
    Vec3 drag_target;

    gint64 last_tick_us, fps_t0_us;
    int fps_frames;
    double fps;
    int animating;
    int fitted;
    gboolean dirty;
    gboolean snapshot_pending;
} View;

typedef struct {
    GtkWidget *dd_preset, *spin_n, *spin_l, *spin_m, *spin_z, *dd_basis;
    GtkWidget *lbl_props, *plot, *spin_samples, *lbl_status;
    GtkWidget *chk_grid, *chk_axes, *chk_nucleus, *chk_sphere, *chk_bbox, *chk_info, *chk_gizmo, *chk_legend;
    GtkWidget *chk_rotate, *chk_phase, *chk_link, *chk_bloom, *chk_wave_win;
    int guard;
} UiRefs;

struct AppState {
    GtkApplication *app;
    GtkWidget *main_window;

    OrbitalSpec spec;
    OrbitalProps props;
    char orbital_name[96];
    int num_samples;

    /* appearance */
    int scheme, style;
    float particle_size, brightness, bloom_strength, nucleus_glow;
    gboolean bloom;

    /* scene overlays */
    gboolean show_grid, show_axes, show_nucleus, show_sphere90, show_bbox, show_info, show_gizmo, show_legend;

    /* cross-section */
    int slice_mode, slice_axis;
    float slice_pos, slice_thick;   /* fractions of the orbital extent */

    /* animation */
    gboolean auto_rotate, phase_anim, link_cameras;
    float rotate_speed, phase_speed;
    double phase;
    gint64 phase_last_us;

    guint regen_timer;
    UiRefs ui;
    View views[2];
};

int app_run(int argc, char **argv);

/* state changes */
void app_apply_spec(AppState *s);                 /* orbital changed: recompute, refit cameras, resample */
void app_schedule_regenerate(AppState *s, guint delay_ms);
void app_regenerate_now(AppState *s);
void app_mark_dirty(AppState *s);                 /* redraw both views */
void app_reset_views(AppState *s, int preset);
void app_snapshot(AppState *s);
void app_set_status(AppState *s, const char *text);
float app_view_extent(const AppState *s, int kind);
float app_grid_step(const AppState *s, int kind);

/* views (view.c) */
void view_init(View *v, AppState *s, int kind);
GtkWidget *view_build_widget(View *v);            /* overlay holding GL area + HUD */
void view_create_window(View *v, GtkApplication *app, const char *title, int w, int h);
void view_request_generation(View *v);
void view_mark_dirty(View *v);
void view_camera_changed(View *v);

/* sidebar (ui.c) */
GtkWidget *ui_build_sidebar(AppState *s);
void ui_sync_info(AppState *s);                   /* refresh properties text, plot, spinners */

#endif /* APP_H */
