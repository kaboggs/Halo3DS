/* New3DS integration additions by Kevin Boggs.
 * Remote Pilot host: https://github.com/kaboggs/RemotePilot
 * MIT terms: LICENSE-KEVIN-MIT.md */
#include <3ds.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <malloc.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#include "remote_pilot_halo.h"
#include "remote_pilot_halo_pad.h"
#include "remote_pilot_halo_touch.h"
#include "remote_pilot_halo_transport.h"
#include "engine_audio.h"
#include "remote_pilot_sha256.h"

#define RP_CONFIG "sdmc:/halo-source/remote-pilot-upstream-dev.cfg"
#define RP_LOG "sdmc:/halo-source-engine.log"
#define RP_SHOT "sdmc:/halo-source/remote-pilot-upstream-dev-shot.rpim"
#define RP_SOCKET_BYTES 0x40000
#define RP_STREAM_IMAGE_BYTES (16 + (120 * 200 + 120 * 160) * 2)
#define RP_STREAM_AUDIO_SAMPLES 8192u
#define RP_STREAM_BYTES (RP_STREAM_IMAGE_BYTES + 8 + RP_STREAM_AUDIO_SAMPLES * 2)

static Thread rp_thread;
static volatile int rp_stop;
static int rp_listener = -1;
static void *rp_soc_memory;
static int rp_soc_started;
static int rp_ps_started;
static unsigned char rp_secret[32];
static int rp_port;
static int rp_config_checked;
#ifdef HALO_N3DS_REMOTE_PILOT_DEV_MENU
static volatile unsigned rp_last_authenticated_ms;
static volatile int rp_retry_requested;
static volatile unsigned rp_bug_it_sequence;
#endif
static LightLock rp_lock;
static int rp_command_state;
static int rp_command_ok;
static char rp_command[128];
static unsigned rp_input_keys;
static unsigned rp_input_frames;
static struct halo_rp_pad_state rp_pad_state;
static struct halo_rp_touch_state rp_touch_state;
static int rp_capture_state;
static unsigned char *rp_stream_buffer;
static unsigned char *rp_stream_packed;
static size_t rp_stream_length;

/* Kevin Boggs: rp_pack_stream - New3DS integration. */
static size_t rp_pack_stream(unsigned char *out, size_t capacity,
    const unsigned char *raw, size_t length)
{
    const size_t pixels = 120u * 200u + 120u * 160u;
    if (length < RP_STREAM_IMAGE_BYTES || capacity < 20) return 0;
    memcpy(out, raw, 16);
    out[12] = 3; out[13] = 0; /* RLE of little-endian RGB565 */
    size_t in = 0, used = 20;
    const unsigned char *source = raw + 16;
    while (in < pixels) {
        size_t run = 1;
        while (run < 128 && in + run < pixels &&
            source[(in + run) * 2] == source[in * 2] &&
            source[(in + run) * 2 + 1] == source[in * 2 + 1]) ++run;
        if (run >= 2) {
            if (used + 3 > capacity) return 0;
            out[used++] = (unsigned char)(0x80u | (run - 1));
            out[used++] = source[in * 2];
            out[used++] = source[in * 2 + 1];
            in += run;
        } else {
            size_t start = in++;
            while (in < pixels && in - start < 128) {
                if (in + 1 < pixels && source[in * 2] == source[(in + 1) * 2] &&
                    source[in * 2 + 1] == source[(in + 1) * 2 + 1]) break;
                ++in;
            }
            size_t count = in - start;
            if (used + 1 + count * 2 > capacity) return 0;
            out[used++] = (unsigned char)(count - 1);
            memcpy(out + used, source + start * 2, count * 2);
            used += count * 2;
        }
    }
    size_t packed = used - 20;
    if (used + length - RP_STREAM_IMAGE_BYTES >= capacity ||
        used + length - RP_STREAM_IMAGE_BYTES >= length) return 0;
    out[16] = (unsigned char)packed;
    out[17] = (unsigned char)(packed >> 8);
    out[18] = (unsigned char)(packed >> 16);
    out[19] = (unsigned char)(packed >> 24);
    memcpy(out + used, raw + RP_STREAM_IMAGE_BYTES,
        length - RP_STREAM_IMAGE_BYTES);
    return used + length - RP_STREAM_IMAGE_BYTES;
}

/* Kevin Boggs: rp_private_ip - New3DS integration. */
static int rp_private_ip(uint32_t host_order)
{
    return (host_order >> 24) == 10 || (host_order >> 20) == 0xAC1 ||
        (host_order >> 16) == 0xC0A8;
}

