<!-- Navigation: breadcrumb trail -->
**Navigation:** [SDK Overview](../../../SDK_Overview.md) > [Core](../../SDK_CoreOverview.md) > [API](../SDK_APIReference.md) > Internal API Layer

---

# NQL SDK API Internal Layer

This page documents the shared internal API layer under `Core/API/Internal/`.

## Scope

Owned files:

- `Core/API/Internal/sdk_api_internal.h`
- `Core/API/Internal/sdk_load_trace.h`

This layer is the glue contract used by the split API/session/frontend/runtime implementation. It is not public API.

## What It Owns

- the large `SdkApiState` aggregate and related runtime-global declarations
- shared constants used across API/frontend/session code
- map scheduler types and helper structures
- startup/readiness telemetry structures
- load-trace helper declarations
- forward declarations that keep split session files linked together

## Change Guidance

- Prefer adding small helper declarations over moving unrelated subsystem logic into this header.
- If a field in `SdkApiState` is only needed by one subsystem, consider whether it belongs in that subsystem instead.
- When changing startup/readiness or map scheduler structures, update the owning docs in Session or Map at the same time.

## Related Docs

- [../SDK_APIReference.md](../SDK_APIReference.md)
- [../Session/SDK_RuntimeSessionAndFrontend.md](../Session/SDK_RuntimeSessionAndFrontend.md)
- [../../Map/SDK_MapSchedulerAndTileCache.md](../../Map/SDK_MapSchedulerAndTileCache.md)
- [../../Runtime/SDK_RuntimeDiagnostics.md](../../Runtime/SDK_RuntimeDiagnostics.md)

