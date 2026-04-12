/**
 * sdk_scene.c — NQL SDK scene implementation
 */
#include "sdk_scene.h"
#include "../Math/sdk_math.h"

void sdk_scene_init(SdkScene* scene, SdkCamera* camera) {
    /* Initializes scene with camera and default triangle at origin */
    scene->camera = camera;
    
    /* Default triangle: at origin, 1M wide */
    scene->triangle.position = sdk_vec3(0.0f, 0.0f, 0.0f);
    scene->triangle.rotation = sdk_vec3(0.0f, 0.0f, 0.0f);
    scene->triangle.scale = sdk_vec3(1.0f, 1.0f, 1.0f);
    
    sdk_mat4_identity(&scene->triangle_mvp);
}

void sdk_scene_update(SdkScene* scene) {
    /* Updates camera matrices and builds triangle MVP matrix */
    if (!scene->camera) return;
    
    /* Update camera matrices */
    sdk_camera_update(scene->camera);
    
    /* Build triangle model matrix: scale * rotation * translation */
    SdkMat4 scale_mat, rot_x, rot_y, rot_z, trans_mat, temp, model;
    
    sdk_mat4_scaling(&scale_mat, scene->triangle.scale);
    sdk_mat4_rotation_x(&rot_x, scene->triangle.rotation.x);
    sdk_mat4_rotation_y(&rot_y, scene->triangle.rotation.y);
    sdk_mat4_rotation_z(&rot_z, scene->triangle.rotation.z);
    sdk_mat4_translation(&trans_mat, scene->triangle.position);
    
    /* model = translation * rot_z * rot_y * rot_x * scale */
    sdk_mat4_multiply(&temp, &rot_x, &scale_mat);
    sdk_mat4_multiply(&temp, &rot_y, &temp);
    sdk_mat4_multiply(&temp, &rot_z, &temp);
    sdk_mat4_multiply(&model, &trans_mat, &temp);
    
    /* MVP = view_projection * model (column-major for HLSL) */
    sdk_mat4_multiply(&scene->triangle_mvp, &scene->camera->view_projection, &model);
}

void sdk_scene_set_triangle_position(SdkScene* scene, SdkVec3 position) {
    /* Sets triangle position in scene */
    scene->triangle.position = position;
}

void sdk_scene_set_triangle_scale(SdkScene* scene, SdkVec3 scale) {
    /* Sets triangle scale in scene */
    scene->triangle.scale = scale;
}

const SdkMat4* sdk_scene_get_triangle_mvp(const SdkScene* scene) {
    /* Returns triangle model-view-projection matrix */
    return &scene->triangle_mvp;
}
