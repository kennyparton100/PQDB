<!-- Navigation: breadcrumb trail -->
**Navigation:** [SDK Overview](../../../../SDK_Overview.md) > [Core](../../../SDK_CoreOverview.md) > [API](../../SDK_APIReference.md) > [Session](../SDK_RuntimeSessionAndFrontend.md) > Headless

---

# NQL SDK Session Headless

This page documents the headless session path in `Core/API/Session/Headless/`.

## Scope

Owned files:

- `sdk_session_headless.h`
- `sdk_session_headless.c`

Primary entry point:

- `sdk_session_start_headless`

## Responsibilities

- starting a world session without the normal frontend/renderer path
- choosing headless spawn points using the same worldgen profile logic
- initializing chunk manager, chunk streamer, persistence, simulation, and worldgen for headless runs
- loading or generating required chunks for scripted/debug scenarios

## Important Differences From Interactive Sessions

- no normal frontend ownership
- no renderer-driven bootstrap UI
- its own spawn helpers and RNG flow
- tuned for tooling, debugger runs, and automated validation

## Change Guidance

- If you change startup assumptions in the normal session path, verify whether headless needs a matching change.
- Do not assume headless can tolerate renderer-only or frontend-only invariants.

## Related Docs

- [../SDK_RuntimeSessionAndFrontend.md](../SDK_RuntimeSessionAndFrontend.md)
- [../../../../Debugging/SDK_HeadlessDebugCLI.md](../../../../Debugging/SDK_HeadlessDebugCLI.md)
- [../../../../Guides/SDK_SessionStartFlow.md](../../../../Guides/SDK_SessionStartFlow.md)

