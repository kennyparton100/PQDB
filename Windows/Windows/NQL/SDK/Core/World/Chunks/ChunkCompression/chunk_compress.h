#ifndef CHUNK_COMPRESS_H
#define CHUNK_COMPRESS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ============================================================================
 * Chunk Compression Library
 * 
 * Compresses 64x64x1024 chunks using volume-based and layer-based encoding.
 * ============================================================================ */

#ifndef CHUNK_SIZE_X
#ifdef CHUNK_WIDTH
#define CHUNK_SIZE_X CHUNK_WIDTH
#else
#define CHUNK_SIZE_X 64
#endif
#endif

#ifndef CHUNK_SIZE_Z
#ifdef CHUNK_DEPTH
#define CHUNK_SIZE_Z CHUNK_DEPTH
#else
#define CHUNK_SIZE_Z 64
#endif
#endif

#ifndef CHUNK_SIZE_Y
#ifdef CHUNK_HEIGHT
#define CHUNK_SIZE_Y CHUNK_HEIGHT
#else
#define CHUNK_SIZE_Y 1024
#endif
#endif

#ifndef CHUNK_TOTAL_BLOCKS
#define CHUNK_TOTAL_BLOCKS (CHUNK_SIZE_X * CHUNK_SIZE_Z * CHUNK_SIZE_Y)
#endif

#ifndef CHUNK_RAW_BYTES
#define CHUNK_RAW_BYTES (CHUNK_TOTAL_BLOCKS * sizeof(uint16_t))
#endif

#define SUPERCHUNK_SIZE_CHUNKS 16
#define SUPERCHUNK_BOUNDARY_RADIUS 1
#define SUPERCHUNK_BOUNDARY_SIZE_CHUNKS (SUPERCHUNK_SIZE_CHUNKS + 2 * SUPERCHUNK_BOUNDARY_RADIUS)
#define SUPERCHUNK_SIZE_X (CHUNK_SIZE_X * SUPERCHUNK_SIZE_CHUNKS)  /* 1024 */
#define SUPERCHUNK_SIZE_Z (CHUNK_SIZE_Z * SUPERCHUNK_SIZE_CHUNKS)  /* 1024 */
#define SUPERCHUNK_CHUNKS (SUPERCHUNK_SIZE_CHUNKS * SUPERCHUNK_SIZE_CHUNKS)  /* 256 */
#define SUPERCHUNK_BOUNDARY_CHUNKS \
    ((SUPERCHUNK_BOUNDARY_SIZE_CHUNKS * SUPERCHUNK_BOUNDARY_SIZE_CHUNKS) - SUPERCHUNK_CHUNKS)  /* 68 */

#define MAX_BLOCK_ID 65535
#define MAX_TOP_Y 1024

/* Version of the compressed format */
#define COMPRESS_FORMAT_VERSION 1

/* ============================================================================
 * Compression Methods (for aggressive terrain compression)
 * ============================================================================ */

#define COMPRESS_METHOD_VOLUME_LAYER      0  /* Original volume+layer */
#define COMPRESS_METHOD_TEMPLATE          1  /* Template-based (22.7% typical) */
#define COMPRESS_METHOD_OCTREE            2  /* Spatial subdivision */
#define COMPRESS_METHOD_DELTA_TEMPLATE    3  /* Delta from template */
#define COMPRESS_METHOD_INTER_CHUNK_DELTA 4  /* Delta from similar chunk */
#define COMPRESS_METHOD_AUTO              5  /* Auto-select best */
#define COMPRESS_METHOD_BITPACK           6  /* Bit-packed palette (NEW) */
#define COMPRESS_METHOD_SPARSE_COLUMN     7  /* Sparse column storage (NEW) */
#define COMPRESS_METHOD_RLE_BITMASK       8  /* Zero-run RLE + bitmask (NEW) */
#define COMPRESS_METHOD_HIERARCHICAL_RLE  9  /* Hierarchical RLE V2 - 95%+ target (NEW) */
#define COMPRESS_METHOD_SUPERCHUNK_HIER    10 /* Superchunk Hierarchical V3 - 96%+ target (NEW) */

#define MAX_TERRAIN_TEMPLATES 32

/* ============================================================================
 * Data Structures
 * ============================================================================ */

/* Forward declarations for recursive structures */
typedef struct Patch Patch;
typedef struct TerrainTemplate TerrainTemplate;

