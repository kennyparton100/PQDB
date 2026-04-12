/**
 * nql_crypto.c - NQL Cryptography foundations.
 * Part of the CPSS Viewer amalgamation.
 *
 * SHA-256, HMAC, Base64, Hex encoding, XOR cipher, RSA primitives,
 * Diffie-Hellman key exchange, LFSR.
 */

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Forward declarations */
typedef NqlValue (*NqlBuiltinFn)(const NqlValue* args, uint32_t argc, void* db_ctx);
void nql_register_func(const char* name, NqlBuiltinFn fn, uint32_t min_args, uint32_t max_args, const char* desc);

/* ======================================================================
 * SHA-256 implementation (FIPS 180-4)
 * ====================================================================== */

static const uint32_t sha256_k[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

#define SHA_ROR(x,n) (((x)>>(n))|((x)<<(32-(n))))
#define SHA_CH(x,y,z) (((x)&(y))^((~(x))&(z)))
#define SHA_MAJ(x,y,z) (((x)&(y))^((x)&(z))^((y)&(z)))
#define SHA_EP0(x) (SHA_ROR(x,2)^SHA_ROR(x,13)^SHA_ROR(x,22))
#define SHA_EP1(x) (SHA_ROR(x,6)^SHA_ROR(x,11)^SHA_ROR(x,25))
#define SHA_SIG0(x) (SHA_ROR(x,7)^SHA_ROR(x,18)^((x)>>3))
#define SHA_SIG1(x) (SHA_ROR(x,17)^SHA_ROR(x,19)^((x)>>10))

static void nql_sha256(const uint8_t* data, size_t len, uint8_t hash[32]) {
    uint32_t h[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    /* Padding */
    size_t padded_len = ((len + 9 + 63) / 64) * 64;
    uint8_t* msg = (uint8_t*)calloc(padded_len, 1);
    if (!msg) return;
    memcpy(msg, data, len);
    msg[len] = 0x80;
    uint64_t bitlen = (uint64_t)len * 8;
    for (int i = 0; i < 8; i++) msg[padded_len - 1 - i] = (uint8_t)(bitlen >> (i * 8));

    for (size_t offset = 0; offset < padded_len; offset += 64) {
        uint32_t w[64];
        for (int i = 0; i < 16; i++)
            w[i] = ((uint32_t)msg[offset + i*4] << 24) | ((uint32_t)msg[offset + i*4+1] << 16) |
                    ((uint32_t)msg[offset + i*4+2] << 8) | (uint32_t)msg[offset + i*4+3];
        for (int i = 16; i < 64; i++)
            w[i] = SHA_SIG1(w[i-2]) + w[i-7] + SHA_SIG0(w[i-15]) + w[i-16];
        uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],hh=h[7];
        for (int i = 0; i < 64; i++) {
            uint32_t t1 = hh + SHA_EP1(e) + SHA_CH(e,f,g) + sha256_k[i] + w[i];
            uint32_t t2 = SHA_EP0(a) + SHA_MAJ(a,b,c);
            hh=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
        }
        h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d; h[4]+=e; h[5]+=f; h[6]+=g; h[7]+=hh;
    }
    free(msg);
    for (int i = 0; i < 8; i++) {
        hash[i*4]   = (uint8_t)(h[i]>>24); hash[i*4+1] = (uint8_t)(h[i]>>16);
        hash[i*4+2] = (uint8_t)(h[i]>>8);  hash[i*4+3] = (uint8_t)h[i];
    }
}

/* ======================================================================
 * NQL FUNCTIONS
 * ====================================================================== */

/** SHA256(string) -- returns hex string */
static NqlValue nql_fn_sha256(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_STRING) return nql_val_null();
    uint8_t hash[32];
    nql_sha256((const uint8_t*)args[0].v.sval, strlen(args[0].v.sval), hash);
    NqlValue v; v.type = NQL_VAL_STRING;
    for (int i = 0; i < 32; i++) sprintf(v.v.sval + i * 2, "%02x", hash[i]);
    v.v.sval[64] = '\0';
    return v;
}

