#include "math_utils.h"

Vec3 vec3_create(float x, float y, float z) { return (Vec3){x, y, z}; }
float vec3_dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
float vec3_length(Vec3 v) { return sqrtf(vec3_dot(v, v)); }
Vec3 vec3_sub(Vec3 a, Vec3 b) { return (Vec3){a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 vec3_add(Vec3 a, Vec3 b) { return (Vec3){a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 vec3_scale(Vec3 v, float s) { return (Vec3){v.x * s, v.y * s, v.z * s}; }

Vec3 vec3_normalize(Vec3 v) {
    float len = vec3_length(v);
    return (len > 0.0f) ? vec3_scale(v, 1.0f / len) : v;
}

Vec3 vec3_cross(Vec3 a, Vec3 b) {
    return (Vec3){a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

Mat4 mat4_identity(void) {
    Mat4 r = {{{0}}};
    r.m[0][0] = r.m[1][1] = r.m[2][2] = r.m[3][3] = 1.0f;
    return r;
}

Mat4 mat4_multiply(Mat4 a, Mat4 b) {
    Mat4 r = {{{0}}};
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            r.m[i][j] = a.m[i][0] * b.m[0][j] + a.m[i][1] * b.m[1][j] +
                        a.m[i][2] * b.m[2][j] + a.m[i][3] * b.m[3][j];
    return r;
}

Mat4 mat4_perspective(float fovy_rad, float aspect, float near_p, float far_p) {
    Mat4 r = {{{0}}};
    float f = 1.0f / tanf(fovy_rad * 0.5f);
    r.m[0][0] = f / aspect;
    r.m[1][1] = f;
    r.m[2][2] = (far_p + near_p) / (near_p - far_p);
    r.m[2][3] = (2.0f * far_p * near_p) / (near_p - far_p);
    r.m[3][2] = -1.0f;
    return r;
}

Mat4 mat4_lookat(Vec3 eye, Vec3 center, Vec3 up) {
    Vec3 f = vec3_normalize(vec3_sub(center, eye));
    Vec3 s = vec3_normalize(vec3_cross(f, up));
    Vec3 u = vec3_cross(s, f);
    Mat4 r = mat4_identity();
    r.m[0][0] = s.x;  r.m[0][1] = s.y;  r.m[0][2] = s.z;  r.m[0][3] = -vec3_dot(s, eye);
    r.m[1][0] = u.x;  r.m[1][1] = u.y;  r.m[1][2] = u.z;  r.m[1][3] = -vec3_dot(u, eye);
    r.m[2][0] = -f.x; r.m[2][1] = -f.y; r.m[2][2] = -f.z; r.m[2][3] = vec3_dot(f, eye);
    return r;
}

Vec4 mat4_mul_vec4(Mat4 m, Vec4 v) {
    return (Vec4){
        m.m[0][0] * v.x + m.m[0][1] * v.y + m.m[0][2] * v.z + m.m[0][3] * v.w,
        m.m[1][0] * v.x + m.m[1][1] * v.y + m.m[1][2] * v.z + m.m[1][3] * v.w,
        m.m[2][0] * v.x + m.m[2][1] * v.y + m.m[2][2] * v.z + m.m[2][3] * v.w,
        m.m[3][0] * v.x + m.m[3][1] * v.y + m.m[3][2] * v.z + m.m[3][3] * v.w};
}

float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

float nice_step(float target) {
    if (!(target > 0.0f)) return 1.0f;
    float e = floorf(log10f(target));
    float base = powf(10.0f, e);
    const float mult[4] = {1.0f, 2.0f, 5.0f, 10.0f};
    float best = base, best_d = 1e30f;
    for (int i = 0; i < 4; i++) {
        float d = fabsf(logf(target / (mult[i] * base)));
        if (d < best_d) { best_d = d; best = mult[i] * base; }
    }
    return best;
}
