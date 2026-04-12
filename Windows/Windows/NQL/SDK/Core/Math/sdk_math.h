/**
 * sdk_math.h — NQL SDK math utilities
 *
 * Matrix and vector operations for 3D transforms.
 * Pure C header — no C++ dependencies.
 */
#ifndef NQLSDK_MATH_H
#define NQLSDK_MATH_H

#include "../sdk_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ==================================================================
 * Vector operations
 * ================================================================== */

/** Create a vector */
SdkVec3 sdk_vec3(float x, float y, float z);

/** Vector addition */
SdkVec3 sdk_vec3_add(SdkVec3 a, SdkVec3 b);

/** Vector subtraction */
SdkVec3 sdk_vec3_sub(SdkVec3 a, SdkVec3 b);

/** Vector scaling */
SdkVec3 sdk_vec3_scale(SdkVec3 v, float s);

/** Vector dot product */
float sdk_vec3_dot(SdkVec3 a, SdkVec3 b);

/** Vector cross product */
SdkVec3 sdk_vec3_cross(SdkVec3 a, SdkVec3 b);

/** Vector length */
float sdk_vec3_length(SdkVec3 v);

/** Normalize vector (returns zero vector if input is zero) */
SdkVec3 sdk_vec3_normalize(SdkVec3 v);

/* ==================================================================
 * Matrix operations (column-major for HLSL)
 * ================================================================== */

/** Set matrix to identity */
void sdk_mat4_identity(SdkMat4* m);

/** Matrix multiplication: out = a * b */
void sdk_mat4_multiply(SdkMat4* out, const SdkMat4* a, const SdkMat4* b);

/** Create translation matrix */
void sdk_mat4_translation(SdkMat4* m, SdkVec3 t);

/** Create scaling matrix */
void sdk_mat4_scaling(SdkMat4* m, SdkVec3 s);

/** Create rotation matrix around X axis (angle in radians) */
void sdk_mat4_rotation_x(SdkMat4* m, float angle_rad);

/** Create rotation matrix around Y axis (angle in radians) */
void sdk_mat4_rotation_y(SdkMat4* m, float angle_rad);

/** Create rotation matrix around Z axis (angle in radians) */
void sdk_mat4_rotation_z(SdkMat4* m, float angle_rad);

/** Create perspective projection matrix */
void sdk_mat4_perspective(SdkMat4* m, float fov_deg, float aspect, float near_plane, float far_plane);

/** Create look-at view matrix (left-handed, +Z forward) */
void sdk_mat4_look_at(SdkMat4* m, SdkVec3 eye, SdkVec3 target, SdkVec3 up);

#ifdef __cplusplus
}
#endif

#endif /* NQLSDK_MATH_H */
