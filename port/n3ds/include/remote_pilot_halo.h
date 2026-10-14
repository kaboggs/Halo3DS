/* New3DS integration additions by Kevin Boggs. */
#ifndef HALO_REMOTE_PILOT_H
#define HALO_REMOTE_PILOT_H

#include <stddef.h>
#include <stdint.h>

/* A fixed-size, allocation-free bridge between the game loop and an optional
 * paired PC transport. All timings are measured work, before frame pacing. */
struct halo_rp_snapshot {
    uint64_t timestamp_ms;
    uint32_t frame;
    int32_t game_tick;
    uint8_t new_model;
    uint8_t menu;
    uint8_t pc_map;
    uint8_t memory_valid;
    char map[32];
    float services_ms;
    float input_ms;
    float simulation_ms;
    float audio_ms;
    float render_ms;
    float work_ms;
    float frame_ms;
    uint32_t heap_free;
    uint32_t linear_free;
    uint32_t application_free;
};

/* Kevin Boggs: halo_rp_set_device_model - New3DS integration. */
void halo_rp_set_device_model(int new_model);
/* Kevin Boggs: halo_rp_record_frame - New3DS integration. */
void halo_rp_record_frame(uint64_t timestamp_ms, uint32_t frame, int32_t game_tick,
    const char *map_path, int menu, int pc_map, const uint64_t stage_ticks[5],
    uint64_t frame_delta_ticks, uint64_t frequency, int memory_valid, uint32_t heap_free,
    uint32_t linear_free, uint32_t application_free);
/* Kevin Boggs: halo_rp_get_snapshot - New3DS integration. */
int halo_rp_get_snapshot(struct halo_rp_snapshot *out);
/* Kevin Boggs: halo_rp_format_meta - New3DS integration. */
size_t halo_rp_format_meta(char *out, size_t capacity);
/* Kevin Boggs: halo_rp_format_perf - New3DS integration. */
size_t halo_rp_format_perf(char *out, size_t capacity);

#endif
