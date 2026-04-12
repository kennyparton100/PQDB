# New World Creation Call Stack

This guide documents the code path for "create a new world and start playing it" from the start menu. It is intentionally focused on the real runtime path in the current codebase, not the intended architecture.

## Scope

This guide covers the local "Create World" flow:

1. Open the world-create menu.
2. Edit `g_world_create_*` settings.
3. Create a new save folder and `meta.txt`.
4. Queue the world for startup.
5. Start the world session.
6. Pick a spawn.
7. Load the first visible chunks.

It does not cover:

- exact map generation
- offline bulk world pre-generation
- local host / server launch variants

## High-Level Stack

```text
WinMain()
  -> nqlsdk_init(const SdkInitDesc* desc)
  -> while (nqlsdk_frame() == SDK_OK)
       -> frontend_handle_input()
            -> create_new_world_save_with_settings(SdkWorldSaveMeta* out_meta)
            -> refresh_world_save_list()
            -> begin_async_world_session_load(const SdkWorldSaveMeta* meta)
       -> push_start_menu_ui()
            -> update_async_world_generation()
                 -> start_world_session(const SdkWorldSaveMeta* selected_world)
                      -> sdk_persistence_init(SdkPersistence* persistence,
                                              const SdkWorldDesc* requested_world,
                                              const char* save_path)
                      -> sdk_persistence_get_world_desc(...)
                      -> sdk_superchunk_normalize_config(SdkSuperchunkConfig* config)
                      -> sdk_superchunk_set_config(const SdkSuperchunkConfig* config)
                      -> sdk_worldgen_init(SdkWorldGen* wg, const SdkWorldDesc* desc)
                      -> init_superchunk_map_scheduler(const SdkMapSchedulerConfig* config)
                      -> sdk_chunk_manager_init(SdkChunkManager* mgr)
                      -> sdk_persistence_bind_construction_registry(...)
                      -> sdk_chunk_streamer_init(...)
                      -> sdk_persistence_get_state(...)
                      -> choose_random_spawn_fast(SdkWorldGen* wg)
                         or choose_center_spawn(SdkWorldGen* wg)
                         or choose_safe_spawn(SdkWorldGen* wg)
                      -> sdk_chunk_manager_set_grid_size(...)
                  -> sdk_chunk_manager_update(...)
                      -> bootstrap_visible_chunks_sync(void)
                           -> sdk_chunk_streamer_schedule_startup_priority(...)
                           -> collect_startup_chunk_readiness(...)
                           -> process_streamed_chunk_results_with_budget(...)
                           -> sync_upload_startup_primary_chunks(...)
                           -> process_pending_chunk_gpu_uploads(...)
                           -> persist_loaded_chunks()
                           -> evict_undesired_loaded_chunks()
                      -> g_sdk.world_session_active = true
```

## Key Data Structures

### `SdkWorldSaveMeta`

Defined in `Core/API/Internal/sdk_api_internal.h`.

```c
typedef struct {
    char folder_id[64];
    char display_name[SDK_START_MENU_WORLD_NAME_MAX];
    char save_path[MAX_PATH];
    uint32_t seed;
    int render_distance_chunks;
    int spawn_mode;
    uint64_t last_write_time;
    bool has_save_data;
    bool settlements_enabled;
    bool construction_cells_enabled;
    bool superchunks_enabled;
    int superchunk_chunk_span;
    bool walls_enabled;
    bool walls_detached;
    int wall_grid_size;
    int wall_grid_offset_x;
    int wall_grid_offset_z;
} SdkWorldSaveMeta;
```

Meaning:

- `folder_id`: folder name such as `world_027`
- `display_name`: UI label
- `save_path`: full path to `save.json`
- `seed`: world seed
- `render_distance_chunks`: requested runtime radius
- `spawn_mode`: persisted new-world spawn selection (`0=random`, `1=center/classic`, `2=safe`)
- `has_save_data`: whether `save.json` already exists
- remaining fields: create-world feature toggles and wall/superchunk settings

### `SdkWorldDesc`

Defined in `Core/sdk_types.h`.

```c
typedef struct SdkWorldDesc {
    uint32_t seed;
    int16_t  sea_level;
    uint16_t macro_cell_size;
    bool settlements_enabled;
    bool construction_cells_enabled;
} SdkWorldDesc;
```

