/* New3DS integration additions by Kevin Boggs. */
#ifndef HALO_REMOTE_PILOT_PAD_H
#define HALO_REMOTE_PILOT_PAD_H
#include <stdint.h>
#include "engine_input.h"

/* One complete, short-lived standard Gamepad API state. The transport lock
 * protects this value when the network and game threads exchange it. */
struct halo_rp_pad_state {
    uint64_t expires_ms;
    unsigned buttons, frames;
    int lx, ly, rx, ry, lt, rt;
};

/* Kevin Boggs: halo_rp_pad_parse - New3DS integration. */
int halo_rp_pad_parse(const char *payload, uint64_t now_ms,
    struct halo_rp_pad_state *out);
/* Kevin Boggs: halo_rp_pad_apply - New3DS integration. */
void halo_rp_pad_apply(struct halo_rp_pad_state *state,
    struct native_input_sample *sample, int original_model, uint64_t now_ms);
#endif
