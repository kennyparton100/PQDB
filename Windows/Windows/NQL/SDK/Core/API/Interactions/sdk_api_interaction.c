#include "../Internal/sdk_api_internal.h"

/* ======================================================================
 * BLOCK COLLISION HELPERS
 * ====================================================================== */

bool is_solid_at(int wx, int wy, int wz)
{
    /* Queries whether a block at world coordinates is solid (has occupancy) */
    if (wy < 0 || wy >= CHUNK_HEIGHT) return false;
    return sdk_construction_world_cell_has_occupancy(&g_sdk.chunk_mgr, wx, wy, wz) != 0;
}

bool aabb_collides(float min_x, float min_y, float min_z,
                          float max_x, float max_y, float max_z)
{
    /* Checks if an AABB overlaps any solid block in the world */
    int bx0 = (int)floorf(min_x);
    int by0 = (int)floorf(min_y);
    int bz0 = (int)floorf(min_z);
    int bx1 = (int)floorf(max_x);
    int by1 = (int)floorf(max_y);
    int bz1 = (int)floorf(max_z);
    for (int by = by0; by <= by1; by++)
        for (int bz = bz0; bz <= bz1; bz++)
            for (int bx = bx0; bx <= bx1; bx++)
                if (sdk_construction_world_cell_intersects_aabb(&g_sdk.chunk_mgr,
                                                                bx, by, bz,
                                                                min_x, min_y, min_z,
                                                                max_x, max_y, max_z)) {
                    return true;
                }
    return false;
}


BlockType get_block_at(int wx, int wy, int wz)
{
    /* Returns the block type at world coordinates */
    if (wy < 0 || wy >= CHUNK_HEIGHT) return BLOCK_AIR;
    return sdk_construction_world_cell_display_material(&g_sdk.chunk_mgr, wx, wy, wz);
}

void mark_chunk_subchunk_dirty(int cx, int cz, int subchunk_index)
{
    /* Marks a specific subchunk as dirty for mesh rebuild */
    SdkChunk* chunk = sdk_chunk_manager_get_chunk(&g_sdk.chunk_mgr, cx, cz);
    if (!chunk) return;
    sdk_chunk_mark_subchunk_dirty(chunk, subchunk_index);
}

void mark_chunk_all_dirty_if_loaded(int cx, int cz)
{
    /* Marks all subchunks dirty if the chunk is loaded */
    SdkChunk* chunk = sdk_chunk_manager_get_chunk(&g_sdk.chunk_mgr, cx, cz);
    if (!chunk) return;
    sdk_chunk_mark_all_dirty(chunk);
}

static void mark_stream_seam_dirty_if_loaded(int cx, int cz)
{
    SdkChunk* chunk = sdk_chunk_manager_get_chunk(&g_sdk.chunk_mgr, cx, cz);

    if (!chunk) return;

    /* If a remesh job has already been queued for the current target generation,
     * skip re-marking to prevent infinite remesh cascades between neighbors.
     * We check mesh_job_generation since that's set immediately when marking dirty,
     * while inflight_mesh_generation isn't set until the job is actually queued. */
    if (chunk->mesh_job_generation == chunk->target_mesh_generation &&
        chunk->target_mesh_generation > 0) {
        return;
    }

    sdk_chunk_mark_all_dirty(chunk);
}

void mark_chunk_stream_neighbors_dirty(int cx, int cz)
{
    /* Marks chunk and cardinal neighbors dirty for local seam updates.
     * Diagonals are not part of chunk-face meshing and only create avoidable
     * remesh churn here. */
    mark_stream_seam_dirty_if_loaded(cx, cz);
    mark_stream_seam_dirty_if_loaded(cx - 1, cz);
    mark_stream_seam_dirty_if_loaded(cx + 1, cz);
    mark_stream_seam_dirty_if_loaded(cx, cz - 1);
    mark_stream_seam_dirty_if_loaded(cx, cz + 1);
}

void mark_chunk_stream_adjacent_neighbors_dirty(int cx, int cz)
{
    /* Freshly streamed chunks already have current mesh data. Only existing
     * loaded cardinals need seam refresh after the adoption boundary changes. */
    mark_stream_seam_dirty_if_loaded(cx - 1, cz);
    mark_stream_seam_dirty_if_loaded(cx + 1, cz);
    mark_stream_seam_dirty_if_loaded(cx, cz - 1);
    mark_stream_seam_dirty_if_loaded(cx, cz + 1);
}