/* A 3D axis-aligned volume of the same block type */
typedef struct {
    uint16_t x1, y1, z1;  /* Start coordinates (inclusive) */
    uint16_t x2, y2, z2;  /* End coordinates (inclusive) */
    uint16_t block_id;    /* 0-255 */
} Volume;

/* A patch entry - single block replacement in a layer */
struct Patch {
    uint16_t x, y, z;     /* Block coordinates */
    uint16_t block_id;    /* Replacement block */
};

/* A terrain template - pre-defined common pattern */
struct TerrainTemplate {
    uint8_t template_id;
    char name[32];
    /* Template is stored as a set of layers */
    uint32_t layer_count;
    struct {
        uint16_t block_id;
        uint16_t y_start;
        uint16_t y_end;
    } layers[16];  /* Up to 16 layers per template */
};

/* A layer: base block + patches */
typedef struct {
    uint16_t base_block_id;
    uint32_t patch_count;
    Patch* patches;
} Layer;

/* Compressed chunk data */
typedef struct {
    int32_t cx, cz;           /* Chunk coordinates */
    uint16_t top_y;           /* Highest Y with blocks */
    uint16_t version;         /* Format version */
    
    /* Volume section */
    uint32_t volume_count;
    Volume* volumes;
    
    /* Layer section */
    uint32_t layer_count;
    Layer* layers;
    
    /* Residual RLE data for remaining blocks */
    uint8_t* residual_data;
    size_t residual_size;
} CompressedChunk;

/* Raw decoded chunk data */
typedef struct {
    int32_t cx, cz;
    uint16_t top_y;
    uint16_t blocks[CHUNK_SIZE_X][CHUNK_SIZE_Z][CHUNK_SIZE_Y];  /* [x][z][y] - 2 bytes per block */
} RawChunk;

/* Template-compressed chunk */
typedef struct {
    uint8_t template_id;
    uint32_t patch_count;
    Patch* patches;
    /* Residual for blocks not matching template + patches */
    uint32_t residual_count;
    Patch* residuals;
} TemplateCompressedChunk;

/* ============================================================================
 * Superchunk Data Structures
 * ============================================================================ */

/* A superchunk is 16x16 chunks (256 chunks total, 1024x1024x1024 blocks) */
typedef struct {
    int32_t scx, scz;  /* Superchunk coordinates (chunk coord / 16) */
    uint16_t max_top_y;
    
    /* Array of 256 compressed chunks (may be NULL if empty) */
    CompressedChunk* chunks[SUPERCHUNK_CHUNKS];
    
    /* Shared volumes that span multiple chunks (larger structures) */
    uint32_t mega_volume_count;
    Volume* mega_volumes;  /* Volumes larger than single chunk */
    
    /* Wall grid - compressed boundary data between chunks */
    uint8_t* wall_grid;  /* Stores wall/boundary block info */
    size_t wall_grid_size;
} CompressedSuperchunk;

/* Wall grid cell - stores boundary information between 4 adjacent chunks */
typedef struct {
    uint16_t block_id;
    uint8_t wall_type;  /* 0=none, 1=wall, 2=corner, 3=edge_feature */
    uint8_t flags;      /* visibility, connectivity, etc. */
} WallGridCell;

/* Raw superchunk data (1024x1024x1024 blocks) - for processing */
typedef struct {
    int32_t scx, scz;
    uint16_t max_top_y;
    /* Note: actual blocks stored in sub-chunks to avoid 1GB stack allocation */
    RawChunk* chunks[SUPERCHUNK_CHUNKS];  /* 256 pointers to chunks */
} RawSuperchunk;

/* ============================================================================
 * Legacy RLE Decoder
 * ============================================================================ */

/* Decode legacy Base64 RLE format into raw blocks */
int rle_decode_legacy(const char* rle_base64, size_t rle_len, RawChunk* out_chunk);

/* Encode raw blocks to legacy RLE format (for testing/comparison) */
int rle_encode_legacy(const RawChunk* chunk, char** out_rle_base64, size_t* out_len);

/* ============================================================================
 * Compression API
 * ============================================================================ */

/* Initialize compressed chunk structure */
void compress_chunk_init(CompressedChunk* cc, int32_t cx, int32_t cz, uint16_t top_y);

/* Free compressed chunk memory */
void compress_chunk_free(CompressedChunk* cc);

/* Compress a raw chunk using volume + layer encoding */
int compress_chunk(const RawChunk* raw, CompressedChunk* out);

