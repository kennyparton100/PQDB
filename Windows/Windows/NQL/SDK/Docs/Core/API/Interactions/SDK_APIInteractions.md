<!-- Navigation: breadcrumb trail -->
**Navigation:** [SDK Overview](../../../SDK_Overview.md) > [Core](../../SDK_CoreOverview.md) > [API](../SDK_APIReference.md) > Interaction API Slice

---

# NQL SDK API Interactions

This page documents the world-interaction slice in `Core/API/Interactions/sdk_api_interaction.c`.

## Scope

Owned file:

- `Core/API/Interactions/sdk_api_interaction.c`

High-signal exported actions:

- `sdk_actor_break_block`
- `sdk_actor_place_block`

## Responsibilities

- block break and block place behavior from the player/actor perspective
- interaction validation against held items and target coordinates
- bridging gameplay actions into chunk/world mutation helpers
- coordinating with drops, construction cells, and interaction-side checks

## Change Guidance

- Edit this file when changing what a place/break action does.
- Edit the gameplay frame loop as well if you are changing when those actions are allowed.
- If the change is really about construction-cell semantics or world occupancy, start in the world subsystem and treat this file as the call site.

## Related Docs

- [../SDK_APIReference.md](../SDK_APIReference.md)
- [../Player/SDK_APIPlayer.md](../Player/SDK_APIPlayer.md)
- [../GameSystems/SDK_APIGameSystems.md](../GameSystems/SDK_APIGameSystems.md)
- [../../World/ConstructionCells/SDK_ConstructionSystem.md](../../World/ConstructionCells/SDK_ConstructionSystem.md)