int is_boundary_water_block(BlockType block)
{
    /* Returns 1 if block is water, ice, or sea ice (boundary water types) */
    return block == BLOCK_WATER || block == BLOCK_ICE || block == BLOCK_SEA_ICE;
}

int is_boundary_water_gap(BlockType block)
{
    /* Returns 1 if block is air or snow (can be filled with boundary water) */
    return block == BLOCK_AIR || block == BLOCK_SNOW;
}

int find_chunk_waterline(const SdkChunk* chunk, int lx, int lz, int* out_waterline, BlockType* out_cap)
{
    /* Finds highest water block in a column and returns waterline height */
    int ly;

    if (out_waterline) *out_waterline = -1;
    if (out_cap) *out_cap = BLOCK_AIR;
    if (!chunk || !chunk->blocks) return 0;
    if ((unsigned)lx >= CHUNK_WIDTH || (unsigned)lz >= CHUNK_DEPTH) return 0;

    for (ly = CHUNK_HEIGHT - 1; ly >= 0; --ly) {
        BlockType block = sdk_chunk_get_block(chunk, lx, ly, lz);
        if (!is_boundary_water_block(block)) continue;
        if (out_waterline) *out_waterline = ly;
        if (out_cap && (block == BLOCK_ICE || block == BLOCK_SEA_ICE)) {
            *out_cap = block;
        }
        return 1;
    }
    return 0;
}

int column_can_accept_boundary_water(const SdkChunk* chunk, int lx, int lz, int waterline)
{
    /* Checks if column can receive boundary water (has gap below waterline) */
    int ly;
    int gap_depth = 0;
    BlockType top;

    if (!chunk || !chunk->blocks) return 0;
    if ((unsigned)lx >= CHUNK_WIDTH || (unsigned)lz >= CHUNK_DEPTH) return 0;
    if (waterline < 1) return 0;
    if (waterline >= CHUNK_HEIGHT) waterline = CHUNK_HEIGHT - 1;

    top = sdk_chunk_get_block(chunk, lx, waterline, lz);
    if (!is_boundary_water_gap(top) && !is_boundary_water_block(top)) return 0;

    for (ly = waterline - 1; ly >= 0; --ly) {
        BlockType block = sdk_chunk_get_block(chunk, lx, ly, lz);
        if (is_boundary_water_block(block)) return 1;
        if (is_boundary_water_gap(block)) {
            gap_depth++;
            if (gap_depth > BOUNDARY_WATER_MAX_DEPTH) return 0;
            continue;
        }
        return 1;
    }
    return 0;
}

int fill_boundary_water_column_to_line(SdkChunk* chunk, int lx, int lz, int waterline, BlockType cap_block)
{
    /* Fills column with water up to specified line, with optional ice cap */
    int changed = 0;
    int ly;

    if (!chunk || !chunk->blocks) return 0;
    if ((unsigned)lx >= CHUNK_WIDTH || (unsigned)lz >= CHUNK_DEPTH) return 0;
    if (waterline < 0) return 0;
    if (waterline >= CHUNK_HEIGHT) waterline = CHUNK_HEIGHT - 1;

    if (cap_block == BLOCK_ICE || cap_block == BLOCK_SEA_ICE) {
        BlockType top = sdk_chunk_get_block(chunk, lx, waterline, lz);
        if (is_boundary_water_gap(top) || top == BLOCK_WATER) {
            sdk_chunk_set_block(chunk, lx, waterline, lz, cap_block);
            changed = 1;
        }
    } else {
        BlockType top = sdk_chunk_get_block(chunk, lx, waterline, lz);
        if (is_boundary_water_gap(top)) {
            sdk_chunk_set_block(chunk, lx, waterline, lz, BLOCK_WATER);
            changed = 1;
        }
    }

    for (ly = waterline - 1; ly >= 0; --ly) {
        BlockType block = sdk_chunk_get_block(chunk, lx, ly, lz);
        if (is_boundary_water_block(block)) continue;
        if (is_boundary_water_gap(block)) {
            sdk_chunk_set_block(chunk, lx, ly, lz, BLOCK_WATER);
            changed = 1;
            continue;
        }
        break;
    }

    return changed;
}

