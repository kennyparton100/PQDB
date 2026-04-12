/*
   LZ4 - Fast LZ compression algorithm
   Copyright (C) 2011-2020, Yann Collet.

   BSD 2-Clause License (http://www.opensource.org/licenses/bsd-license.php)
*/

#include "lz4.h"
#include <string.h>
#include <stdint.h>

#define LZ4_MEMORY_USAGE 14
#define LZ4_HASHLOG (LZ4_MEMORY_USAGE-2)
#define LZ4_HASHTABLESIZE (1 << LZ4_MEMORY_USAGE)
#define LZ4_HASH_SIZE_U32 (1 << LZ4_HASHLOG)

#define MINMATCH 4
#define WILDCOPYLENGTH 8
#define LASTLITERALS 5
#define MFLIMIT (WILDCOPYLENGTH+MINMATCH)

#define ML_BITS  4
#define ML_MASK  ((1U<<ML_BITS)-1)
#define RUN_BITS (8-ML_BITS)
#define RUN_MASK ((1U<<RUN_BITS)-1)

typedef struct {
    const uint8_t* base;
    const uint8_t* end;
    uint32_t hashTable[LZ4_HASH_SIZE_U32];
} LZ4_stream_t_internal;

static uint32_t LZ4_hash4(uint32_t sequence, uint32_t tableSize) {
    return ((sequence * 2654435761U) >> ((32) - LZ4_HASHLOG)) & (tableSize - 1);
}

static uint32_t LZ4_read32(const void* ptr) {
    uint32_t val;
    memcpy(&val, ptr, sizeof(val));
    return val;
}

static void LZ4_write32(void* ptr, uint32_t value) {
    memcpy(ptr, &value, sizeof(value));
}

int LZ4_compressBound(int isize) {
    return (isize > 0x7E000000) ? 0 : (isize + (isize/255) + 16);
}

int LZ4_compress_default(const char* source, char* dest, int inputSize, int maxOutputSize) {
    LZ4_stream_t_internal ctx;
    const uint8_t* ip = (const uint8_t*)source;
    const uint8_t* anchor = ip;
    const uint8_t* const iend = ip + inputSize;
    const uint8_t* const mflimit = iend - MFLIMIT;
    uint8_t* op = (uint8_t*)dest;
    uint8_t* const oend = op + maxOutputSize;

    if (inputSize < 13) goto _last_literals;
    if (maxOutputSize < LZ4_compressBound(inputSize)) return 0;

    memset(&ctx, 0, sizeof(ctx));
    ctx.base = (const uint8_t*)source;
    ctx.end = (const uint8_t*)source + inputSize;

    ip++;

    while (ip < mflimit) {
        uint32_t const h = LZ4_hash4(LZ4_read32(ip), LZ4_HASH_SIZE_U32);
        uint32_t const matchIndex = ctx.hashTable[h];
        const uint8_t* match = ctx.base + matchIndex;
        
        ctx.hashTable[h] = (uint32_t)(ip - ctx.base);

        if ((matchIndex) && (match < ip) && (LZ4_read32(match) == LZ4_read32(ip))) {
            const uint8_t* const start = ip;
            const uint8_t* ref = match;
            uint32_t litLength;
            uint8_t* token;
            uint32_t matchLength;
            
            while (ip < iend && *ref == *ip) { ref++; ip++; }
            
            litLength = (uint32_t)(start - anchor);
            token = op++;
            
            if (op + litLength + (2 + 1 + LASTLITERALS) + (litLength >> 8) > oend) return 0;
            
            if (litLength >= RUN_MASK) {
                uint32_t len = litLength - RUN_MASK;
                *token = (RUN_MASK << ML_BITS);
                for (; len >= 255; len -= 255) *op++ = 255;
                *op++ = (uint8_t)len;
            } else {
                *token = (uint8_t)(litLength << ML_BITS);
            }
            
            memcpy(op, anchor, litLength);
            op += litLength;
            
            matchLength = (uint32_t)(ip - start);
            
            LZ4_write32(op, (uint32_t)(start - match));
            op += 2;
            
            matchLength -= MINMATCH;
            if (matchLength >= ML_MASK) {
                uint32_t len = matchLength - ML_MASK;
                *token += ML_MASK;
                for (; len >= 255; len -= 255) *op++ = 255;
                *op++ = (uint8_t)len;
            } else {
                *token += (uint8_t)matchLength;
            }
            
            anchor = ip;
        } else {
            ip++;
        }
    }

_last_literals:
    {
        uint32_t lastRun = (uint32_t)(iend - anchor);
        if (op + lastRun + 1 + ((lastRun + 255 - RUN_MASK) / 255) > oend) return 0;
        
        if (lastRun >= RUN_MASK) {
            uint32_t len = lastRun - RUN_MASK;
            *op++ = (RUN_MASK << ML_BITS);
            for (; len >= 255; len -= 255) *op++ = 255;
            *op++ = (uint8_t)len;
        } else {
            *op++ = (uint8_t)(lastRun << ML_BITS);
        }
        memcpy(op, anchor, lastRun);
        op += lastRun;
    }

    return (int)(op - (uint8_t*)dest);
}

int LZ4_decompress_safe(const char* source, char* dest, int compressedSize, int maxDecompressedSize) {
    const uint8_t* ip = (const uint8_t*)source;
    const uint8_t* const iend = ip + compressedSize;
    uint8_t* op = (uint8_t*)dest;
    uint8_t* const oend = op + maxDecompressedSize;
    uint8_t* cpy;

    while (ip < iend) {
        uint8_t token = *ip++;
        uint32_t length = token >> ML_BITS;

        if (length == RUN_MASK) {
            uint8_t s;
            do {
                if (ip >= iend) return -1;
                s = *ip++;
                length += s;
            } while (s == 255);
        }

        cpy = op + length;
        if (cpy > oend - MFLIMIT) return -1;
        memcpy(op, ip, length);
        ip += length;
        op = cpy;

        if (ip >= iend) break;

        {
            uint16_t offset;
            memcpy(&offset, ip, 2);
            ip += 2;
            
            const uint8_t* match = op - offset;
            if (match < (uint8_t*)dest) return -1;

            length = token & ML_MASK;
            if (length == ML_MASK) {
                uint8_t s;
                do {
                    if (ip >= iend) return -1;
                    s = *ip++;
                    length += s;
                } while (s == 255);
            }
            length += MINMATCH;

            cpy = op + length;
            if (cpy > oend - WILDCOPYLENGTH) return -1;

            if (offset < 8) {
                op[0] = match[0];
                op[1] = match[1];
                op[2] = match[2];
                op[3] = match[3];
                match += 4;
                memcpy(op + 4, match, 4);
                match -= offset;
                op += 8;
                while (op < cpy) {
                    memcpy(op, match, 8);
                    op += 8;
                    match += 8;
                }
            } else {
                memcpy(op, match, 8);
                op += 8;
                match += 8;
                while (op < cpy) {
                    memcpy(op, match, 8);
                    op += 8;
                    match += 8;
                }
            }
            op = cpy;
        }
    }

    return (int)(op - (uint8_t*)dest);
}
