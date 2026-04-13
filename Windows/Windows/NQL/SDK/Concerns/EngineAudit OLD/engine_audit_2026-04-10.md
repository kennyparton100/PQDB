# Engine Audit - 2026-04-10

## Scope

This audit is based on:

- the current checked-in source tree under `Windows/NQL/SDK/`
- the latest runtime logs captured during construction-cell world startup
- selected high-risk modules inspected directly during this pass
- the current docs tree under `Windows/NQL/SDK/Docs/` (71 markdown files)

This is not a proof that every bug has been found. It is a report of the major faults and structural limits that are visible now and that materially block the project from becoming a stable engine.

## Executive Assessment

The engine has crossed an important threshold: the construction-cell proof of concept is real. Worlds can now reach playability with sub-block content present. That is the good news.

The bad news is that the codebase is still not operating like a trustworthy engine. The current state is "feature proof with unstable foundations", not "bug-free runtime". The main reasons are:

- memory ownership is not reliable enough in core world systems
- startup and post-startup streaming are still not controlled tightly enough
- the construction registry is still a crash surface
- residency/wall systems are doing too much work with too little validation
- the folder split helped discoverability, but ownership boundaries are still blurry
- the docs tree is useful as a map, but many pages are not reliable as source of truth

If the goal is a bug-free engine, the codebase needs a stabilization phase, not more feature expansion.

## Critical Runtime Problems

### 1. Construction registry lifetime is not trustworthy

This is the highest-severity issue found in this pass.

Observed evidence:

- latest runtime log ends with `read access violation`
- faulting pointer was `registry->hash_table = 0x1110111011139E6`

Relevant code:

- `Core/World/ConstructionCells/sdk_construction_cells.c`
- `construction_registry_rebuild_hash(...)`
- `construction_registry_find_match(...)`
- `construction_registry_release_id(...)`

Why this is serious:

- the registry hash table is freed and rebuilt in place
- worker/runtime systems share registry pointers across chunk generation, adoption, persistence, and construction lookups
- there is no obvious registry-level synchronization protecting hash-table publication and use
- a bogus `0x111...` pointer strongly suggests use-after-free, stale memory, or invalid ownership transfer

This is not a cosmetic concern. It means the engine currently has a live memory-safety risk in a core world system.

Required fix direction:

- give the construction registry explicit threading/ownership rules
- stop mutating and republishing shared lookup state without a clear synchronization contract
- separate registry mutation from registry reads
- add invariant checks around refcount, slot lifetime, and hash-table revision transitions
- treat registry rebuilds as critical infrastructure, not helper code

### 2. Startup now reaches the world, but post-startup streaming is still unstable

Observed evidence from the latest log:

- bootstrap eventually reaches `desired=9 resident=9 gpu_ready=9`
- then background work immediately expands into `jobs=373`
- wall health remains `W=16/0/0 N=16/0/0 E=16/0/0 S=16/0/0`
- later readiness regresses from healthy values back down to `gpu_ready=6`, `4`, `3`, `2`, `1`

What this means:

- the sync-safe startup gate is no longer the main blocker
- the broader residency/wall system is still not under control once gameplay begins
- the engine can enter the world before the surrounding streaming model is actually stable

Required fix direction:

- hard-separate startup bootstrap from background residency expansion
- do not flood the queue with large wall/frontier workloads until the immediate world is stable
- define monotonic readiness rules for the startup-safe set so later background work cannot appear to "undo" readiness
- add budgeted, staged expansion rather than opening hundreds of jobs immediately

### 3. Startup latency is still far too high

Observed evidence:

- `Loading nearby chunks` took `116906ms` in the latest log before bootstrap completed

This is not acceptable engine behavior even if the world technically loads.

The current engine is doing "eventually succeeds" work, not "runtime-safe" work.

Required fix direction:

- instrument worldgen and meshing time per chunk class
- distinguish CPU generation cost from GPU upload cost from queueing delay
- stop treating worker activity as sufficient progress when end-to-end latency is still catastrophic
- establish a maximum acceptable startup budget and optimize against it

