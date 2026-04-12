# SDK Terrain Documentation

Minimal documentation for the SDK terrain compatibility module.

**Module:** `SDK/Core/World/Terrain/`  
**Output:** `SDK/Docs/Core/World/Terrain/SDK_Terrain.md`

## Module Overview

The Terrain module provides a compatibility facade over the geology-first worldgen system. It exists primarily for backward compatibility with older code that used a simpler terrain API. New code should use the full worldgen API directly.

**Status:** Legacy/Compatibility module - superseded by `sdk_worldgen.h`

---

## Functions

| Function | Signature | Description | Equivalent |
|----------|-----------|-------------|------------|
| `sdk_terrain_generate_chunk` | `(SdkChunk*, uint32_t seed) → void` | Generate chunk blocks | `sdk_worldgen_generate_chunk()` |
| `sdk_terrain_generate_all` | `(SdkChunkManager*, uint32_t seed) → void` | Generate all chunks | Iterate + `sdk_worldgen_generate_chunk_ctx()` |
| `sdk_terrain_noise` | `(float x, float z) → float` | Sample terrain noise | Internal worldgen noise |
| `sdk_terrain_get_height` | `(int wx, int wz, uint32_t seed) → int` | Get surface height | `sdk_worldgen_get_surface_y()` |

---

## API Surface

```c
#ifndef NQLSDK_TERRAIN_H
#define NQLSDK_TERRAIN_H

#include "../Chunks/sdk_chunk.h"
#include "../Chunks/sdk_chunk_manager.h"
#include "../Worldgen/sdk_worldgen.h"

void sdk_terrain_generate_chunk(SdkChunk* chunk, uint32_t seed);
void sdk_terrain_generate_all(SdkChunkManager* cm, uint32_t seed);
float sdk_terrain_noise(float x, float z);
int sdk_terrain_get_height(int wx, int wz, uint32_t seed);

#endif
```

---

## Migration Guide

**Old (Terrain API):**
```c
int height = sdk_terrain_get_height(wx, wz, seed);
sdk_terrain_generate_chunk(chunk, seed);
```

**New (Worldgen API):**
```c
SdkWorldGen worldgen;
sdk_worldgen_init(&worldgen, &world_desc);

int height = sdk_worldgen_get_surface_y_ctx(&worldgen, wx, wz);
sdk_worldgen_generate_chunk_ctx(&worldgen, chunk);

sdk_worldgen_shutdown(&worldgen);
```

---

## Related Documentation

- `SDK_Worldgen.md` - Full world generation API (use this instead)
- `SDK_WorldgenTerrainPipeline.md` - Terrain generation pipeline

---

**Source Files:**
- `SDK/Core/World/Terrain/sdk_terrain.h` (596 bytes) - Public API
- `SDK/Core/World/Terrain/sdk_terrain.c` (1,308 bytes) - Implementation
