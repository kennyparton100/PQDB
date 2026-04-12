<!-- Navigation: breadcrumb trail -->
**Navigation:** [SDK Overview](../../../SDK_Overview.md) > [Core](../../SDK_CoreOverview.md) > [API](../SDK_APIReference.md) > Game Systems API Slice

---

# NQL SDK API Game Systems Slice

`Docs/Core/API/GameSystems/` existed without a page. The code ownership is real, but it is mostly concentrated in `Core/API/sdk_api.c` rather than a dedicated folder.

## Scope

Primary owners:

- `Core/API/sdk_api.c`
- `Core/API/Internal/sdk_api_internal.h`

Supporting systems:

- `Core/PauseMenu/`
- `Core/SkillMenu/`
- `Core/CreativeModeInventory/`
- `Core/Crafting/`
- `Core/World/Simulation/`
- `Core/World/Settlements/Runtime/`

## What This Slice Means

This conceptual slice is the frame-to-frame gameplay orchestration layer. It is where the engine currently composes player vitals, day/night, modal UI rules, interaction gating, settlement runtime ticking, and HUD updates.

## Change Guidance

- Start in `sdk_api.c` when you are changing frame order, modal blocking rules, player vitals wiring, or gameplay-wide system integration.
- Start in the owning subsystem instead when the change is really about recipes, pause-menu layout, skills data, or entity AI.

## Related Docs

- [../SDK_APIReference.md](../SDK_APIReference.md)
- [../Player/SDK_APIPlayer.md](../Player/SDK_APIPlayer.md)
- [../Interactions/SDK_APIInteractions.md](../Interactions/SDK_APIInteractions.md)
- [../GraphicsConfig/SDK_APIGraphicsConfig.md](../GraphicsConfig/SDK_APIGraphicsConfig.md)
- [../Session/GameModes/GamePlay/SDK_SessionGameplay.md](../Session/GameModes/GamePlay/SDK_SessionGameplay.md)

