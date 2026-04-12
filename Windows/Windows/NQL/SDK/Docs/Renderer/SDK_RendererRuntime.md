<!-- Navigation: breadcrumb trail -->
**Navigation:** [SDK Overview](../SDK_Overview.md) > Renderer

---

# NQL SDK - Renderer Runtime

This document describes the current chunk rendering path, representation selection, wall proxy rules, and water rendering path.

## Renderer Ownership

The renderer does not choose the desired chunk set. It consumes resident chunks from the chunk manager and chooses how to draw each resident chunk.

Current draw-time responsibilities:

- choose one chunk representation per resident chunk
- cull using bounds for that chosen representation
- draw full chunk meshes, far meshes, wall proxies, and water in the correct order
- upload GPU buffers for chunk submeshes and far meshes

## Representation Types

The runtime currently uses three chunk-level render representations:

| Representation | Meaning |
|---|---|
| `FULL` | Draw per-subchunk terrain meshes from resident chunk submeshes |
| `FAR` | Draw one far mesh for the chunk, either stable or experimental |
| `PROXY` | Do not draw the chunk directly in the main chunk pass; draw wall proxy geometry for its superchunk later |

Far mesh kind is tracked separately from the high-level representation:

- `RENDERER_FAR_MESH_NONE`
- `RENDERER_FAR_MESH_EXPERIMENTAL`
- `RENDERER_FAR_MESH_STABLE`

## Representation Selection

Selection happens once per frame for each occupied resident slot before draw submission.

Current rules:

- wall-band chunks are never eligible for chunk far meshes
- wall-band chunks can become `PROXY` only when they belong to distant superchunks that are allowed to use wall proxies
- non-wall chunks can become `FAR` once they pass the configured experimental or stable far threshold
- hysteresis is applied when returning from `FAR` or `PROXY` back to `FULL`

Distance rules:

- stable far mesh enters at `graphics_far_terrain_lod_distance_chunks`
- experimental far mesh can enter earlier at `graphics_experimental_far_mesh_distance_chunks`
- exit distance is currently `enter_distance - 2`

This keeps representation changes sticky instead of flipping every frame around a hard threshold.

## Representation Ownership Table

| Concern | Current Owner |
|---|---|
| Desired chunk coordinates | `sdk_chunk_manager.*` |
| Resident slot and role | `sdk_chunk_manager.*` |
| Chosen render representation | `d3d12_renderer_frame.cpp` |
| Far mesh kind for a resident slot | `d3d12_renderer_frame.cpp` |
| Frustum test bounds for the chosen representation | `d3d12_renderer_frame.cpp` |
| GPU upload of chunk meshes | `d3d12_renderer_chunks.cpp` |
| Wall proxy cache build and draw | `d3d12_renderer_frame.cpp` |

This separation matters. If culling, LOD choice, and wall logic are reintroduced in multiple places, the renderer will drift back into conflicting decisions.

## Wall Logic

The renderer uses the shared superchunk geometry header and two key predicates:

- `chunk_intersects_wall_volume(...)`
- `chunk_supports_active_superchunk_wall(...)`

Current rules:

- black-wall shading applies only to chunks intersecting wall volume
- wall proxies are disabled for the active superchunk and its immediate outer neighborhood
- wall proxies are only allowed for distant superchunks when far terrain LOD is enabled

Important runtime dependency:

- west and north wall geometry is owned by chunks inside the superchunk
- east and south visible perimeter depends on adjacent ring chunks

That means missing perimeter walls can be caused by either residency failure or GPU-mesh/renderability failure. Use the wall-health diagnostics to separate those cases.

## Water Rendering Path

Water is no longer drawn in the same mesh slices as opaque terrain.

Current mesh split:

- opaque terrain: `chunk->subchunks[]`
- water terrain: `chunk->water_subchunks[]`
- stable far terrain: `chunk->far_mesh`
- experimental far terrain: `chunk->experimental_far_mesh`
- exact overlay: `chunk->far_exact_overlay_mesh`

Current PSO split:

- `pso`: opaque terrain
- `water_pso`: water path

Current draw order:

1. opaque chunk meshes and far meshes
2. wall proxies
3. water meshes
4. player character, entities, HUD, and overlays

Current depth/blend intent:

- opaque terrain uses the main terrain PSO
- water uses a separate PSO with the translucent water path

## What "GPU-Ready" Means

For diagnostics, a chunk is treated as GPU-ready if any of these currently exist with vertices:

- an opaque subchunk vertex buffer
- a water subchunk vertex buffer
- a stable far mesh vertex buffer
- an experimental far mesh vertex buffer
- an exact overlay vertex buffer

That definition is used by the wall-health diagnostics to distinguish:

- resident but not uploaded
- uploaded but not chosen for the current representation

## Upload Ownership

`sdk_renderer_upload_chunk_mesh(...)` uploads all currently dirty mesh slices for one chunk:

- opaque subchunks
- water subchunks
- stable far mesh
- experimental far mesh
- exact overlay mesh

It logs `[UPLOAD] ... failed` if any slice upload fails. A resident chunk without GPU buffers is therefore usually an upload or mesh-generation problem, not a residency problem.

## Current Known Limits

- Wall proxies are a distant fallback, not a replacement for the active ring.
- Water rendering is split correctly from opaque terrain, but this is still a chunk-level mesh system rather than a full fluid surface system.
- Experimental far meshes are selected only when the configured distance, availability, and non-wall constraints all line up.

---

## Related Documentation

### Up to Root
- [SDK Overview](../SDK_Overview.md) - Documentation home

### Core Systems
- [../Core/World/Chunks/ChunkManager/SDK_ChunkManager.md](../Core/World/Chunks/ChunkManager/SDK_ChunkManager.md) - Chunk residency (feeds renderer)
- [../Core/MeshBuilder/SDK_MeshBuilder.md](../Core/MeshBuilder/SDK_MeshBuilder.md) - Mesh generation
- [../Core/World/Chunks/SDK_Chunks.md](../Core/World/Chunks/SDK_Chunks.md) - Chunk data structures

### Related Rendering
- [../Shaders/SDK_Shaders.md](../Shaders/SDK_Shaders.md) - Shader system
- [SDK_RendererFoundationAudit.md](SDK_RendererFoundationAudit.md) - Foundation audit
- [../Core/World/Simulation/SDK_Simulation.md](../Core/World/Simulation/SDK_Simulation.md) - Fluid simulation

### Build & Assets
- [../Build/SDK_BuildGuide.md](../Build/SDK_BuildGuide.md) - Build guide
- [../Textures/TexturePackSpec.md](../Textures/TexturePackSpec.md) - Texture packs

---
*Documentation for `SDK/Renderer/`*
