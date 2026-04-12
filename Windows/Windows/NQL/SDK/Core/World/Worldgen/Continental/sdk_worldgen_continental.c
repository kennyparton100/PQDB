/**
 * sdk_worldgen_continental.c -- Continental-scale climate and hydrology fields.
 */
#include "../Internal/sdk_worldgen_internal.h"
#include "../TileCache/sdk_worldgen_tile_cache.h"
#include "../SharedCache/sdk_worldgen_shared_cache.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

typedef struct {
    float x;
    float z;
    float vx;
    float vz;
    uint8_t plate_class;
    float hotspot;
} ContinentalPlateSite;

typedef struct {
    int idx;
    int16_t h;
} MinHeapEntry;

typedef struct {
    int idx;
    int16_t filled;
    int16_t raw;
    int16_t coast;
} FlowSortEntry;

static int floor_div_local(int value, int denom)
{
    if (denom <= 0) return 0;
    if (value >= 0) return value / denom;
    return (value - denom + 1) / denom;
}

static int stored_index(int x, int z)
{
    return z * SDK_WORLDGEN_CONTINENT_STRIDE + x;
}

static int analysis_index(int x, int z)
{
    return z * SDK_WORLDGEN_CONTINENT_ANALYSIS_STRIDE + x;
}

static int in_analysis_bounds(int x, int z)
{
    return x >= 0 && x < SDK_WORLDGEN_CONTINENT_ANALYSIS_STRIDE &&
           z >= 0 && z < SDK_WORLDGEN_CONTINENT_ANALYSIS_STRIDE;
}

static float bilerp4_local(float a, float b, float c, float d, float tx, float tz)
{
    float ab = a * (1.0f - tx) + b * tx;
    float cd = c * (1.0f - tx) + d * tx;
    return ab * (1.0f - tz) + cd * tz;
}

static float ramp01(float v, float lo, float hi)
{
    if (hi <= lo) return (v >= hi) ? 1.0f : 0.0f;
    return sdk_worldgen_clampf((v - lo) / (hi - lo), 0.0f, 1.0f);
}

static float pseudo_latitude_abs(int cell_z)
{
    float world_z = (float)(cell_z * SDK_WORLDGEN_CONTINENT_CELL_BLOCKS);
    float phase = fmodf(world_z / 262144.0f, 2.0f);
    if (phase < 0.0f) phase += 2.0f;
    return fabsf(phase - 1.0f);
}

static float pseudo_latitude_signed(int cell_z)
{
    float world_z = (float)(cell_z * SDK_WORLDGEN_CONTINENT_CELL_BLOCKS);
    float phase = fmodf(world_z / 262144.0f, 2.0f);
    if (phase < 0.0f) phase += 2.0f;
    return phase - 1.0f;
}

static void build_plate_site(int gx, int gz, uint32_t seed, ContinentalPlateSite* out_site)
{
    uint32_t h0 = sdk_worldgen_hash2d(gx, gz, seed ^ 0x5113u);
    uint32_t h1 = sdk_worldgen_hash2d(gx, gz, seed ^ 0x6227u);
    uint32_t h2 = sdk_worldgen_hash2d(gx, gz, seed ^ 0x7339u);
    uint32_t h3 = sdk_worldgen_hash2d(gx, gz, seed ^ 0x844Bu);
    float angle = sdk_worldgen_hashf(h2) * 6.2831853f;
    float speed = 0.18f + sdk_worldgen_hashf(h3) * 0.62f;
    float class_roll = sdk_worldgen_hashf(sdk_worldgen_hash2d(gx, gz, seed ^ 0x955Du));

    out_site->x = (float)gx + 0.18f + sdk_worldgen_hashf(h0) * 0.64f;
    out_site->z = (float)gz + 0.18f + sdk_worldgen_hashf(h1) * 0.64f;
    out_site->vx = cosf(angle) * speed;
    out_site->vz = sinf(angle) * speed;

    if (class_roll < 0.26f) out_site->plate_class = 0;
    else if (class_roll < 0.78f) out_site->plate_class = 1;
    else out_site->plate_class = 2;

    out_site->hotspot = sdk_worldgen_hashf(sdk_worldgen_hash2d(gx, gz, seed ^ 0xA66Fu));
}

static void sample_plate_frame(int cell_x, int cell_z, uint32_t seed,
                               uint8_t* out_plate_class,
                               int8_t* out_boundary_class,
                               float* out_boundary_strength,
                               float* out_hotspot)
{
    const int grid_size = 48;
    int gx = floor_div_local(cell_x, grid_size);
    int gz = floor_div_local(cell_z, grid_size);
    ContinentalPlateSite nearest;
    ContinentalPlateSite second;
    float best_d2 = 1.0e30f;
    float next_d2 = 1.0e30f;
    int ox;
    int oz;

    memset(&nearest, 0, sizeof(nearest));
    memset(&second, 0, sizeof(second));

    for (oz = -1; oz <= 1; ++oz) {
        for (ox = -1; ox <= 1; ++ox) {
            ContinentalPlateSite site;
            float sx;
            float sz;
            float dx;
            float dz;
            float d2;

            build_plate_site(gx + ox, gz + oz, seed, &site);
            sx = site.x * (float)grid_size;
            sz = site.z * (float)grid_size;
            dx = (float)cell_x - sx;
            dz = (float)cell_z - sz;
            d2 = dx * dx + dz * dz;

            if (d2 < best_d2) {
                second = nearest;
                next_d2 = best_d2;
                nearest = site;
                best_d2 = d2;
            } else if (d2 < next_d2) {
                second = site;
                next_d2 = d2;
            }
        }
    }

    *out_plate_class = nearest.plate_class;
    *out_hotspot = nearest.hotspot;
    *out_boundary_strength = 0.0f;
    *out_boundary_class = 0;

    if (next_d2 < 1.0e20f) {
        float px = second.x - nearest.x;
        float pz = second.z - nearest.z;
        float plen = sqrtf(px * px + pz * pz);
        float nx;
        float nz;
        float rel_vx;
        float rel_vz;
        float normal_motion;
        float delta = sqrtf(next_d2) - sqrtf(best_d2);
        float strength;

        if (plen < 0.001f) plen = 1.0f;
        nx = px / plen;
        nz = pz / plen;
        rel_vx = second.vx - nearest.vx;
        rel_vz = second.vz - nearest.vz;
        normal_motion = rel_vx * nx + rel_vz * nz;
        strength = 1.0f - sdk_worldgen_clampf(delta / ((float)grid_size * 0.92f), 0.0f, 1.0f);

        *out_boundary_strength = strength;
        if (strength > 0.18f) {
            if (normal_motion < -0.12f) *out_boundary_class = 1;
            else if (normal_motion > 0.12f) *out_boundary_class = -1;
        }
    }
}

