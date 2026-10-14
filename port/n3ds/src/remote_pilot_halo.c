/* New3DS integration additions by Kevin Boggs.
 * Remote Pilot host: https://github.com/kaboggs/RemotePilot
 * MIT terms: LICENSE-KEVIN-MIT.md */
#include "remote_pilot_halo.h"
#ifdef HALO_N3DS_REMOTE_PILOT_DEV_MENU
#include "remote_pilot_halo_transport.h"
#endif
#include <stdio.h>
#include <string.h>

static struct halo_rp_snapshot current;
static int has_snapshot;
static int is_new_model;
static volatile unsigned snapshot_sequence;

/* The map basename is drawn only from these safe ASCII bytes. This also keeps
 * malformed map paths from injecting content into JSON or a PC file name. */
/* Kevin Boggs: copy_map - New3DS integration. */
static void copy_map(char *out, size_t capacity, const char *path)
{
    const char *base = path ? path : "";
    for (const char *p = base; *p; ++p)
        if (*p == '\\' || *p == '/') base = p + 1;
    size_t used = 0;
    while (base[used] && used + 1 < capacity) {
        char c = base[used];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_')) break;
        out[used] = c;
        ++used;
    }
    out[used] = 0;
}

/* Kevin Boggs: halo_rp_set_device_model - New3DS integration. */
void halo_rp_set_device_model(int new_model) { is_new_model = !!new_model; }

/* Kevin Boggs: halo_rp_record_frame - New3DS integration. */
void halo_rp_record_frame(uint64_t timestamp_ms, uint32_t frame, int32_t game_tick,
    const char *map_path, int menu, int pc_map, const uint64_t stage_ticks[5],
    uint64_t frame_delta_ticks, uint64_t frequency, int memory_valid, uint32_t heap_free,
    uint32_t linear_free, uint32_t application_free)
{
    if (!stage_ticks || !frequency) return;
    struct halo_rp_snapshot next = {0};
    const float scale = 1000.0f / (float)frequency;
    next.timestamp_ms = timestamp_ms;
    next.frame = frame;
    next.game_tick = game_tick;
    next.new_model = (uint8_t)is_new_model;
    next.menu = (uint8_t)!!menu;
    next.pc_map = (uint8_t)!!pc_map;
    copy_map(next.map, sizeof(next.map), map_path);
    next.services_ms = stage_ticks[0] * scale;
    next.input_ms = stage_ticks[1] * scale;
    next.simulation_ms = stage_ticks[2] * scale;
    next.audio_ms = stage_ticks[3] * scale;
    next.render_ms = stage_ticks[4] * scale;
    next.work_ms = next.services_ms + next.input_ms + next.simulation_ms +
        next.audio_ms + next.render_ms;
    next.frame_ms = frame_delta_ticks * scale;
    next.memory_valid = (uint8_t)!!memory_valid;
    if (memory_valid) {
        next.heap_free = heap_free;
        next.linear_free = linear_free;
        next.application_free = application_free;
    }
    ++snapshot_sequence;
    __sync_synchronize();
    current = next;
    __sync_synchronize();
    ++snapshot_sequence;
    has_snapshot = 1;
}

/* Kevin Boggs: halo_rp_get_snapshot - New3DS integration. */
int halo_rp_get_snapshot(struct halo_rp_snapshot *out)
{
    if (!out || !has_snapshot) return 0;
    unsigned before, after;
    for (;;) {
        before = snapshot_sequence;
        if (before & 1U) continue;
        __sync_synchronize();
        *out = current;
        __sync_synchronize();
        after = snapshot_sequence;
        if (before == after) break;
    }
    return 1;
}

/* Kevin Boggs: halo_rp_format_meta - New3DS integration. */
size_t halo_rp_format_meta(char *out, size_t capacity)
{
    if (!out || !capacity) return 0;
#ifdef HALO_N3DS_PC_LOCAL_MENU
    const char *preview = "true";
#else
    const char *preview = "false";
#endif
#ifdef HALO_N3DS_REMOTE_PILOT_DEV_MENU
    unsigned bug_it_sequence = halo_rp_transport_bug_it_sequence();
#else
    unsigned bug_it_sequence = 0;
#endif
    int count = snprintf(out, capacity,
        "{\"schemaVersion\":1,\"gameId\":\"halo3ds\",\"deviceModel\":\"%s\","
        "\"bugItSequence\":%u,"
        "\"capabilities\":{\"perf\":true,\"logs\":true,\"report\":true,"
        "\"shot\":true,\"stream\":true,\"console\":true,\"input\":true,\"pad\":true,\"touch\":true,"
        "\"preview\":%s}}",
        is_new_model ? "new" : "original", bug_it_sequence, preview);
    return count > 0 && (size_t)count < capacity ? (size_t)count : 0;
}

/* Kevin Boggs: halo_rp_format_perf - New3DS integration. */
size_t halo_rp_format_perf(char *out, size_t capacity)
{
    if (!out || !capacity) return 0;
    struct halo_rp_snapshot s;
    if (!halo_rp_get_snapshot(&s)) return 0;
    int count = snprintf(out, capacity,
        "{\"schemaVersion\":1,\"timestampMs\":%llu,\"frame\":%u,\"gameTick\":%ld,"
        "\"gameId\":\"halo3ds\",\"deviceModel\":\"%s\",\"map\":\"%s\","
        "\"mapSource\":\"%s\",\"menu\":%s,\"workMs\":%.3f,"
        "\"servicesMs\":%.3f,\"inputMs\":%.3f,\"simulationMs\":%.3f,"
        "\"audioMs\":%.3f,\"renderMs\":%.3f",
        (unsigned long long)s.timestamp_ms, s.frame, (long)s.game_tick,
        s.new_model ? "new" : "original", s.map,
        s.pc_map ? "pc" : "xbox", s.menu ? "true" : "false",
        s.work_ms, s.services_ms, s.input_ms, s.simulation_ms,
        s.audio_ms, s.render_ms);
    if (count <= 0 || (size_t)count >= capacity) return 0;
    size_t used = (size_t)count;
    if (s.frame_ms > 0.0f) {
        count = snprintf(out + used, capacity - used,
            ",\"frameMs\":%.3f,\"fps\":%.2f", s.frame_ms, 1000.0f / s.frame_ms);
        if (count <= 0 || (size_t)count >= capacity - used) return 0;
        used += (size_t)count;
    }
    if (s.memory_valid) {
        count = snprintf(out + used, capacity - used,
            ",\"heapFreeBytes\":%u,\"linearFreeBytes\":%u,"
            "\"applicationFreeBytes\":%u",
            s.heap_free, s.linear_free, s.application_free);
        if (count <= 0 || (size_t)count >= capacity - used) return 0;
        used += (size_t)count;
    }
    if (used + 2 > capacity) return 0;
    out[used++] = '}'; out[used] = 0;
    return used;
}