int boundary_side_supports_inherited_water(const SdkChunk* chunk, int side, int edge_index, int waterline)
{
    /* Checks if neighbor chunk supports water at boundary (side: 0=W,1=E,2=S,3=N) */
    int neighbor_cx;
    int neighbor_cz;
    int neighbor_lx;
    int neighbor_lz;
    SdkChunk* neighbor;
    int neighbor_line = -1;
    SdkTerrainColumnProfile profile;

    if (!chunk) return 0;

    neighbor_cx = chunk->cx;
    neighbor_cz = chunk->cz;
    neighbor_lx = 0;
    neighbor_lz = 0;

    switch (side) {
        case 0:
            neighbor_cx -= 1;
            neighbor_lx = CHUNK_WIDTH - 1;
            neighbor_lz = edge_index;
            break;
        case 1:
            neighbor_cx += 1;
            neighbor_lx = 0;
            neighbor_lz = edge_index;
            break;
        case 2:
            neighbor_cz -= 1;
            neighbor_lx = edge_index;
            neighbor_lz = CHUNK_DEPTH - 1;
            break;
        case 3:
            neighbor_cz += 1;
            neighbor_lx = edge_index;
            neighbor_lz = 0;
            break;
        default:
            return 0;
    }

    neighbor = sdk_chunk_manager_get_chunk(&g_sdk.chunk_mgr, neighbor_cx, neighbor_cz);
    if (neighbor && neighbor->blocks) {
        if (find_chunk_waterline(neighbor, neighbor_lx, neighbor_lz, &neighbor_line, NULL) &&
            neighbor_line >= waterline) {
            return 1;
        }
        return !column_can_accept_boundary_water(neighbor, neighbor_lx, neighbor_lz, waterline);
    }

    if (!sdk_worldgen_sample_column_ctx(&g_sdk.worldgen,
                                        neighbor_cx * CHUNK_WIDTH + neighbor_lx,
                                        neighbor_cz * CHUNK_DEPTH + neighbor_lz,
                                        &profile)) {
        return 0;
    }

    if (profile.surface_height >= waterline - 1) return 1;
    if (profile.water_height > profile.surface_height && profile.water_height >= waterline) return 1;
    return 0;
}

int boundary_fill_is_contained(const SdkChunk* chunk, int seed_side,
                                      const int target_line[CHUNK_DEPTH][CHUNK_WIDTH])
{
    /* Verifies water fill is contained by checking all 4 boundaries */
    int lx;
    int lz;

    if (!chunk) return 0;

    for (lz = 0; lz < CHUNK_DEPTH; ++lz) {
        for (lx = 0; lx < CHUNK_WIDTH; ++lx) {
            int waterline = target_line[lz][lx];
            if (waterline < 0) continue;

            if (lx == 0 && seed_side != 0 &&
                !boundary_side_supports_inherited_water(chunk, 0, lz, waterline)) {
                return 0;
            }
            if (lx == CHUNK_WIDTH - 1 && seed_side != 1 &&
                !boundary_side_supports_inherited_water(chunk, 1, lz, waterline)) {
                return 0;
            }
            if (lz == 0 && seed_side != 2 &&
                !boundary_side_supports_inherited_water(chunk, 2, lx, waterline)) {
                return 0;
            }
            if (lz == CHUNK_DEPTH - 1 && seed_side != 3 &&
                !boundary_side_supports_inherited_water(chunk, 3, lx, waterline)) {
                return 0;
            }
        }
    }

    return 1;
}

int boundary_barrier_fill_block(BlockType block)
{
    /* Returns replacement block for boundary barrier (subsoil for soft blocks) */
    switch (block) {
        case BLOCK_AIR:
        case BLOCK_WATER:
        case BLOCK_ICE:
        case BLOCK_SEA_ICE:
        case BLOCK_SNOW:
        case BLOCK_LEAVES:
        case BLOCK_LOG:
        case BLOCK_TURF:
        case BLOCK_GRASS:
        case BLOCK_WETLAND_SOD:
        case BLOCK_MARINE_MUD:
        case BLOCK_MARINE_SAND:
        case BLOCK_BEACH_GRAVEL:
            return BLOCK_SUBSOIL;
        default:
            return block;
    }
}

