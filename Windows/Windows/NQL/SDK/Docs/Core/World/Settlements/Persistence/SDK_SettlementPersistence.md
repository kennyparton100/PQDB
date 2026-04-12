# NQL SDK Settlement Persistence

Owned file:

- `Core/World/Settlements/Persistence/sdk_settlement_persistence.c`

This slice owns versioned settlement metadata save/load at the superchunk level.

Key entry points:

- `sdk_settlement_save_superchunk`
- `sdk_settlement_load_superchunk`

Use this file when changing settlement file versioning, serialized fields, or world-path handling.

Related docs:

- [../SDK_SettlementSystem.md](../SDK_SettlementSystem.md)
- [../../Persistence/SDK_PersistenceAndStorage.md](../../Persistence/SDK_PersistenceAndStorage.md)

