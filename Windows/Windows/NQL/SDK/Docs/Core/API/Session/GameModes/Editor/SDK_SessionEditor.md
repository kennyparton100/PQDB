<!-- Navigation: breadcrumb trail -->
**Navigation:** [SDK Overview](../../../../../SDK_Overview.md) > [Core](../../../../SDK_CoreOverview.md) > [API](../../../SDK_APIReference.md) > [Session](../../SDK_RuntimeSessionAndFrontend.md) > [Game Modes](../SDK_SessionGameModes.md) > Editor

---

# NQL SDK Session Editor Modes

This page documents the editor-mode session slice under `Core/API/Session/GameModes/Editor/`.

## Scope

Owned files:

- `sdk_api_session_editor.c`
- `sdk_api_session_editor_helpers.c`
- `sdk_api_session_prop_editor.c`

## Responsibilities

- starting character, animation, and prop editor sessions
- creating synthetic editor chunks and filling editor floor/platform geometry
- stamping authored voxel data into chunk volumes
- rebuilding dirty editor chunks synchronously
- maintaining editor preview mesh state and frame stepping/playback helpers

## Mode Split

- character editor and animation editor share `start_editor_session_common`
- prop editor uses a separate session start path because it spans multiple chunks and uses prop asset chunk payloads

## Change Guidance

- If a bug only happens in editor sessions, start here before touching gameplay session code.
- If you are editing authored voxel dimensions, review the helper file first; many assumptions are centralized there.
- Editor startup intentionally disables normal background expansion and uses immediate chunk rebuilds.

## Related Docs

- [../SDK_SessionGameModes.md](../SDK_SessionGameModes.md)
- [../GamePlay/SDK_SessionGameplay.md](../GamePlay/SDK_SessionGameplay.md)
- [../../Assets/SDK_SessionAssets.md](../../Assets/SDK_SessionAssets.md)
- [../../../../CreativeModeInventory/SDK_CreativeInventory.md](../../../../CreativeModeInventory/SDK_CreativeInventory.md)

