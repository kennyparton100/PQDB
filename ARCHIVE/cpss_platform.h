/**
 * cpss_platform.h - Platform includes, defines, and compiler pragmas.
 * Part of the CPSS Viewer amalgamation.
 */

#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>
#include <direct.h>
#include <io.h>

#ifdef _MSC_VER
#include <intrin.h>
#endif

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#ifdef _MSC_VER
#ifdef _DEBUG
#pragma comment(lib, "zlibd.lib")
#else
#pragma comment(lib, "zlib.lib")
#endif
#endif

#define ARRAY_LEN(x) (sizeof(x) / sizeof((x)[0]))
#define PRESIEVE_LIMIT 1000u
#define DEFAULT_TRILLION 1000000000000ULL
#define TEMP_PATH_CAP 512u
#define CPSR_MAGIC 0x52535043u   /* "CPSR" little-endian — raw (decompressed) format */
#define CPSR_VERSION 1u
#define CPSR_HEADER_SIZE 16u     /* magic(4) + version(4) + flags(4) + reserved(4) */