static float sample_raw_elevation(const SdkWorldGenImpl* impl,
                                  int cell_x, int cell_z,
                                  uint8_t* out_plate_class,
                                  int8_t* out_boundary_class,
                                  float* out_boundary_strength)
{
    uint8_t plate_class = 0;
    int8_t boundary_class = 0;
    float boundary_strength = 0.0f;
    float hotspot = 0.0f;
    float continental_mask;
    float continental_detail;
    float archipelago;
    float hotspot_chain;
    float plateau_noise;
    float ridge_noise;
    float relief_noise;
    float continentality;
    float uplift = 0.0f;
    float subsidence = 0.0f;
    float ocean_depth_bias;
    float mountain_gate;
    float mountain_ridges;
    float mountain_breakup;
    float shield_noise;
    float shield_gain;
    float volcanic_gain;
    float raw;
    float sea = (float)impl->sea_level;

    sample_plate_frame(cell_x, cell_z, impl->seed, &plate_class, &boundary_class, &boundary_strength, &hotspot);

    continental_mask = sdk_worldgen_fbm((float)cell_x * 0.0105f, (float)cell_z * 0.0105f, impl->seed ^ 0x3011u, 5);
    continental_detail = sdk_worldgen_fbm((float)cell_x * 0.0230f, (float)cell_z * 0.0230f, impl->seed ^ 0x3122u, 3);
    archipelago = sdk_worldgen_ridged((float)cell_x * 0.0280f, (float)cell_z * 0.0280f, impl->seed ^ 0x3233u, 3);
    hotspot_chain = sdk_worldgen_ridged((float)cell_x * 0.0410f, (float)cell_z * 0.0180f, impl->seed ^ 0x3344u, 3);
    plateau_noise = sdk_worldgen_fbm((float)cell_x * 0.0155f, (float)cell_z * 0.0155f, impl->seed ^ 0x3455u, 3);
    ridge_noise = sdk_worldgen_ridged((float)cell_x * 0.0480f, (float)cell_z * 0.0480f, impl->seed ^ 0x3566u, 3);
    relief_noise = sdk_worldgen_fbm((float)cell_x * 0.0820f, (float)cell_z * 0.0820f, impl->seed ^ 0x3677u, 2);

    continentality = continental_mask * 0.74f + continental_detail * 0.18f;
    if (plate_class == 1) continentality += 0.44f;
    else if (plate_class == 2) continentality += 0.08f;
    else continentality -= 0.46f;
    continentality += (archipelago - 0.52f) * 0.20f;
    if (boundary_class < 0) continentality -= boundary_strength * 0.14f;
    if (hotspot > 0.92f) continentality += hotspot_chain * 0.14f + 0.08f;

    if (boundary_class > 0) {
        uplift += boundary_strength * (plate_class == 0 ? 110.0f : 165.0f) * (0.55f + ridge_noise * 0.45f);
    } else if (boundary_class < 0) {
        subsidence += boundary_strength * (plate_class == 1 ? 88.0f : 54.0f);
    } else {
        uplift += boundary_strength * 22.0f * fabsf(relief_noise);
    }

    if (hotspot > 0.94f) uplift += 58.0f * hotspot_chain;
    if (plate_class == 1 && plateau_noise > 0.16f) uplift += 26.0f * plateau_noise;

    raw = sea + continentality * 210.0f;
    if (continentality < 0.0f) raw = sea + continentality * 275.0f;
    raw += uplift - subsidence;
    raw += relief_noise * (continentality > 0.15f ? 14.0f : 7.0f);

    mountain_gate = ramp01(boundary_strength, 0.18f, 0.92f) * (boundary_class > 0 ? 1.0f : 0.35f);
    mountain_gate += ramp01(fabsf(continentality), 0.24f, 0.72f) * 0.16f;
    if (plate_class == 0 && boundary_class <= 0) mountain_gate *= 0.55f;
    mountain_gate = sdk_worldgen_clampf(mountain_gate, 0.0f, 1.0f);

    mountain_ridges = sdk_worldgen_ridged((float)cell_x * 0.0710f, (float)cell_z * 0.0710f, impl->seed ^ 0x3788u, 3);
    mountain_breakup = sdk_worldgen_fbm((float)cell_x * 0.1160f, (float)cell_z * 0.1160f, impl->seed ^ 0x3899u, 3);
    raw += ((mountain_ridges - 0.50f) * 138.0f + mountain_breakup * 28.0f) * mountain_gate;

    shield_noise = sdk_worldgen_ridged((float)cell_x * 0.0550f, (float)cell_z * 0.0550f, impl->seed ^ 0x39AAu, 2);
    shield_gain = ramp01(hotspot, 0.90f, 1.0f) * (16.0f + shield_noise * 42.0f);
    volcanic_gain = (plate_class == 0 && boundary_class > 0) ? boundary_strength * 28.0f : 0.0f;
    raw += shield_gain + volcanic_gain;

    ocean_depth_bias = ramp01(-continentality, 0.10f, 0.82f);
    raw -= ocean_depth_bias * (52.0f + ridge_noise * 82.0f);

    if (out_plate_class) *out_plate_class = plate_class;
    if (out_boundary_class) *out_boundary_class = boundary_class;
    if (out_boundary_strength) *out_boundary_strength = boundary_strength;
    return sdk_worldgen_clampf(raw, 4.0f, (float)CHUNK_HEIGHT - 24.0f);
}

static float sample_temperature(const SdkWorldGenImpl* impl, int cell_z, float raw_height)
{
    float lat = pseudo_latitude_abs(cell_z);
    float equator_heat = 1.0f - lat;
    float lapse = sdk_worldgen_clampf((raw_height - (float)impl->sea_level) / 320.0f, 0.0f, 0.78f);
    float temperature = equator_heat - lapse;
    return sdk_worldgen_clampf(temperature, 0.0f, 1.0f);
}

static void prevailing_wind(float lat_signed, float* out_dx, float* out_dz)
{
    float abs_lat = fabsf(lat_signed);
    float dx;
    float dz;
    float len;

    if (abs_lat < 0.28f) {
        dx = -0.92f;
        dz = (lat_signed >= 0.0f) ? -0.24f : 0.24f;
    } else if (abs_lat < 0.66f) {
        dx = 0.94f;
        dz = (lat_signed >= 0.0f) ? 0.18f : -0.18f;
    } else {
        dx = -0.82f;
        dz = (lat_signed >= 0.0f) ? 0.14f : -0.14f;
    }

    len = sqrtf(dx * dx + dz * dz);
    if (len < 0.0001f) {
        *out_dx = 1.0f;
        *out_dz = 0.0f;
        return;
    }

    *out_dx = dx / len;
    *out_dz = dz / len;
}

static void heap_push(MinHeapEntry* heap, int* size, int idx, int16_t h)
{
    int pos = (*size)++;
    heap[pos].idx = idx;
    heap[pos].h = h;

    while (pos > 0) {
        int parent = (pos - 1) >> 1;
        if (heap[parent].h <= heap[pos].h) break;
        {
            MinHeapEntry tmp = heap[parent];
            heap[parent] = heap[pos];
            heap[pos] = tmp;
        }
        pos = parent;
    }
}

static MinHeapEntry heap_pop(MinHeapEntry* heap, int* size)
{
    MinHeapEntry out = heap[0];
    heap[0] = heap[--(*size)];

    {
        int pos = 0;
        for (;;) {
            int left = pos * 2 + 1;
            int right = left + 1;
            int best = pos;

            if (left < *size && heap[left].h < heap[best].h) best = left;
            if (right < *size && heap[right].h < heap[best].h) best = right;
            if (best == pos) break;

            {
                MinHeapEntry tmp = heap[pos];
                heap[pos] = heap[best];
                heap[best] = tmp;
            }
            pos = best;
        }
    }

    return out;
}

