# NQL SDK - Online And Server Runtime

This document describes the local frontend/runtime state used for saved servers, local hosting, and client-connection bookkeeping.

## Scope

Everything in this document is owned by `Core/Server/sdk_server_runtime.c` plus the shared structs declared in `Core/API/Internal/sdk_api_internal.h`.

This subsystem currently manages:

- saved remote-server entries
- local-host bookkeeping for a world started from the frontend
- the current client-connection state shown by the frontend/pause UI
- status messages and frontend transitions when joining, disconnecting, or stopping a local host

It does not implement a full network transport stack. It is the menu/runtime state layer around local-host and remote-server selection.

## Saved Server File

Saved remote servers are stored in:

- `servers.json`

Current stored fields per entry:

- `name`
- `address`

Behavior worth keeping in mind:

- addresses are normalized to include the default port if no port is supplied
- empty display names fall back to the normalized address
- the list is bounded by `SDK_SAVED_SERVER_MAX`

The file is rewritten wholesale on upsert or delete.

## Default Local Host Identity

When a world is prepared for local hosting, the runtime synthesizes a hosted entry with:

- world save id
- world display name
- world seed
- address `127.0.0.1:<SDK_SERVER_DEFAULT_PORT>`

The current default port constant is:

- `SDK_SERVER_DEFAULT_PORT = 28765`

This is deliberately frontend-friendly state. It gives the menus and pause UI a stable local-host identity without exposing transport details in unrelated code.

## Key Runtime Structs

The important internal structs are:

- `SdkSavedServerEntry`
  - stored remote servers
- `SdkHostedServerEntry`
  - world identity and address for the active local host
- `SdkClientConnection`
  - current connection state
- `SdkLocalHostManager`
  - whether a local host is active or stopping

Connection kinds:

- `SDK_CLIENT_CONNECTION_NONE`
- `SDK_CLIENT_CONNECTION_LOCAL_HOST`
- `SDK_CLIENT_CONNECTION_REMOTE_SERVER`

World launch modes:

- `SDK_WORLD_LAUNCH_STANDARD`
- `SDK_WORLD_LAUNCH_LOCAL_HOST_JOIN`
- `SDK_WORLD_LAUNCH_LOCAL_HOST_BACKGROUND`

## Launch Flow

The frontend queues a launch mode through `sdk_prepare_world_launch`.

Once the world session is ready, `sdk_finalize_world_launch` applies the requested behavior:

- standard launch
  - clear client-connection state and enter the world normally
- local-host join
  - populate hosted world state and immediately connect the local client to the synthetic local-host address
- local-host background
  - populate hosted world state, keep the frontend open, and report `Local host started.`

## Disconnect And Stop Behavior

Key transitions:

- `sdk_client_join_active_local_host`
  - converts the active world session into a connected local-host client view
- `sdk_client_disconnect_to_frontend`
  - drops the current client connection, reopens the frontend, and leaves the local host running
- `sdk_local_host_request_stop`
  - requests local-host shutdown while the world session is active
- `sdk_server_runtime_on_world_session_stopped`
  - clears local-host/client state once the world session really ends

The runtime uses status strings such as:

- `Local host started.`
- `Local host still running.`
- `Local host stopped.`
- `No local host is running.`

## Practical Boundary

When touching this area, keep the ownership line clear:

- `Core/Server/sdk_server_runtime.c` owns menu-visible connection state and saved-server storage
- world-session bring-up and teardown are split across `Core/API/Session/Core/sdk_api_session_core.c` and the session bootstrap/game-mode modules it drives
- frontend world selection and async task UI live in `Core/Frontend/sdk_frontend_worlds.c`, `Core/Frontend/sdk_frontend_async.c`, and `Core/Frontend/sdk_frontend_worldgen.c`

If the transport layer grows in the future, this file should remain the state/orchestration boundary rather than absorbing low-level socket or replication logic.

## Related Docs

- [SDK_RuntimeSessionAndFrontend.md](../API/Session/SDK_RuntimeSessionAndFrontend.md)
- [SDK_PersistenceAndStorage.md](../World/Persistence/SDK_PersistenceAndStorage.md)