int find_boundary_barrier_surface(const SdkChunk* chunk, int lx, int lz)
{
    /* Finds top solid block in column for boundary barrier placement */
    int ly;

    if (!chunk || !chunk->blocks) return -1;
    for (ly = CHUNK_HEIGHT - 1; ly >= 0; --ly) {
        BlockType block = sdk_chunk_get_block(chunk, lx, ly, lz);
        if (is_boundary_water_gap(block) || is_boundary_water_block(block)) continue;
        return ly;
    }
    return -1;
}

int build_boundary_water_barrier_column(SdkChunk* chunk, int lx, int lz,
                                               int waterline, int inward_dx, int inward_dz)
{
    /* Builds barrier column to contain boundary water, with inward slope */
    int changed = 0;
    int surface = find_boundary_barrier_surface(chunk, lx, lz);
    int start_y = surface + 1;
    BlockType fill_block = BLOCK_SUBSOIL;
    int ly;
    int nx;
    int nz;

    if (!chunk || !chunk->blocks) return 0;
    if ((unsigned)lx >= CHUNK_WIDTH || (unsigned)lz >= CHUNK_DEPTH) return 0;
    if (waterline < 0) return 0;
    if (waterline >= CHUNK_HEIGHT) waterline = CHUNK_HEIGHT - 1;

    if (surface >= 0) {
        fill_block = (BlockType)boundary_barrier_fill_block(sdk_chunk_get_block(chunk, lx, surface, lz));
    }
    if (start_y < 0) start_y = 0;

    for (ly = start_y; ly <= waterline; ++ly) {
        BlockType block = sdk_chunk_get_block(chunk, lx, ly, lz);
        if (!is_boundary_water_gap(block) && !is_boundary_water_block(block)) continue;
        sdk_chunk_set_block(chunk, lx, ly, lz, fill_block);
        changed = 1;
    }

    nx = lx + inward_dx;
    nz = lz + inward_dz;
    if ((unsigned)nx < CHUNK_WIDTH && (unsigned)nz < CHUNK_DEPTH && waterline > 0) {
        int slope_surface = find_boundary_barrier_surface(chunk, nx, nz);
        int slope_top = waterline - 1;
        uint32_t hash = (uint32_t)((chunk->cx * 73856093) ^ (chunk->cz * 19349663) ^ (lx * 83492791) ^ (lz * 2971215073u));
        if ((hash & 1u) != 0u) slope_top -= 1;
        if (slope_top < 0) slope_top = 0;
        for (ly = slope_surface + 1; ly <= slope_top; ++ly) {
            BlockType block = sdk_chunk_get_block(chunk, nx, ly, nz);
            if (!is_boundary_water_gap(block) && !is_boundary_water_block(block)) continue;
            sdk_chunk_set_block(chunk, nx, ly, nz, fill_block);
            changed = 1;
        }
    }

    return changed;
}

int contain_boundary_fill(SdkChunk* chunk, int seed_side,
                                 const int target_line[CHUNK_DEPTH][CHUNK_WIDTH])
{
    /* Disabled: barrier building for boundary water (uses gravity update instead) */
    (void)chunk;
    (void)seed_side;
    (void)target_line;
    return 0;
}

