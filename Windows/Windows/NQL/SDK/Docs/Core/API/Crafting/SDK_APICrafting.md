<!-- Navigation: breadcrumb trail -->
**Navigation:** [SDK Overview](../../../SDK_Overview.md) > [Core](../../SDK_CoreOverview.md) > [API](../SDK_APIReference.md) > Crafting API Slice

---

# NQL SDK API Crafting Slice

This page documents the crafting-facing API slice implemented in `Core/API/Crafting/sdk_api_crafting.c`.

## Scope

Owned file:

- `Core/API/Crafting/sdk_api_crafting.c`

This file is the API/runtime bridge for crafting UI state and player-facing crafting actions. The actual recipe data and station runtime live outside this slice.

## Responsibilities

- opening and closing crafting-related UI state from the gameplay layer
- translating API/runtime actions into crafting-grid mutations
- coordinating with the station runtime and crafting UI helpers
- keeping crafting interaction separate from direct block placement/break logic

## Neighbor Systems

- recipe and crafting data: `Core/Crafting/`
- station runtime: `Core/Crafting/sdk_station_runtime.c`
- renderer-facing crafting UI payloads: `Core/Crafting/sdk_crafting_ui.c`
- gameplay input that decides when crafting opens: mostly `Core/API/sdk_api.c`

## Change Guidance

- If you are changing recipe resolution or station behavior, start in `Core/Crafting/`, not here.
- If you are changing which inputs open crafting or how crafting blocks interaction, inspect this slice and `Core/API/sdk_api.c` together.

## Related Docs

- [../SDK_APIReference.md](../SDK_APIReference.md)
- [../../Crafting/SDK_Crafting.md](../../Crafting/SDK_Crafting.md)
- [../GameSystems/SDK_APIGameSystems.md](../GameSystems/SDK_APIGameSystems.md)
- [../Interactions/SDK_APIInteractions.md](../Interactions/SDK_APIInteractions.md)