/* Decompress to raw chunk */
int decompress_chunk(const CompressedChunk* cc, RawChunk* out);

/* ============================================================================
 * Volume Detection
 * ============================================================================ */

/* Find all volumes of same block type in a raw chunk */
int volume_detect(const RawChunk* raw, Volume** out_volumes, uint32_t* out_count);

/* Free volume array */
void volume_free(Volume* volumes);

/* ============================================================================
 * Layer Detection
 * ============================================================================ */

/* Build layer representation from remaining blocks after volume extraction */
int layer_build(const RawChunk* raw, const Volume* volumes, uint32_t volume_count,
                Layer** out_layers, uint32_t* out_layer_count);

/* Free layer array and all patch arrays */
void layer_free(Layer* layers, uint32_t count);

/* ============================================================================
 * Binary Encoding/Decoding (for Base64 wrapping)
 * ============================================================================ */

/* Encode compressed chunk to binary buffer */
int compress_encode_binary(const CompressedChunk* cc, uint8_t** out_data, size_t* out_size);

/* Decode binary buffer to compressed chunk */
int compress_decode_binary(const uint8_t* data, size_t size, CompressedChunk* out);

/* ============================================================================
 * Base64 Encoding/Decoding
 * ============================================================================ */

/* Encode binary data to Base64 string */
int base64_encode(const uint8_t* data, size_t len, char** out_str, size_t* out_str_len);

/* Decode Base64 string to binary data */
int base64_decode(const char* str, size_t str_len, uint8_t** out_data, size_t* out_data_len);

/* ============================================================================
 * Utility
 * ============================================================================ */

/* Calculate compression ratio */
double compress_ratio(size_t original_size, size_t compressed_size);

/* Print compression statistics */
void compress_print_stats(const CompressedChunk* cc, size_t original_bytes);

/* Verify decompressed data matches original */
bool compress_verify(const RawChunk* original, const RawChunk* decompressed);

/* ============================================================================
 * Superchunk Compression API
 * ============================================================================ */

/* Initialize compressed superchunk */
void superchunk_init(CompressedSuperchunk* sc, int32_t scx, int32_t scz);

/* Free compressed superchunk and all sub-chunks */
void superchunk_free(CompressedSuperchunk* sc);

/* Compress a raw superchunk using mega-volumes and wall grid optimization */
int compress_superchunk(const RawSuperchunk* raw, CompressedSuperchunk* out);

/* Decompress superchunk to raw */
int decompress_superchunk(const CompressedSuperchunk* sc, RawSuperchunk* out);

/* Build wall grid from superchunk boundaries - stores shared edge data */
int wall_grid_build(const RawSuperchunk* raw, uint8_t** out_grid, size_t* out_size);

/* Extract wall grid cell at superchunk-relative position */
WallGridCell wall_grid_get(const uint8_t* grid, size_t grid_size, uint16_t x, uint16_t z, uint16_t y);

/* Find mega-volumes (structures spanning multiple chunks) */
int mega_volume_detect(const RawSuperchunk* raw, Volume** out_volumes, uint32_t* out_count);

/* Encode superchunk to binary */
int superchunk_encode_binary(const CompressedSuperchunk* sc, uint8_t** out_data, size_t* out_size);

/* Decode binary to superchunk */
int superchunk_decode_binary(const uint8_t* data, size_t size, CompressedSuperchunk* out);

/* Print superchunk compression statistics */
void superchunk_print_stats(const CompressedSuperchunk* sc, size_t original_bytes);

/* ============================================================================
 * Terrain Template Compression API (Phase 1 - Maximum Compression)
 * ============================================================================ */

/* Initialize standard terrain templates */
void template_init_defaults(TerrainTemplate* templates, int* out_count);

/* Find best matching template for a chunk */
int template_find_best_match(const RawChunk* raw, const TerrainTemplate* templates, 
                              int template_count, float* out_score);

/* Compress chunk using template + patches */
int compress_template(const RawChunk* raw, uint8_t template_id, 
                      const TerrainTemplate* template_def,
                      TemplateCompressedChunk* out);

/* Decompress template-compressed chunk */
int decompress_template(const TemplateCompressedChunk* tc, const TerrainTemplate* template_def,
                        RawChunk* out, int32_t cx, int32_t cz);

/* Encode template-compressed chunk to binary */
int template_encode_binary(const TemplateCompressedChunk* tc, uint8_t** out_data, size_t* out_size);

