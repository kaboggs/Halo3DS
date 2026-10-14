/* New3DS integration additions by Kevin Boggs. */
#include <string.h>
#include "remote_pilot_halo_touch.h"

/* Kevin Boggs: rp_touch_decimal - New3DS integration. */
static int rp_touch_decimal(const char **cursor, unsigned maximum, unsigned *out)
{
    const char *p = *cursor;
    unsigned value = 0, digits = 0;
    while (*p >= '0' && *p <= '9') {
        if (++digits > 3) return 0;
        value = value * 10 + (unsigned)(*p++ - '0');
    }
    if (!digits || value > maximum) return 0;
    *cursor = p;
    *out = value;
    return 1;
}

/* Kevin Boggs: halo_rp_touch_parse - New3DS integration. */
int halo_rp_touch_parse(const char *payload, uint64_t now_ms,
    struct halo_rp_touch_state *out)
{
    if (!payload || !out || strncmp(payload, "x=", 2)) return 0;
    const char *p = payload + 2;
    unsigned x, y;
    if (!rp_touch_decimal(&p, 319, &x) || strncmp(p, ",y=", 3)) return 0;
    p += 3;
    if (!rp_touch_decimal(&p, 239, &y) || strncmp(p, ",down=", 6)) return 0;
    p += 6;
    if ((p[0] != '0' && p[0] != '1') || p[1]) return 0;
    struct halo_rp_touch_state next = {0};
    next.x = (unsigned short)x;
    next.y = (unsigned short)y;
    next.down = (unsigned char)(p[0] == '1');
    if (next.down) next.expires_ms = now_ms + 900;
    *out = next;
    return 1;
}

/* Kevin Boggs: halo_rp_touch_apply - New3DS integration. */
void halo_rp_touch_apply(struct halo_rp_touch_state *state,
    struct native_input_sample *sample, uint64_t now_ms)
{
    if (!state || !sample || !state->down) return;
    if (now_ms >= state->expires_ms || (sample->held & NATIVE_KEY_TOUCH)) {
        halo_rp_touch_clear(state);
        return;
    }
    sample->held |= NATIVE_KEY_TOUCH;
    sample->touch_x = state->x;
    sample->touch_y = state->y;
}

/* Kevin Boggs: halo_rp_touch_clear - New3DS integration. */
void halo_rp_touch_clear(struct halo_rp_touch_state *state)
{
    if (state) memset(state, 0, sizeof(*state));
}
