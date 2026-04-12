#ifndef SDK_API_SESSION_BOOTSTRAP_POLICY_H
#define SDK_API_SESSION_BOOTSTRAP_POLICY_H

#include "../../World/Chunks/ChunkManager/sdk_chunk_manager.h"

static inline int sdk_bootstrap_chunk_chebyshev_distance(int cx, int cz, int cam_cx, int cam_cz)
{
    /* Calculates Chebyshev distance from camera for chunk coordinates */
    int dx = cx - cam_cx;
    int dz = cz - cam_cz;
    if (dx < 0) dx = -dx;
    if (dz < 0) dz = -dz;
    return (dx > dz) ? dx : dz;
}

static inline int sdk_bootstrap_target_is_primary_safety(const SdkChunkResidencyTarget* target,
                                                         int cam_cx,
                                                         int cam_cz,
                                                         int safety_radius)
{
    /* Returns true if target is primary chunk within safety radius */
    if (!target) return 0;
    if ((SdkChunkResidencyRole)target->role != SDK_CHUNK_ROLE_PRIMARY) return 0;
    return sdk_bootstrap_chunk_chebyshev_distance(target->cx, target->cz, cam_cx, cam_cz) <=
           safety_radius;
}

static inline int sdk_bootstrap_target_is_primary_neighbor(const SdkChunkResidencyTarget* target,
                                                           int cam_cx,
                                                           int cam_cz,
                                                           int safety_radius)
{
    /* Returns true if target is primary neighbor (safety_radius + 1) */
    int chebyshev;

    if (!target) return 0;
    if ((SdkChunkResidencyRole)target->role != SDK_CHUNK_ROLE_PRIMARY) return 0;
    chebyshev = sdk_bootstrap_chunk_chebyshev_distance(target->cx, target->cz, cam_cx, cam_cz);
    return chebyshev == (safety_radius + 1);
}

static inline int sdk_bootstrap_target_is_close_wall_support(const SdkChunkResidencyTarget* target,
                                                             int cam_cx,
                                                             int cam_cz,
                                                             int safety_radius)
{
    /* Returns true if wall support target is within close range (safety + 1) */
    if (!target) return 0;
    if ((SdkChunkResidencyRole)target->role != SDK_CHUNK_ROLE_WALL_SUPPORT) return 0;
    return sdk_bootstrap_chunk_chebyshev_distance(target->cx, target->cz, cam_cx, cam_cz) <=
           (safety_radius + 1);
}

static inline int sdk_bootstrap_target_is_sync_primary(const SdkChunkResidencyTarget* target,
                                                       int cam_cx,
                                                       int cam_cz,
                                                       int sync_distance,
                                                       int force_all_primary)
{
    /* Returns true if primary target should be loaded synchronously */
    if (!target) return 0;
    if ((SdkChunkResidencyRole)target->role != SDK_CHUNK_ROLE_PRIMARY) return 0;
    if (force_all_primary) return 1;
    return sdk_bootstrap_target_is_primary_safety(target, cam_cx, cam_cz, sync_distance);
}

static inline int sdk_bootstrap_target_is_sync_safety(const SdkChunkResidencyTarget* target,
                                                      int cam_cx,
                                                      int cam_cz,
                                                      int sync_distance)
{
    /* Returns true if target (primary or wall support) is within sync distance */
    if (!target) return 0;
    if ((SdkChunkResidencyRole)target->role == SDK_CHUNK_ROLE_PRIMARY) {
        return sdk_bootstrap_target_is_primary_safety(target, cam_cx, cam_cz, sync_distance);
    }
    if ((SdkChunkResidencyRole)target->role == SDK_CHUNK_ROLE_WALL_SUPPORT) {
        return sdk_bootstrap_chunk_chebyshev_distance(target->cx, target->cz, cam_cx, cam_cz) <=
               sync_distance;
    }
    return 0;
}

#endif /* SDK_API_SESSION_BOOTSTRAP_POLICY_H */