Important detail: `SdkWorldDesc` is smaller than `SdkWorldSaveMeta`. Superchunk and wall settings do not live in `SdkWorldDesc`; they are applied separately through `SdkSuperchunkConfig`.

## Phase 1: World-Create Menu State

### `void frontend_open_world_create_menu(void)`

Source: `Core/Frontend/sdk_frontend_menu.c`

This seeds the editable globals:

- `g_world_create_seed`
- `g_world_create_render_distance`
- `g_world_create_spawn_type`
- `g_world_create_settlements_enabled`
- `g_world_create_construction_cells_enabled`
- `g_world_create_superchunks_enabled`
- `g_world_create_superchunk_chunk_span`
- `g_world_create_walls_enabled`
- `g_world_create_walls_detached`
- `g_world_create_wall_grid_size`
- `g_world_create_wall_grid_offset_x`
- `g_world_create_wall_grid_offset_z`

Default values set here matter because later functions copy directly from these globals.

### `static void world_create_normalize_settings(void)`

Source: `Core/Frontend/sdk_frontend_menu.c`

This normalizes the create-world settings before they are saved:

- if superchunks are off, walls are forced off
- if walls are off, detached walls are forced off
- attached walls force `wall_grid_size = superchunk_chunk_span`
- detached walls enforce a minimum `wall_grid_size = chunk_span + 2`

### `frontend_handle_input()` world-create branch

Source: `Core/Frontend/sdk_frontend_menu.c`

This branch edits the `g_world_create_*` globals and confirms creation when the selected row is `11`.

The actual confirm path is:

```c
SdkWorldSaveMeta created_world;
if (create_new_world_save_with_settings(&created_world)) {
    refresh_world_save_list();
    begin_async_world_session_load(&created_world);
}
```

That is the first important split:

- `create_new_world_save_with_settings()` writes metadata
- `begin_async_world_session_load()` schedules the playable session startup

## Phase 2: Save Folder and `meta.txt` Creation

### `static void apply_current_world_create_settings(SdkWorldSaveMeta* meta)`

Source: `Core/Frontend/sdk_frontend_worlds.c`

Input:

- `meta`: destination metadata struct

It copies the current UI globals into `meta`:

- spawn mode
- settlements
- construction cells
- superchunks
- superchunk span
- walls enabled
- walls detached
- wall grid size
- wall grid offsets

It ends by calling `normalize_world_meta_settings(meta)`.

### `int build_world_save_dir_path(const char* folder_id, char* out_path, size_t out_path_len)`

Source: `Core/Frontend/sdk_frontend_worlds.c`

Parameters:

- `folder_id`: world folder id, for example `world_027`
- `out_path`: output buffer
- `out_path_len`: output buffer size

Builds the save directory path under `SDK_WORLD_SAVE_ROOT`.

### `int build_world_save_file_path(const char* folder_id, char* out_path, size_t out_path_len)`

Source: `Core/Frontend/sdk_frontend_worlds.c`

Parameters:

- `folder_id`: world folder id
- `out_path`: output buffer
- `out_path_len`: output buffer size

Builds the full `save.json` path.

### `void save_world_meta_file(const SdkWorldSaveMeta* meta)`

Source: `Core/Frontend/sdk_frontend_worlds.c`

Parameters:

- `meta`: metadata to serialize

Writes `meta.txt`. This is the file that persists the create-world settings before any runtime world session starts.

### `int create_new_world_save_with_settings(SdkWorldSaveMeta* out_meta)`

Source: `Core/Frontend/sdk_frontend_worlds.c`

Parameters:

- `out_meta`: optional output copy of the created world metadata

What it does:

1. Finds the first unused folder id `world_%03d`.
2. Fills `meta.display_name`.
3. Copies `meta.seed = g_world_create_seed`.
4. Copies `meta.render_distance_chunks = clamp_render_distance_chunks(g_world_create_render_distance)`.
5. Calls `apply_current_world_create_settings(&meta)`.
6. Ensures the world directory exists.
7. Builds `meta.save_path`.
8. Marks `meta.has_save_data = false`.
9. Calls `save_world_meta_file(&meta)`.
10. Returns the populated `SdkWorldSaveMeta`.

Important detail:

- at this point `save.json` does not have to exist yet
- this step mainly creates the folder, metadata, and path identity for the world

## Phase 3: Async Frontend Handoff

### `void begin_async_world_session_load(const SdkWorldSaveMeta* meta)`

Source: `Core/Frontend/sdk_frontend_async.c`

Parameters:

- `meta`: the newly created or selected world metadata

What it does:

1. Calls `sdk_prepare_world_launch(SDK_WORLD_LAUNCH_STANDARD)`.
2. Calls `frontend_open_world_generating()`.
3. Copies `*meta` into `g_world_generation_target`.
4. Clamps `g_world_generation_target.render_distance_chunks`.
5. Sets:
   - `g_world_generation_active = true`
   - `g_world_generation_is_offline = false`
   - `g_world_generation_stage = 2`
6. Sets status text to `"Starting world session..."`

Important detail:

- direct "create and play" does not go through offline world generation stage `0` or `1`
- it jumps straight to stage `2`, which means "call `start_world_session()` on the next frontend update"

### `void push_start_menu_ui(void)`

Source: `Core/Frontend/sdk_frontend_menu.c`

This function is called during `nqlsdk_frame()` while the frontend is visible. Before it pushes the UI to the renderer it calls:

```c
update_async_world_generation();
update_async_world_map_generation();
```

So the real session startup happens from the normal frame loop, not from the input handler itself.

### `void update_async_world_generation(void)`

Source: `Core/Frontend/sdk_frontend_worldgen.c`

For direct world launch, the relevant branch is:

```c
if (g_world_generation_stage == 2) {
    if (g_world_generation_is_offline) {
        ...
    } else {
        if (start_world_session(&g_world_generation_target)) {
            sdk_finalize_world_launch(&g_world_generation_target);
        }
    }
    ...
}
```

This is the exact bridge between frontend state and runtime world/session creation.

## Phase 4: Runtime World Session Creation

### `int start_world_session(const SdkWorldSaveMeta* selected_world)`

Source: `Core/API/Session/sdk_api_session_core.c`

Parameters:

- `selected_world`: the metadata record for the world being opened

This is the main runtime entry point for new-world startup.

### Step 4.1: Build `SdkWorldDesc`

At the top of `start_world_session()`:

```c
world_desc.seed = selected_world->seed;
world_desc.settlements_enabled = selected_world->settlements_enabled;
world_desc.construction_cells_enabled = selected_world->construction_cells_enabled;
```

This copies the runtime worldgen fields from the saved metadata.

### Step 4.2: Open persistence

```c
world_generation_session_step(0.06f, "Opening world save...", 1);
sdk_persistence_init(&g_sdk.persistence, &world_desc, selected_world->save_path);
sdk_persistence_get_world_desc(&g_sdk.persistence, &world_desc);
```

Functions:

- `void world_generation_session_step(float session_progress, const char* status, int present)`
- `void sdk_persistence_init(SdkPersistence* persistence, const SdkWorldDesc* requested_world, const char* save_path)`
- `int sdk_persistence_get_world_desc(const SdkPersistence* persistence, SdkWorldDesc* out_desc)`

Parameter meaning:

- `session_progress`: logical progress in `[0,1]`
- `status`: status text shown in the world-generation UI
- `present`: whether the frontend should be presented during the step
- `persistence`: runtime persistence object
- `requested_world`: seed + worldgen settings requested by the caller
- `save_path`: full path to `save.json`

Important detail:

- `sdk_persistence_init()` is allowed to open an existing `save.json` or initialize a fresh save path
- `sdk_persistence_get_world_desc()` can overwrite the initial `world_desc` with persisted data

### Step 4.3: Apply superchunk and wall config

Still inside `start_world_session()`:

```c
SdkSuperchunkConfig config;
config.enabled = selected_world->superchunks_enabled ? true : false;
config.chunk_span = selected_world->superchunk_chunk_span > 0 ? selected_world->superchunk_chunk_span : 16;
config.walls_enabled = (config.enabled && selected_world->walls_enabled) ? true : false;
config.walls_detached = selected_world->walls_detached;
config.wall_grid_size = selected_world->wall_grid_size > 1 ? selected_world->wall_grid_size : 18;
config.wall_grid_offset_x = selected_world->wall_grid_offset_x;
config.wall_grid_offset_z = selected_world->wall_grid_offset_z;
sdk_superchunk_normalize_config(&config);
sdk_superchunk_set_config(&config);
```