/** HMAC_SHA256(key_string, message_string) -- returns hex string */
static NqlValue nql_fn_hmac_sha256(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_STRING || args[1].type != NQL_VAL_STRING) return nql_val_null();
    const char* key = args[0].v.sval;
    const char* msg = args[1].v.sval;
    size_t key_len = strlen(key), msg_len = strlen(msg);
    uint8_t k[64]; memset(k, 0, 64);
    if (key_len > 64) { nql_sha256((const uint8_t*)key, key_len, k); }
    else memcpy(k, key, key_len);
    uint8_t ipad[64], opad[64];
    for (int i = 0; i < 64; i++) { ipad[i] = k[i] ^ 0x36; opad[i] = k[i] ^ 0x5c; }
    /* inner = SHA256(ipad || msg) */
    size_t inner_len = 64 + msg_len;
    uint8_t* inner_data = (uint8_t*)malloc(inner_len);
    if (!inner_data) return nql_val_null();
    memcpy(inner_data, ipad, 64); memcpy(inner_data + 64, msg, msg_len);
    uint8_t inner_hash[32];
    nql_sha256(inner_data, inner_len, inner_hash);
    free(inner_data);
    /* outer = SHA256(opad || inner_hash) */
    uint8_t outer_data[96];
    memcpy(outer_data, opad, 64); memcpy(outer_data + 64, inner_hash, 32);
    uint8_t hash[32];
    nql_sha256(outer_data, 96, hash);
    NqlValue v; v.type = NQL_VAL_STRING;
    for (int i = 0; i < 32; i++) sprintf(v.v.sval + i * 2, "%02x", hash[i]);
    v.v.sval[64] = '\0';
    return v;
}

/** HEX_ENCODE(string) */
static NqlValue nql_fn_hex_encode(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_STRING) return nql_val_null();
    size_t len = strlen(args[0].v.sval);
    if (len * 2 >= 256) return nql_val_null();
    NqlValue v; v.type = NQL_VAL_STRING;
    for (size_t i = 0; i < len; i++) sprintf(v.v.sval + i * 2, "%02x", (uint8_t)args[0].v.sval[i]);
    v.v.sval[len * 2] = '\0';
    return v;
}

/** HEX_DECODE(hex_string) */
static NqlValue nql_fn_hex_decode(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_STRING) return nql_val_null();
    const char* s = args[0].v.sval;
    size_t len = strlen(s);
    if (len % 2 != 0 || len / 2 >= 256) return nql_val_null();
    NqlValue v; v.type = NQL_VAL_STRING;
    for (size_t i = 0; i < len / 2; i++) {
        unsigned int byte; sscanf(s + i * 2, "%2x", &byte);
        v.v.sval[i] = (char)byte;
    }
    v.v.sval[len / 2] = '\0';
    return v;
}

/* Base64 tables */
static const char b64_enc[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/** BASE64_ENCODE(string) */
static NqlValue nql_fn_b64_encode(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_STRING) return nql_val_null();
    const uint8_t* src = (const uint8_t*)args[0].v.sval;
    size_t len = strlen(args[0].v.sval);
    size_t out_len = ((len + 2) / 3) * 4;
    if (out_len >= 256) return nql_val_null();
    NqlValue v; v.type = NQL_VAL_STRING;
    size_t j = 0;
    for (size_t i = 0; i < len; i += 3) {
        uint32_t n = (uint32_t)src[i] << 16;
        if (i + 1 < len) n |= (uint32_t)src[i+1] << 8;
        if (i + 2 < len) n |= (uint32_t)src[i+2];
        v.v.sval[j++] = b64_enc[(n >> 18) & 63];
        v.v.sval[j++] = b64_enc[(n >> 12) & 63];
        v.v.sval[j++] = (i + 1 < len) ? b64_enc[(n >> 6) & 63] : '=';
        v.v.sval[j++] = (i + 2 < len) ? b64_enc[n & 63] : '=';
    }
    v.v.sval[j] = '\0';
    return v;
}

static int b64_dec_char(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62; if (c == '/') return 63;
    return -1;
}