int continue_boundary_water_from_neighbor(SdkChunk* chunk, const SdkChunk* neighbor, int side)
{
    /* Propagates boundary water from neighbor chunk using flood fill */
    int target_line[CHUNK_DEPTH][CHUNK_WIDTH];
    uint8_t target_cap[CHUNK_DEPTH][CHUNK_WIDTH];
    uint8_t queued[CHUNK_DEPTH][CHUNK_WIDTH];
    uint8_t qx[BOUNDARY_WATER_MAX_COLUMNS];
    uint8_t qz[BOUNDARY_WATER_MAX_COLUMNS];
    uint8_t qcap[BOUNDARY_WATER_MAX_COLUMNS];
    int qline[BOUNDARY_WATER_MAX_COLUMNS];
    int head = 0;
    int tail = 0;
    int changed = 0;
    int lx;
    int lz;

    if (!chunk || !neighbor || !chunk->blocks || !neighbor->blocks) return 0;
    memset(target_line, 0xFF, sizeof(target_line));
    memset(target_cap, 0, sizeof(target_cap));
    memset(queued, 0, sizeof(queued));

    if (side == 0 || side == 1) {
        for (lz = 0; lz < CHUNK_DEPTH; ++lz) {
            int neighbor_lx = (side == 0) ? (CHUNK_WIDTH - 1) : 0;
            int chunk_lx = (side == 0) ? 0 : (CHUNK_WIDTH - 1);
            int waterline = -1;
            BlockType cap = BLOCK_AIR;
            if (!find_chunk_waterline(neighbor, neighbor_lx, lz, &waterline, &cap)) continue;
            if (!column_can_accept_boundary_water(chunk, chunk_lx, lz, waterline)) continue;
            qx[tail] = (uint8_t)chunk_lx;
            qz[tail] = (uint8_t)lz;
            qline[tail] = waterline;
            qcap[tail] = (uint8_t)cap;
            queued[lz][chunk_lx] = 1u;
            tail++;
        }
    } else {
        for (lx = 0; lx < CHUNK_WIDTH; ++lx) {
            int neighbor_lz = (side == 2) ? (CHUNK_DEPTH - 1) : 0;
            int chunk_lz = (side == 2) ? 0 : (CHUNK_DEPTH - 1);
            int waterline = -1;
            BlockType cap = BLOCK_AIR;
            if (!find_chunk_waterline(neighbor, lx, neighbor_lz, &waterline, &cap)) continue;
            if (!column_can_accept_boundary_water(chunk, lx, chunk_lz, waterline)) continue;
            qx[tail] = (uint8_t)lx;
            qz[tail] = (uint8_t)chunk_lz;
            qline[tail] = waterline;
            qcap[tail] = (uint8_t)cap;
            queued[chunk_lz][lx] = 1u;
            tail++;
        }
    }

    while (head < tail && tail < BOUNDARY_WATER_MAX_COLUMNS) {
        static const int dx[4] = { -1, 1, 0, 0 };
        static const int dz[4] = { 0, 0, -1, 1 };
        int cx = qx[head];
        int cz = qz[head];
        int inherited_line = qline[head];
        uint8_t inherited_cap = qcap[head];
        int dir;
        head++;

        if (target_line[cz][cx] >= inherited_line) continue;
        if (!column_can_accept_boundary_water(chunk, cx, cz, inherited_line)) continue;

        target_line[cz][cx] = inherited_line;
        target_cap[cz][cx] = inherited_cap;

        for (dir = 0; dir < 4; ++dir) {
            int nx = cx + dx[dir];
            int nz = cz + dz[dir];
            if ((unsigned)nx >= CHUNK_WIDTH || (unsigned)nz >= CHUNK_DEPTH) continue;
            if (queued[nz][nx]) continue;
            if (!column_can_accept_boundary_water(chunk, nx, nz, inherited_line)) continue;
            qx[tail] = (uint8_t)nx;
            qz[tail] = (uint8_t)nz;
            qline[tail] = inherited_line;
            qcap[tail] = inherited_cap;
            queued[nz][nx] = 1u;
            tail++;
            if (tail >= BOUNDARY_WATER_MAX_COLUMNS) break;
        }
    }

    if (!boundary_fill_is_contained(chunk, side, target_line)) {
        changed |= contain_boundary_fill(chunk, side, target_line);
        if (!boundary_fill_is_contained(chunk, side, target_line)) {
            return changed;
        }
    }

    for (lz = 0; lz < CHUNK_DEPTH; ++lz) {
        for (lx = 0; lx < CHUNK_WIDTH; ++lx) {
            if (target_line[lz][lx] < 0) continue;
            if (fill_boundary_water_column_to_line(chunk, lx, lz, target_line[lz][lx], (BlockType)target_cap[lz][lx])) {
                changed = 1;
            }
        }
    }

    return changed;
}

void resolve_loaded_chunk_boundary_water(SdkChunk* chunk)
{
    /* Resolves boundary water from all 4 neighbors when chunk is loaded */
    int changed = 0;
    SdkChunk* west;
    SdkChunk* east;
    SdkChunk* north;
    SdkChunk* south;

    if (!chunk || !chunk->blocks) return;

    west = sdk_chunk_manager_get_chunk(&g_sdk.chunk_mgr, chunk->cx - 1, chunk->cz);
    east = sdk_chunk_manager_get_chunk(&g_sdk.chunk_mgr, chunk->cx + 1, chunk->cz);
    north = sdk_chunk_manager_get_chunk(&g_sdk.chunk_mgr, chunk->cx, chunk->cz - 1);
    south = sdk_chunk_manager_get_chunk(&g_sdk.chunk_mgr, chunk->cx, chunk->cz + 1);

    if (west) changed |= continue_boundary_water_from_neighbor(chunk, west, 0);
    if (east) changed |= continue_boundary_water_from_neighbor(chunk, east, 1);
    if (north) changed |= continue_boundary_water_from_neighbor(chunk, north, 2);
    if (south) changed |= continue_boundary_water_from_neighbor(chunk, south, 3);

    if (changed) {
        mark_chunk_stream_neighbors_dirty(chunk->cx, chunk->cz);
    }
}

