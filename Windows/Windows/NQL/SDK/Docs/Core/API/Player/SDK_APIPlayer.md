<!-- Navigation: breadcrumb trail -->
**Navigation:** [SDK Overview](../../../SDK_Overview.md) > [Core](../../SDK_CoreOverview.md) > [API](../SDK_APIReference.md) > Player API Slice

---

# NQL SDK API Player Slice

This page documents the player-facing API glue in `Core/API/Player/sdk_api_player.c`.

## Scope

Owned file:

- `Core/API/Player/sdk_api_player.c`

This file is the player control bridge between the runtime frame loop and lower-level world/entity systems.

## Responsibilities

- applying player movement and control helpers
- routing player actions into world/entity operations
- exposing player-side runtime state transitions that do not belong in the low-level entity layer

## Change Guidance

- If a change is about the player as controller/avatar, this slice is a good first stop.
- If the change is about universal entity behavior, pathfinding, or simulation, start in the entity or world system instead.
- Cross-check the gameplay frame loop before assuming this file is the only owner of player behavior.

## Related Docs

- [../SDK_APIReference.md](../SDK_APIReference.md)
- [../GameSystems/SDK_APIGameSystems.md](../GameSystems/SDK_APIGameSystems.md)
- [../Interactions/SDK_APIInteractions.md](../Interactions/SDK_APIInteractions.md)
- [../../Entities/SDK_Entities.md](../../Entities/SDK_Entities.md)

