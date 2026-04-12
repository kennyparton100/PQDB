<!-- Navigation: breadcrumb trail -->
**Navigation:** [SDK Overview](../SDK_Overview.md) > Shaders

---

# SDK Shader Pipeline

## Scope

This page documents the current HLSL shader sources and how the renderer loads them.

Source files:

- `Shaders/triangle_vs.hlsl`
- `Shaders/triangle_ps.hlsl`
- `Renderer/d3d12_pipeline.cpp`

## How It Works

The renderer builds its root signature and graphics PSOs in `d3d12_pipeline.cpp`.

Current flow:

```text
sdk_pipeline_create*
  -> resolve shader path
  -> D3DCompileFromFile(vertex shader)
  -> D3DCompileFromFile(pixel shader)
  -> create root signature
  -> create PSO(s)
```

Shader lookup tries these locations:

1. `<exe_dir>/Shaders/<file>`
2. `Shaders/<file>` from the current working directory
3. `SDK/Shaders/<file>` from the current working directory

The vertex shader consumes the renderer vertex format:

- position
- packed color
- face normal id
- UV
- texture index

The pixel shader handles several jobs in one file:

- block texture lookup from a texture array
- font-texture lookup for HUD/text paths
- directional + hemispheric lighting
- fog
- water tinting / underwater fogging
- special black-wall material override

## Root Signature Contract

Current root parameters:

- `b0`: transform constants for the vertex shader
- `b1`: lighting/material constants for the pixel shader
- `t0`: block texture array SRV

Current static samplers:

- point sampler
- font sampler
- linear sampler
- anisotropic samplers for 2x, 4x, 8x, and 16x

## Known Issues And Design Flaws

- The shader filenames still say `triangle_*`, but they now drive the voxel runtime rather than a simple demo triangle.
- Shaders are compiled from disk at startup instead of being precompiled or embedded.
- `d3d12_pipeline.cpp` uses `D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION` in all build configurations, including Release.
- Startup therefore depends on the copied `Shaders/` directory and pays runtime compile cost.
- The pixel shader mixes world rendering, water shading, and font/HUD texture behavior into one file, which makes ownership blurry and testing harder.

---

## Related Documentation

### Up to Root
- [SDK Overview](../SDK_Overview.md) - Documentation home

### Related Systems
- [../Renderer/SDK_RendererRuntime.md](../Renderer/SDK_RendererRuntime.md) - Renderer runtime
- [../Renderer/SDK_RendererFoundationAudit.md](../Renderer/SDK_RendererFoundationAudit.md) - Renderer audit
- [../Build/SDK_BuildGuide.md](../Build/SDK_BuildGuide.md) - Build and asset copy
- [../Core/MeshBuilder/SDK_MeshBuilder.md](../Core/MeshBuilder/SDK_MeshBuilder.md) - Mesh generation (feeds shaders)
- [../Textures/TexturePackSpec.md](../Textures/TexturePackSpec.md) - Texture packs

### Core Systems
- [../Core/SDK_CoreOverview.md](../Core/SDK_CoreOverview.md) - Core subsystems

---
*Documentation for `SDK/Shaders/`*
