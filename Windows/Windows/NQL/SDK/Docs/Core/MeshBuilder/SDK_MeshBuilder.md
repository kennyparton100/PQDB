<!-- Navigation: breadcrumb trail -->
**Navigation:** [SDK Overview](../../SDK_Overview.md) > [Core](../SDK_CoreOverview.md) > MeshBuilder

---

# SDK MeshBuilder Documentation

Comprehensive documentation for the SDK MeshBuilder module providing voxel chunk mesh generation with face culling, ambient occlusion, and level-of-detail proxies.

**Module:** `SDK/Core/MeshBuilder/`  
**Output:** `SDK/Docs/Core/MeshBuilder/SDK_MeshBuilder.md`

## Table of Contents

- [Module Overview](#module-overview)
- [Architecture](#architecture)
- [Mesh Buffer](#mesh-buffer)
- [Face Culling](#face-culling)
- [Far Proxies](#far-proxies)
- [Key Functions](#key-functions)
- [Global State](#global-state)
- [API Surface](#api-surface)
- [Integration Notes](#integration-notes)
- [AI Context Hints](#ai-context-hints)

---

## Module Overview

The MeshBuilder generates vertex buffers from chunk block data for rendering. It implements face culling (only rendering visible block faces), ambient occlusion for lighting variation, and low-poly far proxies for distant chunks.

**Key Features:**
- Face culling against neighbors (AIR blocks or chunk boundaries)
- Cross-chunk neighbor lookups via ChunkManager
- Ambient occlusion for corner shading
- Smooth lighting support (toggleable)
- Far proxy meshes for distant LOD
- Debug coloration for worldgen visualization
- Dynamic buffer growth

---

## Architecture

### Mesh Generation Flow

```
sdk_mesh_build_chunk(chunk, cm, output)
           │
           ├──► For each subchunk marked dirty
           │       └──► Iterate blocks (16x16x16)
           │               ├──► Skip AIR blocks
           │               └──► For each of 6 faces:
           │                       ├──► Check neighbor (same chunk or cm lookup)
           │                       ├──► If neighbor is AIR/occludable:
           │                       │       └──► Generate quad (4 vertices)
           │                       └──► Apply AO shading
           │
           └──► Store generated vertices in chunk submesh
```

### Far Proxy Generation

```
sdk_mesh_build_chunk_far_proxy(chunk, cm, output)
           │
           ├──► Create low-res grid (stride=4: 5x5x5 points)
           ├──► Sample blocks at each grid point
           ├──► Generate simplified mesh surface
           └──► Much fewer vertices than full mesh
```

### Neighbor Lookup

```
get_neighbor_block(chunk, cm, lx, ly, lz)
           │
           ├──► If within chunk bounds:
           │       └──► Return chunk->blocks[local index]
           │
           └──► Else if cm provided:
                   ├──► Convert to world coordinates
                   ├──► cm->get_chunk_at(world_x, world_z)
                   └──► Lookup in neighbor chunk
```

---

## Mesh Buffer

**SdkMeshBuffer Structure:**

```c
typedef struct {
    BlockVertex* vertices;   // Vertex array (position, color, texcoord, normal)
    uint32_t     count;      // Current vertex count
    uint32_t     capacity;   // Allocated capacity
} SdkMeshBuffer;
```

**BlockVertex Format:**
```c
typedef struct {
    float    x, y, z;        // Position
    uint32_t color;          // ARGB color
    float    u, v;           // Texture coordinates
    float    nx, ny, nz;     // Normal (optional)
} BlockVertex;
```

**Buffer Operations:**

| Function | Purpose |
|----------|---------|
| `sdk_mesh_buffer_init` | Allocate initial capacity |
| `sdk_mesh_buffer_free` | Release memory |
| `sdk_mesh_buffer_clear` | Reset count to 0 (reuse buffer) |
| `sdk_mesh_buffer_add_quad` | Add 4 vertices (2 triangles) |
| `sdk_mesh_buffer_add_tri` | Add 3 vertices (auto-grow if needed) |

**Auto-Growth:**
- Initial capacity: `CHUNK_BLOCKS_PER_LAYER * 24` (~24,576 vertices)
- Doubles when full
- Maximum safeguard: 0x3fffffff (prevent overflow)

---

## Face Culling

### Face Indices

| Index | Name | Direction | Normal |
|-------|------|-----------|--------|
| 0 | `FACE_NEG_X` | Left | (-1, 0, 0) |
| 1 | `FACE_POS_X` | Right | (1, 0, 0) |
| 2 | `FACE_NEG_Y` | Bottom | (0, -1, 0) |
| 3 | `FACE_POS_Y` | Top | (0, 1, 0) |
| 4 | `FACE_NEG_Z` | Back | (0, 0, -1) |
| 5 | `FACE_POS_Z` | Front | (0, 0, 1) |

### Culling Logic

A face is generated if the neighbor block:
- Is `BLOCK_AIR` (transparent)
- Is outside loaded chunk set (treat as AIR for edge chunks)
- Does not fully occlude (opaque non-fluid blocks occlude)

```c
static int block_occludes_ao(BlockType block) {
    return block != BLOCK_AIR &&
           sdk_block_is_opaque(block) &&
           (sdk_block_get_behavior_flags(block) & SDK_BLOCK_BEHAVIOR_FLUID) == 0;
}
```

### Ambient Occlusion

AO calculates corner shading based on 3 neighboring blocks:

```
For each vertex of a face:
    side_a = block_occludes(neighbor_on_side_a)
    side_b = block_occludes(neighbor_on_side_b)
    corner = block_occludes(diagonal_corner)
    
    occ = (side_a && side_b) ? 3 : (side_a + side_b + corner)
    
    AO levels:
        0 blocks → 1.00 (full brightness)
        1 block  → 0.88
        2 blocks → 0.74
        3 blocks → 0.60 (darkest corner)
```

---

## Far Proxies

Far proxies are low-poly meshes for distant chunk rendering:

### Regular Far Proxy

- **Grid stride:** 4 blocks (4x downsample)
- **Grid size:** `(CHUNK_WIDTH / 4) + 1 = 5` points per axis
- **Vertices:** ~125 sample points vs 4096 blocks
- **Use case:** Distant chunks beyond full mesh range

### Experimental Far Proxy

- **Grid stride:** 8 blocks (8x downsample)
- **Grid size:** `(CHUNK_WIDTH / 8) + 1 = 3` points per axis
- **Even lower poly:** ~27 sample points
- **Use case:** Very distant horizon chunks

### Proxy Constants

```c
#define FAR_PROXY_CELL_STRIDE 4
#define FAR_PROXY_GRID_SIZE 5  // (16/4)+1
#define EXPERIMENTAL_FAR_PROXY_CELL_STRIDE 8
#define EXPERIMENTAL_FAR_PROXY_GRID_SIZE 3  // (16/8)+1
```

---

## Key Functions

### Buffer Management

| Function | Signature | Description |
|----------|-----------|-------------|
| `sdk_mesh_buffer_init` | `(SdkMeshBuffer*, uint32_t) → void` | Initialize with capacity |
| `sdk_mesh_buffer_free` | `(SdkMeshBuffer*) → void` | Free memory |
| `sdk_mesh_buffer_clear` | `(SdkMeshBuffer*) → void` | Reset count |
| `sdk_mesh_buffer_add_quad` | `(SdkMeshBuffer*, 4x BlockVertex*) → void` | Add 4 vertices |
| `sdk_mesh_buffer_add_tri` | `(SdkMeshBuffer*, 3x BlockVertex*) → void` | Add 3 vertices |

### Mesh Generation

| Function | Signature | Description |
|----------|-----------|-------------|
| `sdk_mesh_build_chunk` | `(SdkChunk*, SdkChunkManager*, SdkMeshBuffer*) → void` | Build all dirty subchunks |
| `sdk_mesh_build_chunk_far_proxy` | `(SdkChunk*, SdkChunkManager*, SdkMeshBuffer*) → void` | Build LOD proxy |
| `sdk_mesh_build_chunk_experimental_far_proxy` | `(SdkChunk*, SdkChunkManager*, SdkMeshBuffer*) → void` | Build ultra-LOD proxy |
| `sdk_mesh_build_all` | `(SdkChunkManager*) → void` | Build all dirty chunks in manager |

### Utility

| Function | Signature | Description |
|----------|-----------|-------------|
| `sdk_mesh_get_neighbor_offsets` | `(int face, int* dx, int* dy, int* dz) → void` | Get face direction offsets |
| `sdk_mesh_set_thread_worldgen_debug_enabled` | `(int) → void` | Enable debug colors per thread |
| `sdk_mesh_set_smooth_lighting_enabled` | `(int) → void` | Toggle smooth lighting globally |

---

## Global State

```c
// Thread-local debug coloring
static __declspec(thread) int g_mesh_worldgen_debug_enabled = 1;

// Global smooth lighting toggle (volatile for thread safety)
static volatile LONG g_mesh_smooth_lighting_enabled = 1;

// Face normals for lighting
static const float face_normals[6][3];

// Per-face light levels (all 1.0 = disabled shading)
static const float face_light[6];
```

---

## API Surface

### Public Header (sdk_mesh_builder.h)

```c
/* Face indices */
#define FACE_NEG_X 0  /* Left */
#define FACE_POS_X 1  /* Right */
#define FACE_NEG_Y 2  /* Bottom */
#define FACE_POS_Y 3  /* Top */
#define FACE_NEG_Z 4  /* Back */
#define FACE_POS_Z 5  /* Front */

/* Initial buffer capacity */
#define MESH_BUFFER_INITIAL_VERTS (CHUNK_BLOCKS_PER_LAYER * 24)

/* Mesh buffer */
typedef struct {
    BlockVertex* vertices;
    uint32_t     count;
    uint32_t     capacity;
} SdkMeshBuffer;

/* Buffer lifecycle */
void sdk_mesh_buffer_init(SdkMeshBuffer* buf, uint32_t capacity);
void sdk_mesh_buffer_free(SdkMeshBuffer* buf);
void sdk_mesh_buffer_clear(SdkMeshBuffer* buf);

/* Geometry building */
void sdk_mesh_buffer_add_quad(SdkMeshBuffer* buf, 
    const BlockVertex* v0, const BlockVertex* v1, 
    const BlockVertex* v2, const BlockVertex* v3);

/* Chunk meshing */
void sdk_mesh_build_chunk(SdkChunk* chunk, SdkChunkManager* cm, SdkMeshBuffer* output);
void sdk_mesh_build_chunk_far_proxy(SdkChunk* chunk, SdkChunkManager* cm, SdkMeshBuffer* output);
void sdk_mesh_build_chunk_experimental_far_proxy(SdkChunk* chunk, SdkChunkManager* cm, SdkMeshBuffer* output);
void sdk_mesh_build_all(SdkChunkManager* cm);

/* Utilities */
void sdk_mesh_get_neighbor_offsets(int face, int* out_dx, int* out_dy, int* out_dz);
void sdk_mesh_set_thread_worldgen_debug_enabled(int enabled);
void sdk_mesh_set_smooth_lighting_enabled(int enabled);
```

---

## Integration Notes

### Chunk Manager Integration

The MeshBuilder works with the ChunkManager for cross-chunk face culling:

```c
// In frame update:
SdkChunkManager* cm = get_chunk_manager();

// Build all dirty chunks
sdk_mesh_build_all(cm);

// Or build specific chunk
SdkChunk* chunk = cm->get_chunk(chunk_x, chunk_z);
if (chunk && chunk->dirty_subchunks_mask) {
    sdk_mesh_build_chunk(chunk, cm, &scratch_buffer);
}
```

### Rendering Integration

```c
// Full mesh for nearby chunks
if (distance < FAR_PROXY_DISTANCE) {
    render_submesh(&chunk->submeshes[subchunk_index]);
} 
// Far proxy for distant chunks
else if (distance < MAX_RENDER_DISTANCE) {
    render_mesh(&chunk->far_proxy_mesh);
}
```

### Debug Visualization

```c
// Enable worldgen debug colors (per thread)
sdk_mesh_set_thread_worldgen_debug_enabled(1);

// This colors blocks based on their worldgen origin:
// - Different colors for different generation stages
// - Useful for debugging worldgen issues
```

---

## AI Context Hints

### Adding New Block Types

When adding blocks with special rendering:

```c
// In init_face_visuals() - add custom handling:
if (block_type == BLOCK_MY_NEW_BLOCK) {
    // Custom vertex colors, texture indices, etc.
    uint32_t custom_color = calculate_custom_color(world_x, world_y, world_z);
    // ...
}
```

### Custom Face Generation

To generate faces with custom vertices:

```c
BlockVertex v[4];
// Set positions based on block location and face
float x = chunk_world_x + local_x;
float y = local_y;
float z = chunk_world_z + local_z;

// Face POS_Y (top) - create quad at y+1
v[0] = (BlockVertex){x,     y+1, z,     color, u0, v0, 0, 1, 0};
v[1] = (BlockVertex){x+1,   y+1, z,     color, u1, v0, 0, 1, 0};
v[2] = (BlockVertex){x,     y+1, z+1,   color, u0, v1, 0, 1, 0};
v[3] = (BlockVertex){x+1,   y+1, z+1,   color, u1, v1, 0, 1, 0};

sdk_mesh_buffer_add_quad(buffer, &v[0], &v[1], &v[2], &v[3]);
```

### Optimizing Mesh Generation

```c
// Reuse scratch buffer across chunks (avoid reallocation)
static SdkMeshBuffer scratch = {0};
if (!scratch.vertices) {
    sdk_mesh_buffer_init(&scratch, MESH_BUFFER_INITIAL_VERTS);
}

// Build multiple chunks
for (int i = 0; i < dirty_count; i++) {
    sdk_mesh_buffer_clear(&scratch);
    sdk_mesh_build_chunk(chunks[i], cm, &scratch);
    // Copy scratch to chunk mesh storage
}
```

### AO Customization

To modify ambient occlusion levels:

```c
// In ao_factor_from_neighbors():
static float ao_factor_from_neighbors(int side_a, int side_b, int corner) {
    int occ = (side_a && side_b) ? 3 : (side_a + side_b + corner);
    switch (occ) {
        case 0:  return 1.00f;
        case 1:  return 0.90f;  // Less dark for 1 neighbor
        case 2:  return 0.80f;  // Less dark for 2 neighbors
        default: return 0.70f;  // Less dark for 3 neighbors
    }
}
```

---

## Related Documentation

### Up to Parent
- [SDK Core Overview](../SDK_CoreOverview.md) - Core subsystems hub
- [SDK Overview](../../SDK_Overview.md) - Documentation root

### Related Systems
- [../World/Chunks/SDK_Chunks.md](../World/Chunks/SDK_Chunks.md) - Chunk data structures
- [../World/Chunks/ChunkManager/SDK_ChunkManager.md](../World/Chunks/ChunkManager/SDK_ChunkManager.md) - Cross-chunk lookups
- [../../Renderer/SDK_RendererRuntime.md](../../Renderer/SDK_RendererRuntime.md) - Rendering system
- [../World/Chunks/ChunkStreamer/SDK_ChunkStreamer.md](../World/Chunks/ChunkStreamer/SDK_ChunkStreamer.md) - Async streaming (meshes)

### World Generation
- [../World/Worldgen/SDK_Worldgen.md](../World/Worldgen/SDK_Worldgen.md) - Debug coloration
- [../World/SDK_WorldOverview.md](../World/SDK_WorldOverview.md) - World systems hub

---

**Source Files:**
- `SDK/Core/MeshBuilder/sdk_mesh_builder.h` (3,040 bytes) - Public API
- `SDK/Core/MeshBuilder/sdk_mesh_builder.c` (66,977 bytes) - Implementation

---
*Documentation for `SDK/Core/MeshBuilder/`*