/* Print template compression statistics */
void template_print_stats(const TemplateCompressedChunk* tc, size_t original_bytes);

/* ============================================================================
 * Octree Compression API (Phase 2 - Balanced Compression)
 * ============================================================================ */

/* Octree node for spatial subdivision */
typedef struct OctreeNode {
    uint16_t x, y, z;      /* Origin of this node */
    uint16_t size_x, size_y, size_z;  /* Size per dimension (allows non-cubic) */
    uint8_t is_uniform;    /* 1 if all blocks same */
    uint16_t block_id;     /* Block type if uniform - now 16-bit */
    struct OctreeNode* children[8];  /* NULL for uniform nodes */
    
    /* Raw block data for non-uniform leaf nodes (when is_uniform=0 and no children) */
    uint16_t* raw_data;    /* Raw block data storage (size_x * size_y * size_z * 2 bytes) */
} OctreeNode;

/* Octree forest for 64x64x1024 chunks - stack of octrees per Y-slice */
typedef struct {
    OctreeNode* roots[16];  /* Up to 16 slices of 64 Y each */
    uint32_t node_count;
    uint8_t slice_count;    /* Number of Y-slices used */
} OctreeCompressedChunk;

/* Build octree from raw chunk */
int octree_build(const RawChunk* raw, OctreeCompressedChunk* out);

/* Free octree forest */
void octree_free_forest(OctreeCompressedChunk* oc);

/* Decompress octree to raw chunk */
int decompress_octree(const OctreeCompressedChunk* oc, RawChunk* out);

/* Encode octree to binary */
int octree_encode_binary(const OctreeCompressedChunk* oc, uint8_t** out_data, size_t* out_size);

/* Print octree compression statistics */
void octree_print_stats(const OctreeCompressedChunk* oc, size_t original_bytes);

/* ============================================================================
 * Bit-Packed Palette Compression API (NEW - Phase 6: Massive Performance)
 * ============================================================================ */

/* Bit-packed compressed chunk - 5/6/8 bits per block */
typedef struct {
    uint8_t bits_per_block;      /* 5, 6, or 8 bits */
    uint8_t palette_size;        /* Number of entries in palette (up to 64) */
    uint8_t palette[64];         /* Block ID palette - maps index -> block_id */
    uint32_t data_size;          /* Size of packed data in bytes */
    uint8_t* data;               /* Bit-packed block data */
} BitPackedCompressedChunk;

/* Compress chunk using bit-packed palette */
int compress_bitpack(const RawChunk* raw, BitPackedCompressedChunk* out);

/* Decompress bit-packed chunk */
int decompress_bitpack(const BitPackedCompressedChunk* bc, RawChunk* out);

/* Encode bit-packed chunk to binary */
int bitpack_encode_binary(const BitPackedCompressedChunk* bc, uint8_t** out_data, size_t* out_size);

/* Free bit-packed chunk memory */
void bitpack_free(BitPackedCompressedChunk* bc);

/* Print bit-pack compression statistics */
void bitpack_print_stats(const BitPackedCompressedChunk* bc, size_t original_bytes);

/* ============================================================================
 * Sparse Column Storage Compression API (NEW - Phase 6: Massive Performance)
 * ============================================================================ */

/* Column data for sparse storage */
typedef struct {
    uint16_t height;           /* Number of blocks stored in this column (from Y=0) */
    uint8_t* blocks;           /* Block data for column [height] */
} SparseColumn;

/* Sparse column compressed chunk - only stores columns with non-air blocks */
typedef struct {
    uint16_t surface_height[64][64];  /* Heightmap: surface Y for each X,Z column */
    uint8_t has_column[64][64];       /* Bitmask: 1 if column has data */
    SparseColumn columns[64][64];     /* Column data (only valid if has_column=1) */
    uint32_t total_stored_blocks;     /* Total blocks actually stored */
} SparseColumnCompressedChunk;

/* Compress chunk using sparse column storage */
int compress_sparse_column(const RawChunk* raw, SparseColumnCompressedChunk* out);

/* Decompress sparse column chunk */
int decompress_sparse_column(const SparseColumnCompressedChunk* sc, RawChunk* out);

/* Encode sparse column chunk to binary */
int sparse_column_encode_binary(const SparseColumnCompressedChunk* sc, uint8_t** out_data, size_t* out_size);

