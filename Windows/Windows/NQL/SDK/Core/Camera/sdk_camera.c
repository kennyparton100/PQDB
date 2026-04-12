/**
 * sdk_camera.c — NQL SDK camera implementation
 */
#include "sdk_camera.h"
#include "../Math/sdk_math.h"
#include <math.h>
#include <stdio.h>
#include <windows.h>

void sdk_camera_init(SdkCamera* cam, float aspect) {
    /* Default: at origin, looking down -Z, +Y up */
    cam->position = sdk_vec3(0.0f, 0.0f, 0.0f);
    cam->target = sdk_vec3(0.0f, 0.0f, -1.0f);
    cam->up = sdk_vec3(0.0f, 1.0f, 0.0f);
    
    /* Default perspective: 60° FOV, 0.1M near, 100M far */
    cam->fov_deg = 60.0f;
    cam->aspect = aspect;
    cam->near_plane = 0.1f;
    cam->far_plane = 100.0f;
    
    /* Initialize matrices to identity */
    sdk_mat4_identity(&cam->view);
    sdk_mat4_identity(&cam->projection);
    sdk_mat4_identity(&cam->view_projection);
}

void sdk_camera_update(SdkCamera* cam) {
    /* Update view matrix from position/target/up */
    sdk_mat4_look_at(&cam->view, cam->position, cam->target, cam->up);
    
    /* Update projection matrix */
    sdk_mat4_perspective(&cam->projection, cam->fov_deg, cam->aspect, 
                          cam->near_plane, cam->far_plane);
    
    /* For HLSL column-vector convention: pos' = MVP * pos
     * MVP = Projection * View (order matters!)
     * P transforms view->clip, V transforms world->view
     * So MVP = P * V (apply V first, then P)
     */
    sdk_mat4_multiply(&cam->view_projection, &cam->projection, &cam->view);
    
    /* Extract frustum from view_projection */
    sdk_camera_extract_frustum(cam);
}

void sdk_camera_extract_frustum(SdkCamera* cam) {
    SdkFrustum* f = &cam->frustum;
    const SdkMat4* m = &cam->view_projection;
    
    /* Extract planes from view_projection matrix (m[col][row] layout).
     * For D3D12 clip space (z in [0,w]) with column-vector convention:
     * Left:   row3 + row0
     * Right:  row3 - row0  
     * Bottom: row3 + row1
     * Top:    row3 - row1
     * Near:   row2
     * Far:    row3 - row2
     * 
     * Since matrix is stored as m[col][row], to get row i we access m[0][i], m[1][i], m[2][i], m[3][i]
     */
    
    /* Left plane: row3 + row0 */
    f->planes[0][0] = m->m[0][3] + m->m[0][0];
    f->planes[0][1] = m->m[1][3] + m->m[1][0];
    f->planes[0][2] = m->m[2][3] + m->m[2][0];
    f->planes[0][3] = m->m[3][3] + m->m[3][0];
    
    /* Right plane: row3 - row0 */
    f->planes[1][0] = m->m[0][3] - m->m[0][0];
    f->planes[1][1] = m->m[1][3] - m->m[1][0];
    f->planes[1][2] = m->m[2][3] - m->m[2][0];
    f->planes[1][3] = m->m[3][3] - m->m[3][0];
    
    /* Bottom plane: row3 + row1 */
    f->planes[2][0] = m->m[0][3] + m->m[0][1];
    f->planes[2][1] = m->m[1][3] + m->m[1][1];
    f->planes[2][2] = m->m[2][3] + m->m[2][1];
    f->planes[2][3] = m->m[3][3] + m->m[3][1];
    
    /* Top plane: row3 - row1 */
    f->planes[3][0] = m->m[0][3] - m->m[0][1];
    f->planes[3][1] = m->m[1][3] - m->m[1][1];
    f->planes[3][2] = m->m[2][3] - m->m[2][1];
    f->planes[3][3] = m->m[3][3] - m->m[3][1];
    
    /* Near plane: row2 (D3D z in [0,w]) */
    f->planes[4][0] = m->m[0][2];
    f->planes[4][1] = m->m[1][2];
    f->planes[4][2] = m->m[2][2];
    f->planes[4][3] = m->m[3][2];
    
    /* Far plane: row3 - row2 */
    f->planes[5][0] = m->m[0][3] - m->m[0][2];
    f->planes[5][1] = m->m[1][3] - m->m[1][2];
    f->planes[5][2] = m->m[2][3] - m->m[2][2];
    f->planes[5][3] = m->m[3][3] - m->m[3][2];
    
    /* Normalize all plane normals */
    for (int i = 0; i < 6; i++) {
        float nx = f->planes[i][0];
        float ny = f->planes[i][1];
        float nz = f->planes[i][2];
        float len = sqrtf(nx*nx + ny*ny + nz*nz);
        if (len > 0.0001f) {
            f->planes[i][0] = nx / len;
            f->planes[i][1] = ny / len;
            f->planes[i][2] = nz / len;
            f->planes[i][3] = f->planes[i][3] / len;
        }
    }
    
}

bool sdk_frustum_contains_sphere(const SdkFrustum* frustum, float cx, float cy, float cz, float radius) {
    /* Test sphere against all 6 planes
     * Planes have inward-pointing normals, so:
     * - dist >= 0 means on or inside the frustum half-space
     * - dist < 0 means outside that plane
     * Sphere is outside if center_dist < -radius (outside by more than radius)
     */
    
    for (int i = 0; i < 6; i++) {
        float dist = frustum->planes[i][0] * cx +
                     frustum->planes[i][1] * cy +
                     frustum->planes[i][2] * cz +
                     frustum->planes[i][3];

        /* If sphere is completely outside this plane, it's outside frustum
         * With inward normals: outside means dist < -radius */
        if (dist < -radius) {
            return false;
        }
    }

    return true;
}

bool sdk_frustum_contains_aabb(const SdkFrustum* frustum,
                               float min_x, float min_y, float min_z,
                               float max_x, float max_y, float max_z) {
    /* Tests if an axis-aligned bounding box is inside or intersects the frustum */
    float center_x = (min_x + max_x) * 0.5f;
    float center_y = (min_y + max_y) * 0.5f;
    float center_z = (min_z + max_z) * 0.5f;
    float extent_x = (max_x - min_x) * 0.5f;
    float extent_y = (max_y - min_y) * 0.5f;
    float extent_z = (max_z - min_z) * 0.5f;

    for (int i = 0; i < 6; i++) {
        float nx = frustum->planes[i][0];
        float ny = frustum->planes[i][1];
        float nz = frustum->planes[i][2];
        float dist = nx * center_x + ny * center_y + nz * center_z + frustum->planes[i][3];
        float radius = fabsf(nx) * extent_x + fabsf(ny) * extent_y + fabsf(nz) * extent_z;
        if (dist < -radius) {
            return false;
        }
    }

    return true;
}

void sdk_camera_look_at(SdkCamera* cam, SdkVec3 position, SdkVec3 target) {
    /* Sets camera position and target look-at point */
    cam->position = position;
    cam->target = target;
}

void sdk_camera_set_projection(SdkCamera* cam, float fov_deg, float aspect, 
                                float near_plane, float far_plane) {
    /* Sets camera projection parameters (FOV, aspect, near/far planes) */
    cam->fov_deg = fov_deg;
    cam->aspect = aspect;
    cam->near_plane = near_plane;
    cam->far_plane = far_plane;
}