Important detail:

- wall and superchunk settings do not come from `SdkWorldDesc`
- they are applied from `SdkWorldSaveMeta` directly

### Step 4.4: Start worldgen

```c
world_generation_session_step(0.14f, "Initializing world generator...", 1);
sdk_worldgen_init(&g_sdk.worldgen, &world_desc);
```

Function:

- `void sdk_worldgen_init(SdkWorldGen* wg, const SdkWorldDesc* desc)`

Parameters:

- `wg`: worldgen runtime instance
- `desc`: runtime world descriptor

If `g_sdk.worldgen.impl` is still null after this call, startup fails.

### Step 4.5: Prepare world systems

Main calls in order:

```c
init_superchunk_map_scheduler(&map_config);
reset_session_runtime_state();
skills_reset_progression();
station_close_ui();
station_load_all_from_persistence();
sdk_chunk_manager_init(&g_sdk.chunk_mgr);
sdk_chunk_manager_set_background_expansion(&g_sdk.chunk_mgr, false);
sdk_persistence_bind_construction_registry(&g_sdk.persistence, g_sdk.chunk_mgr.construction_registry);
sdk_chunk_streamer_init(&g_sdk.chunk_streamer, &g_sdk.worldgen.desc, &g_sdk.persistence);
sdk_renderer_set_chunk_manager(&g_sdk.chunk_mgr);
sdk_entity_init(&g_sdk.entities);
```

This is where the runtime chunk/persistence/entity systems become live.

### Step 4.6: Restore state or choose a new spawn

```c
have_persisted_state = sdk_persistence_get_state(&g_sdk.persistence, &persisted_state);
```

If there is no saved player state, new-world startup uses:

```c
switch (current_new_world_spawn_mode(selected_world)) {
    case 0: choose_random_spawn_fast(&g_sdk.worldgen); break;
    case 1: choose_center_spawn(&g_sdk.worldgen); break;
    case 2:
    default: choose_safe_spawn(&g_sdk.worldgen); break;
}
```

Functions:

- `int current_new_world_spawn_mode(const SdkWorldSaveMeta* selected_world)`
- `static void choose_random_spawn_fast(SdkWorldGen* wg)`
- `static void choose_center_spawn(SdkWorldGen* wg)`
- `static void choose_safe_spawn(SdkWorldGen* wg)`

Parameter meaning:

- `selected_world`: used to detect whether the active frontend creation settings still apply
- `wg`: current worldgen instance used to sample terrain columns and score spawn candidates

Important detail:

- `current_new_world_spawn_mode()` now prefers persisted `selected_world->spawn_mode`
- it reads `g_world_create_spawn_type` only as a narrow fallback while the just-created world is still in the immediate generating handoff
- if a world has no persisted `spawn_mode`, runtime defaults to safe mode

## Phase 5: Visible Chunk Planning

### `int current_world_render_distance_chunks(const SdkWorldSaveMeta* selected_world)`

Source: `Core/API/Session/sdk_api_session_core.c`

Parameters:

- `selected_world`: world metadata

Returns:

- `selected_world->render_distance_chunks` if valid
- otherwise a value derived from current graphics settings

### Visible chunk planning calls

```c
sdk_chunk_manager_set_grid_size(&g_sdk.chunk_mgr, g_chunk_grid_size_setting);
sdk_chunk_manager_update(&g_sdk.chunk_mgr, g_sdk.chunk_mgr.cam_cx, g_sdk.chunk_mgr.cam_cz);
```

This computes the desired chunk set around the chosen spawn camera position.

## Phase 6: First Visible Chunks

### `void bootstrap_visible_chunks_sync(void)`

Source: `Core/API/Session/sdk_api_session_bootstrap.c`

This is the function responsible for the `"Loading nearby terrain X/Y/Z"` status.

Main flow:

1. Count `sync_target_total` by scanning desired chunk residency targets.
2. If `sync_target_total > 0`, write:

```c
"Loading nearby terrain %d/0/0"
```

3. Call:

```c
sdk_chunk_streamer_schedule_startup_priority(&g_sdk.chunk_streamer,
                                             &g_sdk.chunk_mgr,
                                             startup_safe_primary_radius());
```