/* Free sparse column chunk memory */
void sparse_column_free(SparseColumnCompressedChunk* sc);

/* Print sparse column compression statistics */
void sparse_column_print_stats(const SparseColumnCompressedChunk* sc, size_t original_bytes);

/* ============================================================================
 * Zero-Run RLE + Bitmask Compression API (NEW - Phase 6: Massive Performance)
 * ============================================================================ */

/* Bitmask-compressed chunk - stores 1-bit presence mask + RLE for air runs + PALETTE for non-air */
typedef struct {
    uint32_t num_segments;      /* Number of Y-segments (each 64x64x16 = 65536 blocks) */
    uint8_t** presence_masks;     /* Bitmask per segment: 1 = non-air, 0 = air */
    uint32_t* mask_sizes;         /* Size of each mask in bytes */
    
    /* Palette-compressed non-air block data */
    uint8_t palette[64];          /* Palette of up to 64 unique block IDs */
    uint8_t palette_size;         /* Number of entries in palette (1-64) */
    uint8_t bits_per_index;       /* 5, 6, or 8 bits per index */
    uint8_t* compressed_block_data; /* Bit-packed palette indices */
    uint32_t compressed_block_data_size; /* Size in bytes */
    uint32_t total_non_air_blocks; /* Count of non-air blocks */
    
    uint32_t total_air_runs;      /* Number of air RLE entries */
    uint32_t* air_run_lengths;    /* Length of each air run */
    
    /* Legacy fields for backward compatibility during transition */
    uint8_t* block_data;          /* DEPRECATED: Non-air block values packed together */
    uint32_t block_data_size;     /* DEPRECATED: Total non-air blocks stored */
} RLEBitmaskCompressedChunk;

/* Compress chunk using zero-run RLE + bitmask */
int compress_rle_bitmask(const RawChunk* raw, RLEBitmaskCompressedChunk* out);

/* Decompress RLE + bitmask chunk */
int decompress_rle_bitmask(const RLEBitmaskCompressedChunk* rc, RawChunk* out);

/* Encode RLE bitmask chunk to binary */
int rle_bitmask_encode_binary(const RLEBitmaskCompressedChunk* rc, uint8_t** out_data, size_t* out_size);

/* Free RLE bitmask chunk memory */
void rle_bitmask_free(RLEBitmaskCompressedChunk* rc);

/* Print RLE bitmask compression statistics */
void rle_bitmask_print_stats(const RLEBitmaskCompressedChunk* rc, size_t original_bytes);

/* ============================================================================
 * Hierarchical RLE + Bitmask V2 API (NEW - Phase 7: 95%+ Compression Target)
 * ============================================================================ */

/* Segment types for hierarchical compression */
#define SEG_TYPE_ALL_AIR     0  /* Segment is 100% air - no data stored */
#define SEG_TYPE_UNIFORM     1  /* Segment is 100% same block type */
#define SEG_TYPE_MIXED       2  /* Segment has mixed blocks - full mask stored */

/* Hierarchical compressed segment - variable size based on type */
typedef struct {
    uint8_t type;              /* SEG_TYPE_* */
    uint16_t uniform_block;    /* For SEG_TYPE_UNIFORM: the block ID (now 16-bit) */
    uint32_t mask_size;        /* For SEG_TYPE_MIXED: size of mask in bytes */
    
    /* For SEG_TYPE_MIXED: either bitmap mask OR sparse position list */
    union {
        uint8_t* mask;             /* Bitmap: presence mask (legacy) */
        uint16_t* positions;       /* Sparse: array of non-air block positions */
    };
    uint32_t num_positions;    /* For sparse: count of non-air positions (0 = use bitmap) */
} HierarchicalSegment;

/* Hierarchical RLE V2 compressed chunk - targets 95%+ compression */
typedef struct {
    uint64_t segment_summary;   /* 64-bit mask: 1 = segment has blocks, 0 = all air */
    uint32_t num_segments;      /* Number of segments (64 max) */
    HierarchicalSegment* segments; /* Per-segment data (only for non-empty) */
    
    /* Palette-compressed non-air block data */
    uint16_t palette[64];          /* Palette of unique block IDs (16-bit) */
    uint8_t palette_size;         /* Number of palette entries */
    uint8_t bits_per_index;       /* 1-8 bits per index */
    uint8_t* compressed_block_data; /* Bit-packed palette indices */
    uint32_t compressed_block_data_size;
    uint32_t total_non_air_blocks;
} HierarchicalRLECompressedChunk;

