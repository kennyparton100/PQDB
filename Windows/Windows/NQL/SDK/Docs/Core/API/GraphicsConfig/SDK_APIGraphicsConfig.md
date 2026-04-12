<!-- Navigation: breadcrumb trail -->
**Navigation:** [SDK Overview](../../../SDK_Overview.md) > [Core](../../SDK_CoreOverview.md) > [API](../SDK_APIReference.md) > Graphics Config Slice

---

# NQL SDK API Graphics Configuration Slice

`Docs/Core/API/GraphicsConfig/` existed without a page. The runtime ownership is shared across API, settings, pause-menu UI, and renderer code.

## Scope

Primary owners:

- `Core/API/sdk_api.c`
- `Core/Settings/sdk_settings.c`

Supporting systems:

- `Core/PauseMenu/sdk_pause_menu.c`
- `Renderer/`

## Responsibilities

- holding the live `g_graphics_settings` runtime state
- loading and saving persisted graphics configuration
- translating pause-menu or frontend edits into normalized runtime settings
- pushing graphics choices into renderer-facing state

## Change Guidance

- If you add a new graphics option, touch all three layers: persisted settings, menu/UI plumbing, and runtime application.
- Do not make `sdk_api.c` the only source of truth for values that are meant to persist.

## Related Docs

- [../SDK_APIReference.md](../SDK_APIReference.md)
- [../../Settings/SDK_Settings.md](../../Settings/SDK_Settings.md)
- [../../Settings/SDK_SettingsAndControls.md](../../Settings/SDK_SettingsAndControls.md)
- [../../PauseMenu/SDK_PauseMenu.md](../../PauseMenu/SDK_PauseMenu.md)
- [../../../Renderer/SDK_RendererRuntime.md](../../../Renderer/SDK_RendererRuntime.md)

