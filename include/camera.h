#ifndef CAMERA_H
#define CAMERA_H

#include "math_utils.h"

/* Orbit camera. World convention: Z is "up" (the quantisation axis of the atom). */
typedef struct {
    Vec3 target;
    float distance;
    float yaw;    /* radians, rotation about +Z */
    float pitch;  /* radians, elevation above the XY plane */
    float min_distance, max_distance;
    float fovy;   /* vertical field of view, radians */
    float near_plane, far_plane;
} Camera;

typedef enum { CAMERA_VIEW_ISO = 0, CAMERA_VIEW_FRONT, CAMERA_VIEW_SIDE, CAMERA_VIEW_TOP } CameraPreset;

Camera camera_create(void);
Vec3 camera_get_position(const Camera *cam);
/* Orthonormal camera basis in world space. */
void camera_get_basis(const Camera *cam, Vec3 *right, Vec3 *up, Vec3 *forward);
Mat4 camera_get_view_matrix(const Camera *cam);
Mat4 camera_get_projection_matrix(const Camera *cam, float aspect);
Mat4 camera_get_viewproj(const Camera *cam, float aspect);

void camera_orbit(Camera *cam, float delta_yaw, float delta_pitch);
void camera_pan_pixels(Camera *cam, float dx_px, float dy_px, float viewport_h_px);
void camera_zoom_factor(Camera *cam, float factor);
/* Frame a sphere of the given radius centred at the origin. */
void camera_fit(Camera *cam, float radius, float aspect);
void camera_set_preset(Camera *cam, CameraPreset preset);
/* Keep clip planes tight around the scene (call every frame; extent = scene radius). */
void camera_update_clip(Camera *cam, float extent);

#endif /* CAMERA_H */
