<!-- Navigation: breadcrumb trail -->
**Navigation:** [SDK Overview](../../../../../../SDK_Overview.md) > [Core](../../../../../SDK_CoreOverview.md) > [API](../../../../SDK_APIReference.md) > [Session](../../../SDK_RuntimeSessionAndFrontend.md) > [Gameplay](../SDK_SessionGameplay.md) > Map

---

# NQL SDK Session Gameplay Map

This page documents the gameplay map helper slice under `Core/API/Session/GameModes/GamePlay/Map/`.

## Scope

Owned files:

- `sdk_api_session_map.c`
- `sdk_api_session_map_render.c`

## Responsibilities

- establishing world/seed identity for map cache files
- building exact map-tile cache paths and validating persisted tile headers
- loading and saving exact tile data on disk
- map scheduler queue helpers owned by the session layer
- color and shading helpers for fallback/exact map rendering
- wall/gate coloration logic used by map rendering

## Change Guidance

- If you are changing on-disk map cache identity, versioning, or world scoping, start here.
- If you are changing minimap colors, wall highlighting, or exact/fallback comparison, this slice is a primary owner.
- If you are changing tile production algorithms, cross-check `Core/Map/` and worldgen before editing this file.

## Related Docs

- [../SDK_SessionGameplay.md](../SDK_SessionGameplay.md)
- [../../../Debug/SDK_SessionDebug.md](../../../Debug/SDK_SessionDebug.md)
- [../../../../../Map/SDK_MapSchedulerAndTileCache.md](../../../../../Map/SDK_MapSchedulerAndTileCache.md)
- [../../../../../World/Superchunks/SDK_SuperChunks.md](../../../../../World/Superchunks/SDK_SuperChunks.md)