### 4. The wall system is still failing its contract

Observed evidence:

- repeated `WALL HEALTH W=16/0/0 N=16/0/0 E=16/0/0 S=16/0/0`
- large pending job counts without meaningful wall progress

What this implies:

- wall-support chunks are still desired but not becoming resident/ready at the expected rate
- the active superchunk perimeter model is not operationally healthy
- the engine is still treating wall logic as a special residency mode without robust completion rules

Required fix direction:

- define one authoritative wall completion model
- stop mixing wall-support loading, wall health reporting, and general frontier streaming without a clear state machine
- make wall progress observable in terms of queued, resident, meshed, uploaded, and draw-ready counts

### 5. Readiness accounting is still overloaded and easy to misread

The latest logs show that the engine can report:

- `resident=9 gpu_ready=9`
- then later drop to smaller numbers while the player is already in the world

That makes the current readiness output unsafe as an operational metric.

The issue is not just logging quality. It reflects that bootstrap readiness and ongoing runtime residency are still entangled.

Required fix direction:

- split "startup-safe readiness" from "live residency health"
- freeze bootstrap success accounting once the world is declared playable
- use separate counters for runtime health, background backlog, and wall completion

## Major Architectural Problems

### 6. Folder structure is better, but ownership is still inconsistent

The project is no longer the original flat mess. That is an improvement.

But the split is still incomplete:

- some systems are properly nested by responsibility
- others still act like legacy monoliths wearing a folder hierarchy as a skin
- docs and comments still frequently refer to older flat paths

Examples:

- docs still reference `Core/sdk_api_session.c`, `Core/sdk_server_runtime.c`, `Core/sdk_construction_cells.c`
- live code is now spread across nested session/world folders
- practical ownership still depends on knowing historical file names, not just the current tree

This limits maintainability because developers must understand both the old and new structure simultaneously.

### 7. Too many core systems are still concentrated in oversized files

Large files are not automatically bad, but several of the biggest files here are too large for safe iteration:

- `Core/API/sdk_api.c`
- `Core/World/ConstructionCells/sdk_construction_cells.c`
- `Core/World/Simulation/sdk_simulation.c`
- `Core/World/Chunks/ChunkStreamer/sdk_chunk_streamer.c`
- `Renderer/d3d12_renderer_frame.cpp`
- `Renderer/d3d12_renderer_hud.cpp`

Consequences:

- ownership is unclear
- changes have high regression risk
- debugging requires too much context at once
- AI assistance and human review both become less reliable

The codebase still contains "god files". The folder structure did not fully solve that.

### 8. Global-state coupling is still too high

The runtime still depends heavily on global session state such as `g_sdk` and global status text/state buffers.

Consequences:

- subsystem behavior is hard to reason about in isolation
- testing is difficult
- concurrency hazards are harder to localize
- runtime helpers can silently depend on unrelated initialization order

This is especially visible in:

- startup readiness collection
- session/bootstrap flow
- chunk adoption and upload flow
- frontend/session coordination

### 9. Runtime, tooling, and experiments are not cleanly separated

The source tree still mixes engine/runtime code with tool-style code and analysis utilities in ways that pollute navigation and maintenance.

Examples:

- `main.c` files under chunk-analysis and chunk-compression areas
- debugger and exact-map pathways sharing logic with live runtime pathways
- investigation-specific diagnostics getting committed into runtime-heavy modules

This matters because debugging and tooling code tends to grow different priorities than engine runtime code. Mixing them increases search noise and accidental coupling.

### 10. Invariants are not enforced strongly enough

The current engine relies too heavily on "this should stay in sync" assumptions between:

- desired chunk sets
- resident chunk slots
- streamer tracks
- result adoption
- GPU upload readiness
- construction registry refcounts

The latest logs show how easily these assumptions drift:

- startup becomes ready
- background work expands aggressively
- health counters become confusing or regress
- registry access later faults

This is the signature of a system with too few hard invariants and too much soft coordination.

