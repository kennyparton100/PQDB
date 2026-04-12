<!-- Navigation: breadcrumb trail -->
**Navigation:** [SDK Overview](../SDK_Overview.md) > Guides

---

# NQL SDK Guides

This folder is the practical guide layer for the SDK.

Use these pages when you need to change the engine correctly and quickly, especially when loading an AI model with only the context it needs for one task.

These guides are intentionally narrower than the large subsystem docs. They focus on:

- the real current file ownership
- the call stack that matters for the change
- the minimum files to load first
- the verification steps that catch the common regressions

## Start Here

- [SDK_AIChangeContext.md](SDK_AIChangeContext.md)
  - task-based context packs for AI-driven edits
- [SDK_AddMenuOptions.md](SDK_AddMenuOptions.md)
  - how start-menu and frontend option changes actually work
- [SDK_AddWorldCreateAndWorldgenOptions.md](SDK_AddWorldCreateAndWorldgenOptions.md)
  - how a world-create option becomes real runtime/worldgen input
- [SDK_SessionStartFlow.md](SDK_SessionStartFlow.md)
  - how the engine starts a world session and when the game becomes playable
- [NewWorldCreation/SDK_NewWorldCreationCallStack.md](NewWorldCreation/SDK_NewWorldCreationCallStack.md)
  - deeper call-stack trace for the create-world path

## Which Guide To Use

If the task is:

- adding or changing a menu entry:
  - start with [SDK_AddMenuOptions.md](SDK_AddMenuOptions.md)
- adding a new world-create flag or generation toggle:
  - start with [SDK_AddWorldCreateAndWorldgenOptions.md](SDK_AddWorldCreateAndWorldgenOptions.md)
- changing session bring-up, startup loading, or chunk bootstrap:
  - start with [SDK_SessionStartFlow.md](SDK_SessionStartFlow.md)
- deciding what source files to load into an AI model:
  - start with [SDK_AIChangeContext.md](SDK_AIChangeContext.md)

## Trust Model

These guides should be treated as higher-trust than many older leaf docs because they were written against the current split source tree.

If a large subsystem page disagrees with these guides or with the source:

1. trust the source first
2. trust these guides second
3. treat the older subsystem page as historical unless re-verified

## Current Engine Reality

These guides reflect the current state of the engine, not the intended final architecture.

That matters because:

- the project used to be much flatter
- some old docs still refer to flat paths
- startup/runtime behavior has changed significantly during the session/bootstrap refactor
- construction-cell support is now real, but some runtime stability work is still ongoing

---

## Related Documentation

- [../SDK_Overview.md](../SDK_Overview.md)
- [../SDK_DocumentationAudit.md](../SDK_DocumentationAudit.md)
- [../Core/API/Session/SDK_RuntimeSessionAndFrontend.md](../Core/API/Session/SDK_RuntimeSessionAndFrontend.md)
- [../Core/World/Chunks/SDK_ChunkResidencyAndStreaming.md](../Core/World/Chunks/SDK_ChunkResidencyAndStreaming.md)

---
*Guide index for `SDK/Docs/Guides/`*
