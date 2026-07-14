#include "math3d.h"

float vec3_dot(Vec3 a, Vec3 b)
{
	return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec3 vec3_sub(Vec3 a, Vec3 b)
{
	Vec3 r;
	r.x = a.x - b.x;
	r.y = a.y - b.y;
	r.z = a.z - b.z;
	return r;
}

Vec3 vec3_add(Vec3 a, Vec3 b)
{
	Vec3 r;
	r.x = a.x + b.x;
	r.y = a.y + b.y;
	r.z = a.z + b.z;
	return r;
}

Vec3 vec3_cross(Vec3 a, Vec3 b)
{
	Vec3 r;
	r.x = a.y * b.z - a.z * b.y;
	r.y = a.z * b.x - a.x * b.z;
	r.z = a.x * b.y - a.y * b.x;
	return r;
}

Vec3 vec3_normalize(Vec3 v)
{
	float len = sqrtf(vec3_dot(v, v));
	if (len > 0.0f)
	{
		v.x /= len;
		v.y /= len;
		v.z /= len;
	}
	return v;
}

int vec3_equal(Vec3 a, Vec3 b)
{
	return a.x == b.x && a.y == b.y && a.z == b.z;
}

float clampf(float v, float mn, float mx)
{
	return v < mn ? mn : (v > mx ? mx : v);
}

void mat4_identity(Mat4 *out)
{
	out->m11 = 1.0f; out->m12 = 0.0f; out->m13 = 0.0f; out->m14 = 0.0f;
	out->m21 = 0.0f; out->m22 = 1.0f; out->m23 = 0.0f; out->m24 = 0.0f;
	out->m31 = 0.0f; out->m32 = 0.0f; out->m33 = 1.0f; out->m34 = 0.0f;
	out->m41 = 0.0f; out->m42 = 0.0f; out->m43 = 0.0f; out->m44 = 1.0f;
}

void mat4_transpose(Mat4 *out, const Mat4 *in)
{
	Mat4 tmp;
	tmp.m11 = in->m11; tmp.m12 = in->m21; tmp.m13 = in->m31; tmp.m14 = in->m41;
	tmp.m21 = in->m12; tmp.m22 = in->m22; tmp.m23 = in->m32; tmp.m24 = in->m42;
	tmp.m31 = in->m13; tmp.m32 = in->m23; tmp.m33 = in->m33; tmp.m34 = in->m43;
	tmp.m41 = in->m14; tmp.m42 = in->m24; tmp.m43 = in->m34; tmp.m44 = in->m44;
	*out = tmp;
}

void mat4_mul(Mat4 *out, const Mat4 *a, const Mat4 *b)
{
	Mat4 tmp;

	tmp.m11 = a->m11 * b->m11 + a->m12 * b->m21 + a->m13 * b->m31 + a->m14 * b->m41;
	tmp.m12 = a->m11 * b->m12 + a->m12 * b->m22 + a->m13 * b->m32 + a->m14 * b->m42;
	tmp.m13 = a->m11 * b->m13 + a->m12 * b->m23 + a->m13 * b->m33 + a->m14 * b->m43;
	tmp.m14 = a->m11 * b->m14 + a->m12 * b->m24 + a->m13 * b->m34 + a->m14 * b->m44;

	tmp.m21 = a->m21 * b->m11 + a->m22 * b->m21 + a->m23 * b->m31 + a->m24 * b->m41;
	tmp.m22 = a->m21 * b->m12 + a->m22 * b->m22 + a->m23 * b->m32 + a->m24 * b->m42;
	tmp.m23 = a->m21 * b->m13 + a->m22 * b->m23 + a->m23 * b->m33 + a->m24 * b->m43;
	tmp.m24 = a->m21 * b->m14 + a->m22 * b->m24 + a->m23 * b->m34 + a->m24 * b->m44;

	tmp.m31 = a->m31 * b->m11 + a->m32 * b->m21 + a->m33 * b->m31 + a->m34 * b->m41;
	tmp.m32 = a->m31 * b->m12 + a->m32 * b->m22 + a->m33 * b->m32 + a->m34 * b->m42;
	tmp.m33 = a->m31 * b->m13 + a->m32 * b->m23 + a->m33 * b->m33 + a->m34 * b->m43;
	tmp.m34 = a->m31 * b->m14 + a->m32 * b->m24 + a->m33 * b->m34 + a->m34 * b->m44;

	tmp.m41 = a->m41 * b->m11 + a->m42 * b->m21 + a->m43 * b->m31 + a->m44 * b->m41;
	tmp.m42 = a->m41 * b->m12 + a->m42 * b->m22 + a->m43 * b->m32 + a->m44 * b->m42;
	tmp.m43 = a->m41 * b->m13 + a->m42 * b->m23 + a->m43 * b->m33 + a->m44 * b->m43;
	tmp.m44 = a->m41 * b->m14 + a->m42 * b->m24 + a->m43 * b->m34 + a->m44 * b->m44;

	*out = tmp;
}

void mat4_perspective(Mat4 *out, float fovY, float aspect, float zn, float zf)
{
	float h = 1.0f / tanf(fovY * 0.5f);
	float w = h / aspect;
	out->m11 = w;      out->m12 = 0.0f; out->m13 = 0.0f;                   out->m14 = 0.0f;
	out->m21 = 0.0f;   out->m22 = h;    out->m23 = 0.0f;                   out->m24 = 0.0f;
	out->m31 = 0.0f;   out->m32 = 0.0f; out->m33 = zf / (zf - zn);        out->m34 = 1.0f;
	out->m41 = 0.0f;   out->m42 = 0.0f; out->m43 = -(zn * zf) / (zf - zn); out->m44 = 0.0f;
}

void mat4_ortho_lh(Mat4 *out, float width, float height, float zn, float zf)
{
	out->m11 = 2.0f / width; out->m12 = 0.0f;          out->m13 = 0.0f;              out->m14 = 0.0f;
	out->m21 = 0.0f;         out->m22 = 2.0f / height; out->m23 = 0.0f;              out->m24 = 0.0f;
	out->m31 = 0.0f;         out->m32 = 0.0f;          out->m33 = 1.0f / (zf - zn);  out->m34 = 0.0f;
	out->m41 = 0.0f;         out->m42 = 0.0f;          out->m43 = -zn / (zf - zn);   out->m44 = 1.0f;
}

void mat4_lookat_lh(Mat4 *out, Vec3 eye, Vec3 target, Vec3 up)
{
	Vec3 zaxis = vec3_normalize(vec3_sub(target, eye));
	Vec3 xaxis = vec3_normalize(vec3_cross(up, zaxis));
	Vec3 yaxis = vec3_cross(zaxis, xaxis);

	out->m11 = xaxis.x; out->m12 = yaxis.x; out->m13 = zaxis.x; out->m14 = 0.0f;
	out->m21 = xaxis.y; out->m22 = yaxis.y; out->m23 = zaxis.y; out->m24 = 0.0f;
	out->m31 = xaxis.z; out->m32 = yaxis.z; out->m33 = zaxis.z; out->m34 = 0.0f;
	out->m41 = -vec3_dot(xaxis, eye);
	out->m42 = -vec3_dot(yaxis, eye);
	out->m43 = -vec3_dot(zaxis, eye);
	out->m44 = 1.0f;
}

void mat4_to_colmajor(float out[16], const Mat4 *m)
{
	/* ImGuizmo's matrix_t uses C float m[4][4] which is row-major — identical
	 * to our Mat4 byte layout. Direct memcpy, no transpose needed.
	 * (Despite the name "colmajor", this is just a float[16] copy for ImGuizmo.) */
	const float *src = &m->m11;
	out[ 0] = src[ 0]; out[ 1] = src[ 1]; out[ 2] = src[ 2]; out[ 3] = src[ 3];
	out[ 4] = src[ 4]; out[ 5] = src[ 5]; out[ 6] = src[ 6]; out[ 7] = src[ 7];
	out[ 8] = src[ 8]; out[ 9] = src[ 9]; out[10] = src[10]; out[11] = src[11];
	out[12] = src[12]; out[13] = src[13]; out[14] = src[14]; out[15] = src[15];
}

void mat4_from_colmajor(Mat4 *m, const float in[16])
{
	m->m11 = in[ 0]; m->m12 = in[ 1]; m->m13 = in[ 2]; m->m14 = in[ 3];
	m->m21 = in[ 4]; m->m22 = in[ 5]; m->m23 = in[ 6]; m->m24 = in[ 7];
	m->m31 = in[ 8]; m->m32 = in[ 9]; m->m33 = in[10]; m->m34 = in[11];
	m->m41 = in[12]; m->m42 = in[13]; m->m43 = in[14]; m->m44 = in[15];
}
