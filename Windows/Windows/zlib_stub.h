/* Minimal zlib stub for testing purposes */
#ifndef ZLIB_STUB_H
#define ZLIB_STUB_H

#include <stdlib.h>
#include <stdint.h>
#include <string.h>

/* Basic types */
typedef unsigned char Byte;
typedef unsigned int uInt;
typedef unsigned long uLong;
typedef uLong uLongf;
typedef void* voidpf;
typedef void* voidp;
typedef struct z_stream_s z_stream;
typedef Byte Bytef;

typedef struct z_stream_s {
    Bytef *next_in;
    uInt avail_in;
    uLong total_in;
    Bytef *next_out;
    uInt avail_out;
    uLong total_out;
    char *msg;
    void *state;
    void *workspace;
    int data_type;
    uLong adler;
    uLong reserved;
} z_stream;

/* Return codes */
#define Z_OK 0
#define Z_STREAM_END 1
#define Z_NO_FLUSH 0

/* Stub functions - for testing, just do simple copy/no-op */
int inflateInit(z_stream* strm) { 
    strm->total_in = 0;
    strm->total_out = 0;
    return Z_OK; 
}

int inflate(z_stream* strm, int flush) { 
    /* For testing, just copy available data */
    if (strm->avail_in > 0 && strm->avail_out > 0) {
        uInt copy_len = (strm->avail_in < strm->avail_out) ? strm->avail_in : strm->avail_out;
        memcpy(strm->next_out, strm->next_in, copy_len);
        strm->next_in += copy_len;
        strm->next_out += copy_len;
        strm->avail_in -= copy_len;
        strm->avail_out -= copy_len;
        strm->total_in += copy_len;
        strm->total_out += copy_len;
        return Z_OK;
    }
    return Z_STREAM_END;
}

int inflateEnd(z_stream* strm) { return Z_OK; }

int compress2(Bytef* dest, uLongf* destLen, const Bytef* source, uLong sourceLen, int level) { 
    /* Simple copy for testing */
    if (*destLen < sourceLen) return -1;
    memcpy(dest, source, sourceLen);
    *destLen = sourceLen;
    return Z_OK;
}

uLong compressBound(uLong sourceLen) { return sourceLen + 16; }

#endif /* ZLIB_STUB_H */
