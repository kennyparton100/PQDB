# SDK LZ4 Documentation

Comprehensive documentation for the SDK LZ4 fast compression module used for chunk data compression.

**Module:** `SDK/Core/LZ4/`  
**Output:** `SDK/Docs/Core/LZ4/SDK_LZ4.md`

## Table of Contents

- [Module Overview](#module-overview)
- [Algorithm Overview](#algorithm-overview)
- [Public API](#public-api)
- [Compression Format](#compression-format)
- [Constants & Limits](#constants--limits)
- [Usage Examples](#usage-examples)
- [Safety Considerations](#safety-considerations)
- [Performance Notes](#performance-notes)
- [AI Context Hints](#ai-context-hints)

---

## Module Overview

The LZ4 module provides fast lossless compression optimized for real-time chunk data storage. It implements the LZ4 algorithm (version 1.9.4) with a simplified API focused on one-shot compression and decompression operations.

**Key Features:**
- Extremely fast compression speed (memory-bandwidth limited)
- Moderate compression ratio (typical 2:1 for game data)
- Simple 3-function API
- Streaming-capable internal structure
- BSD 2-Clause licensed (Yann Collet)

---

## Algorithm Overview

LZ4 is an LZ77-family compression algorithm optimized for speed:

### Core Mechanism

```
Input: Uncompressed data stream
Output: Token sequence with literal runs and match references

1. Hash table tracks recent 4-byte sequences
2. At each position, check for match in hash table
3. If match found (>= 4 bytes):
   - Output: [token][literal length bytes][literals][match offset][match length bytes]
4. If no match:
   - Accumulate literals until match found or limit reached
5. Final literal run at end
```

### Token Structure

```
Token byte: RRRR_MMMM
- RRRR (4 bits): Run length - 1 (literal count - 1, or 15+ extended)
- MMMM (4 bits): Match length - 4 (or 15+ extended)

Extended lengths use additional bytes (255 = continue, <255 = final)
```

### Match Reference

```
Match offset: 2-byte little-endian
- Distance back from current position (1-65535)
- Match length: minimum 4 bytes, can extend to many KB
```

---

## Public API

### Function Summary

| Function | Signature | Description |
|----------|-----------|-------------|
| `LZ4_compress_default` | `(const char* src, char* dst, int srcSize, int dstCapacity) → int` | Compress data |
| `LZ4_decompress_safe` | `(const char* src, char* dst, int compressedSize, int dstCapacity) → int` | Decompress data |
| `LZ4_compressBound` | `(int inputSize) → int` | Calculate max compressed size |

### LZ4_compress_default

```c
int LZ4_compress_default(
    const char* source,      // Input data
    char* dest,              // Output buffer
    int inputSize,           // Source size in bytes
    int maxOutputSize        // Destination capacity
);
// Returns: compressed size, or 0 if failed
```

**Behavior:**
- Compresses `inputSize` bytes from `source` into `dest`
- Requires `maxOutputSize >= LZ4_compressBound(inputSize)`
- Returns compressed size on success, 0 on failure
- Uses default compression level (fast)

### LZ4_decompress_safe

```c
int LZ4_decompress_safe(
    const char* source,      // Compressed data
    char* dest,              // Output buffer
    int compressedSize,      // Source size (compressed)
    int maxDecompressedSize  // Destination capacity
);
// Returns: decompressed size, or negative error code
```

**Behavior:**
- Decompresses `compressedSize` bytes into `dest`
- Requires `maxDecompressedSize >= original size`
- Returns decompressed size on success
- Returns negative value on error (corrupt data, buffer overflow)
- **Safe**: will not write beyond `maxDecompressedSize`

### LZ4_compressBound

```c
int LZ4_compressBound(int isize);
// Returns: maximum compressed size for input of isize bytes
```

**Formula:** `isize + (isize/255) + 16`

Used to allocate output buffers for compression.

---

## Compression Format

### Stream Structure

```
[Sequence 1] [Sequence 2] ... [Sequence N] [Final literals]

Sequence:
    [Token]                    - 1 byte
    [Literal Length Extended]  - 0-N bytes (if RRRR == 15)
    [Literals]                   - LiteralLength bytes
    [Match Offset]             - 2 bytes little-endian
    [Match Length Extended]    - 0-N bytes (if MMMM == 15)

Final Literals:
    [Token]                    - 1 byte
    [Literal Length Extended]  - 0-N bytes
    [Literals]                   - LastRun bytes
```

### Token Encoding

```c
// Literal run length encoding
token = (literal_run_length << 4) & 0xF0;
if (literal_run_length >= 15) {
    token = 0xF0;
    remaining = literal_run_length - 15;
    while (remaining >= 255) {
        output 255;
        remaining -= 255;
    }
    output remaining;
}

// Match length encoding (adds MINMATCH=4)
match_length_token = match_length - 4;
if (match_length_token >= 15) {
    token |= 0x0F;
    remaining = match_length_token - 15;
    while (remaining >= 255) {
        output 255;
        remaining -= 255;
    }
    output remaining;
} else {
    token |= match_length_token;
}
```

### Decompression Flow

```c
while (input remains) {
    token = read_byte();
    literal_len = (token >> 4);
    if (literal_len == 15) literal_len += read_extended_length();
    
    copy_literals(literal_len);
    if (input exhausted) break;
    
    offset = read_u16_le();
    match_len = (token & 0x0F) + 4;
    if ((token & 0x0F) == 15) match_len += read_extended_length();
    
    copy_match(offset, match_len);  // May overlap
}
```

---

## Constants & Limits

### Build Constants

| Constant | Value | Description |
|----------|-------|-------------|
| `LZ4_VERSION_MAJOR` | 1 | Major version |
| `LZ4_VERSION_MINOR` | 9 | Minor version |
| `LZ4_VERSION_RELEASE` | 4 | Release version |
| `LZ4_MEMORY_USAGE` | 14 | Hash table size exponent |
| `LZ4_HASHTABLESIZE` | 16384 | Hash table entries (2^14) |
| `LZ4_HASH_SIZE_U32` | 4096 | Hash table size in uint32s |
| `MINMATCH` | 4 | Minimum match length |
| `WILDCOPYLENGTH` | 8 | Fast copy threshold |
| `LASTLITERALS` | 5 | Trailer safety margin |
| `MFLIMIT` | 12 | Minimum input for compression |

### Compression Limits

| Limit | Value | Notes |
|-------|-------|-------|
| Max input size | ~1.9 GB (0x7E000000) | Practical limit |
| Max compression overhead | input/255 + 16 bytes | Worst case |
| Min match offset | 1 byte | Back-reference distance |
| Max match offset | 65535 bytes | 16-bit offset field |
| Match extension | 255-byte blocks | Unlimited via chaining |

---

## Usage Examples

### Basic Compression

```c
#include "LZ4/lz4.h"

void compress_chunk(const void* data, int size, void* compressed, int* compressed_size) {
    int max_size = LZ4_compressBound(size);
    
    *compressed_size = LZ4_compress_default(
        (const char*)data,
        (char*)compressed,
        size,
        max_size
    );
    
    if (*compressed_size == 0) {
        // Compression failed - store uncompressed
        memcpy(compressed, data, size);
        *compressed_size = size;
    }
}
```

### Basic Decompression

```c
int decompress_chunk(const void* compressed, int compressed_size, 
                     void* output, int max_output_size) {
    int result = LZ4_decompress_safe(
        (const char*)compressed,
        (char*)output,
        compressed_size,
        max_output_size
    );
    
    if (result < 0) {
        // Decompression error
        return 0; // Failed
    }
    
    return result; // Decompressed size
}
```

### Buffer Allocation Pattern

```c
void* allocate_compress_buffer(int input_size, int* buffer_size) {
    *buffer_size = LZ4_compressBound(input_size);
    return malloc(*buffer_size);
}

void* allocate_decompress_buffer(int max_original_size) {
    return malloc(max_original_size);
}
```

---

## Safety Considerations

### Compression Safety

- **Output buffer**: Must be at least `LZ4_compressBound(inputSize)`
- **Return value**: Check for 0 (failure)
- **Small input**: Returns 0 for inputs < 13 bytes (consider storing uncompressed)

### Decompression Safety

- **Output buffer**: Must be large enough for original data
- **Compressed size**: Must be exact size of compressed input
- **Return value**: Negative indicates corrupt/invalid data
- **Overflow protection**: Function will not write beyond `dstCapacity`

### Corruption Handling

```c
int safe_decompress(const char* compressed, int compressed_size,
                    char* output, int output_capacity,
                    int expected_size) {
    int result = LZ4_decompress_safe(compressed, output, 
                                      compressed_size, output_capacity);
    
    if (result < 0) {
        log_error("LZ4 decompression failed: error %d", result);
        return -1;
    }
    
    if (result != expected_size) {
        log_error("LZ4 size mismatch: got %d, expected %d", result, expected_size);
        return -1;
    }
    
    return result;
}
```

---

## Performance Notes

### Compression Speed

- **Typical**: >500 MB/s compression
- **Memory bound**: Performance limited by memory bandwidth
- **Small inputs**: Overhead dominates, batch if possible
- **Already compressed**: May expand slightly (rare, handled by caller)

### Decompression Speed

- **Typical**: >1 GB/s decompression
- **Multiple GB/s**: Possible on modern CPUs
- **Fast path**: Uses wildcopy for long matches
- **Overlap handling**: Correctly handles overlapping matches

### Memory Usage

- **Compression**: ~64KB hash table (stream state)
- **Decompression**: No additional memory required
- **Stack safe**: No large stack allocations

### Best Practices

1. **Batch compression**: Compress multiple chunks together if possible
2. **Buffer reuse**: Reallocate hash table state between calls
3. **Size checking**: Always verify sizes before decompression
4. **Fallback**: Store uncompressed if compression expands data

---

## AI Context Hints

### Chunk Compression Workflow

```c
typedef struct {
    void* compressed_data;
    int compressed_size;
    int original_size;
    bool is_compressed;  // False if compression expanded
} ChunkCompressionResult;

ChunkCompressionResult compress_chunk_data(const void* chunk, int size) {
    ChunkCompressionResult result = {0};
    int max_size = LZ4_compressBound(size);
    
    result.compressed_data = malloc(max_size);
    result.compressed_size = LZ4_compress_default(
        chunk, result.compressed_data, size, max_size
    );
    result.original_size = size;
    
    if (result.compressed_size == 0 || result.compressed_size >= size) {
        // Compression failed or expanded - store raw
        free(result.compressed_data);
        result.compressed_data = malloc(size);
        memcpy(result.compressed_data, chunk, size);
        result.compressed_size = size;
        result.is_compressed = false;
    } else {
        result.is_compressed = true;
        // Shrink to actual size
        result.compressed_data = realloc(result.compressed_data, 
                                          result.compressed_size);
    }
    
    return result;
}
```

### Persisting Compressed Data

```c
void save_compressed_data(FILE* file, const void* data, int size) {
    char* compressed = malloc(LZ4_compressBound(size));
    int compressed_size = LZ4_compress_default(data, compressed, size, 
                                              LZ4_compressBound(size));
    
    // Write header
    fwrite(&size, sizeof(int), 1, file);              // Original size
    fwrite(&compressed_size, sizeof(int), 1, file);   // Compressed size
    
    // Write data (use compressed if beneficial)
    if (compressed_size > 0 && compressed_size < size) {
        fwrite(compressed, 1, compressed_size, file);
    } else {
        fwrite(data, 1, size, file);
        compressed_size = size;  // Update header in real implementation
    }
    
    free(compressed);
}
```

### Parallel Compression

```c
// LZ4 is single-threaded but stateless
// Each thread can have its own LZ4_stream_t_internal
void parallel_compress_chunks(Chunk* chunks, int count, int num_threads) {
    #pragma omp parallel for num_threads(num_threads)
    for (int i = 0; i < count; i++) {
        // Each thread calls LZ4_compress_default independently
        // No shared state conflicts
        chunks[i].compressed_size = LZ4_compress_default(...);
    }
}
```

---

## Related Documentation

- `SDK/Core/World/` - World chunk storage uses LZ4
- `SDK/Core/Persistence/` - Save file compression
- `SDK/Core/Networking/` - Network packet compression

---

**Source Files:**
- `SDK/Core/LZ4/lz4.h` (1,961 bytes) - Public API header
- `SDK/Core/LZ4/lz4.c` (6,495 bytes) - LZ4 1.9.4 implementation

**External Reference:**
- LZ4 Official: https://github.com/lz4/lz4
- Format Spec: https://github.com/lz4/lz4/blob/dev/doc/lz4_Frame_format.md