## Specific Codebase Concerns By Area

### Chunk Manager / Streamer / Bootstrap

Main concerns:

- desired-set shape is now more complex than the docs describe
- startup-safe loading, wall-support loading, frontier loading, and background expansion are still too entangled
- queue growth is not matched by clear backpressure or degradation rules
- streamer worker count and scheduling policy are runtime-critical but not documented consistently

Practical problem:

- there is no simple answer to "what is allowed to load right now and why?"

That is a design smell. An engine should be able to answer that precisely.

### Construction Cells

Main concerns:

- the feature is now real, but it is sitting on unsafe core ownership
- the system crosses chunk storage, world-cell semantics, persistence, meshing, topology, gameplay placement, and worldgen
- the central implementation file is too large and too consequential

Practical problem:

- a single change to construction cells can affect memory lifetime, startup cost, topology answers, save/load, and mesh generation simultaneously

That is too much blast radius for one subsystem.

### Renderer

This pass did not perform a fresh renderer audit, but the codebase still carries previously documented renderer-foundation risk and there is still too much runtime sensitivity to upload/allocator sequencing.

Even when a specific D3D12 device-removal incident was caused by two apps running at once, the renderer side still appears fragile enough that it should be treated as a stability project, not a solved subsystem.

### Persistence

Persistence remains central to world safety but is still tightly coupled to runtime chunk/state layout.

Main risk:

- persistence correctness depends on runtime ownership being right
- construction registry persistence is now part of the core save/load contract
- that makes the construction-registry crash even more serious, because it is no longer optional side data

## Documentation Review

## Overall Accuracy And Usefulness

Overall rating:

- usefulness as a navigation map: medium to good
- usefulness as a source of truth: inconsistent
- usefulness for making safe engine changes: too low

The docs are not worthless. Some of the refreshed overview/session pages are helpful. But the tree still contains too many pages that mix:

- current behavior
- historical behavior
- inferred behavior
- copied code snippets from older layouts

That makes the docs easy to read and easy to misuse.

## Docs That Are Currently Useful

These are among the better pages reviewed in this pass:

- `Docs/SDK_Overview.md`
- `Docs/Core/API/Session/SDK_RuntimeSessionAndFrontend.md`
- `Docs/Guides/NewWorldCreation/SDK_NewWorldCreationCallStack.md`
- `Docs/Core/World/Worldgen/ConstructionCells/SDK_WorldgenConstructionCells.md`
- `Docs/SDK_DocumentationAudit.md`

Why they are useful:

- they acknowledge drift explicitly
- they describe current flows rather than idealized architecture
- they at least partially reflect the recent session/bootstrap refactor

## Docs That Are Currently Misleading Or Outdated

### `Docs/Core/World/Chunks/SDK_ChunkResidencyAndStreaming.md`

Problems:

- role table omits `SDK_CHUNK_ROLE_WALL_SUPPORT` even though it is a live runtime role
- says steady-state desired count is `340 total = 256 primary + 84 frontier`
- recent runtime logs show `desired=400(P256/F140/T4)`
- says worker count is clamped to `12`, while `sdk_chunk_streamer.c` currently defines `SDK_CHUNK_STREAMER_MAX_WORKERS 16`

Verdict:

- partially useful
- not accurate enough for current residency debugging

### `Docs/Core/World/Chunks/SDK_ChunkStreamer.md`

Problems:

- quality is lower than the better overview pages
- contains encoding/rendering corruption in the ASCII diagrams
- describes a more generic streamer model than the actual code now implements
- does not reflect the current startup-priority and wall-support realities well enough

Verdict:

- should not be treated as authoritative
- needs rewrite, not patching

### `Docs/Core/World/ConstructionCells/SDK_ConstructionSystem.md`

Problems:

- still points readers to older flat paths such as `Core/sdk_construction_cells.c`, `Core/sdk_persistence.c`, and `Core/sdk_api_session.c`
- the system it describes now spans newer nested folders and newer worldgen/topology integration

Verdict:

