# NQL SDK Runtime Diagnostics

This page maps the current startup status text and log families to the owning runtime systems.

## Startup Status Text

Current world-session startup reports these major stages:

| Progress | Status | Owner |
|---|---|---|
| `0.06` | `Opening world save...` | persistence load |
| `0.14` | `Initializing world generator...` | worldgen init |
| `0.22` | `Preparing world systems...` | world/session setup |
| `0.30` | `Loading station state...` | persistence station load |
| `0.38` | `Preparing chunk systems...` | chunk manager + streamer |
| `0.46` | `Initializing entities...` | entity setup |
| `0.54` | `Restoring world state...` | player/world restore |
| `0.54+` | `Choosing random/center/safe spawn...` | spawn search |
| `0.64` | `Planning visible chunks...` | desired residency plan |
| `0.66` | `Loading nearby terrain desired/resident/gpu_ready` | sync-safe chunk bootstrap |
| `0.97` | `Nearby terrain ready. Background streaming continues...` | background wall/remesh backlog |
| `0.98` | `Finalizing world session...` | final handoff |
| `1.00` | `World session ready` | active play |

Important correction:

- startup no longer treats wall support as a mandatory synchronous completion stage
- the world becomes playable after the nearby sync-safe set is ready
- wall support, edge/corner finalize work, and dirty remesh can continue in the background
- bounded bootstrap stall detection now fails startup cleanly instead of hanging forever

## Log Families

### `[STREAM]`

Chunk streaming, bootstrap, and adoption.

Current meaning usually includes:

- visible scheduling/backlog state
- no-progress wall-support snapshots
- result adoption and queue pressure

Historical note:

- old logs such as `perimeter wall bootstrap switching to sync takeover` belong to earlier builds and should not be treated as current expected behavior

### `[RESIDENCY]`

Chunk-manager desired/active state, topology changes, and transition planning.

### `[WALL]`

Wall readiness/health reporting for active superchunk edges.

### `[SPAWN]`

Spawn selection and scoring output for new worlds or non-persisted sessions.

### `[PERSIST]`

Persistence save/load events, including world snapshot writes.

## Honest Progress Notes

- Offline world generation now uses real completion counts instead of queue-pressure ratios.
- Runtime startup status now reflects the sync-safe set plus background backlog instead of pretending the last wall/remesh jobs are still "finishing terrain chunks".
- If the visible text and the work disagree, treat that as a bug and update this page together with the code.

## Related Docs

- [SDK_RuntimeSessionAndFrontend.md](../API/Session/SDK_RuntimeSessionAndFrontend.md)
- [SDK_ChunkResidencyAndStreaming.md](../World/Chunks/SDK_ChunkResidencyAndStreaming.md)
- [SDK_WorldSystemsGapAudit.md](../../SDK_WorldSystemsGapAudit.md)
