/**
 * sdk_scene.h — NQL SDK scene management
 *
 * Simple scene with transform objects and MVP computation.
 * Pure C header — no C++ dependencies.
 */
#ifndef NQLSDK_SCENE_H
#define NQLSDK_SCENE_H

#include "../sdk_types.h"
#include "../Camera/sdk_camera.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Transform component — position, rotation, scale in world space.
 * 1 unit = 1 meter.
 */
typedef struct SdkTransform {
    SdkVec3 position;
    SdkVec3 rotation;   /* Euler angles in radians (XYZ) */
    SdkVec3 scale;
} SdkTransform;

/**
 * Scene object — camera + single triangle transform for M2.
 * Later: expand to support multiple meshes.
 */
typedef struct SdkScene {
    SdkCamera* camera;           /* Active camera (not owned) */
    SdkTransform triangle;       /* Triangle transform in world space */
    SdkMat4 triangle_mvp;        /* Cached MVP matrix for triangle */
} SdkScene;

/** Initialize scene with defaults */
void sdk_scene_init(SdkScene* scene, SdkCamera* camera);

/** Update MVP matrices from current transforms and camera */
void sdk_scene_update(SdkScene* scene);

/** Set triangle position in world space */
void sdk_scene_set_triangle_position(SdkScene* scene, SdkVec3 position);

/** Set triangle scale (default 1,1,1 for 1M wide) */
void sdk_scene_set_triangle_scale(SdkScene* scene, SdkVec3 scale);

/** Get triangle MVP matrix for rendering */
const SdkMat4* sdk_scene_get_triangle_mvp(const SdkScene* scene);

#ifdef __cplusplus
}
#endif

#endif /* NQLSDK_SCENE_H */