/* Compress chunk using hierarchical RLE V2 */
int compress_hierarchical_rle(const RawChunk* raw, HierarchicalRLECompressedChunk* out);

/* Decompress hierarchical RLE V2 chunk */
int decompress_hierarchical_rle(const HierarchicalRLECompressedChunk* hc, RawChunk* out);

/* Encode hierarchical RLE chunk to binary */
int hierarchical_rle_encode_binary(const HierarchicalRLECompressedChunk* hc, uint8_t** out_data, size_t* out_size);

/* Free hierarchical RLE chunk memory */
void hierarchical_rle_free(HierarchicalRLECompressedChunk* hc);

/* Print hierarchical RLE compression statistics */
void hierarchical_rle_print_stats(const HierarchicalRLECompressedChunk* hc, size_t original_bytes);

/* ============================================================================
 * Superchunk Hierarchical RLE V3 API (NEW - Phase 8: 96%+ Compression Target)
 * ============================================================================ */

/* Superchunk segment - 16x16 chunk area treated as one unit */
typedef struct {
    uint16_t chunk_x;          /* Chunk X within superchunk (0-15) */
    uint16_t chunk_z;          /* Chunk Z within superchunk (0-15) */
    uint64_t segment_summary;  /* 64-bit mask: 1 = segment has blocks */
    HierarchicalSegment segments[64]; /* Per-segment data (variable size) */
} SuperchunkChunkSegments;

/* Superchunk Hierarchical V3 compressed data - treats 256 chunks as one unit */
typedef struct {
    int32_t scx, scz;          /* Superchunk coordinates */
    uint16_t num_chunks;       /* Number of chunks with data (max 256) */
    uint8_t wall_ring_present; /* 1 if outer wall ring is present */
    
    /* Global palette shared across ALL chunks in superchunk - now 16-bit */
    uint16_t palette[256];     /* Single shared palette (expanded for 16-bit blocks) */
    uint16_t palette_size;      /* Number of entries (up to 256) */
    uint8_t bits_per_index;    /* 1-8 bits per index */
    
    /* Per-chunk segment data */
    SuperchunkChunkSegments* chunk_segments; /* Array[num_chunks] */
    
    /* Global compressed block data for ALL non-air blocks in superchunk */
    uint8_t* compressed_block_data;
    uint32_t compressed_block_data_size;
    uint32_t total_non_air_blocks;
    
    /* Wall chunks stored separately (if present) */
    uint8_t num_wall_chunks;
    struct {
        int8_t chunk_x;        /* Relative chunk coordinate (-1..16) */
        int8_t chunk_z;        /* Relative chunk coordinate (-1..16) */
        uint8_t* compressed_data; /* Compressed wall chunk data */
        uint32_t compressed_size;
    }* wall_chunks;
} SuperchunkHierarchicalCompressedChunk;

/* Check if chunk is a wall chunk (mostly stone bricks) */
int chunk_is_wall(const RawChunk* raw, float threshold);

/* Detect full superchunks from a collection of chunks */
typedef struct {
    int32_t scx, scz;
    RawChunk* chunks[256];     /* 16x16 grid, NULL if missing/wall */
    uint8_t is_wall[256];      /* 1 if this slot is a wall chunk */
    RawChunk* wall_chunks[SUPERCHUNK_BOUNDARY_CHUNKS];
    int8_t wall_rel_x[SUPERCHUNK_BOUNDARY_CHUNKS];
    int8_t wall_rel_z[SUPERCHUNK_BOUNDARY_CHUNKS];
    uint16_t num_terrain;      /* Non-wall chunks */
    uint16_t num_wall;         /* Boundary ring / frontier chunks */
} DetectedSuperchunk;

/* Detect full superchunks from loaded chunks */
int detect_superchunks(RawChunk** chunks, int num_chunks, 
                        DetectedSuperchunk** out_superchunks, int* out_count);

/* Compress superchunk using hierarchical V3 method */
int compress_superchunk_hierarchical(const DetectedSuperchunk* sc, 
                                      SuperchunkHierarchicalCompressedChunk* out);

/* Decompress superchunk hierarchical V3 */
int decompress_superchunk_hierarchical(const SuperchunkHierarchicalCompressedChunk* sc,
                                        RawChunk** out_chunks, int* out_count);

