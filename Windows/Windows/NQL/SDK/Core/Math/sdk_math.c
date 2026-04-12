/**
 * sdk_math.c — NQL SDK math implementations
 */
#include "sdk_math.h"
#include <math.h>

/* ==================================================================
 * Vector operations
 * ================================================================== */

SdkVec3 sdk_vec3(float x, float y, float z) {
    /* Constructs a 3D vector from x, y, z components */
    SdkVec3 v = { x, y, z };
    return v;
}

SdkVec3 sdk_vec3_add(SdkVec3 a, SdkVec3 b) {
    /* Returns vector sum of a and b */
    SdkVec3 v = { a.x + b.x, a.y + b.y, a.z + b.z };
    return v;
}

SdkVec3 sdk_vec3_sub(SdkVec3 a, SdkVec3 b) {
    /* Returns vector difference a - b */
    SdkVec3 v = { a.x - b.x, a.y - b.y, a.z - b.z };
    return v;
}

SdkVec3 sdk_vec3_scale(SdkVec3 v, float s) {
    /* Returns vector scaled by scalar s */
    SdkVec3 r = { v.x * s, v.y * s, v.z * s };
    return r;
}

float sdk_vec3_dot(SdkVec3 a, SdkVec3 b) {
    /* Returns dot product of vectors a and b */
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

SdkVec3 sdk_vec3_cross(SdkVec3 a, SdkVec3 b) {
    /* Returns cross product of vectors a and b */
    SdkVec3 v = {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
    return v;
}

float sdk_vec3_length(SdkVec3 v) {
    /* Returns Euclidean length (magnitude) of vector */
    return sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
}

SdkVec3 sdk_vec3_normalize(SdkVec3 v) {
    /* Returns unit vector in same direction, or zero if near-zero length */
    float len = sdk_vec3_length(v);
    if (len > 0.000001f) {
        float inv = 1.0f / len;
        return sdk_vec3_scale(v, inv);
    }
    return sdk_vec3(0, 0, 0);
}

/* ==================================================================
 * Matrix operations (column-major for HLSL)
 * ================================================================== */

void sdk_mat4_identity(SdkMat4* m) {
    /* Sets matrix to identity (1s on diagonal, 0s elsewhere) */
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            m->m[i][j] = (i == j) ? 1.0f : 0.0f;
}

void sdk_mat4_multiply(SdkMat4* out, const SdkMat4* a, const SdkMat4* b) {
    /* Multiplies matrices a * b, storing result in out */
    SdkMat4 temp;
    for (int col = 0; col < 4; col++) {
        for (int row = 0; row < 4; row++) {
            float sum = 0.0f;
            for (int k = 0; k < 4; k++) {
                sum += a->m[k][row] * b->m[col][k];
            }
            temp.m[col][row] = sum;
        }
    }
    *out = temp;
}

void sdk_mat4_translation(SdkMat4* m, SdkVec3 t) {
    /* Sets matrix to translation by vector t */
    sdk_mat4_identity(m);
    m->m[3][0] = t.x;
    m->m[3][1] = t.y;
    m->m[3][2] = t.z;
}

void sdk_mat4_scaling(SdkMat4* m, SdkVec3 s) {
    /* Sets matrix to scaling by vector s components */
    sdk_mat4_identity(m);
    m->m[0][0] = s.x;
    m->m[1][1] = s.y;
    m->m[2][2] = s.z;
}

void sdk_mat4_rotation_x(SdkMat4* m, float angle_rad) {
    /* Sets matrix to rotation around X axis by angle in radians */
    float c = cosf(angle_rad);
    float s = sinf(angle_rad);
    sdk_mat4_identity(m);
    m->m[1][1] = c;  m->m[2][1] = s;
    m->m[1][2] = -s; m->m[2][2] = c;
}

void sdk_mat4_rotation_y(SdkMat4* m, float angle_rad) {
    /* Sets matrix to rotation around Y axis by angle in radians */
    float c = cosf(angle_rad);
    float s = sinf(angle_rad);
    sdk_mat4_identity(m);
    m->m[0][0] = c;  m->m[2][0] = -s;
    m->m[0][2] = s;  m->m[2][2] = c;
}

void sdk_mat4_rotation_z(SdkMat4* m, float angle_rad) {
    /* Sets matrix to rotation around Z axis by angle in radians */
    float c = cosf(angle_rad);
    float s = sinf(angle_rad);
    sdk_mat4_identity(m);
    m->m[0][0] = c;  m->m[1][0] = s;
    m->m[0][1] = -s; m->m[1][1] = c;
}

void sdk_mat4_perspective(SdkMat4* m, float fov_deg, float aspect, float near_plane, float far_plane) {
    /* Sets perspective projection matrix with FOV, aspect ratio, near/far planes */
    float fov_rad = fov_deg * SDK_DEG2RAD;
    float f = 1.0f / tanf(fov_rad * 0.5f);
    float nf = 1.0f / (far_plane - near_plane);  /* Left-handed: positive range */
    
    sdk_mat4_identity(m);
    m->m[0][0] = f / aspect;
    m->m[1][1] = f;
    m->m[2][2] = far_plane * nf;                     /* col 2, row 2: depth scale */
    m->m[2][3] = 1.0f;                               /* col 2, row 3: clip_w = +z (left-handed) */
    m->m[3][2] = -near_plane * far_plane * nf;       /* col 3, row 2: depth offset */
    m->m[3][3] = 0.0f;                               /* col 3, row 3 */
}

void sdk_mat4_look_at(SdkMat4* m, SdkVec3 eye, SdkVec3 target, SdkVec3 up) {
    /* Sets view matrix looking from eye toward target with up direction */
    SdkVec3 z_axis = sdk_vec3_normalize(sdk_vec3_sub(target, eye));    /* Forward (+Z in left-handed) */
    SdkVec3 x_axis = sdk_vec3_normalize(sdk_vec3_cross(up, z_axis));   /* Right */
    SdkVec3 y_axis = sdk_vec3_cross(z_axis, x_axis);                  /* Up (recomputed) */
    
    sdk_mat4_identity(m);
    /* Column-vector, left-handed: column j stores j-th components of each axis
     * Column 0 = [Rx, Ux, Fx, 0], Column 1 = [Ry, Uy, Fy, 0], etc. */
    m->m[0][0] = x_axis.x;  m->m[0][1] = y_axis.x;  m->m[0][2] = z_axis.x;
    m->m[1][0] = x_axis.y;  m->m[1][1] = y_axis.y;  m->m[1][2] = z_axis.y;
    m->m[2][0] = x_axis.z;  m->m[2][1] = y_axis.z;  m->m[2][2] = z_axis.z;
    m->m[3][0] = -sdk_vec3_dot(x_axis, eye);
    m->m[3][1] = -sdk_vec3_dot(y_axis, eye);
    m->m[3][2] = -sdk_vec3_dot(z_axis, eye);
}