void capture_persisted_state(SdkPersistedState* out_state);
void apply_persisted_state(const SdkPersistedState* state);
SdkChunk* generate_or_load_chunk_sync(int cx, int cz, SdkChunkResidencyRole role);
int is_station_block(BlockType type);
void station_handle_block_change(int wx, int wy, int wz, BlockType old_type, BlockType new_type);
void skills_clamp_selection(void);

void set_block_at(int wx, int wy, int wz, BlockType type)
{
    /* Sets block at world position, updating occupancy and notifying systems */
    uint64_t occupancy[SDK_CONSTRUCTION_OCCUPANCY_WORD_COUNT];
    BlockType old_type;
    if (editor_session_active() && !editor_block_in_bounds(wx, wy, wz)) return;
    if (wy < 0 || wy >= CHUNK_HEIGHT) return;
    int cx = sdk_world_to_chunk_x(wx);
    int cz = sdk_world_to_chunk_z(wz);
    SdkChunk* chunk = sdk_chunk_manager_get_chunk(&g_sdk.chunk_mgr, cx, cz);
    if (!chunk) return;
    int lx = sdk_world_to_local_x(wx, cx);
    int lz = sdk_world_to_local_z(wz, cz);
    old_type = sdk_construction_chunk_get_display_material(chunk, lx, wy, lz);
    if (type == BLOCK_AIR) sdk_construction_clear_occupancy(occupancy);
    else sdk_construction_fill_full_occupancy(occupancy);
    sdk_construction_chunk_set_cell_payload(chunk, lx, wy, lz, type, occupancy);
    if (old_type != type) {
        station_handle_block_change(wx, wy, wz, old_type, type);
        sdk_simulation_on_block_changed(&g_sdk.chunk_mgr, wx, wy, wz, old_type, type);
    }

    if (lx == 0) {
        mark_chunk_subchunk_dirty(cx - 1, cz, sdk_chunk_subchunk_index_from_y(wy));
    }
    if (lx == CHUNK_WIDTH - 1) {
        mark_chunk_subchunk_dirty(cx + 1, cz, sdk_chunk_subchunk_index_from_y(wy));
    }
    if (lz == 0) {
        mark_chunk_subchunk_dirty(cx, cz - 1, sdk_chunk_subchunk_index_from_y(wy));
    }
    if (lz == CHUNK_DEPTH - 1) {
        mark_chunk_subchunk_dirty(cx, cz + 1, sdk_chunk_subchunk_index_from_y(wy));
    }
}

static void damage_held_tool(HotbarEntry* held)
{
    /* Applies durability damage to held tool, breaking it if depleted */
    if (!held || held->count <= 0 || !sdk_item_is_tool(held->item)) return;
    held->durability--;
    if (held->durability <= 0) {
        clear_hotbar_entry(held);
    }
}

static void spawn_construction_fragments(const SdkConstructionEditOutcome* outcome)
{
    /* Spawns item drops for construction fragments from a cut/edit operation */
    int i;

    if (!outcome) return;
    for (i = 0; i < outcome->fragment_count; ++i) {
        const SdkConstructionFragment* fragment = &outcome->fragments[i];
        if (fragment->payload.occupied_count == 0u) continue;
        sdk_entity_spawn_shaped_item(&g_sdk.entities,
                                     (float)fragment->wx + 0.5f,
                                     (float)fragment->wy + 0.5f,
                                     (float)fragment->wz + 0.5f,
                                     (BlockType)fragment->payload.material,
                                     &fragment->payload);
    }
}