static int flow_rank_desc(const void* a, const void* b)
{
    const FlowSortEntry* aa = (const FlowSortEntry*)a;
    const FlowSortEntry* bb = (const FlowSortEntry*)b;

    if (aa->filled > bb->filled) return -1;
    if (aa->filled < bb->filled) return 1;
    if (aa->raw > bb->raw) return -1;
    if (aa->raw < bb->raw) return 1;
    if (aa->coast > bb->coast) return -1;
    if (aa->coast < bb->coast) return 1;
    if (aa->idx < bb->idx) return -1;
    if (aa->idx > bb->idx) return 1;
    return 0;
}

static uint32_t basin_hash(int cell_x, int cell_z)
{
    return sdk_worldgen_hash32((uint32_t)cell_x * 0x9e3779b9u ^ ((uint32_t)cell_z * 0x85ebca6bu)) | 1u;
}

static int is_local_minimum(const int16_t* raw, int x, int z)
{
    int idx = analysis_index(x, z);
    int16_t h = raw[idx];
    int dx;
    int dz;

    for (dz = -1; dz <= 1; ++dz) {
        for (dx = -1; dx <= 1; ++dx) {
            int nx;
            int nz;
            if (dx == 0 && dz == 0) continue;
            nx = x + dx;
            nz = z + dz;
            if (!in_analysis_bounds(nx, nz)) continue;
            if (raw[analysis_index(nx, nz)] < h) return 0;
        }
    }
    return 1;
}

static void compute_ocean_connectivity(const int16_t* raw_height,
                                       int sea_level,
                                       uint8_t* out_ocean_mask,
                                       int* queue)
{
    int qhead = 0;
    int qtail = 0;
    int x;
    int z;

    if (!queue) {
        memset(out_ocean_mask, 0, SDK_WORLDGEN_CONTINENT_ANALYSIS_COUNT);
        return;
    }

    memset(out_ocean_mask, 0, SDK_WORLDGEN_CONTINENT_ANALYSIS_COUNT);

    for (z = 0; z < SDK_WORLDGEN_CONTINENT_ANALYSIS_STRIDE; ++z) {
        for (x = 0; x < SDK_WORLDGEN_CONTINENT_ANALYSIS_STRIDE; ++x) {
            int idx = analysis_index(x, z);
            int on_edge = (x == 0 || z == 0 ||
                           x == SDK_WORLDGEN_CONTINENT_ANALYSIS_STRIDE - 1 ||
                           z == SDK_WORLDGEN_CONTINENT_ANALYSIS_STRIDE - 1);
            if (!on_edge) continue;
            if (raw_height[idx] > sea_level) continue;
            out_ocean_mask[idx] = 1u;
            queue[qtail++] = idx;
        }
    }

    while (qhead < qtail) {
        static const int dx4[4] = { 1, -1, 0, 0 };
        static const int dz4[4] = { 0, 0, 1, -1 };
        int idx = queue[qhead++];
        int cx = idx % SDK_WORLDGEN_CONTINENT_ANALYSIS_STRIDE;
        int cz = idx / SDK_WORLDGEN_CONTINENT_ANALYSIS_STRIDE;
        int i;

        for (i = 0; i < 4; ++i) {
            int nx = cx + dx4[i];
            int nz = cz + dz4[i];
            int nidx;
            if (!in_analysis_bounds(nx, nz)) continue;
            nidx = analysis_index(nx, nz);
            if (out_ocean_mask[nidx]) continue;
            if (raw_height[nidx] > sea_level) continue;
            out_ocean_mask[nidx] = 1u;
            queue[qtail++] = nidx;
        }
    }
}

static void compute_coast_distance(const int16_t* raw_height,
                                   const uint8_t* ocean_mask,
                                   int sea_level,
                                   int16_t* out_coast_distance,
                                   int* queue)
{
    int qhead = 0;
    int qtail = 0;
    int x;
    int z;

    if (!queue) {
        for (z = 0; z < SDK_WORLDGEN_CONTINENT_ANALYSIS_STRIDE; ++z) {
            for (x = 0; x < SDK_WORLDGEN_CONTINENT_ANALYSIS_STRIDE; ++x) {
                out_coast_distance[analysis_index(x, z)] = 0;
            }
        }
        return;
    }

    for (z = 0; z < SDK_WORLDGEN_CONTINENT_ANALYSIS_STRIDE; ++z) {
        for (x = 0; x < SDK_WORLDGEN_CONTINENT_ANALYSIS_STRIDE; ++x) {
            int idx = analysis_index(x, z);
            int is_ocean = ocean_mask[idx] != 0u;
            int coastal = 0;
            static const int dx4[4] = { 1, -1, 0, 0 };
            static const int dz4[4] = { 0, 0, 1, -1 };
            int i;

            out_coast_distance[idx] = 32767;

            for (i = 0; i < 4 && !coastal; ++i) {
                int nx = x + dx4[i];
                int nz = z + dz4[i];
                int nidx;
                if (!in_analysis_bounds(nx, nz)) continue;
                nidx = analysis_index(nx, nz);
                if (is_ocean != (ocean_mask[nidx] != 0u)) coastal = 1;
            }

            if (coastal) {
                out_coast_distance[idx] = 0;
                queue[qtail++] = idx;
            }
        }
    }

    while (qhead < qtail) {
        static const int dx4[4] = { 1, -1, 0, 0 };
        static const int dz4[4] = { 0, 0, 1, -1 };
        int idx = queue[qhead++];
        int cx = idx % SDK_WORLDGEN_CONTINENT_ANALYSIS_STRIDE;
        int cz = idx / SDK_WORLDGEN_CONTINENT_ANALYSIS_STRIDE;
        int16_t base = out_coast_distance[idx];
        int i;

        for (i = 0; i < 4; ++i) {
            int nx = cx + dx4[i];
            int nz = cz + dz4[i];
            int nidx;
            int16_t candidate;
            if (!in_analysis_bounds(nx, nz)) continue;
            nidx = analysis_index(nx, nz);
            candidate = (int16_t)(base + 1);
            if (out_coast_distance[nidx] > candidate) {
                out_coast_distance[nidx] = candidate;
                queue[qtail++] = nidx;
            }
        }
    }

    for (z = 0; z < SDK_WORLDGEN_CONTINENT_ANALYSIS_STRIDE; ++z) {
        for (x = 0; x < SDK_WORLDGEN_CONTINENT_ANALYSIS_STRIDE; ++x) {
            int idx = analysis_index(x, z);
            if (ocean_mask[idx]) out_coast_distance[idx] = (int16_t)(-out_coast_distance[idx]);
            else if (raw_height[idx] <= sea_level && !ocean_mask[idx]) out_coast_distance[idx] = 0;
        }
    }
}

