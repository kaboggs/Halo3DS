/* New3DS integration additions by Kevin Boggs. */
#ifndef HALO_REMOTE_PILOT_TOUCH_H
#define HALO_REMOTE_PILOT_TOUCH_H
#include <stdint.h>
#include "engine_input.h"

/* Network and game threads exchange this state under the transport lock. */
struct halo_rp_touch_state {
    uint64_t expires_ms;
    unsigned short x, y;
    unsigned char down;
};

/* Kevin Boggs: halo_rp_touch_parse - New3DS integration. */
int halo_rp_touch_parse(const char *payload, uint64_t now_ms,
    struct halo_rp_touch_state *out);
/* Kevin Boggs: halo_rp_touch_apply - New3DS integration. */
void halo_rp_touch_apply(struct halo_rp_touch_state *state,
    struct native_input_sample *sample, uint64_t now_ms);
/* Kevin Boggs: halo_rp_touch_clear - New3DS integration. */
void halo_rp_touch_clear(struct halo_rp_touch_state *state);
#endif
