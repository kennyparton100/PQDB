<!-- Navigation: breadcrumb trail -->
**Navigation:** [SDK Overview](../../../SDK_Overview.md) > [Core](../../SDK_CoreOverview.md) > [API](../SDK_APIReference.md) > Frontend API Slice

---

# NQL SDK API Frontend Slice

This page documents the frontend-facing API glue in `Core/API/Frontend/sdk_api_frontend.c`.

## Scope

Owned file:

- `Core/API/Frontend/sdk_api_frontend.c`

This slice exposes frontend mutations and queries to the rest of the runtime. The actual frontend screen logic still lives in `Core/Frontend/`.

## Responsibilities

- bridging API/global runtime state to frontend views
- routing world-list, world-create, and frontend mode actions into the frontend layer
- keeping frontend transitions out of the lowest-level public API surface in `sdk_api.h`

## Change Guidance

- If you are adding a new menu option, edit the frontend layer first, then update this slice if an API bridge is needed.
- If you are changing world creation or launch behavior, cross-check the session docs before editing this file.

## Related Docs

- [../SDK_APIReference.md](../SDK_APIReference.md)
- [../../Frontend/SDK_Frontend.md](../../Frontend/SDK_Frontend.md)
- [../Session/SDK_RuntimeSessionAndFrontend.md](../Session/SDK_RuntimeSessionAndFrontend.md)
- [../../../Guides/SDK_AddMenuOptions.md](../../../Guides/SDK_AddMenuOptions.md)

