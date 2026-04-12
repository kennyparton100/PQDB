# NQL SDK Settlement Placement

Owned file:

- `Core/World/Settlements/Placement/sdk_settlement_placement.c`

This slice owns settlement suitability, purpose selection, variant selection, and approximate continental sampling for siting.

Key entry points:

- `sdk_settlement_determine_purpose_for_type`
- `sdk_settlement_determine_variant`
- `sdk_settlement_evaluate_suitability_full`
- `sdk_settlement_sample_continental_approximation`

Use this file when changing where settlements may spawn and what kind of settlement a location becomes.

Related docs:

- [../SDK_SettlementSystem.md](../SDK_SettlementSystem.md)
- [../Types/SDK_SettlementTypes.md](../Types/SDK_SettlementTypes.md)

