/* ============================================================================
 * RLE Decoder - Legacy Format Support
 * 
 * Decodes the existing Base64 RLE format used in save.json
 * Format analysis: Appears to be (block_id, count) pairs with custom alphabet
 * ============================================================================ */

#include "chunk_compress.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Standard Base64 alphabet used in the RLE encoding */
static const char base64_alphabet[] = 
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/* Reverse lookup table for Base64 decoding */
static int8_t base64_decode_table[256];
static int base64_table_initialized = 0;

static void init_base64_table(void) {
    if (base64_table_initialized) return;
    memset(base64_decode_table, -1, sizeof(base64_decode_table));
    for (int i = 0; i < 64; i++) {
        base64_decode_table[(unsigned char)base64_alphabet[i]] = (int8_t)i;
    }
    base64_table_initialized = 1;
}

/* Decode Base64 to binary */
static size_t decode_base64_raw(const char* input, size_t len, uint8_t* output, size_t out_cap) {
    init_base64_table();
    
    size_t out_len = 0;
    uint32_t buffer = 0;
    int bits = 0;
    
    for (size_t i = 0; i < len && out_len < out_cap; i++) {
        unsigned char c = (unsigned char)input[i];
        if (c == '=') break;  /* Padding */
        
        int8_t val = base64_decode_table[c];
        if (val < 0) continue;  /* Skip non-alphabet chars */
        
        buffer = (buffer << 6) | val;
        bits += 6;
        
        while (bits >= 8 && out_len < out_cap) {
            bits -= 8;
            output[out_len++] = (buffer >> bits) & 0xFF;
        }
    }
    
    return out_len;
}

/* Read a 16-bit unsigned integer (little-endian) */
static int read_u16(const uint8_t* data, size_t len, size_t* pos, uint16_t* out) {
    if (*pos + 2 > len) return 0;
    *out = (uint16_t)data[*pos] | ((uint16_t)data[*pos + 1] << 8);
    *pos += 2;
    return 1;
}

/* Read a variable-length unsigned integer (7 bits per byte, MSB indicates continuation) */
static int read_varuint(const uint8_t* data, size_t len, size_t* pos, uint32_t* out) {
    uint32_t value = 0;
    uint32_t shift = 0;
    
    while (*pos < len && shift < 35) {
        uint8_t byte = data[*pos];
        (*pos)++;
        value |= (uint32_t)(byte & 0x7F) << shift;
        if ((byte & 0x80) == 0) {
            *out = value;
            return 1;
        }
        shift += 7;
    }
    return 0;
}

/* Parse the RLE byte stream into raw blocks - 16-bit version */
static int parse_rle_stream(const uint8_t* data, size_t len, RawChunk* out) {
    /* Initialize all blocks to 0 (air) */
    memset(out->blocks, 0, sizeof(out->blocks));
    
    if (len < 4) {
        fprintf(stderr, "RLE data too short\n");
        return -1;
    }
    
    /* Parse format indicator - cell_rle format detection */
    /* Try to detect format: "BI" (8-bit legacy), "B2" (16-bit), or raw data */
    int is_16bit = (data[0] == 'B' && data[1] == '2');
    int is_legacy = (data[0] == 'B' && data[1] == 'I');
    
    /* Current position in RLE data */
    size_t pos = 2;  /* Skip "BI"/"B2" header if present */
    
    if (!is_16bit && !is_legacy) {
        /* No recognizable header - likely raw 16-bit data from cell_rle */
        is_16bit = 1;
        pos = 0;  /* Start from beginning - no header to skip */
    }
    
    /* Current position in block array */
    uint32_t block_idx = 0;
    const uint32_t total_blocks = CHUNK_TOTAL_BLOCKS;
    
    if (is_16bit) {
        /* 16-bit cell_rle format: (code_u16, run_len_varuint) pairs */
        while (pos < len && block_idx < total_blocks) {
            uint16_t code_u16 = 0;
            uint32_t run_len = 0;
            
            if (!read_u16(data, len, &pos, &code_u16) ||
                !read_varuint(data, len, &pos, &run_len)) {
                fprintf(stderr, "Error: Failed to read RLE pair at offset %zu\n", pos);
                return -1;
            }
            
            if (run_len == 0 || block_idx + run_len > total_blocks) {
                fprintf(stderr, "Error: Invalid run length %u at block %u\n", run_len, block_idx);
                return -1;
            }
            
            /* Fill the run */
            for (uint32_t i = 0; i < run_len && block_idx < total_blocks; i++) {
                uint32_t x = block_idx % CHUNK_SIZE_X;
                uint32_t z = (block_idx / CHUNK_SIZE_X) % CHUNK_SIZE_Z;
                uint32_t y = block_idx / (CHUNK_SIZE_X * CHUNK_SIZE_Z);
                
                if (x < CHUNK_SIZE_X && z < CHUNK_SIZE_Z && y < CHUNK_SIZE_Y) {
                    out->blocks[x][z][y] = code_u16;
                }
                block_idx++;
            }
        }
    } else {
        /* Legacy 8-bit format - original parsing logic */
        while (pos < len && block_idx < total_blocks) {
            if (pos >= len) break;
            
            uint8_t b = data[pos++];
            
            uint16_t block_id;
            uint16_t count;
            
            if (b < 32) {
                block_id = (b >> 5) & 0x07;
                count = (b & 0x1F) + 1;
            } else if (b < 64) {
                block_id = (b >> 4) & 0x0F;
                count = (b & 0x0F) + 1;
            } else if (b < 96) {
                block_id = 16 + ((b >> 4) & 0x0F);
                count = (b & 0x0F) + 1;
            } else {
                if (pos >= len) break;
                uint8_t b2 = data[pos++];
                block_id = (b - 96) * 4 + (b2 >> 6);
                count = (b2 & 0x3F) + 1;
            }
            
            for (uint16_t i = 0; i < count && block_idx < total_blocks; i++) {
                uint32_t x = block_idx % CHUNK_SIZE_X;
                uint32_t z = (block_idx / CHUNK_SIZE_X) % CHUNK_SIZE_Z;
                uint32_t y = block_idx / (CHUNK_SIZE_X * CHUNK_SIZE_Z);
                
                if (x < CHUNK_SIZE_X && z < CHUNK_SIZE_Z && y < CHUNK_SIZE_Y) {
                    out->blocks[x][z][y] = block_id;
                }
                block_idx++;
            }
        }
    }

    return 0;
}