/** BASE64_DECODE(string) */
static NqlValue nql_fn_b64_decode(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_STRING) return nql_val_null();
    const char* s = args[0].v.sval;
    size_t len = strlen(s);
    if (len % 4 != 0) return nql_val_null();
    NqlValue v; v.type = NQL_VAL_STRING;
    size_t j = 0;
    for (size_t i = 0; i < len; i += 4) {
        int a = b64_dec_char(s[i]), b = b64_dec_char(s[i+1]);
        int c = (s[i+2] != '=') ? b64_dec_char(s[i+2]) : 0;
        int d = (s[i+3] != '=') ? b64_dec_char(s[i+3]) : 0;
        if (a < 0 || b < 0) break;
        uint32_t n = ((uint32_t)a << 18) | ((uint32_t)b << 12) | ((uint32_t)c << 6) | (uint32_t)d;
        v.v.sval[j++] = (char)((n >> 16) & 0xFF);
        if (s[i+2] != '=') v.v.sval[j++] = (char)((n >> 8) & 0xFF);
        if (s[i+3] != '=') v.v.sval[j++] = (char)(n & 0xFF);
        if (j >= 254) break;
    }
    v.v.sval[j] = '\0';
    return v;
}

/** XOR_CIPHER(text, key) -- XOR each byte of text with cycling key */
static NqlValue nql_fn_xor_cipher(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_STRING || args[1].type != NQL_VAL_STRING) return nql_val_null();
    const char* text = args[0].v.sval;
    const char* key = args[1].v.sval;
    size_t tlen = strlen(text), klen = strlen(key);
    if (klen == 0) return nql_val_null();
    NqlValue v; v.type = NQL_VAL_STRING;
    for (size_t i = 0; i < tlen && i < 255; i++)
        v.v.sval[i] = text[i] ^ key[i % klen];
    v.v.sval[tlen < 255 ? tlen : 255] = '\0';
    return v;
}

/** RSA_KEYGEN(p, q) -- returns [n, e, d] where e=65537 */
static NqlValue nql_fn_rsa_keygen(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc; NQL_NULL_GUARD2(args);
    int64_t p = nql_val_as_int(&args[0]);
    int64_t q = nql_val_as_int(&args[1]);
    if (p < 3 || q < 3 || p == q) return nql_val_null();
    int64_t n = p * q;
    int64_t phi = (p - 1) * (q - 1);
    int64_t e = 65537;
    if (phi <= e) return nql_val_null();
    /* Extended GCD to find d = e^{-1} mod phi */
    int64_t g = phi, x = 0, y = 1, aa = e;
    while (aa > 0) {
        int64_t qq = g / aa, tmp = g - qq * aa; g = aa; aa = tmp;
        tmp = x - qq * y; x = y; y = tmp;
    }
    if (g != 1) return nql_val_null(); /* e and phi not coprime */
    int64_t d = x % phi;
    if (d < 0) d += phi;
    NqlArray* r = nql_array_alloc(3);
    if (!r) return nql_val_null();
    nql_array_push(r, nql_val_int(n));
    nql_array_push(r, nql_val_int(e));
    nql_array_push(r, nql_val_int(d));
    return nql_val_array(r);
}

/** RSA_ENCRYPT(message_int, e, n) -- m^e mod n */
static NqlValue nql_fn_rsa_encrypt(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc; NQL_NULL_GUARD3(args);
    int64_t m = nql_val_as_int(&args[0]);
    int64_t e = nql_val_as_int(&args[1]);
    int64_t n = nql_val_as_int(&args[2]);
    if (n <= 0 || m < 0 || m >= n) return nql_val_null();
    uint64_t result = powmod_u64((uint64_t)m, (uint64_t)e, (uint64_t)n);
    return nql_val_int((int64_t)result);
}

/** RSA_DECRYPT(cipher_int, d, n) -- c^d mod n */
static NqlValue nql_fn_rsa_decrypt(const NqlValue* args, uint32_t argc, void* ctx) {
    return nql_fn_rsa_encrypt(args, argc, ctx); /* same operation */
}

