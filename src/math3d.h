#ifndef MATH3D_H
#define MATH3D_H

#include <math.h>

#ifndef MATH3D_PI
#define MATH3D_PI 3.14159265358979323846f
#endif

typedef struct Vec3
{
	float x, y, z;
} Vec3;

typedef struct Mat4
{
	float m11, m12, m13, m14;
	float m21, m22, m23, m24;
	float m31, m32, m33, m34;
	float m41, m42, m43, m44;
} Mat4;

/* Vector operations. */
float vec3_dot(Vec3 a, Vec3 b);
Vec3 vec3_sub(Vec3 a, Vec3 b);
Vec3 vec3_add(Vec3 a, Vec3 b);
Vec3 vec3_cross(Vec3 a, Vec3 b);
Vec3 vec3_normalize(Vec3 v);
int vec3_equal(Vec3 a, Vec3 b);

/* Scalar utility. */
float clampf(float v, float mn, float mx);

/* 4x4 row-major matrix operations. */
void mat4_identity(Mat4 *out);
void mat4_transpose(Mat4 *out, const Mat4 *in);
void mat4_mul(Mat4 *out, const Mat4 *a, const Mat4 *b);
void mat4_perspective(Mat4 *out, float fovY, float aspect, float zn, float zf);
void mat4_lookat_lh(Mat4 *out, Vec3 eye, Vec3 target, Vec3 up);

/* Convert between row-major Mat4 and column-major float[16].
 * mat4_to_colmajor:  Mat4 (row-major)    -> float[16] (column-major)
 * mat4_from_colmajor: float[16] (column-major) -> Mat4 (row-major)
 * Both are equivalent to transpose.
 */
void mat4_to_colmajor(float out[16], const Mat4 *m);
void mat4_from_colmajor(Mat4 *m, const float in[16]);

#endif /* MATH3D_H */