- conceptually useful
- operationally stale for source navigation

### `Docs/Core/Server/SDK_OnlineAndServerRuntime.md`

Problems:

- still refers to old flat ownership paths
- says world-session bring-up/teardown lives in `sdk_api_session.c`
- says frontend async task UI lives in `sdk_api_frontend.c`
- both are now misleading in a refactored tree

Verdict:

- okay for rough concepts
- poor for real code navigation

## Docs Tree Structural Problems

### 11. The docs tree mirrors intent more than reality

Many pages are organized as if the codebase already had clean subsystem ownership everywhere.

It does not.

Result:

- the docs often read cleaner than the engine actually is
- that makes the system feel better understood than it really is

### 12. Freshness is not machine-visible

The docs do not consistently tell the reader:

- when the page was last verified
- what code paths were checked
- whether the page is overview, current source-of-truth, or historical note

That is why drift keeps reappearing.

### 13. There are too many large low-trust pages

71 markdown files is not inherently a problem. The problem is that too many leaf pages are large, detailed, and only partly current.

That creates false confidence.

For a codebase in active refactor, a shorter set of high-trust pages is better than a huge set of half-trust pages.

## Folder Structure Review

## What Improved

The move away from the original flat project was correct.

Real benefits:

- subsystem discovery is better
- world/runtime areas are easier to browse than before
- recent session splitting clearly helped isolate some startup fixes

## What Still Needs Work

### 14. The structure is still half transitional

The project currently lives in two mental models at once:

- old flat-file ownership
- new nested feature ownership

That is visible in code, docs, and naming.

### 15. Some directories represent domains, others represent implementation accidents

Examples:

- `Core/API/Session/Core/` is a meaningful split
- `Core/World/Chunks/ChunkStreamer/` is meaningful
- but many references elsewhere still behave as if there is one canonical `sdk_api_session.c` or one canonical flat world layer

The structure needs a final cleanup pass where the old mental model is removed, not merely outnumbered.

### 16. Runtime concerns and data concerns are mixed

For example, construction cells currently cut across:

- data representation
- persistence
- runtime placement
- worldgen
- topology
- mesh generation

Those are valid cross-links, but the implementation does not make those boundaries easy to reason about.

## Prioritized Fix Order

### P0 - Stabilize Before More Features

1. Fix construction-registry ownership and hash-table lifetime.
2. Make background residency expansion staged and bounded after startup.
3. Make wall-support progress observable and enforceable.
4. Freeze startup-safe success accounting once the world is playable.
5. Add assertions and telemetry for chunk/registry invariants.

### P1 - Reduce Structural Risk

1. Split `sdk_construction_cells.c` by responsibility.
2. Split streamer scheduling, worker execution, and shutdown/result-drain logic.
3. Continue splitting `sdk_api.c` and any remaining session god-files.
4. Move tool/test `main.c` programs out of core runtime folders.
5. Reduce global-state dependency in session/bootstrap flow.

### P1 - Repair Documentation Trust

1. Mark every page with freshness status: `verified`, `partial`, or `historical`.
2. Rewrite `SDK_ChunkStreamer.md`.
3. Rewrite `SDK_ChunkResidencyAndStreaming.md` around the actual current roles and counts.
4. Update stale flat source paths in construction/server/runtime docs.
5. Prefer smaller high-trust overview pages over giant leaf dumps.

### P2 - Clean Final Structure

1. Remove remaining old-path mental models from docs/comments.
2. Separate runtime, tools, experiments, and debugging more clearly.
3. Standardize naming around ownership rather than file history.

## Bottom Line

The engine is no longer at "nothing works". It is at "core ideas are working, but the foundations are still unsafe".

The most important fact from this pass is not that construction cells now show up. It is that the engine can now reach gameplay while still carrying:

- a live construction-registry crash surface
- uncontrolled background chunk expansion
- weak wall-system completion behavior
- documentation that is too often plausible but not authoritative

If the project wants to become a bug-free engine, the next milestone should be stabilization and ownership cleanup, not more feature surface.