4. Poll readiness with:

```c
collect_startup_chunk_readiness(&readiness);
```

5. While resident primary chunks are below target:

```c
process_streamed_chunk_results_with_budget(512, 0.0f);
```

6. While GPU-ready primary chunks are below desired primary:

```c
process_streamed_chunk_results_with_budget(16, 0.75f);
sync_upload_startup_primary_chunks(max(4, readiness.desired_primary));
process_pending_chunk_gpu_uploads(max(4, readiness.desired_primary), 4.0f);
```

7. Persist, reschedule startup priority, finalize, upload any remaining startup work, evict undesired chunks.

Important detail:

- startup progress is now based on real residency and GPU-ready chunk state
- it no longer depends on `g_sdk.world_session_active`
- if readiness stops changing for the bounded timeout, startup fails in a controlled way instead of hanging forever

### Meaning of `"Loading nearby terrain 4/0/0"`

That string is currently formatted as:

```text
desired_sync_targets / resident_primary / gpu_ready_primary
```

So:

- `4/0/0` means the bootstrap path believes four primary chunks are required
- none are currently resident
- none are currently GPU-ready

It does not mean "four chunks already loaded".

## Phase 7: Session Becomes Playable

At the end of `start_world_session()`:

```c
world_generation_session_step(1.0f, "World session ready", 1);
g_sdk.world_session_active = true;
```

Important detail:

- the world session becomes active immediately after successful bootstrap
- the old blocking generation-summary screen is no longer part of the success path

## Minimal Function Index

The smallest useful call stack for a new world is:

```text
frontend_open_world_create_menu(void)
frontend_handle_input(void)
create_new_world_save_with_settings(SdkWorldSaveMeta* out_meta)
apply_current_world_create_settings(SdkWorldSaveMeta* meta)
save_world_meta_file(const SdkWorldSaveMeta* meta)
begin_async_world_session_load(const SdkWorldSaveMeta* meta)
push_start_menu_ui(void)
update_async_world_generation(void)
start_world_session(const SdkWorldSaveMeta* selected_world)
sdk_persistence_init(SdkPersistence* persistence, const SdkWorldDesc* requested_world, const char* save_path)
sdk_persistence_get_world_desc(const SdkPersistence* persistence, SdkWorldDesc* out_desc)
sdk_superchunk_normalize_config(SdkSuperchunkConfig* config)
sdk_superchunk_set_config(const SdkSuperchunkConfig* config)
sdk_worldgen_init(SdkWorldGen* wg, const SdkWorldDesc* desc)
sdk_persistence_get_state(const SdkPersistence* persistence, SdkPersistedState* out_state)
current_new_world_spawn_mode(const SdkWorldSaveMeta* selected_world)
choose_random_spawn_fast(SdkWorldGen* wg)
choose_center_spawn(SdkWorldGen* wg)
choose_safe_spawn(SdkWorldGen* wg)
current_world_render_distance_chunks(const SdkWorldSaveMeta* selected_world)
sdk_chunk_manager_set_grid_size(SdkChunkManager* mgr, int grid_size)
sdk_chunk_manager_update(SdkChunkManager* mgr, int cam_cx, int cam_cz)
bootstrap_visible_chunks_sync(void)
```

## Practical Debugging Notes

If a newly created world gets stuck on startup, the most relevant handoff points are:

1. `create_new_world_save_with_settings()`
   - confirms whether the intended settings reached `meta.txt`
2. `begin_async_world_session_load()`
   - confirms the target world was copied into `g_world_generation_target`
3. `start_world_session()`
   - confirms `SdkWorldSaveMeta` was translated into `SdkWorldDesc` and `SdkSuperchunkConfig`
4. `bootstrap_visible_chunks_sync()`
   - confirms how many nearby chunks are considered mandatory and whether they ever become resident and GPU-ready

If bootstrap now fails rather than hangs, inspect the load-trace / debug output for the bounded stall reason:

- `bootstrap_startup_resident_stalled`
- `bootstrap_startup_gpu_stalled`

Those traces now include the blocking readiness categories, not just the top-level counts.

If you are tracing `"Loading nearby terrain 4/0/0"`, the first function to inspect is `bootstrap_visible_chunks_sync(void)`, not the menu code.
