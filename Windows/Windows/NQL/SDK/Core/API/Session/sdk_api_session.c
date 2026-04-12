/*
 * sdk_api_session.c - Session management for SDK API
 * 
 * This file has been refactored into 6 modular files:
 * - sdk_api_session_internal.h: Shared declarations and structs
 * - sdk_api_session_core.c: Session lifecycle, spawn, editor, persistence
 * - sdk_api_session_map.c: Map tile I/O, scheduler, worker threads
 * - sdk_api_session_map_render.c: Map color rendering
 * - sdk_api_session_debug.c: Debug compare tools
 * - sdk_api_session_bootstrap.c: Chunk loading/eviction
 */

#include "sdk_api_session_internal.h"

/* Include all session modules */
#include "Assets/sdk_api_session_assets.c"
#include "GameModes/Editor/sdk_api_session_editor_helpers.c"
#include "GameModes/Editor/sdk_api_session_editor.c"
#include "GameModes/Editor/sdk_api_session_prop_editor.c"
#include "GameModes/GamePlay/Spawn/sdk_api_session_spawn.c"
#include "Core/sdk_api_session_generation.c"
#include "Core/sdk_api_session_core.c"
#include "GameModes/GamePlay/Map/sdk_api_session_map.c"
#include "GameModes/GamePlay/Map/sdk_api_session_map_render.c"
#include "Debug/sdk_api_session_debug.c"
#include "Bootstrap/sdk_api_session_bootstrap.c"

/* 
 * Global definitions - these need to be defined in exactly one translation unit
 * The internal header declares them as extern, this is where they're defined.
 */
int g_async_world_release_slot_cursor = 0;
int g_async_world_release_total = -1;
int g_async_world_release_freed = 0;
int g_async_world_release_force_abandon = 0;

SdkMapDebugCompareState g_map_debug_compare = {0};

