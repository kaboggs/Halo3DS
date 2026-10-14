/* New3DS integration additions by Kevin Boggs.
 * FIPS 180-4 SHA-256 for the short, authenticated Remote Pilot messages.
 * This path does not depend on the FS hash service, which gives an unusable
 * constant digest in the current emulator. No dynamic allocation or I/O. */
#include <stdint.h>
#include <string.h>
#include "remote_pilot_sha256.h"

static const uint32_t k[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
    0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
    0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
    0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
};

/* Kevin Boggs: rotate - New3DS integration. */
static uint32_t rotate(uint32_t x, unsigned shift)
{
    return (x >> shift) | (x << (32 - shift));
}

/* Kevin Boggs: block_hash - New3DS integration. */
static void block_hash(uint32_t h[8], const unsigned char block[64])
{
    uint32_t words[64];
    for (unsigned i = 0; i < 16; ++i)
        words[i] = ((uint32_t)block[4 * i] << 24) |
            ((uint32_t)block[4 * i + 1] << 16) |
            ((uint32_t)block[4 * i + 2] << 8) | block[4 * i + 3];
    for (unsigned i = 16; i < 64; ++i) {
        uint32_t a = words[i - 15], b = words[i - 2];
        uint32_t s0 = rotate(a, 7) ^ rotate(a, 18) ^ (a >> 3);
        uint32_t s1 = rotate(b, 17) ^ rotate(b, 19) ^ (b >> 10);
        words[i] = words[i - 16] + s0 + words[i - 7] + s1;
    }
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
    uint32_t e = h[4], f = h[5], g = h[6], t = h[7];
    for (unsigned i = 0; i < 64; ++i) {
        uint32_t s1 = rotate(e, 6) ^ rotate(e, 11) ^ rotate(e, 25);
        uint32_t choose = (e & f) ^ (~e & g);
        uint32_t first = t + s1 + choose + k[i] + words[i];
        uint32_t s0 = rotate(a, 2) ^ rotate(a, 13) ^ rotate(a, 22);
        uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        uint32_t second = s0 + majority;
        t = g; g = f; f = e; e = d + first;
        d = c; c = b; b = a; a = first + second;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d;
    h[4] += e; h[5] += f; h[6] += g; h[7] += t;
    volatile uint32_t *wipe = words;
    for (unsigned i = 0; i < 64; ++i) wipe[i] = 0;
}

/* Kevin Boggs: rp_sha256_init - New3DS integration. */
void rp_sha256_init(rp_sha256_ctx *ctx)
{
    static const uint32_t initial[8] = {
        0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u
    };
    memcpy(ctx->state, initial, sizeof(initial));
    memset(ctx->buffer, 0, sizeof(ctx->buffer));
    ctx->total = 0;
    ctx->used = 0;
}

/* Kevin Boggs: rp_sha256_update - New3DS integration. */
void rp_sha256_update(rp_sha256_ctx *ctx, const unsigned char *data, size_t length)
{
    if (!length) return;
    ctx->total += length;
    if (ctx->used) {
        size_t take = length < 64 - ctx->used ? length : 64 - ctx->used;
        memcpy(ctx->buffer + ctx->used, data, take);
        ctx->used += take;
        data += take;
        length -= take;
        if (ctx->used == 64) {
            block_hash(ctx->state, ctx->buffer);
            ctx->used = 0;
        }
    }
    while (length >= 64) {
        block_hash(ctx->state, data);
        data += 64;
        length -= 64;
    }
    if (length) {
        memcpy(ctx->buffer, data, length);
        ctx->used = length;
    }
}

/* Kevin Boggs: rp_sha256_final - New3DS integration. */
void rp_sha256_final(rp_sha256_ctx *ctx, unsigned char output[32])
{
    unsigned char last[64] = {0};
    size_t remaining = ctx->used;
    if (remaining) memcpy(last, ctx->buffer, remaining);
    last[remaining] = 0x80;
    if (remaining >= 56) {
        block_hash(ctx->state, last);
        memset(last, 0, sizeof(last));
    }
    uint64_t bits = ctx->total * 8;
    for (unsigned i = 0; i < 8; ++i)
        last[56 + i] = (unsigned char)(bits >> (56 - 8 * i));
    block_hash(ctx->state, last);
    for (unsigned i = 0; i < 8; ++i)
        for (unsigned j = 0; j < 4; ++j)
            output[4 * i + j] = (unsigned char)(ctx->state[i] >> (24 - 8 * j));
    volatile unsigned char *wipe = last;
    for (unsigned i = 0; i < sizeof(last); ++i) wipe[i] = 0;
    volatile unsigned char *wipe_context = (unsigned char *)ctx;
    for (unsigned i = 0; i < sizeof(*ctx); ++i) wipe_context[i] = 0;
}

/* Kevin Boggs: rp_sha256 - New3DS integration. */
void rp_sha256(const unsigned char *data, size_t length, unsigned char output[32])
{
    rp_sha256_ctx ctx;
    rp_sha256_init(&ctx);
    rp_sha256_update(&ctx, data, length);
    rp_sha256_final(&ctx, output);
}
