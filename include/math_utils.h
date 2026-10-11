#ifndef MATH_UTILS_H
#define MATH_UTILS_H

#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef struct { float x, y, z; } Vec3;
typedef struct { float x, y, z, w; } Vec4;
typedef struct { float m[4][4]; } Mat4;   /* row-major, uploaded with transpose = GL_TRUE */

Vec3 vec3_create(float x, float y, float z);
Vec3 vec3_normalize(Vec3 v);
Vec3 vec3_cross(Vec3 a, Vec3 b);
float vec3_dot(Vec3 a, Vec3 b);
float vec3_length(Vec3 v);
Vec3 vec3_sub(Vec3 a, Vec3 b);
Vec3 vec3_add(Vec3 a, Vec3 b);
Vec3 vec3_scale(Vec3 v, float s);

Mat4 mat4_identity(void);
Mat4 mat4_multiply(Mat4 a, Mat4 b);
Mat4 mat4_perspective(float fovy_rad, float aspect, float near_p, float far_p);
Mat4 mat4_lookat(Vec3 eye, Vec3 center, Vec3 up);
Vec4 mat4_mul_vec4(Mat4 m, Vec4 v);

float clampf(float v, float lo, float hi);
/* Round `target` to the nearest "nice" 1/2/5 x 10^k step (for grids and tick marks). */
float nice_step(float target);

#endif /* MATH_UTILS_H */
