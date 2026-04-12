# SDK Scene Module

## Scope

This page documents the small legacy module under `Core/Scene/`.

Current source:

- `Core/Scene/sdk_scene.h`
- `Core/Scene/sdk_scene.c`

## What It Actually Is

Despite the name, this is not a general-purpose scene graph.

`SdkScene` currently stores:

- one camera pointer
- one triangle transform
- one cached triangle MVP matrix

The renderer still embeds `SdkScene` in its internal state and initializes it during renderer startup, but most of the voxel-world runtime bypasses this module entirely.

## How It Works

Current flow:

```text
sdk_scene_init
  -> store camera pointer
  -> initialize one triangle transform
  -> zero triangle MVP

sdk_scene_update
  -> update camera matrices
  -> build model matrix for triangle
  -> compute triangle_mvp = camera.view_projection * model
```

The module is a thin math wrapper around one transform and one cached matrix.

## Why It Still Exists

- the D3D12 renderer still includes `SdkScene` in `d3d12_renderer_internal.h`
- the startup path still calls `sdk_scene_init(...)`
- it preserves the earlier colored-triangle demo path that predates the full voxel runtime

## Known Issues And Design Flaws

- The module name is misleading. It suggests broad scene ownership, but the implementation only handles one triangle.
- It is still compiled into the main application even though world rendering no longer depends on it as the primary scene model.
- There is no ownership model for multiple objects, no hierarchy, no resource binding, and no culling responsibilities.
- The app and renderer have outgrown the original demo, but the legacy naming and wiring remain.

## Related Docs

- App entry point: [../../App/SDK_AppOverview.md](../../App/SDK_AppOverview.md)
- Renderer runtime: [../../Renderer/SDK_RendererRuntime.md](../../Renderer/SDK_RendererRuntime.md)
- Shaders and pipeline: [../../Shaders/SDK_Shaders.md](../../Shaders/SDK_Shaders.md)
