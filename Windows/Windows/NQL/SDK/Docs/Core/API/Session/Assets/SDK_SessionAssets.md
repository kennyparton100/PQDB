<!-- Navigation: breadcrumb trail -->
**Navigation:** [SDK Overview](../../../../SDK_Overview.md) > [Core](../../../SDK_CoreOverview.md) > [API](../../SDK_APIReference.md) > [Session](../SDK_RuntimeSessionAndFrontend.md) > Assets

---

# NQL SDK Session Asset Glue

This page documents the session-owned asset helper slice in `Core/API/Session/Assets/`.

## Scope

Owned file:

- `Core/API/Session/Assets/sdk_api_session_assets.c`

## Responsibilities

- local lookup helpers for character, animation, prop, block, item, and particle-effect asset ids
- editor/session-facing asset selection glue
- keeping editor session code from duplicating asset-index search logic

## What It Does Not Own

- asset file formats
- asset-library disk scanning
- content build pipelines
- renderer asset upload formats

## Change Guidance

- Edit this slice when editor or session code needs a different asset lookup path.
- If the underlying asset schema changes, update the asset libraries and metadata owners first, then adjust these lookups.

## Related Docs

- [../SDK_RuntimeSessionAndFrontend.md](../SDK_RuntimeSessionAndFrontend.md)
- [../GameModes/Editor/SDK_SessionEditor.md](../GameModes/Editor/SDK_SessionEditor.md)
- [../../../RoleAssets/SDK_RoleAssets.md](../../../RoleAssets/SDK_RoleAssets.md)
- [../../../../Build/SDK_ContentToolsAndAssetLibraries.md](../../../../Build/SDK_ContentToolsAndAssetLibraries.md)