/* Encode superchunk hierarchical to binary */
int superchunk_hierarchical_encode_binary(const SuperchunkHierarchicalCompressedChunk* sc,
                                           uint8_t** out_data, size_t* out_size);

/* Free superchunk hierarchical memory */
void superchunk_hierarchical_free(SuperchunkHierarchicalCompressedChunk* sc);

/* Print superchunk hierarchical statistics */
void superchunk_hierarchical_print_stats(const SuperchunkHierarchicalCompressedChunk* sc, 
                                          size_t original_bytes);

/* ============================================================================
 * Lazy Loading API - For fast partial chunk loading (NEW)
 * ============================================================================ */

/* Load a single chunk from superchunk without decompressing all 256 chunks.
 * Only decompresses the requested chunk's segments. Much faster for partial loads.
 * Returns allocated RawChunk on success, NULL on failure. */
RawChunk* superchunk_load_single_chunk(const SuperchunkHierarchicalCompressedChunk* sc, 
                                        int chunk_x, int chunk_z);

/* Load chunks within radius from superchunk center (for view distance loading).
 * Only decompresses chunks within the specified radius.
 * out_chunks: pre-allocated array of RawChunk pointers
 * Returns number of chunks loaded. */
int superchunk_load_chunks_radius(const SuperchunkHierarchicalCompressedChunk* sc,
                                   int center_x, int center_z, int radius,
                                   RawChunk** out_chunks, int max_chunks);

/* Get block at specific position without full chunk decompression.
 * Useful for raycasting or single-block queries.
 * Returns block ID or 0 (air) on failure. */
uint16_t superchunk_get_block(const SuperchunkHierarchicalCompressedChunk* sc,
                               int chunk_x, int chunk_z, int x, int y, int z);

/* ============================================================================
 * Segment-Level Partial Decode API (NEW - for Hierarchical RLE V2)
 * ============================================================================ */

/* Decompress only specific segments from hierarchical RLE chunk.
 * seg_start: first segment to decompress (0-63)
 * seg_count: number of segments to decompress
 * out: must be pre-initialized RawChunk (memset to 0)
 * Returns 0 on success, -1 on failure. */
int decompress_hierarchical_segments(const HierarchicalRLECompressedChunk* hc,
                                        int seg_start, int seg_count, RawChunk* out);

/* Get single block from hierarchical RLE without full decompression.
 * Returns block ID or 0 (air) on failure. */
uint16_t hierarchical_rle_get_block(const HierarchicalRLECompressedChunk* hc, 
                                    int x, int y, int z);

/* Check if segment has any blocks (for culling visible Y-levels).
 * Returns 1 if segment has blocks, 0 if all air. */
int hierarchical_rle_segment_has_blocks(const HierarchicalRLECompressedChunk* hc, int seg_idx);

/* ============================================================================
 * Segment-Level Partial Decode API (NEW - for RLE + Bitmask)
 * ============================================================================ */

/* Decompress only specific segments from RLE+Bitmask chunk.
 * seg_start: first segment to decompress (0-63, each 16 Y-blocks)
 * seg_count: number of segments to decompress
 * out: must be pre-initialized RawChunk (memset to 0)
 * Returns 0 on success, -1 on failure. */
int decompress_rle_bitmask_segments(const RLEBitmaskCompressedChunk* rc,
                                     int seg_start, int seg_count, RawChunk* out);

/* Get single block from RLE+Bitmask without full decompression.
 * Returns block ID or 0 (air) on failure. */
uint16_t rle_bitmask_get_block(const RLEBitmaskCompressedChunk* rc, int x, int y, int z);

/* Check if segment has any non-air blocks.
 * Returns 1 if segment has blocks, 0 if all air. */
int rle_bitmask_segment_has_blocks(const RLEBitmaskCompressedChunk* rc, int seg_idx);

/* ============================================================================
 * Sparse Column Direct Access API (NEW - zero-copy column access)
 * ============================================================================ */

/* Get pointer to column data without copying (zero-copy access).
 * Returns pointer to column block data or NULL if column is all air.
 * height: outputs number of blocks stored (from Y=0)
 * Useful for terrain mesh generation. */
const uint8_t* sparse_column_get_column_ptr(const SparseColumnCompressedChunk* sc,
                                            int x, int z, int* height);

/* Get surface height at specific X,Z without full decompression.
 * Returns surface Y coordinate or 0 if all air. */
int sparse_column_get_surface_height(const SparseColumnCompressedChunk* sc, int x, int z);

