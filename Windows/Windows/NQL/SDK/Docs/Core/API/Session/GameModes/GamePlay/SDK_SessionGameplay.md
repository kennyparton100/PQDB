<!-- Navigation: breadcrumb trail -->
**Navigation:** [SDK Overview](../../../../../SDK_Overview.md) > [Core](../../../../SDK_CoreOverview.md) > [API](../../../SDK_APIReference.md) > [Session](../../SDK_RuntimeSessionAndFrontend.md) > [Game Modes](../SDK_SessionGameModes.md) > Gameplay

---

# NQL SDK Session Gameplay Mode

This page documents the gameplay-mode session slice.

## Scope

Gameplay helpers live in:

- `Core/API/Session/GameModes/GamePlay/Map/`
- `Core/API/Session/GameModes/GamePlay/Spawn/`

But the main gameplay frame orchestration still largely lives in:

- `Core/API/sdk_api.c`

## Responsibilities

- normal world-play session behavior after session startup succeeds
- gameplay-specific spawn selection and world-create setting interpretation
- map tile cache identity, disk I/O, and map rendering helpers
- integrating those helpers into the main runtime frame loop

## Change Guidance

- Spawn-related changes usually start in `Spawn/`.
- map cache or minimap/exact-map changes usually start in `Map/`.
- if the gameplay frame order or modal behavior changes, `sdk_api.c` is still the owner.

## Related Docs

- [Map/SDK_SessionGameplayMap.md](Map/SDK_SessionGameplayMap.md)
- [Spawn/SDK_SessionGameplaySpawn.md](Spawn/SDK_SessionGameplaySpawn.md)
- [../../../GameSystems/SDK_APIGameSystems.md](../../../GameSystems/SDK_APIGameSystems.md)
- [../../../../Map/SDK_MapSchedulerAndTileCache.md](../../../../Map/SDK_MapSchedulerAndTileCache.md)

