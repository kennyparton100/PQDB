# SDK Function Documentation Progress

## Completed Files
1. **sdk_api.c** - Major public API functions documented (nqlsdk_init, nqlsdk_frame, nqlsdk_shutdown, etc.)
   - ~40+ functions documented
   - Remaining: Some static helper functions
   
2. **sdk_api_player.c** - All functions documented (30+ functions)
   - Player mechanics, hotbar, combat, firearms, throwables, vehicles

3. **sdk_benchmark.c** - All functions documented (5 functions)
   - Performance benchmarking for worldgen and mesh building

4. **sdk_camera.c** - All functions documented (6 functions)
   - Camera initialization, update, frustum culling, projection

5. **sdk_input.c** - All functions documented (20+ functions)
   - Input settings, key bindings, action detection, raw input

6. **sdk_profiler.c** - All functions documented (10 functions)
   - Frame profiling, zone timing, CSV logging

7. **sdk_math.c** - All functions documented (15 functions)
   - Vector operations (add, sub, dot, cross, normalize)
   - Matrix operations (identity, multiply, translation, scaling, rotation, perspective, look_at)

8. **sdk_entity.c** - Key public API functions documented (20+ functions)
   - Mob type queries (is_humanoid, is_vehicle, width, height, speed)
   - Entity spawning (items, mobs, shaped items)
   - Settlement control (set/clear control, targets)
   - Combat (player_attack)
   - Ticking (tick_all with AI, physics, pickup)

9. **sdk_api_interaction.c** - All functions documented (20+ functions)
   - Block collision queries (is_solid_at, aabb_collides, get_block_at)
   - Chunk dirty marking for mesh updates
   - Boundary water propagation and flood fill
   - Block placement/breaking with tool support
   - DDA voxel raycast

10. **sdk_creative_inventory.c** - All functions documented (8 functions)
    - Creative menu filtering and search
    - Entry selection and clamping

11. **sdk_map_input.c** - All functions documented (1 function)
    - Map pan and zoom input handling

12. **sdk_settings.c** - All functions documented (12 functions)
    - Graphics settings load/save
    - JSON parsing helpers
    - Settings validation and clamping

13. **sdk_frontend_menu.c** - Key functions documented (10+ functions)
    - World creation menu (seed input, settings normalization)
    - Text input helpers
    - Menu navigation and UI building

14. **sdk_crafting_ui.c** - All functions documented (12 functions)
    - Crafting grid dimensions and recipe matching
    - Taking results (single/bulk)
    - Station UI slot detection and input handling

15. **sdk_station_runtime.c** - All functions documented (25+ functions)
    - Station type queries (is_station_block, is_processing_station)
    - Fuel values and processing recipes
    - State management (find, ensure, remove, sync)
    - Persistence (load all, sync to persistence)
    - UI operations (open, close, take output, place items)
    - NPC interactions (place item, take output)
    - Tick processing (fuel, smelting, campfire cooking)

16. **sdk_frontend_worlds.c** - All functions documented (12 functions)
    - World save path builders (dir, save file, meta file)
    - Meta file load/save
    - World save list refresh and sorting
    - Legacy world migration
    - World creation with random/UI settings
    - Active world meta sync

17. **sdk_frontend_online.c** - All functions documented (6 functions)
    - Local host status queries (is_local_host, can_start, can_play)
    - Map generation eligibility
    - Online menu navigation

18. **sdk_frontend_async.c** - All functions documented (9 functions)
    - Async world map generation (begin, update, stop, cancel)
    - World session loading
    - Local host start
    - Internal helpers (load world desc, apply superchunk config)

19. **sdk_frontend_worldgen.c** - All functions documented (5 functions)
    - World generation from save meta (offline/bulk)
    - Scheduler initialization and progress updates
    - Superchunk config application

20. **sdk_server_runtime.c** - All functions documented (20+ functions)
    - JSON parsing helpers (skip ws, read file, trim, find key, parse string)
    - Server address normalization
    - Saved server management (load, upsert, delete)
    - Local host management (populate entry, can start, matches ID)
    - World launch (prepare, finalize)
    - Server runtime (reset, tick, on session stopped)

21. **sdk_api_session_bootstrap.c** - All functions documented (20+ functions)
    - Chunk loading (active wall stages, visible chunks, nearby chunks)
    - Chunk eviction and result adoption
    - Stream budgets and GPU upload limits
    - Wall support counting and health stats
    - Wall finalization

22. **sdk_api_session_core.c** - Partially documented (80+ functions total, key functions done)
    - Editor voxel buffer helpers
    - World spawn mode and render distance queries
    - World geometry release
    - Asset index finders (character, animation, prop, block, item, particle)
    - Startup readiness helpers
    - Wall corner mesh building

23. **sdk_api_crafting.c** - Placeholder file (no functions)

24. **sdk_api_frontend.c** - Placeholder file (no functions)

25. **sdk_api_session_debug.c** - All functions documented (3 functions)
    - Map debug compare shutdown and worker management
    - Tile comparison (exact vs fallback)

26. **sdk_skills_menu.c** - All functions documented (7 functions)
    - Skill point calculations (total, spent, available)
    - Tab row counting
    - Selection clamping and menu input handling
    - Progression reset

27. **sdk_api_session_map_render.c** - All functions documented (12 functions)
    - Map color utilities (clamp, shade, opaque, blend)
    - World/grid helpers (origin, gate run, wall band)
    - Block profile helpers (bedrock, ground)
    - Surface color computation

28. **sdk_session_headless.c** - Partially documented (15+ functions)
    - RNG utilities (xorshift, range)
    - Spawn selection helpers (wall band check, relief estimate, scoring)
    - Spawn choosers (center, random, safe)

29. **sdk_role_assets.c** - All functions documented (4 functions)
    - Character and prop asset lookup by ID
    - Role-to-asset resolution (character, prop)

30. **sdk_scene.c** - All functions documented (5 functions)
    - Scene initialization and update
    - Triangle position/scale setters
    - MVP matrix retrieval

31. **sdk_api_session_bootstrap_policy.h** - All functions documented (6 inline functions)
    - Chebyshev distance calculation
    - Bootstrap target classification (primary safety, neighbor, wall support)
    - Sync target predicates

32. **sdk_runtime_host.c** - All functions documented (2 functions)
    - Runtime host default initialization
    - Main run loop with init/shutdown lifecycle

33. **sdk_automation.c** - All functions documented (10 functions)
    - Text trimming (left/right)
    - Ready target parsing
    - Script management (init, free, append, load file)
    - Frame override control

## Remaining Files (11+)
**Large files to defer:**
- Core/API/Session/sdk_api_session_core.c (137KB, 80+ functions)
- Core/API/Session/sdk_api_session_map.c (78KB)
- Core/Frontend/sdk_frontend_assets.c (74KB)
- Core/MeshBuilder/sdk_mesh_builder.c (1621 lines)
- Core/PauseMenu/sdk_pause_menu.c (787 lines)

**Medium/Small files to prioritize:**
- Core/API/Session/sdk_api_session_map.c (check actual size)
- Core/Crafting/sdk_crafting_ui.c
- Core/Crafting/sdk_station_runtime.c
- Core/Entities/sdk_entity.c (partially done - continue)
- Core/World/Blocks/sdk_block.c (1038 lines - mostly data tables)
- Core/World/Buildings/sdk_building_family.c
- Remaining World/**/*.c files (check sizes)
