<!-- Navigation: breadcrumb trail -->
**Navigation:** [SDK Overview](../../../../SDK_Overview.md) > [Core](../../../SDK_CoreOverview.md) > [API](../../SDK_APIReference.md) > [Session](../SDK_RuntimeSessionAndFrontend.md) > Core Runtime

---

# NQL SDK Session Core Runtime

This page documents the central session runtime slice in `Core/API/Session/Core/`.

## Scope

Owned files:

- `Core/API/Session/Core/sdk_api_session_core.c`
- `Core/API/Session/Core/sdk_api_session_generation.c`

## Responsibilities

- resetting and clearing per-session runtime state
- progressive world-geometry release during shutdown/reset
- collecting exact startup readiness and live runtime streaming health
- latching bootstrap completion once startup succeeds
- managing startup-safe mode and startup status text
- driving the world-generation session overlay and generation-stage summary

## Why This Slice Matters

This is where the engine decides whether startup is still in bootstrap, in post-bootstrap grace mode, or in normal runtime streaming.

## High-Signal Functions

- `reset_session_runtime_state`
- `release_world_geometry_step`
- `collect_startup_chunk_readiness`
- `collect_runtime_chunk_health`
- `startup_safe_mode_active`
- `startup_bootstrap_completed`
- `world_generation_session_step`
- `gen_stage_begin`
- `gen_stage_end`

## Change Guidance

- Use this slice for startup telemetry and state transitions.
- Do not turn this file into a second chunk streamer. Scheduling policy belongs lower in the chunk systems.
- If you change the meaning of startup completion, update both bootstrap docs and runtime status text together.

## Related Docs

- [../SDK_RuntimeSessionAndFrontend.md](../SDK_RuntimeSessionAndFrontend.md)
- [../Bootstrap/SDK_SessionBootstrap.md](../Bootstrap/SDK_SessionBootstrap.md)
- [../../../Runtime/SDK_RuntimeDiagnostics.md](../../../Runtime/SDK_RuntimeDiagnostics.md)
- [../../../../Guides/SDK_SessionStartFlow.md](../../../../Guides/SDK_SessionStartFlow.md)