static void compute_precipitation_runoff(const SdkWorldGenImpl* impl,
                                         const int16_t* raw_height,
                                         const uint8_t* ocean_mask,
                                         const int16_t* coast_distance,
                                         uint8_t* out_precip,
                                         uint8_t* out_runoff,
                                         uint8_t* out_flatness)
{
    int x;
    int z;

    for (z = 0; z < SDK_WORLDGEN_CONTINENT_ANALYSIS_STRIDE; ++z) {
        for (x = 0; x < SDK_WORLDGEN_CONTINENT_ANALYSIS_STRIDE; ++x) {
            int idx = analysis_index(x, z);
            float lat_signed = pseudo_latitude_signed(z - SDK_WORLDGEN_CONTINENT_ANALYSIS_HALO);
            float abs_lat = fabsf(lat_signed);
            float wind_dx;
            float wind_dz;
            float band_humidity;
            float fetch = 0.0f;
            float fetch_norm = 0.0f;
            float barrier = 0.0f;
            float local_relief = 0.0f;
            float coastal = sdk_worldgen_clampf(1.0f - (float)abs(coast_distance[idx]) / 18.0f, 0.0f, 1.0f);
            float temperature = sample_temperature(impl,
                                                   z - SDK_WORLDGEN_CONTINENT_ANALYSIS_HALO,
                                                   (float)raw_height[idx]);
            float precip;
            float runoff;
            int dx;
            int dz;
            int step;

            prevailing_wind(lat_signed, &wind_dx, &wind_dz);

            for (step = 1; step <= 14; ++step) {
                int sx = x - (int)lrintf(wind_dx * (float)step);
                int sz = z - (int)lrintf(wind_dz * (float)step);
                float weight;
                if (!in_analysis_bounds(sx, sz)) break;
                weight = 1.0f - (float)(step - 1) / 14.0f;
                fetch_norm += weight;
                if (ocean_mask[analysis_index(sx, sz)]) fetch += weight;
                else {
                    float barrier_height = (float)raw_height[analysis_index(sx, sz)] - (float)raw_height[idx];
                    if (barrier_height > barrier) barrier = barrier_height;
                }
            }

            if (fetch_norm > 0.0001f) fetch /= fetch_norm;

            if (abs_lat < 0.22f) band_humidity = 0.30f;
            else if (abs_lat < 0.36f) band_humidity = 0.08f;
            else if (abs_lat < 0.70f) band_humidity = 0.18f;
            else band_humidity = 0.04f;

            for (dz = -1; dz <= 1; ++dz) {
                for (dx = -1; dx <= 1; ++dx) {
                    int nx = x + dx;
                    int nz = z + dz;
                    int nidx;
                    if (!in_analysis_bounds(nx, nz)) continue;
                    nidx = analysis_index(nx, nz);
                    if (raw_height[nidx] > raw_height[idx]) {
                        local_relief = fmaxf(local_relief, (float)(raw_height[nidx] - raw_height[idx]));
                    }
                }
            }

            precip = 0.12f + band_humidity;
            precip += fetch * 0.42f;
            precip += coastal * 0.18f;
            precip += sdk_worldgen_clampf(local_relief / 220.0f, 0.0f, 0.16f);
            precip -= sdk_worldgen_clampf(barrier / 260.0f, 0.0f, 0.34f);
            precip += sdk_worldgen_clampf((0.55f - temperature) * 0.12f, 0.0f, 0.10f);

            if (ocean_mask[idx]) precip = fmaxf(precip, 0.55f);
            precip = sdk_worldgen_clampf(precip, 0.0f, 1.0f);

            runoff = precip * (0.52f + sdk_worldgen_clampf(local_relief / 110.0f, 0.0f, 0.28f));
            runoff += coastal * 0.08f;
            runoff -= temperature * 0.14f;
            if (ocean_mask[idx]) runoff = 1.0f;
            runoff = sdk_worldgen_clampf(runoff, 0.0f, 1.0f);

            out_precip[idx] = sdk_worldgen_pack_unorm8(precip);
            out_runoff[idx] = sdk_worldgen_pack_unorm8(runoff);
            out_flatness[idx] = sdk_worldgen_pack_unorm8(1.0f - sdk_worldgen_clampf(local_relief / 90.0f, 0.0f, 1.0f));
        }
    }
}

static void fill_depressions(const int16_t* raw_height,
                             int16_t* out_filled,
                             MinHeapEntry* heap,
                             uint8_t* visited)
{
    int heap_size = 0;
    int x;
    int z;

    if (!heap || !visited) {
        if (out_filled != raw_height) {
            memcpy(out_filled, raw_height, sizeof(int16_t) * SDK_WORLDGEN_CONTINENT_ANALYSIS_COUNT);
        }
        return;
    }

    memcpy(out_filled, raw_height, sizeof(int16_t) * SDK_WORLDGEN_CONTINENT_ANALYSIS_COUNT);
    memset(visited, 0, SDK_WORLDGEN_CONTINENT_ANALYSIS_COUNT);

    for (z = 0; z < SDK_WORLDGEN_CONTINENT_ANALYSIS_STRIDE; ++z) {
        for (x = 0; x < SDK_WORLDGEN_CONTINENT_ANALYSIS_STRIDE; ++x) {
            int idx = analysis_index(x, z);
            int on_edge = (x == 0 || z == 0 ||
                           x == SDK_WORLDGEN_CONTINENT_ANALYSIS_STRIDE - 1 ||
                           z == SDK_WORLDGEN_CONTINENT_ANALYSIS_STRIDE - 1);
            if (!on_edge) continue;
            visited[idx] = 1u;
            heap_push(heap, &heap_size, idx, raw_height[idx]);
        }
    }

    while (heap_size > 0) {
        static const int dx4[4] = { 1, -1, 0, 0 };
        static const int dz4[4] = { 0, 0, 1, -1 };
        MinHeapEntry cur = heap_pop(heap, &heap_size);
        int cx = cur.idx % SDK_WORLDGEN_CONTINENT_ANALYSIS_STRIDE;
        int cz = cur.idx / SDK_WORLDGEN_CONTINENT_ANALYSIS_STRIDE;
        int i;

        for (i = 0; i < 4; ++i) {
            int nx = cx + dx4[i];
            int nz = cz + dz4[i];
            int nidx;
            int16_t filled;

            if (!in_analysis_bounds(nx, nz)) continue;
            nidx = analysis_index(nx, nz);
            if (visited[nidx]) continue;

            visited[nidx] = 1u;
            filled = raw_height[nidx];
            if (filled < cur.h) filled = cur.h;
            out_filled[nidx] = filled;
            heap_push(heap, &heap_size, nidx, filled);
        }
    }
}

