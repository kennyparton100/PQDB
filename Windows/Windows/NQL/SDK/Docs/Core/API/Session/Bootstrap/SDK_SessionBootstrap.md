<!-- Navigation: breadcrumb trail -->
**Navigation:** [SDK Overview](../../../../SDK_Overview.md) > [Core](../../../SDK_CoreOverview.md) > [API](../../SDK_APIReference.md) > [Session](../SDK_RuntimeSessionAndFrontend.md) > Bootstrap

---

# NQL SDK Session Bootstrap

This page documents the synchronous world-entry bootstrap layer in `Core/API/Session/Bootstrap/`.

## Scope

Owned files:

- `Core/API/Session/Bootstrap/sdk_api_session_bootstrap.c`
- `Core/API/Session/Bootstrap/sdk_api_session_bootstrap_policy.h`

## Responsibilities

- defining which desired chunks belong to the sync safety set
- loading nearby terrain to residency before the world is declared playable
- ensuring that same safety set becomes GPU-ready
- detecting bootstrap stalls with explicit readiness diagnostics
- reporting wall-support stage health during staged startup work

## Important Behaviors

- bootstrap is phase-based: resident first, then GPU-ready
- the sync safety set is narrower than the full desired runtime backlog
- stall detection tracks exact bootstrap readiness plus queue/worker state
- wall support is observed separately from the terrain-ready contract

## Key Entry Points

- `bootstrap_visible_chunks_sync`
- `load_missing_active_wall_stage_sync`
- policy helpers in `sdk_api_session_bootstrap_policy.h`

## Change Guidance

- Edit this slice when changing what "world is safe to enter" means.
- Do not mix bootstrap truth with runtime backlog telemetry here; that is a separate concern.
- If startup looks hung but workers are active, inspect the policy helpers before touching the streamer itself.

## Related Docs

- [../SDK_RuntimeSessionAndFrontend.md](../SDK_RuntimeSessionAndFrontend.md)
- [../Core/SDK_SessionCoreRuntime.md](../Core/SDK_SessionCoreRuntime.md)
- [../../../World/Chunks/ChunkStreamer/SDK_ChunkStreamer.md](../../../World/Chunks/ChunkStreamer/SDK_ChunkStreamer.md)
- [../../../World/Chunks/ChunkManager/SDK_ChunkManager.md](../../../World/Chunks/ChunkManager/SDK_ChunkManager.md)

