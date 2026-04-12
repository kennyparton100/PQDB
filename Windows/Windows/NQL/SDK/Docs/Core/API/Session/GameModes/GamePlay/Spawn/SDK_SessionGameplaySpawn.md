<!-- Navigation: breadcrumb trail -->
**Navigation:** [SDK Overview](../../../../../../SDK_Overview.md) > [Core](../../../../../SDK_CoreOverview.md) > [API](../../../../SDK_APIReference.md) > [Session](../../../SDK_RuntimeSessionAndFrontend.md) > [Gameplay](../SDK_SessionGameplay.md) > Spawn

---

# NQL SDK Session Gameplay Spawn

This page documents the gameplay spawn helper slice in `Core/API/Session/GameModes/GamePlay/Spawn/`.

## Scope

Owned file:

- `sdk_api_session_spawn.c`

## Responsibilities

- interpreting persisted or in-flight spawn mode
- converting frontend world-create spawn settings into runtime behavior
- shared spawn RNG helpers
- spawn candidate scoring and relief estimation
- center, random, and safe spawn selection
- avoiding spawn placement inside superchunk wall bands

## Spawn Modes

Current persisted meanings:

- `0 = random`
- `1 = center/classic`
- `2 = safe`

## Change Guidance

- If spawn candidate quality changes, start here.
- If wall avoidance or world-create setting propagation changes, cross-check superchunk config docs and session startup docs.
- Keep the mode interpretation and the persisted `meta.txt` meaning synchronized.

## Related Docs

- [../SDK_SessionGameplay.md](../SDK_SessionGameplay.md)
- [../../../SDK_RuntimeSessionAndFrontend.md](../../../SDK_RuntimeSessionAndFrontend.md)
- [../../../../../World/Superchunks/SDK_SuperChunks.md](../../../../../World/Superchunks/SDK_SuperChunks.md)
- [../../../../../../Guides/SDK_SessionStartFlow.md](../../../../../../Guides/SDK_SessionStartFlow.md)