/** DH_KEYGEN(g, p) -- generate private key a, public key g^a mod p: [private, public] */
static NqlValue nql_fn_dh_keygen(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc; NQL_NULL_GUARD2(args);
    int64_t g = nql_val_as_int(&args[0]);
    int64_t p = nql_val_as_int(&args[1]);
    if (p < 3 || g < 2) return nql_val_null();
    int64_t a = (rand() % (p - 2)) + 1; /* private key */
    int64_t pub = (int64_t)powmod_u64((uint64_t)g, (uint64_t)a, (uint64_t)p);
    NqlArray* r = nql_array_alloc(2);
    if (!r) return nql_val_null();
    nql_array_push(r, nql_val_int(a));
    nql_array_push(r, nql_val_int(pub));
    return nql_val_array(r);
}

/** DH_SHARED_SECRET(other_public, my_private, p) -- other^private mod p */
static NqlValue nql_fn_dh_shared(const NqlValue* args, uint32_t argc, void* ctx) {
    return nql_fn_rsa_encrypt(args, argc, ctx); /* same POWMOD */
}

/** LFSR(state, taps_array, steps) -- linear feedback shift register */
static NqlValue nql_fn_lfsr(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc; NQL_NULL_GUARD3(args);
    int64_t state = nql_val_as_int(&args[0]);
    if (args[1].type != NQL_VAL_ARRAY || !args[1].v.array) return nql_val_null();
    NqlArray* taps = args[1].v.array;
    int64_t steps = nql_val_as_int(&args[2]);
    if (steps < 1 || steps > 100000) return nql_val_null();
    NqlArray* result = nql_array_alloc((uint32_t)steps);
    if (!result) return nql_val_null();
    for (int64_t s = 0; s < steps; s++) {
        int bit = 0;
        for (uint32_t i = 0; i < taps->length; i++)
            bit ^= (int)((state >> nql_val_as_int(&taps->items[i])) & 1);
        state = ((state >> 1) | ((int64_t)bit << 63));
        nql_array_push(result, nql_val_int(state));
    }
    return nql_val_array(result);
}

/** HASH_INT(n) -- simple hash of integer to int64 (FNV-1a style) */
static NqlValue nql_fn_hash_int(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc; NQL_NULL_GUARD1(args);
    int64_t n = nql_val_as_int(&args[0]);
    uint64_t h = 14695981039346656037ULL;
    uint8_t* bytes = (uint8_t*)&n;
    for (int i = 0; i < 8; i++) { h ^= bytes[i]; h *= 1099511628211ULL; }
    return nql_val_int((int64_t)h);
}

/* ======================================================================
 * REGISTRATION
 * ====================================================================== */

void nql_crypto_register_functions(void) {
    nql_register_func("SHA256",            nql_fn_sha256,       1, 1, "SHA-256 hash as hex string");
    nql_register_func("HMAC_SHA256",       nql_fn_hmac_sha256,  2, 2, "HMAC-SHA256(key, message)");
    nql_register_func("HEX_ENCODE",        nql_fn_hex_encode,   1, 1, "String to hex");
    nql_register_func("HEX_DECODE",        nql_fn_hex_decode,   1, 1, "Hex to string");
    nql_register_func("BASE64_ENCODE",     nql_fn_b64_encode,   1, 1, "String to Base64");
    nql_register_func("BASE64_DECODE",     nql_fn_b64_decode,   1, 1, "Base64 to string");
    nql_register_func("XOR_CIPHER",        nql_fn_xor_cipher,   2, 2, "XOR cipher with repeating key");
    nql_register_func("RSA_KEYGEN",        nql_fn_rsa_keygen,   2, 2, "RSA keygen from primes: [n, e, d]");
    nql_register_func("RSA_ENCRYPT",       nql_fn_rsa_encrypt,  3, 3, "RSA encrypt: m^e mod n");
    nql_register_func("RSA_DECRYPT",       nql_fn_rsa_decrypt,  3, 3, "RSA decrypt: c^d mod n");
    nql_register_func("DH_KEYGEN",         nql_fn_dh_keygen,    2, 2, "Diffie-Hellman keygen: [private, public]");
    nql_register_func("DH_SHARED_SECRET",  nql_fn_dh_shared,    3, 3, "DH shared secret: pub^priv mod p");
    nql_register_func("LFSR",              nql_fn_lfsr,          3, 3, "Linear feedback shift register");
    nql_register_func("HASH_INT",          nql_fn_hash_int,     1, 1, "FNV-1a hash of integer");
}
