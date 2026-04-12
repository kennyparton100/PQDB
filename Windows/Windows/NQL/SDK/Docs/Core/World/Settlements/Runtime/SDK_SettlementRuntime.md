# NQL SDK Settlement Runtime

Owned files:

- `Core/World/Settlements/Runtime/sdk_settlement_runtime.c`
- `Core/World/Settlements/Runtime/sdk_settlement_runtime.h`

This slice owns the loaded-settlement runtime, not metadata generation.

Key entry points:

- `sdk_settlement_runtime_init`
- `sdk_settlement_runtime_shutdown`
- `sdk_settlement_runtime_tick_loaded`
- `sdk_settlement_runtime_query_debug`
- `sdk_settlement_runtime_notify_chunk_loaded`
- `sdk_settlement_runtime_notify_chunk_unloaded`

Use this file when changing active settlement simulation, resident/building runtime state, or runtime perf/debug counters.

Related docs:

- [../SDK_SettlementSystem.md](../SDK_SettlementSystem.md)
- [../../../Entities/SDK_Entities.md](../../../Entities/SDK_Entities.md)
