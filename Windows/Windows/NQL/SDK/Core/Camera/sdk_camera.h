/**
 * sdk_camera.h — NQL SDK camera object
 *
 * Simple camera with position, target, and projection parameters.
 * Pure C header — no C++ dependencies.
 */
#ifndef NQLSDK_CAMERA_H
#define NQLSDK_CAMERA_H

#include "../sdk_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Frustum planes for view culling (left, right, bottom, top, near, far)
 * Each plane: normal xyz + distance w (ax + by + cz + d = 0)
 */
typedef struct SdkFrustum {
    float planes[6][4]; /* 6 planes, each with (nx, ny, nz, d) */
} SdkFrustum;

/**
 * Camera object — positionable view with perspective projection.
 * Left-handed coordinate system: +X right, +Y up, +Z forward.
 */
typedef struct SdkCamera {
    SdkVec3 position;   /* Camera eye position */
    SdkVec3 target;     /* Point camera is looking at */
    SdkVec3 up;         /* World up vector (typically 0,1,0) */
    float   fov_deg;    /* Field of view in degrees */
    float   aspect;     /* Aspect ratio (width/height) */
    float   near_plane; /* Near clip plane distance */
    float   far_plane;  /* Far clip plane distance */
    
    /* Cached matrices (updated by sdk_camera_update) */
    SdkMat4 view;
    SdkMat4 projection;
    SdkMat4 view_projection;
    
    /* Cached frustum (updated by sdk_camera_update) */
    SdkFrustum frustum;
} SdkCamera;

/** Initialize camera with default values */
void sdk_camera_init(SdkCamera* cam, float aspect);

/** Update view and projection matrices from current parameters */
void sdk_camera_update(SdkCamera* cam);

/** Set camera position and target (up defaults to +Y) */
void sdk_camera_look_at(SdkCamera* cam, SdkVec3 position, SdkVec3 target);

/** Set projection parameters */
void sdk_camera_set_projection(SdkCamera* cam, float fov_deg, float aspect, float near_plane, float far_plane);

/** Extract frustum planes from view_projection matrix */
void sdk_camera_extract_frustum(SdkCamera* cam);

/** Test if a sphere is inside the frustum (center xyz, radius) */
bool sdk_frustum_contains_sphere(const SdkFrustum* frustum, float cx, float cy, float cz, float radius);

/** Test if an axis-aligned bounding box is inside the frustum */
bool sdk_frustum_contains_aabb(const SdkFrustum* frustum,
                               float min_x, float min_y, float min_z,
                               float max_x, float max_y, float max_z);

#ifdef __cplusplus
}
#endif

#endif /* NQLSDK_CAMERA_H */
