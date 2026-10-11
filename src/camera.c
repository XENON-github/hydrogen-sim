#include <stddef.h>
#include "camera.h"

#define PITCH_LIMIT (89.9f * (float)M_PI / 180.0f)

Camera camera_create(void) {
    Camera c;
    c.target = vec3_create(0.0f, 0.0f, 0.0f);
    c.distance = 40.0f;
    c.yaw = -0.75f;
    c.pitch = 0.50f;
    c.min_distance = 0.5f;
    c.max_distance = 2000.0f;
    c.fovy = 40.0f * (float)M_PI / 180.0f;
    c.near_plane = 0.1f;
    c.far_plane = 500.0f;
    return c;
}

Vec3 camera_get_position(const Camera *c) {
    float cp = cosf(c->pitch);
    return vec3_add(c->target, vec3_create(c->distance * cp * cosf(c->yaw),
                                           c->distance * cp * sinf(c->yaw),
                                           c->distance * sinf(c->pitch)));
}

void camera_get_basis(const Camera *c, Vec3 *right, Vec3 *up, Vec3 *forward) {
    Vec3 eye = camera_get_position(c);
    Vec3 f = vec3_normalize(vec3_sub(c->target, eye));
    Vec3 r = vec3_normalize(vec3_cross(f, vec3_create(0.0f, 0.0f, 1.0f)));
    Vec3 u = vec3_cross(r, f);
    if (right) *right = r;
    if (up) *up = u;
    if (forward) *forward = f;
}

Mat4 camera_get_view_matrix(const Camera *c) {
    return mat4_lookat(camera_get_position(c), c->target, vec3_create(0.0f, 0.0f, 1.0f));
}

Mat4 camera_get_projection_matrix(const Camera *c, float aspect) {
    return mat4_perspective(c->fovy, aspect, c->near_plane, c->far_plane);
}

Mat4 camera_get_viewproj(const Camera *c, float aspect) {
    return mat4_multiply(camera_get_projection_matrix(c, aspect), camera_get_view_matrix(c));
}

void camera_orbit(Camera *c, float dyaw, float dpitch) {
    c->yaw += dyaw;
    if (c->yaw > (float)M_PI) c->yaw -= 2.0f * (float)M_PI;
    if (c->yaw < -(float)M_PI) c->yaw += 2.0f * (float)M_PI;
    c->pitch = clampf(c->pitch + dpitch, -PITCH_LIMIT, PITCH_LIMIT);
}

void camera_pan_pixels(Camera *c, float dx, float dy, float viewport_h) {
    if (viewport_h < 1.0f) return;
    Vec3 r, u;
    camera_get_basis(c, &r, &u, NULL);
    float world_per_px = 2.0f * c->distance * tanf(c->fovy * 0.5f) / viewport_h;
    c->target = vec3_add(c->target, vec3_add(vec3_scale(r, -dx * world_per_px),
                                             vec3_scale(u, dy * world_per_px)));
}

void camera_zoom_factor(Camera *c, float factor) {
    c->distance = clampf(c->distance * factor, c->min_distance, c->max_distance);
}

void camera_fit(Camera *c, float radius, float aspect) {
    float half_v = c->fovy * 0.5f;
    float half = atanf(tanf(half_v) * fminf(1.0f, aspect));
    c->target = vec3_create(0.0f, 0.0f, 0.0f);
    c->distance = clampf(1.1f * radius / sinf(half), c->min_distance, c->max_distance);
}

void camera_set_preset(Camera *c, CameraPreset p) {
    switch (p) {
    case CAMERA_VIEW_FRONT: c->yaw = -(float)M_PI * 0.5f; c->pitch = 0.0f; break;
    case CAMERA_VIEW_SIDE:  c->yaw = 0.0f;                c->pitch = 0.0f; break;
    case CAMERA_VIEW_TOP:   c->yaw = -(float)M_PI * 0.5f; c->pitch = PITCH_LIMIT; break;
    default:                c->yaw = -0.75f;              c->pitch = 0.50f; break;
    }
}

void camera_update_clip(Camera *c, float extent) {
    float tdist = vec3_length(c->target);
    c->near_plane = fmaxf(0.02f, c->distance * 0.01f);
    c->far_plane = c->distance + tdist + 2.5f * extent + 10.0f;
    c->max_distance = fmaxf(60.0f, extent * 12.0f);
    c->min_distance = fmaxf(0.3f, extent * 0.02f);
}