/* Kevin Boggs: rp_hex_value - New3DS integration. */
static int rp_hex_value(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

/* The owner's explicit session=on setting persists across boots. */
/* Kevin Boggs: rp_load_config - New3DS integration. */
static int rp_load_config(void)
{
    FILE *file = fopen(RP_CONFIG, "rb");
    if (!file) return 0;
    char line[160], token[80] = {0};
    int port = 0, armed = 0;
    while (fgets(line, sizeof(line), file)) {
        if (!strncmp(line, "port=", 5)) port = atoi(line + 5);
        else if (!strncmp(line, "token=", 6))
            sscanf(line + 6, "%79[^\r\n]", token);
        else if (!strcmp(line, "session=on\n") || !strcmp(line, "session=on\r\n")) armed = 1;
    }
    fclose(file);
    if (!armed || port < 1024 || port > 65535 || strlen(token) != 64) return 0;
    for (int i = 0; i < 32; ++i) {
        int high = rp_hex_value(token[i * 2]);
        int low = rp_hex_value(token[i * 2 + 1]);
        if (high < 0 || low < 0) return 0;
        rp_secret[i] = (unsigned char)((high << 4) | low);
    }
    rp_port = port;
    return 1;
}

/* Kevin Boggs: rp_wait - New3DS integration. */
static int rp_wait(int sock, int writing, uint64_t deadline)
{
    while (!rp_stop && osGetTime() < deadline) {
        fd_set set; FD_ZERO(&set); FD_SET(sock, &set);
        struct timeval timeout = {0, 100000};
        int ready = writing ? select(sock + 1, NULL, &set, NULL, &timeout) :
            select(sock + 1, &set, NULL, NULL, &timeout);
        if (ready > 0) return 1;
        if (ready < 0 && errno != EINTR && errno != EAGAIN) return 0;
    }
    return 0;
}

/* Kevin Boggs: rp_send - New3DS integration. */
static int rp_send(int sock, const void *buffer, size_t size, uint64_t deadline)
{
    const char *bytes = buffer;
    while (size) {
        if (!rp_wait(sock, 1, deadline)) return 0;
        int written = send(sock, bytes, size, 0);
        if (written < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
        if (written <= 0) return 0;
        bytes += written; size -= (size_t)written;
    }
    return 1;
}

/* Kevin Boggs: rp_read_line - New3DS integration. */
static int rp_read_line(int sock, char *buffer, size_t capacity, uint64_t deadline)
{
    size_t used = 0;
    while (used + 1 < capacity) {
        if (!rp_wait(sock, 0, deadline)) return 0;
        int count = recv(sock, buffer + used, 1, 0);
        if (count < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
        if (count != 1) return 0;
        if (buffer[used++] == '\n') { buffer[used] = 0; return 1; }
    }
    return 0;
}

/* HMAC uses the bundled FIPS 180-4 SHA-256 implementation. The emulator's
 * FS SHA service produced the same digest for distinct nonces. */
/* Kevin Boggs: rp_mac - New3DS integration. */
static int rp_mac(const unsigned char nonce[16], const char *action,
    unsigned char output[32])
{
    unsigned char inner[64 + 16 + 160] = {0};
    unsigned char outer[64 + 32] = {0};
    unsigned char digest[32];
    size_t length = strlen(action);
    if (length > 160) return 0;
    for (int i = 0; i < 64; ++i) {
        unsigned char key = i < 32 ? rp_secret[i] : 0;
        inner[i] = key ^ 0x36;
        outer[i] = key ^ 0x5c;
    }
    memcpy(inner + 64, nonce, 16);
    memcpy(inner + 80, action, length);
    rp_sha256(inner, 80 + length, digest);
    memcpy(outer + 64, digest, 32);
    rp_sha256(outer, sizeof(outer), output);
    volatile unsigned char *clear_inner = inner, *clear_outer = outer, *clear_digest = digest;
    for (size_t i = 0; i < sizeof(inner); ++i) clear_inner[i] = 0;
    for (size_t i = 0; i < sizeof(outer); ++i) clear_outer[i] = 0;
    for (size_t i = 0; i < sizeof(digest); ++i) clear_digest[i] = 0;
    return 1;
}

/* Bind the response header and every body byte to this connection's nonce.
 * The digest is streamed so a full-resolution capture stays on the SD card. */
/* Kevin Boggs: rp_response_mac_start - New3DS integration. */
static void rp_response_mac_start(rp_sha256_ctx *inner, unsigned char outer[64],
    const unsigned char nonce[16], const char *header, size_t header_length)
{
    unsigned char pad[64];
    for (int i = 0; i < 64; ++i) {
        unsigned char key = i < 32 ? rp_secret[i] : 0;
        pad[i] = key ^ 0x36;
        outer[i] = key ^ 0x5c;
    }
    rp_sha256_init(inner);
    rp_sha256_update(inner, pad, sizeof(pad));
    rp_sha256_update(inner, nonce, 16);
    rp_sha256_update(inner, (const unsigned char *)"response\n", 9);
    rp_sha256_update(inner, (const unsigned char *)header, header_length);
    volatile unsigned char *wipe = pad;
    for (size_t i = 0; i < sizeof(pad); ++i) wipe[i] = 0;
}

/* Kevin Boggs: rp_response_mac_final - New3DS integration. */
static void rp_response_mac_final(rp_sha256_ctx *inner, unsigned char outer[64],
    unsigned char output[32])
{
    unsigned char digest[32];
    rp_sha256_final(inner, digest);
    rp_sha256_ctx finish;
    rp_sha256_init(&finish);
    rp_sha256_update(&finish, outer, 64);
    rp_sha256_update(&finish, digest, sizeof(digest));
    rp_sha256_final(&finish, output);
    volatile unsigned char *wipe = digest, *wipe_outer = outer;
    for (size_t i = 0; i < sizeof(digest); ++i) wipe[i] = 0;
    for (size_t i = 0; i < 64; ++i) wipe_outer[i] = 0;
}

/* Kevin Boggs: rp_hex_encode - New3DS integration. */
static void rp_hex_encode(const unsigned char *data, size_t size, char *out)
{
    static const char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < size; ++i) {
        out[i * 2] = digits[data[i] >> 4];
        out[i * 2 + 1] = digits[data[i] & 15];
    }
    out[size * 2] = 0;
}

/* Kevin Boggs: rp_log_tail - New3DS integration. */
static size_t rp_log_tail(char *out, size_t capacity)
{
    FILE *file = fopen(RP_LOG, "rb");
    if (!file) return 0;
    if (fseek(file, 0, SEEK_END)) { fclose(file); return 0; }
    long size = ftell(file);
    if (size < 0) { fclose(file); return 0; }
    long start = size > (long)(capacity - 1) ? size - (long)(capacity - 1) : 0;
    if (fseek(file, start, SEEK_SET)) { fclose(file); return 0; }
    size_t count = fread(out, 1, capacity - 1, file);
    fclose(file);
    out[count] = 0;
    return count;
}

/* Kevin Boggs: rp_report - New3DS integration. */
static size_t rp_report(char *out, size_t capacity)
{
    if (capacity < 512) return 0;
    size_t meta = halo_rp_format_meta(out, capacity);
    if (!meta || meta + 2 >= capacity) return 0;
    out[meta++] = '\n';
    size_t perf = halo_rp_format_perf(out + meta, capacity - meta);
    if (perf) meta += perf;
    if (meta + 6 >= capacity) return 0;
    memcpy(out + meta, "\nLOG\n", 5); meta += 5;
    meta += rp_log_tail(out + meta, capacity - meta);
    return meta;
}

/* Kevin Boggs: rp_console - New3DS integration. */
static size_t rp_console(const char *command, char *out, size_t capacity)
{
    size_t length = strlen(command);
    if (length < 12 || length >= sizeof(rp_command) || strncmp(command, "rp_preview ", 11)) return 0;
    for (size_t i = 0; i < length; ++i)
        if ((unsigned char)command[i] < 32 || (unsigned char)command[i] > 126) return 0;
    LightLock_Lock(&rp_lock);
    if (rp_command_state) { LightLock_Unlock(&rp_lock); return 0; }
    memcpy(rp_command, command, length + 1);
    rp_command_state = 1;
    LightLock_Unlock(&rp_lock);
    uint64_t deadline = osGetTime() + 2000;
    int completed = 0, succeeded = 0;
    while (!rp_stop && osGetTime() < deadline) {
        LightLock_Lock(&rp_lock);
        if (rp_command_state == 3) {
            completed = 1; succeeded = rp_command_ok;
            rp_command_state = 0;
        }
        LightLock_Unlock(&rp_lock);
        if (completed) break;
        svcSleepThread(10000000);
    }
    if (!completed) {
        LightLock_Lock(&rp_lock); rp_command_state = 0; LightLock_Unlock(&rp_lock);
    }
    const char *result = completed ? (succeeded ? "ok" : "rejected") : "timeout";
    length = strlen(result);
    if (length > capacity) return 0;
    memcpy(out, result, length);
    return length;
}

/* Kevin Boggs: rp_input - New3DS integration. */
static size_t rp_input(const char *payload, char *out, size_t capacity)
{
    unsigned held = 0, frames = 0;
    int consumed = 0;
    if (sscanf(payload, "keys=%x,frames=%u%n", &held, &frames, &consumed) != 2 ||
        !consumed || payload[consumed] || !frames || frames > 120) return 0;
    const unsigned allowed = NATIVE_KEY_A | NATIVE_KEY_B | NATIVE_KEY_X | NATIVE_KEY_Y |
        NATIVE_KEY_START | NATIVE_KEY_SELECT |
        NATIVE_KEY_L | NATIVE_KEY_R | NATIVE_KEY_ZL | NATIVE_KEY_ZR |
        NATIVE_KEY_DUP | NATIVE_KEY_DDOWN | NATIVE_KEY_DLEFT | NATIVE_KEY_DRIGHT;
    if (held & ~allowed) return 0;
    LightLock_Lock(&rp_lock);
    rp_input_keys = held;
    rp_input_frames = frames;
    LightLock_Unlock(&rp_lock);
    if (capacity < 6) return 0;
    memcpy(out, "queued", 6);
    return 6;
}

/* Kevin Boggs: rp_pad - New3DS integration. */
static size_t rp_pad(const char *payload, char *out, size_t capacity)
{
    struct halo_rp_pad_state parsed;
    if (capacity < 6 || !halo_rp_pad_parse(payload, osGetTime(), &parsed)) return 0;
    LightLock_Lock(&rp_lock);
    rp_pad_state = parsed;
    LightLock_Unlock(&rp_lock);
    memcpy(out, "queued", 6);
    return 6;
}

/* Kevin Boggs: rp_touch - New3DS integration. */
static size_t rp_touch(const char *payload, char *out, size_t capacity)
{
    struct halo_rp_touch_state parsed;
    if (capacity < 6 || !halo_rp_touch_parse(payload, osGetTime(), &parsed)) return 0;
    LightLock_Lock(&rp_lock);
    rp_touch_state = parsed;
    LightLock_Unlock(&rp_lock);
    memcpy(out, "queued", 6);
    return 6;
}

/* Kevin Boggs: rp_capture_request - New3DS integration. */
static FILE *rp_capture_request(int stream, size_t *bytes,
    const unsigned char **memory)
{
    *memory = NULL;
    LightLock_Lock(&rp_lock);
    if (rp_capture_state) { LightLock_Unlock(&rp_lock); return NULL; }
    rp_capture_state = stream ? 2 : 1;
    LightLock_Unlock(&rp_lock);
    uint64_t deadline = osGetTime() + 4000;
    int state = 0;
    while (!rp_stop && osGetTime() < deadline) {
        LightLock_Lock(&rp_lock); state = rp_capture_state; LightLock_Unlock(&rp_lock);
        if (state == 3 || state == 4 || state == 5) break;
        svcSleepThread(10000000);
    }
    LightLock_Lock(&rp_lock); rp_capture_state = 0; LightLock_Unlock(&rp_lock);
    if (state != (stream ? 4 : 3)) return NULL;
    if (stream) {
        if (!rp_stream_buffer) return NULL;
        *bytes = rp_stream_length;
        *memory = rp_stream_buffer;
        if (!rp_stream_packed) rp_stream_packed = malloc(RP_STREAM_BYTES + 512);
        if (rp_stream_packed) {
            size_t packed = rp_pack_stream(rp_stream_packed,
                RP_STREAM_BYTES + 512, rp_stream_buffer, rp_stream_length);
            if (packed) { *bytes = packed; *memory = rp_stream_packed; }
        }
        return NULL;
    }
    FILE *file = fopen(RP_SHOT, "rb");
    if (!file) return NULL;
    if (fseek(file, 0, SEEK_END)) { fclose(file); return NULL; }
    long count = ftell(file);
    const long expected = 16 + (240 * 400 + 240 * 320) * 3;
    if (count != expected || fseek(file, 0, SEEK_SET)) { fclose(file); return NULL; }
    *bytes = (size_t)count;
    return file;
}

/* Kevin Boggs: rp_exchange - New3DS integration. */
static void rp_exchange(int sock)
{
    /* Byte-wise socket reads can take over a second under emulator load or
     * weak Wi-Fi. Keep the exchange bounded while allowing a whole request. */
    uint64_t deadline = osGetTime() + 5000;
    char line[256];
    if (!rp_read_line(sock, line, sizeof(line), deadline) ||
        strcmp(line, "RP1 HELLO halo3ds\n")) return;
    unsigned char nonce[16], digest[32];
    if (R_FAILED(PS_GenerateRandomBytes(nonce, sizeof(nonce))) ||
        !rp_mac(nonce, "server\n", digest)) return;
    char nonce_hex[33], digest_hex[65], challenge[128];
    rp_hex_encode(nonce, sizeof(nonce), nonce_hex);
    rp_hex_encode(digest, sizeof(digest), digest_hex);
    int count = snprintf(challenge, sizeof(challenge),
        "RP1 CHALLENGE %s %s\n", nonce_hex, digest_hex);
    if (count <= 0 || (size_t)count >= sizeof(challenge) ||
        !rp_send(sock, challenge, (size_t)count, deadline)) return;

    if (!rp_read_line(sock, line, sizeof(line), deadline)) return;
    char supplied[65] = {0}, kind[16] = {0}, payload[128] = {0};
    unsigned id = 0;
    int consumed = 0;
    if (sscanf(line, "RP1 %64s %u %15s %127[^\r\n]%n", supplied, &id, kind, payload,
        &consumed) != 4 || !consumed || line[consumed] != '\n' ||
        line[consumed + 1] || strlen(supplied) != 64 || id > 1000000000U) return;
    char action[160];
    count = snprintf(action, sizeof(action), "%u %s %s\n", id, kind, payload);
    if (count <= 0 || (size_t)count >= sizeof(action) ||
        !rp_mac(nonce, action, digest)) return;
    char expected[65]; rp_hex_encode(digest, sizeof(digest), expected);
    unsigned mismatch = 0;
    for (int i = 0; i < 64; ++i) mismatch |= (unsigned)(supplied[i] ^ expected[i]);
    if (mismatch) return;
#ifdef HALO_N3DS_REMOTE_PILOT_DEV_MENU
    rp_last_authenticated_ms = (unsigned)osGetTime();
#endif

    char body[4096];
    size_t length = 0;
    FILE *capture_file = NULL;
    const unsigned char *capture_memory = NULL;
    const char *response_kind = "none";
    if (!strcmp(kind, "meta") && !strcmp(payload, "-")) {
        length = halo_rp_format_meta(body, sizeof(body)); response_kind = "meta";
    } else if (!strcmp(kind, "perf") && !strcmp(payload, "-")) {
        length = halo_rp_format_perf(body, sizeof(body)); response_kind = "perf";
    } else if (!strcmp(kind, "log") && !strcmp(payload, "tail")) {
        length = rp_log_tail(body, sizeof(body)); response_kind = "log";
    } else if (!strcmp(kind, "report") && !strcmp(payload, "capture")) {
        length = rp_report(body, sizeof(body)); response_kind = "report";
    } else if (!strcmp(kind, "console")) {
        length = rp_console(payload, body, sizeof(body)); response_kind = "result";
    } else if (!strcmp(kind, "input")) {
        length = rp_input(payload, body, sizeof(body)); response_kind = "result";
    } else if (!strcmp(kind, "pad")) {
        length = rp_pad(payload, body, sizeof(body)); response_kind = "result";
    } else if (!strcmp(kind, "touch")) {
        length = rp_touch(payload, body, sizeof(body)); response_kind = "result";
    } else if (!strcmp(kind, "shot") && !strcmp(payload, "capture")) {
        capture_file = rp_capture_request(0, &length, &capture_memory);
        if (capture_file) response_kind = "shot";
    } else if (!strcmp(kind, "stream") && !strcmp(payload, "capture")) {
        capture_file = rp_capture_request(1, &length, &capture_memory);
        if (capture_memory) response_kind = "stream";
    } else if (!strcmp(kind, "none") && !strcmp(payload, "-")) {
        /* An authenticated heartbeat keeps discovery cheap. */
    } else {
        const char *error = "unsupported action";
        length = strlen(error); memcpy(body, error, length);
        response_kind = "result";
    }
    if ((!strcmp(kind, "console") || !strcmp(kind, "input") || !strcmp(kind, "touch") ||
         !strcmp(kind, "shot") || !strcmp(kind, "stream")) &&
        !length && !capture_file && !capture_memory) {
        const char *error = "unavailable";
        length = strlen(error); memcpy(body, error, length);
        response_kind = "result";
    }
    char header[96];
    count = snprintf(header, sizeof(header), "RP1 %s %u %lu\n",
        response_kind, id, (unsigned long)length);
    deadline = osGetTime() + (capture_file || capture_memory ? 12000 : 5000);
    if (count <= 0 || (size_t)count >= sizeof(header) ||
        !rp_send(sock, header, (size_t)count, deadline)) {
        if (capture_file) fclose(capture_file);
        return;
    }
    rp_sha256_ctx response_inner;
    unsigned char response_outer[64];
    rp_response_mac_start(&response_inner, response_outer, nonce, header, (size_t)count);
    if (capture_file) {
        char chunk[8192];
        size_t remaining = length;
        while (remaining) {
            size_t read = fread(chunk, 1, remaining < sizeof(chunk) ? remaining : sizeof(chunk), capture_file);
            if (!read || !rp_send(sock, chunk, read, deadline)) break;
            rp_sha256_update(&response_inner, (const unsigned char *)chunk, read);
            remaining -= read;
        }
        fclose(capture_file);
        if (remaining) return;
    } else if (capture_memory) {
        if (!rp_send(sock, capture_memory, length, deadline)) return;
        rp_sha256_update(&response_inner, capture_memory, length);
    } else if (length) {
        if (!rp_send(sock, body, length, deadline)) return;
        rp_sha256_update(&response_inner, (const unsigned char *)body, length);
    }
    rp_response_mac_final(&response_inner, response_outer, digest);
    rp_hex_encode(digest, sizeof(digest), digest_hex);
    char trailer[80];
    count = snprintf(trailer, sizeof(trailer), "RP1 MAC %s\n", digest_hex);
    if (count <= 0 || (size_t)count >= sizeof(trailer) ||
        !rp_send(sock, trailer, (size_t)count, deadline)) return;
    shutdown(sock, SHUT_WR);
    /* Let the PC consume the last bytes before SOCU closes the connection. */
    if (rp_wait(sock, 0, osGetTime() + 500)) {
        char ignored[8]; recv(sock, ignored, sizeof(ignored), 0);
    }
}

/* Kevin Boggs: rp_worker - New3DS integration. */
static void rp_worker(void *unused)
{
    (void)unused;
    while (!rp_stop) {
#ifdef HALO_N3DS_REMOTE_PILOT_DEV_MENU
        if (rp_retry_requested || rp_listener < 0) {
            LightLock_Lock(&rp_lock);
            halo_rp_touch_clear(&rp_touch_state);
            LightLock_Unlock(&rp_lock);
            if (rp_listener >= 0) { close(rp_listener); rp_listener = -1; }
            struct sockaddr_in address = {0};
            address.sin_addr.s_addr = htonl(INADDR_ANY);
            address.sin_family = AF_INET;
            address.sin_port = htons((uint16_t)rp_port);
            rp_listener = socket(AF_INET, SOCK_STREAM, 0);
            if (rp_listener >= 0) {
                int reuse = 1;
                setsockopt(rp_listener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
                if (bind(rp_listener, (struct sockaddr *)&address, sizeof(address)) < 0 ||
                    listen(rp_listener, 2) < 0) {
                    close(rp_listener); rp_listener = -1;
                }
            }
            rp_retry_requested = 0;
            rp_last_authenticated_ms = 0;
            if (rp_listener < 0) { svcSleepThread(500000000); continue; }
        }
#endif
        if (!rp_wait(rp_listener, 0, osGetTime() + 200)) continue;
        struct sockaddr_in peer; socklen_t size = sizeof(peer);
        int sock = accept(rp_listener, (struct sockaddr *)&peer, &size);
        if (sock < 0) continue;
        struct sockaddr_in local = {0}; socklen_t local_size = sizeof(local);
        if (peer.sin_family == AF_INET && rp_private_ip(ntohl(peer.sin_addr.s_addr)) &&
            getsockname(sock, (struct sockaddr *)&local, &local_size) == 0 &&
            local.sin_family == AF_INET && rp_private_ip(ntohl(local.sin_addr.s_addr)))
            rp_exchange(sock);
        close(sock);
    }
}

/* Kevin Boggs: halo_rp_transport_start - New3DS integration. */
void halo_rp_transport_start(void)
{
    if (!rp_config_checked) {
        rp_config_checked = 1;
        if (!rp_load_config()) return;
    }
    if (!rp_port || rp_thread) return;
    LightLock_Init(&rp_lock);
    memset(&rp_pad_state, 0, sizeof(rp_pad_state));
    halo_rp_touch_clear(&rp_touch_state);
    rp_soc_memory = memalign(0x1000, RP_SOCKET_BYTES);
    if (!rp_soc_memory) goto failed;
    if (R_FAILED(socInit(rp_soc_memory, RP_SOCKET_BYTES))) goto failed;
    rp_soc_started = 1;
    if (R_FAILED(psInit())) goto failed;
    rp_ps_started = 1;
    struct sockaddr_in address = {0};
    /* Binding all local addresses survives DHCP changes. The accepted local
     * and peer addresses must both be private before any handshake data. */
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_family = AF_INET;
    address.sin_port = htons((uint16_t)rp_port);
    rp_listener = socket(AF_INET, SOCK_STREAM, 0);
    if (rp_listener < 0) goto failed;
    int reuse = 1;
    setsockopt(rp_listener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    if (bind(rp_listener, (struct sockaddr *)&address, sizeof(address)) < 0 ||
        listen(rp_listener, 2) < 0) goto failed;
    rp_stop = 0;
    rp_thread = threadCreate(rp_worker, NULL, 32768, 0x30, -2, false);
    if (rp_thread) return;
failed:
    if (rp_listener >= 0) { close(rp_listener); rp_listener = -1; }
    if (rp_ps_started) { psExit(); rp_ps_started = 0; }
    if (rp_soc_started) { socExit(); rp_soc_started = 0; }
    free(rp_soc_memory); rp_soc_memory = NULL;
    free(rp_stream_buffer); rp_stream_buffer = NULL;
    free(rp_stream_packed); rp_stream_packed = NULL;
}

/* Kevin Boggs: halo_rp_transport_stop - New3DS integration. */
void halo_rp_transport_stop(void)
{
    rp_stop = 1;
    if (rp_thread) { threadJoin(rp_thread, U64_MAX); threadFree(rp_thread); rp_thread = NULL; }
    if (rp_listener >= 0) { close(rp_listener); rp_listener = -1; }
    if (rp_ps_started) { psExit(); rp_ps_started = 0; }
    if (rp_soc_started) { socExit(); rp_soc_started = 0; }
    free(rp_soc_memory); rp_soc_memory = NULL;
    free(rp_stream_buffer); rp_stream_buffer = NULL;
    free(rp_stream_packed); rp_stream_packed = NULL;
    memset(rp_secret, 0, sizeof(rp_secret));
    memset(&rp_pad_state, 0, sizeof(rp_pad_state));
    halo_rp_touch_clear(&rp_touch_state);
    rp_port = 0;
    rp_config_checked = 0;
#ifdef HALO_N3DS_REMOTE_PILOT_DEV_MENU
    rp_retry_requested = 0;
    rp_last_authenticated_ms = 0;
#endif
}
#ifdef HALO_N3DS_REMOTE_PILOT_DEV_MENU
/* Kevin Boggs: halo_rp_transport_status - New3DS integration. */
enum halo_rp_link_state halo_rp_transport_status(void)
{
    if (!rp_port) return HALO_RP_NOT_ARMED;
    if (!rp_thread || rp_listener < 0) return HALO_RP_NETWORK_ERROR;
    if (rp_retry_requested) return HALO_RP_RETRYING;
    unsigned last = rp_last_authenticated_ms;
    if (last && (unsigned)((unsigned)osGetTime() - last) < 8000U)
        return HALO_RP_AUTHENTICATED;
    return HALO_RP_WAITING;
}
/* Kevin Boggs: halo_rp_transport_retry - New3DS integration. */
void halo_rp_transport_retry(void)
{
    /* An unarmed file is never enabled by this button. It must be rearmed
     * explicitly on the SD card before the listener can start. */
    if (!rp_port) {
        rp_config_checked = 0;
        halo_rp_transport_start();
    } else if (!rp_thread) {
        halo_rp_transport_start();
    } else {
        rp_retry_requested = 1;
    }
}
/* Kevin Boggs: halo_rp_transport_address - New3DS integration. */
int halo_rp_transport_address(char out[16])
{
    if (!out) return 0;
    out[0] = 0;
    if (!rp_soc_started) return 0;
    struct in_addr address = {.s_addr = (uint32_t)gethostid()};
    if (!rp_private_ip(ntohl(address.s_addr))) return 0;
    const char *shown = inet_ntoa(address);
    if (!shown || strlen(shown) >= 16) return 0;
    strcpy(out, shown);
    return 1;
}
/* Kevin Boggs: halo_rp_transport_bug_it_request - New3DS integration. */
int halo_rp_transport_bug_it_request(void)
{
    if (!rp_port || !rp_thread) return 0;
    if (++rp_bug_it_sequence == 0) ++rp_bug_it_sequence;
    return 1;
}
/* Kevin Boggs: halo_rp_transport_bug_it_sequence - New3DS integration. */
unsigned int halo_rp_transport_bug_it_sequence(void)
{
    return rp_bug_it_sequence;
}
#endif

/* Kevin Boggs: halo_rp_take_console - New3DS integration. */
int halo_rp_take_console(char *out, size_t capacity)
{
    if (!out || capacity < sizeof(rp_command) || !rp_thread) return 0;
    int ready = 0;
    LightLock_Lock(&rp_lock);
    if (rp_command_state == 1) {
        memcpy(out, rp_command, sizeof(rp_command));
        rp_command_state = 2;
        ready = 1;
    }
    LightLock_Unlock(&rp_lock);
    return ready;
}

/* Kevin Boggs: halo_rp_complete_console - New3DS integration. */
void halo_rp_complete_console(int succeeded)
{
    if (!rp_thread) return;
    LightLock_Lock(&rp_lock);
    if (rp_command_state == 2) {
        rp_command_ok = !!succeeded;
        rp_command_state = 3;
    }
    LightLock_Unlock(&rp_lock);
}

/* Kevin Boggs: halo_rp_apply_input - New3DS integration. */
void halo_rp_apply_input(struct native_input_sample *sample)
{
    if (!sample || !rp_thread) return;
    LightLock_Lock(&rp_lock);
    if (rp_input_frames) {
        sample->held |= rp_input_keys;
        --rp_input_frames;
    }
    halo_rp_pad_apply(&rp_pad_state, sample,
        n3ds_input_platform_original_model(), osGetTime());
    halo_rp_touch_apply(&rp_touch_state, sample, osGetTime());
    LightLock_Unlock(&rp_lock);
}

/* Kevin Boggs: rp_put_u16 - New3DS integration. */
static void rp_put_u16(unsigned char *out, unsigned value)
{
    out[0] = (unsigned char)value;
    out[1] = (unsigned char)(value >> 8);
}

/* The framebuffer's .rgb diagnostic bytes are BGR888 in portrait memory
 * order. Emit RGB888 and preserve that orientation for the RPIM decoder. */
/* Kevin Boggs: rp_write_screen - New3DS integration. */
static int rp_write_screen(FILE *file, gfxScreen_t screen, unsigned width,
    unsigned height, unsigned step)
{
    gfxSetDoubleBuffering(screen, false);
    unsigned char *front = gfxGetFramebuffer(screen, GFX_LEFT, NULL, NULL);
    gfxSetDoubleBuffering(screen, true);
    if (!front) return 0;
    GSPGPU_InvalidateDataCache(front, width * height * 3);
    unsigned char bytes[3072]; size_t used = 0;
    for (unsigned y = 0; y < height; y += step) {
        for (unsigned x = 0; x < width; x += step) {
            const unsigned char *pixel = front + (y * width + x) * 3;
            bytes[used++] = pixel[2];
            bytes[used++] = pixel[1];
            bytes[used++] = pixel[0];
            if (used == sizeof(bytes)) {
                if (fwrite(bytes, 1, used, file) != used) return 0;
                used = 0;
            }
        }
    }
    return !used || fwrite(bytes, 1, used, file) == used;
}

/* The live preview stays in RAM and uses RGB565. This avoids two SD writes
 * per request on the game thread and cuts the network payload by one third. */
/* Kevin Boggs: rp_copy_stream_screen - New3DS integration. */
static int rp_copy_stream_screen(unsigned char *out, gfxScreen_t screen,
    unsigned width, unsigned height)
{
    gfxSetDoubleBuffering(screen, false);
    unsigned char *front = gfxGetFramebuffer(screen, GFX_LEFT, NULL, NULL);
    gfxSetDoubleBuffering(screen, true);
    if (!front) return 0;
    GSPGPU_InvalidateDataCache(front, width * height * 3);
    size_t used = 0;
    for (unsigned y = 0; y < height; y += 2) {
        for (unsigned x = 0; x < width; x += 2) {
            const unsigned char *pixel = front + (y * width + x) * 3;
            unsigned rgb565 = ((unsigned)(pixel[2] >> 3) << 11) |
                ((unsigned)(pixel[1] >> 2) << 5) | (pixel[0] >> 3);
            out[used++] = (unsigned char)rgb565;
            out[used++] = (unsigned char)(rgb565 >> 8);
        }
    }
    return 1;
}

/* Kevin Boggs: halo_rp_capture_if_requested - New3DS integration. */
void halo_rp_capture_if_requested(void)
{
    if (!rp_thread) return;
    LightLock_Lock(&rp_lock);
    int state = rp_capture_state;
    if (state == 1 || state == 2) rp_capture_state = 6;
    LightLock_Unlock(&rp_lock);
    if (state != 1 && state != 2) return;
    unsigned step = state == 2 ? 2 : 1;
    int ok = 0;
    if (state == 2) {
        if (!rp_stream_buffer) rp_stream_buffer = malloc(RP_STREAM_BYTES);
        if (rp_stream_buffer) {
            unsigned char *header = rp_stream_buffer;
            memset(header, 0, 16);
            memcpy(header, "RPIM", 4);
            rp_put_u16(header + 4, 120); rp_put_u16(header + 6, 200);
            rp_put_u16(header + 8, 120); rp_put_u16(header + 10, 160);
            rp_put_u16(header + 12, 2); /* little-endian RGB565 */
            rp_put_u16(header + 14, 1); /* portrait */
            ok = rp_copy_stream_screen(header + 16, GFX_TOP, 240, 400) &&
                rp_copy_stream_screen(header + 16 + 120 * 200 * 2,
                    GFX_BOTTOM, 240, 320);
            if (ok) {
                unsigned char *audio = header + RP_STREAM_IMAGE_BYTES;
                memcpy(audio, "RPAU", 4);
#ifdef HALO_N3DS_REMOTE_PILOT_DEV_MENU
                unsigned samples = n3ds_audio_remote_take(
                    (short *)(void *)(audio + 8), RP_STREAM_AUDIO_SAMPLES);
#else
                unsigned samples = 0;
#endif
                rp_put_u16(audio + 4, samples);
                rp_put_u16(audio + 6, 16364);
                rp_stream_length = RP_STREAM_IMAGE_BYTES + 8 + samples * 2;
            }
        }
    } else {
        FILE *file = fopen(RP_SHOT, "wb");
        ok = file != NULL;
        if (ok) {
        unsigned char header[16] = {'R','P','I','M'};
        rp_put_u16(header + 4, 240 / step);
        rp_put_u16(header + 6, 400 / step);
        rp_put_u16(header + 8, 240 / step);
        rp_put_u16(header + 10, 320 / step);
        rp_put_u16(header + 12, 1); /* RGB888 */
        rp_put_u16(header + 14, 1); /* portrait, rotate 90 degrees CCW */
        ok = fwrite(header, 1, sizeof(header), file) == sizeof(header) &&
            rp_write_screen(file, GFX_TOP, 240, 400, step) &&
            rp_write_screen(file, GFX_BOTTOM, 240, 320, step);
        if (fclose(file)) ok = 0;
        }
    }
    LightLock_Lock(&rp_lock);
    if (rp_capture_state == 6) rp_capture_state = ok ? (state == 2 ? 4 : 3) : 5;
    LightLock_Unlock(&rp_lock);
}