int sdk_actor_break_block(HotbarEntry* held, int wx, int wy, int wz, int spawn_drop)
{
    /* Breaks block at position using held tool, spawning drops if requested */
    int cx;
    int cz;
    int lx;
    int lz;
    SdkChunk* chunk;
    SdkWorldCellCode code;
    BlockType bt;
    ToolClass tool = TOOL_NONE;
    SdkConstructionEditOutcome outcome;

    if (wy < 0 || wy >= CHUNK_HEIGHT) return 0;
    cx = sdk_world_to_chunk_x(wx);
    cz = sdk_world_to_chunk_z(wz);
    chunk = sdk_chunk_manager_get_chunk(&g_sdk.chunk_mgr, cx, cz);
    if (!chunk) return 0;
    lx = sdk_world_to_local_x(wx, cx);
    lz = sdk_world_to_local_z(wz, cz);
    code = sdk_chunk_get_cell_code(chunk, lx, wy, lz);
    bt = sdk_construction_chunk_get_display_material(chunk, lx, wy, lz);
    if (bt == BLOCK_AIR || bt == BLOCK_WATER) return 0;
    if (held && held->count > 0 && sdk_item_is_tool(held->item)) {
        tool = sdk_item_get_tool_class(held->item);
    }

    if ((tool == TOOL_SAW || tool == TOOL_CHISEL) &&
        sdk_construction_cut_world_cell(&g_sdk.chunk_mgr, wx, wy, wz, g_last_hit_face, tool, &outcome)) {
        if (spawn_drop) spawn_construction_fragments(&outcome);
        damage_held_tool(held);
        return 1;
    }

    if (!sdk_world_cell_is_full_block(code) &&
        sdk_construction_remove_world_cell(&g_sdk.chunk_mgr, wx, wy, wz, &outcome)) {
        if (spawn_drop) spawn_construction_fragments(&outcome);
        damage_held_tool(held);
        return 1;
    }

    if (spawn_drop) {
        ItemType drop = sdk_block_to_item(bt);
        if (drop != ITEM_NONE) {
            sdk_entity_spawn_item(&g_sdk.entities, (float)wx + 0.5f, (float)wy + 0.5f, (float)wz + 0.5f, drop);
        }
    }

    set_block_at(wx, wy, wz, BLOCK_AIR);
    damage_held_tool(held);

    return 1;
}

int sdk_actor_place_block(HotbarEntry* held,
                          int wx, int wy, int wz,
                          float actor_x, float actor_feet_y, float actor_z,
                          float actor_half_w, float actor_height,
                          int enforce_actor_clearance)
{
    /* Places block at position from hotbar, respecting actor clearance and construction shapes */
    BlockType place_block = BLOCK_AIR;
    int consume_item = 0;
    int occupied;

    if (!held) return 0;
    occupied = sdk_construction_world_cell_has_occupancy(&g_sdk.chunk_mgr, wx, wy, wz);

    if (enforce_actor_clearance) {
        float pbx0 = actor_x - actor_half_w;
        float pbx1 = actor_x + actor_half_w;
        float pbz0 = actor_z - actor_half_w;
        float pbz1 = actor_z + actor_half_w;
        float pby0 = actor_feet_y;
        float pby1 = actor_feet_y + actor_height;
        int inside_actor = (wx + 1 > pbx0 && wx < pbx1 &&
                            wy + 1 > pby0 && wy < pby1 &&
                            wz + 1 > pbz0 && wz < pbz1);
        if (inside_actor) return 0;
    }

    if (held->payload_kind == SDK_ITEM_PAYLOAD_SHAPED_CONSTRUCTION &&
        held->shaped.occupied_count > 0u) {
        SdkConstructionPlacementResolution resolution;
        if (occupied) {
            int cx = sdk_world_to_chunk_x(wx);
            int cz = sdk_world_to_chunk_z(wz);
            SdkChunk* chunk = sdk_chunk_manager_get_chunk(&g_sdk.chunk_mgr, cx, cz);
            int lx = sdk_world_to_local_x(wx, cx);
            int lz = sdk_world_to_local_z(wz, cz);
            if (!chunk) return 0;
            if (sdk_world_cell_is_full_block(sdk_chunk_get_cell_code(chunk, lx, wy, lz)) &&
                sdk_chunk_get_block(chunk, lx, wy, lz) != BLOCK_AIR) {
                return 0;
            }
        }
        if (!g_last_hit_world_valid) {
            return 0;
        }
        memset(&resolution, 0, sizeof(resolution));
        if (!sdk_construction_resolve_face_placement(&g_sdk.chunk_mgr,
                                                     wx, wy, wz,
                                                     &held->shaped,
                                                     g_last_hit_face,
                                                     g_construction_place_rotation,
                                                     g_last_hit_world_x,
                                                     g_last_hit_world_y,
                                                     g_last_hit_world_z,
                                                     &resolution) ||
            !resolution.valid) {
            return 0;
        }
        if (!sdk_construction_place_payload(&g_sdk.chunk_mgr, wx, wy, wz,
                                            &resolution.resolved_payload, NULL)) {
            return 0;
        }
        held->count--;
        if (held->count <= 0) clear_hotbar_entry(held);
        return 1;
    }

    if (!hotbar_get_place_block(held, &place_block, &consume_item)) return 0;
    if (occupied) return 0;

    set_block_at(wx, wy, wz, place_block);
    if (consume_item) {
        held->count--;
        if (held->count <= 0) {
            clear_hotbar_entry(held);
        }
    }
    return 1;
}

