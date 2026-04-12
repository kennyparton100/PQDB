<!-- Navigation: breadcrumb trail -->
**Navigation:** [SDK Overview](../../SDK_Overview.md) > [Core](../SDK_CoreOverview.md) > Automation

---

# NQL SDK Automation

This page documents the lightweight automation layer in `Core/Automation/`.

## Scope

Owned files:

- `Core/Automation/sdk_automation.h`
- `Core/Automation/sdk_automation.c`

This subsystem is a support layer for scripted input, frame overrides, and repeatable debug/test runs.

## What It Owns

- parsing automation script files into `SdkAutomationScript`
- storing appended automation actions
- exposing per-frame override state
- providing a deterministic hook that higher-level runtime code can consume

## Key Entry Points

- `sdk_automation_script_init`
- `sdk_automation_script_free`
- `sdk_automation_script_append`
- `sdk_automation_script_load_file`
- `sdk_automation_clear_frame_override`
- `sdk_automation_set_frame_override`
- `sdk_automation_frame_override`

## Change Guidance

- Add new action syntax here only after identifying which runtime system should execute it.
- Keep automation data plain and serializable. This layer should not own live gameplay resources.
- If a test needs deterministic state for one frame, prefer frame overrides here over ad hoc branches elsewhere.

## Related Docs

- [SDK_CoreOverview.md](../SDK_CoreOverview.md)
- [../API/Internal/SDK_APIInternal.md](../API/Internal/SDK_APIInternal.md)
- [../../Debugging/SDK_Debugging.md](../../Debugging/SDK_Debugging.md)
- [../../Tests/SDK_TestsAndBenchmarks.md](../../Tests/SDK_TestsAndBenchmarks.md)