/* Public API: Decode legacy RLE format */
int rle_decode_legacy(const char* rle_base64, size_t rle_len, RawChunk* out_chunk) {
    if (!rle_base64 || !out_chunk) return -1;
    
    /* Allocate buffer for decoded binary (3/4 of Base64 length) */
    size_t max_binary_len = (rle_len * 3) / 4 + 4;
    uint8_t* binary = (uint8_t*)malloc(max_binary_len);
    if (!binary) return -1;
    
    /* Decode Base64 to binary */
    size_t binary_len = decode_base64_raw(rle_base64, rle_len, binary, max_binary_len);
    
    if (binary_len == 0) {
        free(binary);
        return -1;
    }
    
    /* Parse the binary RLE stream */
    int result = parse_rle_stream(binary, binary_len, out_chunk);
    
    free(binary);
    return result;
}

/* Public API: Encode to legacy format (simplified - for testing) */
int rle_encode_legacy(const RawChunk* chunk, char** out_rle_base64, size_t* out_len) {
    /* This is a simplified encoder for testing purposes */
    /* Full implementation would match the game's exact encoding */
    
    if (!chunk || !out_rle_base64) return -1;
    
    /* Allocate output buffer (generous size) */
    size_t max_out = CHUNK_TOTAL_BLOCKS * 2 + 100;
    uint8_t* binary = (uint8_t*)malloc(max_out);
    if (!binary) return -1;
    
    /* Write header */
    binary[0] = 'B';
    binary[1] = 'I';
    size_t pos = 2;
    
    /* Simple RLE encoder - encode runs of same block */
    uint16_t current_block = chunk->blocks[0][0][0];
    uint16_t run_count = 0;
    
    for (uint32_t idx = 0; idx < CHUNK_TOTAL_BLOCKS; idx++) {
        uint32_t x = idx % CHUNK_SIZE_X;
        uint32_t z = (idx / CHUNK_SIZE_X) % CHUNK_SIZE_Z;
        uint32_t y = idx / (CHUNK_SIZE_X * CHUNK_SIZE_Z);
        
        uint16_t block = chunk->blocks[x][z][y];
        
        if (block == current_block && run_count < 63 && pos < max_out - 1) {
            run_count++;
        } else {
            /* Encode the run */
            if (current_block < 8 && run_count <= 32 && pos < max_out) {
                binary[pos++] = (uint8_t)((current_block << 5) | ((run_count - 1) & 0x1F));
            } else if (current_block < 16 && run_count <= 16 && pos < max_out) {
                binary[pos++] = (uint8_t)(32 | (current_block << 4) | ((run_count - 1) & 0x0F));
            } else {
                /* Two-byte encoding */
                if (pos + 1 < max_out) {
                    uint8_t b1 = (uint8_t)(96 + (current_block / 4));
                    uint8_t b2 = (uint8_t)(((current_block % 4) << 6) | ((run_count - 1) & 0x3F));
                    binary[pos++] = b1;
                    binary[pos++] = b2;
                }
            }
            
            current_block = block;
            run_count = 1;
        }
    }
    
    /* Encode final run */
    if (run_count > 0 && pos < max_out) {
        if (current_block < 8 && run_count <= 32) {
            binary[pos++] = (uint8_t)((current_block << 5) | ((run_count - 1) & 0x1F));
        } else if (current_block < 16 && run_count <= 16) {
            binary[pos++] = (uint8_t)(32 | (current_block << 4) | ((run_count - 1) & 0x0F));
        } else if (pos + 1 < max_out) {
            uint8_t b1 = (uint8_t)(96 + (current_block / 4));
            uint8_t b2 = (uint8_t)(((current_block % 4) << 6) | ((run_count - 1) & 0x3F));
            binary[pos++] = b1;
            binary[pos++] = b2;
        }
    }
    
    /* Encode binary to Base64 */
    size_t base64_len = ((pos + 2) / 3) * 4 + 1;
    *out_rle_base64 = (char*)malloc(base64_len);
    if (!*out_rle_base64) {
        free(binary);
        return -1;
    }
    
    /* Simple Base64 encoding */
    const char* alphabet = base64_alphabet;
    size_t out_pos = 0;
    
    for (size_t i = 0; i < pos; i += 3) {
        uint32_t val = binary[i] << 16;
        if (i + 1 < pos) val |= binary[i + 1] << 8;
        if (i + 2 < pos) val |= binary[i + 2];
        
        (*out_rle_base64)[out_pos++] = alphabet[(val >> 18) & 0x3F];
        (*out_rle_base64)[out_pos++] = alphabet[(val >> 12) & 0x3F];
        (*out_rle_base64)[out_pos++] = (i + 1 < pos) ? alphabet[(val >> 6) & 0x3F] : '=';
        (*out_rle_base64)[out_pos++] = (i + 2 < pos) ? alphabet[val & 0x3F] : '=';
    }
    
    (*out_rle_base64)[out_pos] = '\0';
    if (out_len) *out_len = out_pos;
    
    free(binary);
    return 0;
}