static void compute_drainage(const SdkWorldGenImpl* impl,
                             const int16_t* raw_height,
                             const int16_t* filled_height,
                             const int16_t* coast_distance,
                             const uint8_t* ocean_mask,
                             const uint8_t* precip,
                             const uint8_t* runoff,
                             const uint8_t* flatness,
                             int* out_downstream,
                             uint32_t* out_flow_accum,
                             uint32_t* out_basin_id,
                             uint8_t* out_trunk_order,
                             uint8_t* out_lake_mask,
                             uint8_t* out_closed_mask,
                             int16_t* out_lake_level,
                             uint16_t* out_lake_id,
                             uint8_t* out_water_access,
                             uint8_t* out_harbor_score,
                             uint8_t* out_confluence,
                             uint8_t* out_flood_risk,
                             FlowSortEntry* sort_entries,
                             int* stack)
{
    int x;
    int z;

    if (!sort_entries || !stack) {
        memset(out_downstream, 0xff, sizeof(int) * SDK_WORLDGEN_CONTINENT_ANALYSIS_COUNT);
        memset(out_flow_accum, 0, sizeof(uint32_t) * SDK_WORLDGEN_CONTINENT_ANALYSIS_COUNT);
        memset(out_basin_id, 0, sizeof(uint32_t) * SDK_WORLDGEN_CONTINENT_ANALYSIS_COUNT);
        memset(out_trunk_order, 0, SDK_WORLDGEN_CONTINENT_ANALYSIS_COUNT);
        memset(out_lake_mask, 0, SDK_WORLDGEN_CONTINENT_ANALYSIS_COUNT);
        memset(out_closed_mask, 0, SDK_WORLDGEN_CONTINENT_ANALYSIS_COUNT);
        memset(out_lake_level, 0, sizeof(int16_t) * SDK_WORLDGEN_CONTINENT_ANALYSIS_COUNT);
        memset(out_lake_id, 0, sizeof(uint16_t) * SDK_WORLDGEN_CONTINENT_ANALYSIS_COUNT);
        memset(out_water_access, 0, SDK_WORLDGEN_CONTINENT_ANALYSIS_COUNT);
        memset(out_harbor_score, 0, SDK_WORLDGEN_CONTINENT_ANALYSIS_COUNT);
        memset(out_confluence, 0, SDK_WORLDGEN_CONTINENT_ANALYSIS_COUNT);
        memset(out_flood_risk, 0, SDK_WORLDGEN_CONTINENT_ANALYSIS_COUNT);
        return;
    }

    for (z = 0; z < SDK_WORLDGEN_CONTINENT_ANALYSIS_STRIDE; ++z) {
        for (x = 0; x < SDK_WORLDGEN_CONTINENT_ANALYSIS_STRIDE; ++x) {
            int idx = analysis_index(x, z);
            int16_t spill_depth = (int16_t)(filled_height[idx] - raw_height[idx]);
            int lake_candidate = 0;
            int closed_candidate = 0;
            float lake_strength = 0.0f;
            float closed_strength = 0.0f;
            int best = -1;
            int dx;
            int dz;

            out_flow_accum[idx] = ocean_mask[idx] ? 0u :
                (uint32_t)(1u +
                           (uint32_t)(sdk_worldgen_unpack_unorm8(runoff[idx]) * 6.0f) +
                           (uint32_t)(sdk_worldgen_unpack_unorm8(precip[idx]) * 4.0f));
            out_downstream[idx] = -1;
            out_basin_id[idx] = 0u;
            out_lake_level[idx] = 0;
            out_lake_id[idx] = 0u;
            out_lake_mask[idx] = 0u;
            out_closed_mask[idx] = 0u;
            out_harbor_score[idx] = 0u;
            out_confluence[idx] = 0u;
            out_flood_risk[idx] = 0u;

            if (spill_depth > 2 && !ocean_mask[idx] && raw_height[idx] > impl->sea_level) {
                float inland = sdk_worldgen_clampf(((float)coast_distance[idx] - 3.0f) / 14.0f, 0.0f, 1.0f);
                float wetness = sdk_worldgen_unpack_unorm8(precip[idx]);
                float aridity = 1.0f - wetness;

                lake_strength = sdk_worldgen_clampf(((float)spill_depth - 2.0f) / 12.0f, 0.0f, 1.0f);
                lake_strength *= sdk_worldgen_clampf(0.35f + inland * 0.65f, 0.0f, 1.0f);
                lake_strength *= sdk_worldgen_clampf(0.45f + wetness * 0.45f, 0.0f, 1.0f);

                closed_strength = lake_strength *
                    sdk_worldgen_clampf(((float)coast_distance[idx] - 10.0f) / 20.0f, 0.0f, 1.0f) *
                    sdk_worldgen_clampf(0.40f + aridity * 0.80f, 0.0f, 1.0f);

                if (is_local_minimum(raw_height, x, z)) {
                    lake_candidate = (lake_strength > 0.06f);
                    closed_candidate = (closed_strength > 0.14f);
                }
            }

            if (raw_height[idx] <= impl->sea_level && !ocean_mask[idx]) {
                lake_candidate = 1;
                closed_candidate = 1;
                lake_strength = fmaxf(lake_strength, 0.72f);
                closed_strength = fmaxf(closed_strength, 0.62f);
            }

            if (lake_candidate) {
                out_lake_level[idx] = filled_height[idx];
                out_lake_mask[idx] = sdk_worldgen_pack_unorm8(lake_strength);
                if (closed_candidate) out_closed_mask[idx] = sdk_worldgen_pack_unorm8(closed_strength);
                out_lake_id[idx] = (uint16_t)((basin_hash(x, z) & 0xfffeu) | 1u);
            }

            if (ocean_mask[idx] || out_closed_mask[idx] != 0u) {
                sort_entries[idx].idx = idx;
                sort_entries[idx].filled = filled_height[idx];
                sort_entries[idx].raw = raw_height[idx];
                sort_entries[idx].coast = coast_distance[idx];
                continue;
            }

            for (dz = -1; dz <= 1; ++dz) {
                for (dx = -1; dx <= 1; ++dx) {
                    int nx;
                    int nz;
                    int nidx;
                    int better = 0;
                    if (dx == 0 && dz == 0) continue;
                    nx = x + dx;
                    nz = z + dz;
                    if (!in_analysis_bounds(nx, nz)) continue;
                    nidx = analysis_index(nx, nz);

                    if (filled_height[nidx] < filled_height[idx]) better = 1;
                    else if (filled_height[nidx] == filled_height[idx] && raw_height[nidx] < raw_height[idx]) better = 1;
                    else if (filled_height[nidx] == filled_height[idx] &&
                             raw_height[nidx] == raw_height[idx] &&
                             coast_distance[nidx] < coast_distance[idx]) better = 1;
                    else if (filled_height[nidx] == filled_height[idx] &&
                             raw_height[nidx] == raw_height[idx] &&
                             coast_distance[nidx] == coast_distance[idx] &&
                             nidx < idx) better = 1;

                    if (!better) continue;
                    if (best < 0) {
                        best = nidx;
                        continue;
                    }

                    if (filled_height[nidx] < filled_height[best]) best = nidx;
                    else if (filled_height[nidx] == filled_height[best] && raw_height[nidx] < raw_height[best]) best = nidx;
                    else if (filled_height[nidx] == filled_height[best] &&
                             raw_height[nidx] == raw_height[best] &&
                             coast_distance[nidx] < coast_distance[best]) best = nidx;
                    else if (filled_height[nidx] == filled_height[best] &&
                             raw_height[nidx] == raw_height[best] &&
                             coast_distance[nidx] == coast_distance[best] &&
                             nidx < best) best = nidx;
                }
            }

            out_downstream[idx] = best;
            sort_entries[idx].idx = idx;
            sort_entries[idx].filled = filled_height[idx];
            sort_entries[idx].raw = raw_height[idx];
            sort_entries[idx].coast = coast_distance[idx];
        }
    }

    qsort(sort_entries, SDK_WORLDGEN_CONTINENT_ANALYSIS_COUNT, sizeof(sort_entries[0]), flow_rank_desc);

    for (x = 0; x < SDK_WORLDGEN_CONTINENT_ANALYSIS_COUNT; ++x) {
        int idx = sort_entries[x].idx;
        int down = out_downstream[idx];
        if (down >= 0) out_flow_accum[down] += out_flow_accum[idx];
    }

    for (x = 0; x < SDK_WORLDGEN_CONTINENT_ANALYSIS_COUNT; ++x) {
        int idx = sort_entries[x].idx;
        uint32_t flow = out_flow_accum[idx];
        if (flow > 6500u) out_trunk_order[idx] = 4u;
        else if (flow > 2800u) out_trunk_order[idx] = 3u;
        else if (flow > 1100u) out_trunk_order[idx] = 2u;
        else if (flow > 320u) out_trunk_order[idx] = 1u;
        else out_trunk_order[idx] = 0u;
    }

    for (x = 0; x < SDK_WORLDGEN_CONTINENT_ANALYSIS_COUNT; ++x) {
        int idx = sort_entries[x].idx;
        if (out_basin_id[idx] != 0u) continue;

        {
            int top = 0;
            int cur = idx;
            uint32_t basin = 0u;

            while (cur >= 0 && out_basin_id[cur] == 0u) {
                stack[top++] = cur;
                if (ocean_mask[cur]) {
                    int cx = cur % SDK_WORLDGEN_CONTINENT_ANALYSIS_STRIDE;
                    int cz = cur / SDK_WORLDGEN_CONTINENT_ANALYSIS_STRIDE;
                    basin = basin_hash(cx, cz);
                    break;
                }
                if (out_downstream[cur] < 0 || out_downstream[cur] == cur) {
                    int cx = cur % SDK_WORLDGEN_CONTINENT_ANALYSIS_STRIDE;
                    int cz = cur / SDK_WORLDGEN_CONTINENT_ANALYSIS_STRIDE;
                    basin = basin_hash(cx, cz);
                    break;
                }
                cur = out_downstream[cur];
            }

            if (cur >= 0 && out_basin_id[cur] != 0u) basin = out_basin_id[cur];
            if (basin == 0u) {
                basin = basin_hash(idx % SDK_WORLDGEN_CONTINENT_ANALYSIS_STRIDE,
                                   idx / SDK_WORLDGEN_CONTINENT_ANALYSIS_STRIDE);
            }

            while (top > 0) out_basin_id[stack[--top]] = basin;
        }
    }

    for (z = 0; z < SDK_WORLDGEN_CONTINENT_ANALYSIS_STRIDE; ++z) {
        for (x = 0; x < SDK_WORLDGEN_CONTINENT_ANALYSIS_STRIDE; ++x) {
            int idx = analysis_index(x, z);
            float river_factor = sdk_worldgen_clampf((float)out_flow_accum[idx] / 6500.0f, 0.0f, 1.0f);
            float coast_access = ocean_mask[idx] ? 1.0f :
                sdk_worldgen_clampf(1.0f - (float)abs(coast_distance[idx]) / 10.0f, 0.0f, 1.0f);
            float lake_access = sdk_worldgen_unpack_unorm8(out_lake_mask[idx]) * 0.85f;
            int tributaries = 0;
            int i;

            out_water_access[idx] = sdk_worldgen_pack_unorm8(fmaxf(coast_access, fmaxf(river_factor, lake_access)));

            if (!ocean_mask[idx] && raw_height[idx] > impl->sea_level &&
                coast_distance[idx] >= 0 && coast_distance[idx] <= 2) {
                float harbor = coast_access *
                    sdk_worldgen_unpack_unorm8(flatness[idx]) *
                    sdk_worldgen_clampf(1.0f - sdk_worldgen_unpack_unorm8(runoff[idx]) * 0.35f, 0.0f, 1.0f);
                out_harbor_score[idx] = sdk_worldgen_pack_unorm8(harbor);
            }

            for (i = 0; i < 8; ++i) {
                static const int dx8[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
                static const int dz8[8] = { -1, -1, 0, 1, 1, 1, 0, -1 };
                int nx = x + dx8[i];
                int nz = z + dz8[i];
                int nidx;
                if (!in_analysis_bounds(nx, nz)) continue;
                nidx = analysis_index(nx, nz);
                if (out_downstream[nidx] == idx && out_flow_accum[nidx] > 420u) ++tributaries;
            }

            if (tributaries >= 2) {
                float confluence = sdk_worldgen_clampf((float)(tributaries - 1) * 0.42f + river_factor * 0.35f, 0.0f, 1.0f);
                out_confluence[idx] = sdk_worldgen_pack_unorm8(confluence);
            }

            {
                float flood = river_factor * sdk_worldgen_clampf(1.0f - sdk_worldgen_unpack_unorm8(flatness[idx]) * 0.35f, 0.0f, 1.0f);
                flood += sdk_worldgen_unpack_unorm8(out_lake_mask[idx]) * 0.35f;
                flood += coast_access * 0.16f;
                out_flood_risk[idx] = sdk_worldgen_pack_unorm8(sdk_worldgen_clampf(flood, 0.0f, 1.0f));
            }
        }
    }
}

static SdkWorldGenImpl* worldgen_impl(SdkWorldGen* wg)
{
    if (!wg) return NULL;
    return (SdkWorldGenImpl*)wg->impl;
}

SdkWorldGenContinentalTile* sdk_worldgen_require_continental_tile(SdkWorldGen* wg, int wx, int wz)
{
    return sdk_worldgen_shared_get_continental_tile(wg, wx, wz);
}

const SdkContinentalCell* sdk_worldgen_get_continental_cell(SdkWorldGen* wg, int cell_x, int cell_z)
{
    SdkWorldGenContinentalTile* tile;
    int tile_x;
    int tile_z;
    int local_x;
    int local_z;

    if (!wg || !wg->impl) return NULL;

    tile_x = floor_div_local(cell_x, SDK_WORLDGEN_CONTINENT_TILE_SIZE);
    tile_z = floor_div_local(cell_z, SDK_WORLDGEN_CONTINENT_TILE_SIZE);
    tile = sdk_worldgen_require_continental_tile(wg,
                                                 cell_x * SDK_WORLDGEN_CONTINENT_CELL_BLOCKS,
                                                 cell_z * SDK_WORLDGEN_CONTINENT_CELL_BLOCKS);
    if (!tile) return NULL;

    local_x = cell_x - tile_x * SDK_WORLDGEN_CONTINENT_TILE_SIZE + SDK_WORLDGEN_CONTINENT_TILE_HALO;
    local_z = cell_z - tile_z * SDK_WORLDGEN_CONTINENT_TILE_SIZE + SDK_WORLDGEN_CONTINENT_TILE_HALO;
    local_x = sdk_worldgen_clampi(local_x, 0, SDK_WORLDGEN_CONTINENT_STRIDE - 1);
    local_z = sdk_worldgen_clampi(local_z, 0, SDK_WORLDGEN_CONTINENT_STRIDE - 1);
    return &tile->cells[stored_index(local_x, local_z)];
}

void sdk_worldgen_sample_continental_state(SdkWorldGen* wg, int wx, int wz, SdkContinentalSample* out_sample)
{
    SdkWorldGenImpl* impl = worldgen_impl(wg);
    float sample_x;
    float sample_z;
    int ix;
    int iz;
    float tx;
    float tz;
    float w00;
    float w10;
    float w01;
    float w11;
    const SdkContinentalCell* c00;
    const SdkContinentalCell* c10;
    const SdkContinentalCell* c01;
    const SdkContinentalCell* c11;
    const SdkContinentalCell* nearest = NULL;
    float best_weight = -1.0f;
    float ocean_weight;
    float lake_weight;
    float closed_weight;

    if (!out_sample) return;
    memset(out_sample, 0, sizeof(*out_sample));
    if (!wg || !impl) return;

    sample_x = ((float)wx / (float)SDK_WORLDGEN_CONTINENT_CELL_BLOCKS) - 0.5f;
    sample_z = ((float)wz / (float)SDK_WORLDGEN_CONTINENT_CELL_BLOCKS) - 0.5f;
    ix = (int)floorf(sample_x);
    iz = (int)floorf(sample_z);
    tx = sdk_worldgen_clampf(sample_x - (float)ix, 0.0f, 1.0f);
    tz = sdk_worldgen_clampf(sample_z - (float)iz, 0.0f, 1.0f);
    w00 = (1.0f - tx) * (1.0f - tz);
    w10 = tx * (1.0f - tz);
    w01 = (1.0f - tx) * tz;
    w11 = tx * tz;

    c00 = sdk_worldgen_get_continental_cell(wg, ix,     iz);
    c10 = sdk_worldgen_get_continental_cell(wg, ix + 1, iz);
    c01 = sdk_worldgen_get_continental_cell(wg, ix,     iz + 1);
    c11 = sdk_worldgen_get_continental_cell(wg, ix + 1, iz + 1);
    if (!c00 || !c10 || !c01 || !c11) return;

    out_sample->raw_height = bilerp4_local((float)c00->raw_height, (float)c10->raw_height,
                                           (float)c01->raw_height, (float)c11->raw_height, tx, tz);
    out_sample->filled_height = bilerp4_local((float)c00->filled_height, (float)c10->filled_height,
                                              (float)c01->filled_height, (float)c11->filled_height, tx, tz);
    out_sample->lake_level = bilerp4_local((float)c00->lake_level, (float)c10->lake_level,
                                           (float)c01->lake_level, (float)c11->lake_level, tx, tz);
    out_sample->coast_distance = bilerp4_local((float)c00->coast_distance, (float)c10->coast_distance,
                                               (float)c01->coast_distance, (float)c11->coast_distance, tx, tz);
    out_sample->flow_accum = bilerp4_local((float)c00->flow_accum, (float)c10->flow_accum,
                                           (float)c01->flow_accum, (float)c11->flow_accum, tx, tz);
    out_sample->precipitation = bilerp4_local(sdk_worldgen_unpack_unorm8(c00->precipitation), sdk_worldgen_unpack_unorm8(c10->precipitation),
                                              sdk_worldgen_unpack_unorm8(c01->precipitation), sdk_worldgen_unpack_unorm8(c11->precipitation),
                                              tx, tz);
    out_sample->runoff = bilerp4_local(sdk_worldgen_unpack_unorm8(c00->runoff), sdk_worldgen_unpack_unorm8(c10->runoff),
                                       sdk_worldgen_unpack_unorm8(c01->runoff), sdk_worldgen_unpack_unorm8(c11->runoff),
                                       tx, tz);
    out_sample->trunk_river_order = bilerp4_local((float)c00->trunk_river_order, (float)c10->trunk_river_order,
                                                  (float)c01->trunk_river_order, (float)c11->trunk_river_order, tx, tz);
    out_sample->water_access = bilerp4_local(sdk_worldgen_unpack_unorm8(c00->water_access), sdk_worldgen_unpack_unorm8(c10->water_access),
                                             sdk_worldgen_unpack_unorm8(c01->water_access), sdk_worldgen_unpack_unorm8(c11->water_access),
                                             tx, tz);
    out_sample->harbor_score = bilerp4_local(sdk_worldgen_unpack_unorm8(c00->harbor_score), sdk_worldgen_unpack_unorm8(c10->harbor_score),
                                             sdk_worldgen_unpack_unorm8(c01->harbor_score), sdk_worldgen_unpack_unorm8(c11->harbor_score),
                                             tx, tz);
    out_sample->confluence_score = bilerp4_local(sdk_worldgen_unpack_unorm8(c00->confluence_score), sdk_worldgen_unpack_unorm8(c10->confluence_score),
                                                 sdk_worldgen_unpack_unorm8(c01->confluence_score), sdk_worldgen_unpack_unorm8(c11->confluence_score),
                                                 tx, tz);
    out_sample->flood_risk = bilerp4_local(sdk_worldgen_unpack_unorm8(c00->flood_risk), sdk_worldgen_unpack_unorm8(c10->flood_risk),
                                           sdk_worldgen_unpack_unorm8(c01->flood_risk), sdk_worldgen_unpack_unorm8(c11->flood_risk),
                                           tx, tz);
    out_sample->buildable_flatness = bilerp4_local(sdk_worldgen_unpack_unorm8(c00->buildable_flatness), sdk_worldgen_unpack_unorm8(c10->buildable_flatness),
                                                   sdk_worldgen_unpack_unorm8(c01->buildable_flatness), sdk_worldgen_unpack_unorm8(c11->buildable_flatness),
                                                   tx, tz);
    ocean_weight = bilerp4_local((float)c00->ocean_mask, (float)c10->ocean_mask,
                                 (float)c01->ocean_mask, (float)c11->ocean_mask, tx, tz);
    lake_weight = bilerp4_local(sdk_worldgen_unpack_unorm8(c00->lake_mask), sdk_worldgen_unpack_unorm8(c10->lake_mask),
                                sdk_worldgen_unpack_unorm8(c01->lake_mask), sdk_worldgen_unpack_unorm8(c11->lake_mask), tx, tz);
    closed_weight = bilerp4_local(sdk_worldgen_unpack_unorm8(c00->closed_basin_mask), sdk_worldgen_unpack_unorm8(c10->closed_basin_mask),
                                  sdk_worldgen_unpack_unorm8(c01->closed_basin_mask), sdk_worldgen_unpack_unorm8(c11->closed_basin_mask), tx, tz);

    if (w00 > best_weight) { best_weight = w00; nearest = c00; }
    if (w10 > best_weight) { best_weight = w10; nearest = c10; }
    if (w01 > best_weight) { best_weight = w01; nearest = c01; }
    if (w11 > best_weight) { best_weight = w11; nearest = c11; }

    if (nearest) {
        out_sample->basin_id = nearest->basin_id;
        out_sample->lake_id = nearest->lake_id;
        out_sample->downstream_cx = nearest->downstream_cx;
        out_sample->downstream_cz = nearest->downstream_cz;
    }

    out_sample->ocean_mask = (uint8_t)(ocean_weight >= 0.55f);
    out_sample->land_mask = (uint8_t)(!out_sample->ocean_mask &&
                                      out_sample->raw_height > (float)impl->sea_level + 1.0f);
    out_sample->lake_mask = sdk_worldgen_pack_unorm8(lake_weight);
    out_sample->closed_basin_mask = sdk_worldgen_pack_unorm8(closed_weight);
    if (out_sample->ocean_mask) {
        out_sample->lake_mask = 0u;
        out_sample->closed_basin_mask = 0u;
    }
}

void sdk_worldgen_build_continental_tile(SdkWorldGen* wg, SdkWorldGenContinentalTile* tile)
{
    SdkWorldGenImpl* impl = worldgen_impl(wg);
    SdkWorldGenContinentScratch* scratch;
    int analysis_origin_x;
    int analysis_origin_z;
    int16_t* raw_height;
    int16_t* filled_height;
    int16_t* coast_distance;
    uint8_t* ocean_mask;
    uint8_t* precip;
    uint8_t* runoff;
    uint8_t* flatness;
    int* downstream;
    uint32_t* flow_accum;
    uint32_t* basin_id;
    uint8_t* trunk_order;
    uint8_t* lake_mask;
    uint8_t* closed_mask;
    int16_t* lake_level;
    uint16_t* lake_id;
    uint8_t* water_access;
    uint8_t* harbor_score;
    uint8_t* confluence;
    uint8_t* flood_risk;
    int* queue;
    uint8_t* visited;
    MinHeapEntry* heap;
    FlowSortEntry* sort_entries;
    int* stack;
    int x;
    int z;

    if (!impl || !tile) return;
    scratch = impl->continent_scratch;
    raw_height = scratch->raw_height;
    filled_height = scratch->filled_height;
    coast_distance = scratch->coast_distance;
    ocean_mask = scratch->ocean_mask;
    precip = scratch->precipitation;
    runoff = scratch->runoff;
    flatness = scratch->flatness;
    downstream = scratch->downstream;
    flow_accum = scratch->flow_accum;
    basin_id = scratch->basin_id;
    trunk_order = scratch->trunk_order;
    lake_mask = scratch->lake_mask;
    closed_mask = scratch->closed_mask;
    lake_level = scratch->lake_level;
    lake_id = scratch->lake_id;
    water_access = scratch->water_access;
    harbor_score = scratch->harbor_score;
    confluence = scratch->confluence;
    flood_risk = scratch->flood_risk;
    queue = scratch->queue;
    visited = scratch->visited;
    heap = (MinHeapEntry*)scratch->heap;
    sort_entries = (FlowSortEntry*)scratch->sort_entries;
    stack = scratch->stack;

    if (!raw_height || !filled_height || !coast_distance || !ocean_mask ||
        !precip || !runoff || !flatness || !downstream || !flow_accum ||
        !basin_id || !trunk_order || !lake_mask || !closed_mask ||
        !lake_level || !lake_id || !water_access || !harbor_score ||
        !confluence || !flood_risk || !queue || !visited || !heap ||
        !sort_entries || !stack) {
        return;
    }

    analysis_origin_x = tile->tile_x * SDK_WORLDGEN_CONTINENT_TILE_SIZE - SDK_WORLDGEN_CONTINENT_ANALYSIS_HALO;
    analysis_origin_z = tile->tile_z * SDK_WORLDGEN_CONTINENT_TILE_SIZE - SDK_WORLDGEN_CONTINENT_ANALYSIS_HALO;

    for (z = 0; z < SDK_WORLDGEN_CONTINENT_ANALYSIS_STRIDE; ++z) {
        for (x = 0; x < SDK_WORLDGEN_CONTINENT_ANALYSIS_STRIDE; ++x) {
            int cell_x = analysis_origin_x + x;
            int cell_z = analysis_origin_z + z;
            raw_height[analysis_index(x, z)] =
                (int16_t)lrintf(sample_raw_elevation(impl, cell_x, cell_z, NULL, NULL, NULL));
        }
    }

    compute_ocean_connectivity(raw_height, impl->sea_level, ocean_mask, queue);
    compute_coast_distance(raw_height, ocean_mask, impl->sea_level, coast_distance, queue);
    compute_precipitation_runoff(impl, raw_height, ocean_mask, coast_distance, precip, runoff, flatness);
    fill_depressions(raw_height, filled_height, heap, visited);
    compute_drainage(impl,
                     raw_height,
                     filled_height,
                     coast_distance,
                     ocean_mask,
                     precip,
                     runoff,
                     flatness,
                     downstream,
                     flow_accum,
                     basin_id,
                     trunk_order,
                     lake_mask,
                     closed_mask,
                     lake_level,
                     lake_id,
                     water_access,
                     harbor_score,
                     confluence,
                     flood_risk,
                     sort_entries,
                     stack);

    for (z = 0; z < SDK_WORLDGEN_CONTINENT_STRIDE; ++z) {
        for (x = 0; x < SDK_WORLDGEN_CONTINENT_STRIDE; ++x) {
            int ax = x + SDK_WORLDGEN_CONTINENT_ANALYSIS_MARGIN;
            int az = z + SDK_WORLDGEN_CONTINENT_ANALYSIS_MARGIN;
            int aidx = analysis_index(ax, az);
            int cell_x = analysis_origin_x + ax;
            int cell_z = analysis_origin_z + az;
            int down = downstream[aidx];
            SdkContinentalCell* cell = &tile->cells[stored_index(x, z)];

            memset(cell, 0, sizeof(*cell));
            cell->raw_height = raw_height[aidx];
            cell->filled_height = filled_height[aidx];
            cell->lake_level = lake_level[aidx];
            cell->coast_distance = coast_distance[aidx];
            cell->flow_accum = flow_accum[aidx];
            cell->basin_id = basin_id[aidx];
            cell->lake_id = lake_id[aidx];
            cell->land_mask = (uint8_t)(raw_height[aidx] > impl->sea_level);
            cell->ocean_mask = ocean_mask[aidx];
            cell->lake_mask = lake_mask[aidx];
            cell->closed_basin_mask = closed_mask[aidx];
            cell->precipitation = precip[aidx];
            cell->runoff = runoff[aidx];
            cell->trunk_river_order = trunk_order[aidx];
            cell->water_access = water_access[aidx];
            cell->harbor_score = harbor_score[aidx];
            cell->confluence_score = confluence[aidx];
            cell->flood_risk = flood_risk[aidx];
            cell->buildable_flatness = flatness[aidx];

            if (down >= 0) {
                int down_x = down % SDK_WORLDGEN_CONTINENT_ANALYSIS_STRIDE;
                int down_z = down / SDK_WORLDGEN_CONTINENT_ANALYSIS_STRIDE;
                cell->downstream_cx = analysis_origin_x + down_x;
                cell->downstream_cz = analysis_origin_z + down_z;
            } else {
                cell->downstream_cx = cell_x;
                cell->downstream_cz = cell_z;
            }
        }
    }
}
