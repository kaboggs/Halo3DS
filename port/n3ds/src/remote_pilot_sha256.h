/* New3DS integration additions by Kevin Boggs. */
#ifndef REMOTE_PILOT_SHA256_H
#define REMOTE_PILOT_SHA256_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t state[8];
    unsigned char buffer[64];
    uint64_t total;
    size_t used;
} rp_sha256_ctx;

void rp_sha256_init(rp_sha256_ctx *ctx);
void rp_sha256_update(rp_sha256_ctx *ctx, const unsigned char *data, size_t length);
void rp_sha256_final(rp_sha256_ctx *ctx, unsigned char output[32]);

/* FIPS 180-4 SHA-256. The caller owns a 32-byte output buffer. */
void rp_sha256(const unsigned char *data, size_t length, unsigned char output[32]);

#endif