/* ============================================================================
 * Inter-Chunk Delta Compression API (Phase 4 - Superchunk Optimization)
 * ============================================================================ */

/* Delta-compressed chunk - stores differences from a reference chunk */
typedef struct {
    int32_t ref_cx, ref_cz;      /* Reference chunk coordinates */
    uint32_t ref_hash;            /* Hash of reference chunk data for validation */
    uint32_t diff_count;          /* Number of differing blocks */
    Patch* differences;           /* Deltas from reference */
    
    /* Backup compression - used if reference chunk changed */
    uint8_t backup_method;        /* Method used for backup data */
    uint8_t* backup_data;         /* Backup compressed chunk data */
    size_t backup_size;           /* Size of backup data */
} DeltaCompressedChunk;

/* Compute hash of chunk data for validation */
uint32_t chunk_compute_hash(const RawChunk* raw);

/* Chunk similarity analysis */
typedef struct {
    int32_t chunk1_cx, chunk1_cz;
    int32_t chunk2_cx, chunk2_cz;
    float similarity;              /* 0.0 to 1.0 */
    uint32_t diff_count;           /* Number of different blocks */
} ChunkSimilarity;

/* Analyze similarity between two chunks */
float chunk_analyze_similarity(const RawChunk* chunk1, const RawChunk* chunk2, 
                                uint32_t* out_diff_count);

/* Find most similar chunks in a superchunk for delta compression */
int superchunk_find_similar_pairs(const RawSuperchunk* raw, 
                                   ChunkSimilarity* out_pairs, int max_pairs);

/* Compress chunk as delta from reference */
int compress_delta(const RawChunk* raw, const RawChunk* reference,
                    DeltaCompressedChunk* out);

/* Decompress delta chunk using reference */
int decompress_delta(const DeltaCompressedChunk* dc, const RawChunk* reference,
                      RawChunk* out);

/* Free delta compressed chunk including backup data */
void delta_free(DeltaCompressedChunk* dc);

/* Encode delta-compressed chunk to binary */
int delta_encode_binary(const DeltaCompressedChunk* dc, uint8_t** out_data, size_t* out_size);

/* Print delta compression statistics */
void delta_print_stats(const DeltaCompressedChunk* dc, size_t original_bytes);

/* ============================================================================
 * Compression Method Selection (Phase 5 - Auto-select best)
 * ============================================================================ */

/* Compression method recommendation result */
typedef struct {
    int method_id;              /* Selected method (0-5) */
    const char* method_name;    /* Human-readable name */
    float compression_ratio;    /* 0.0-1.0 (higher is better) */
    size_t estimated_size;      /* Estimated compressed size */
    int compress_ms;            /* Compression time estimate */
    int decompress_ms;          /* Decompression time estimate */
} CompressionRecommendation;

/* Analyze chunk and recommend best compression method */
void compress_analyze_and_recommend(const RawChunk* raw, 
                                     CompressionRecommendation* out);

/* Auto-compress chunk using best method */
int compress_auto(const RawChunk* raw, uint8_t** out_data, size_t* out_size,
                  CompressionRecommendation* out_info);

/* ============================================================================
 * Automatic Template Generation (Analyze chunks to create optimal templates)
 * ============================================================================ */

/* Extract template from a single chunk by analyzing its layer structure */
int template_extract_from_chunk(const RawChunk* raw, TerrainTemplate* out_template, char* name);

/* ============================================================================
 * Compression Header Format (stores method ID and metadata)
 * ============================================================================ */

/* Chunk compression header - 16 bytes */
typedef struct {
    uint8_t method_id;          /* COMPRESS_METHOD_* constant */
    uint8_t version;            /* Format version */
    uint16_t flags;             /* Compression flags */
    uint32_t uncompressed_size; /* Original size in bytes */
    uint32_t compressed_size;   /* Compressed size in bytes */
    uint32_t checksum;           /* CRC32 of uncompressed data */
} ChunkCompressionHeader;

/* Write header to binary buffer */
int compress_write_header(const ChunkCompressionHeader* header, uint8_t* buffer, size_t* pos);

/* Read header from binary buffer */
int compress_read_header(const uint8_t* buffer, size_t size, ChunkCompressionHeader* header);

/* Initialize header with default values */
void compress_header_init(ChunkCompressionHeader* header, int method_id, size_t uncompressed);

#endif /* CHUNK_COMPRESS_H */
