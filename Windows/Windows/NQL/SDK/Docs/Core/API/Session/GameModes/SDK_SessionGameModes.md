<!-- Navigation: breadcrumb trail -->
**Navigation:** [SDK Overview](../../../../SDK_Overview.md) > [Core](../../../SDK_CoreOverview.md) > [API](../../SDK_APIReference.md) > [Session](../SDK_RuntimeSessionAndFrontend.md) > Game Modes

---

# NQL SDK Session Game Modes

This page documents the split game-mode/session-kind structure under `Core/API/Session/GameModes/`.

## Scope

The folder is split into:

- `Editor/`
- `GamePlay/`

In practice, game-mode dispatch also depends on:

- `Core/API/sdk_api.c`
- `Core/API/Session/sdk_api_session_internal.h`

## Session Kinds

The runtime distinguishes multiple session kinds, including standard world gameplay, character editor, animation editor, prop editor, and headless runs. The code does not yet hide these behind a clean object model. `g_session_kind` is still the control point.

## Change Guidance

- Start here when a feature behaves differently between gameplay and editor sessions.
- If you are adding a new session kind, expect to touch shared session state, startup/shutdown paths, frontend launch flow, and renderer/UI assumptions.

## Related Docs

- [Editor/SDK_SessionEditor.md](Editor/SDK_SessionEditor.md)
- [GamePlay/SDK_SessionGameplay.md](GamePlay/SDK_SessionGameplay.md)
- [../Headless/SDK_SessionHeadless.md](../Headless/SDK_SessionHeadless.md)
- [../SDK_RuntimeSessionAndFrontend.md](../SDK_RuntimeSessionAndFrontend.md)