bool raycast_block(
    float ox, float oy, float oz,       /* Ray origin */
    float dx, float dy, float dz,       /* Ray direction (normalised) */
    float max_dist,
    int* out_bx, int* out_by, int* out_bz,
    int* out_face,
    int* out_prev_bx, int* out_prev_by, int* out_prev_bz,
    float* out_hit_dist)
{
    /* DDA voxel raycast - finds first solid block along ray, returns hit info */
    /* Current voxel */
    int bx = (int)floorf(ox);
    int by = (int)floorf(oy);
    int bz = (int)floorf(oz);

    /* Step direction */
    int step_x = (dx > 0) ? 1 : (dx < 0) ? -1 : 0;
    int step_y = (dy > 0) ? 1 : (dy < 0) ? -1 : 0;
    int step_z = (dz > 0) ? 1 : (dz < 0) ? -1 : 0;

    /* Distance to next voxel boundary on each axis */
    float t_max_x = (dx != 0.0f) ? ((step_x > 0 ? (float)(bx + 1) - ox : ox - (float)bx) / fabsf(dx)) : 1e30f;
    float t_max_y = (dy != 0.0f) ? ((step_y > 0 ? (float)(by + 1) - oy : oy - (float)by) / fabsf(dy)) : 1e30f;
    float t_max_z = (dz != 0.0f) ? ((step_z > 0 ? (float)(bz + 1) - oz : oz - (float)bz) / fabsf(dz)) : 1e30f;

    /* How far along the ray we move for one full voxel step on each axis */
    float t_delta_x = (dx != 0.0f) ? (1.0f / fabsf(dx)) : 1e30f;
    float t_delta_y = (dy != 0.0f) ? (1.0f / fabsf(dy)) : 1e30f;
    float t_delta_z = (dz != 0.0f) ? (1.0f / fabsf(dz)) : 1e30f;

    float dist = 0.0f;
    int prev_bx = bx, prev_by = by, prev_bz = bz;
    int face = 3; /* POS_Y default */

    for (int i = 0; i < 200 && dist < max_dist; i++) {
        float local_hit_dist = 0.0f;
        int local_face = face;
        if (sdk_construction_world_cell_raycast(&g_sdk.chunk_mgr,
                                                bx, by, bz,
                                                ox, oy, oz,
                                                dx, dy, dz,
                                                max_dist,
                                                &local_hit_dist,
                                                &local_face)) {
            *out_bx = bx; *out_by = by; *out_bz = bz;
            *out_face = local_face;
            *out_prev_bx = prev_bx; *out_prev_by = prev_by; *out_prev_bz = prev_bz;
            if (out_hit_dist) *out_hit_dist = local_hit_dist;
            return true;
        }

        prev_bx = bx; prev_by = by; prev_bz = bz;

        /* Step to next voxel boundary */
        if (t_max_x < t_max_y && t_max_x < t_max_z) {
            dist = t_max_x;
            bx += step_x;
            t_max_x += t_delta_x;
            face = (step_x > 0) ? 0 : 1; /* NEG_X or POS_X */
        } else if (t_max_y < t_max_z) {
            dist = t_max_y;
            by += step_y;
            t_max_y += t_delta_y;
            face = (step_y > 0) ? 2 : 3; /* NEG_Y or POS_Y */
        } else {
            dist = t_max_z;
            bz += step_z;
            t_max_z += t_delta_z;
            face = (step_z > 0) ? 4 : 5; /* NEG_Z or POS_Z */
        }
    }
    return false;
}

