# NQL SDK Settlement Layout

Owned file:

- `Core/World/Settlements/Layout/sdk_settlement_layout.c`

This slice turns `SettlementMetadata` into deterministic building placements.

Key entry points:

- `sdk_settlement_generate_layout`
- `sdk_settlement_free_layout`

Use this file when changing building placement patterns, density, road/building spacing, or deterministic layout rules.

Related docs:

- [../SDK_SettlementSystem.md](../SDK_SettlementSystem.md)
- [../Building/SDK_SettlementBuilding.md](../Building/SDK_SettlementBuilding.md)
- [../Roads/SDK_SettlementRoads.md](../Roads/SDK_SettlementRoads.md)

