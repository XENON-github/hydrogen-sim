#ifndef APP_H
#define APP_H

#include <glad/glad.h>
#include <gtk/gtk.h>
#include <stdatomic.h>
#include "camera.h"
#include "hydrogen.h"
#include "renderer.h"
#include "hud.h"
#include "superposition.h"
#include "transitions.h"
#include "volume.h"
#include "recorder.h"

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

    /* superposition cloud + isosurface volume */
    int super_count;
    SuperSamplingResult super_res;
    int cloud_super;                /* the uploaded cloud is the weighted (superposition) kind */
    int cloud_req_version, cloud_version;
    atomic_int vol_id, vol_progress;
    int vol_generating, vol_ready;
    int vol_req_version;
    Volume vol;                     /* metadata only (textures live on the GPU) */
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
    guint tick_id;
} View;

enum { MODE_STATIONARY = 0, MODE_SUPER = 1, MODE_TRANSITION = 2 };
enum { REC_WHAT_ORBIT = 0, REC_WHAT_TIME = 1, REC_WHAT_BOTH = 2 };

typedef struct {
    gboolean active;
    int view, w, h, fps, total, frame, what, include_hud;
    RecFormat fmt;
    Recorder *rec;
    GLuint fbo, tex;
    int fbo_w, fbo_h;
    char path[300];
    float yaw0;
    double t_start, t_span;         /* simulation time covered by the clip (a.u.) */
    int sweep_clip;                 /* transition mode: the clip is one full sweep */
    int cycles;                     /* beat cycles (or phase cycles) covered by the clip */
    double phase_span;              /* stationary: global phase covered (rad) */
    guchar *buf;
    gboolean cancel;
} RecState;

/* Per-frame overrides used while recording so output is deterministic. */
typedef struct { double t, phase, tau; } FrameOverride;

typedef struct {
    GtkWidget *dd_preset, *spin_n, *spin_l, *spin_m, *spin_z, *dd_basis;
    GtkWidget *lbl_props, *plot, *spin_samples, *lbl_status;
    GtkWidget *chk_grid, *chk_axes, *chk_nucleus, *chk_sphere, *chk_bbox, *chk_info, *chk_gizmo, *chk_legend;
    GtkWidget *chk_rotate, *chk_phase, *chk_link, *chk_bloom, *chk_wave_win;
    /* superposition */
    GtkWidget *dd_mode, *dd_super_preset, *spin_ncomp, *chk_play, *sc_time_speed, *lbl_time, *chk_arrow;
    GtkWidget *sp_n[SUPER_MAX], *sp_l[SUPER_MAX], *sp_m[SUPER_MAX], *sc_amp[SUPER_MAX], *sp_phase[SUPER_MAX];
    GtkWidget *comp_row[SUPER_MAX];
    /* energy levels & transitions */
    GtkWidget *energy_area, *lbl_decay, *lbl_trans, *spin_fn, *spin_fl, *spin_fm, *sc_tr_dur, *chk_tr_loop;
    LevelLayout layout;
    int hover;
    /* render mode / isosurface */
    GtkWidget *dd_render, *sc_iso_prob, *sc_iso_opacity, *dd_iso_res;
    /* export */
    GtkWidget *dd_rec_view, *dd_rec_fmt, *dd_rec_what, *dd_rec_size, *spin_rec_secs, *spin_rec_fps, *spin_rec_cycles;
    GtkWidget *chk_rec_hud, *btn_rec, *prog_rec, *lbl_rec;
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

    /* superposition / transitions */
    int mode;                       /* MODE_* */
    SuperSpec super;                /* user-defined component list (Z and basis follow `spec`) */
    gboolean time_play;
    float time_speed;               /* multiplier on the auto-scaled rate */
    double sim_t;                   /* simulated time, atomic units */
    gint64 sim_last_us;
    int advancing;
    int tr_final_n, tr_final_l, tr_final_m;
    float tr_duration;              /* seconds for the 0 -> 1 sweep */
    gboolean tr_loop;
    double tr_tau;                  /* 0..1.5: sweep (0..1) then hold */
    gboolean show_dipole_arrow;
    double r_extent_eff;            /* max component extent (a0) */
    int data_version;               /* bumped when the cloud/volume must be rebuilt */
    DipoleMatrix dip;
    int dip_valid;
    float trace[HUD_TRACE_N * 3];
    int trace_head;
    double trace_next_t;

    /* render mode + isosurface */
    int render_mode;                /* RENDER_PARTICLES / ISO / BOTH */
    float iso_prob, iso_opacity;
    int iso_res_idx;

    RecState rec;

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
/* superposition / transitions (app.c) */
void app_effective_spec(const AppState *s, SuperSpec *out, int apply_sweep);
void app_effective_spec_tau(const AppState *s, SuperSpec *out, double tau);   /* with the sweep amplitudes for this tau */   /* components actually shown now */
double app_time_rate(const AppState *s);                                        /* a.u. of time per wall-clock second at 1x */
void app_data_changed(AppState *s);               /* orbital/components/mode changed: recompute + rebuild data */
void app_set_mode(AppState *s, int mode);
void app_render_mode_changed(AppState *s);
void app_update_dipole(AppState *s);
int app_advance(AppState *s, gint64 now_us);      /* advances phase / time / sweep once per frame; 1 if animating */
void app_fill_super_hud(AppState *s, SuperHud *u, const FrameOverride *ov);
void app_reset_time(AppState *s);
void app_refresh_trace(AppState *s);              /* amplitudes/phases edited: rebuild the dipole history */
void app_start_recording(AppState *s);
void app_cancel_recording(AppState *s);
void app_sync_super_widgets(AppState *s);          /* ui.c */
float app_grid_step(const AppState *s, int kind);

/* views (view.c) */
void view_init(View *v, AppState *s, int kind);
GtkWidget *view_build_widget(View *v);            /* overlay holding GL area + HUD */
void view_create_window(View *v, GtkApplication *app, const char *title, int w, int h);
void view_request_generation(View *v);
void view_mark_dirty(View *v);
void view_camera_changed(View *v);
void view_ensure_data(View *v);                   /* build whatever cloud / volume the current mode needs */

/* sidebar (ui.c) */
GtkWidget *ui_build_sidebar(AppState *s);
void ui_sync_info(AppState *s);                   /* refresh properties text, plot, spinners */

#endif /* APP_H */
