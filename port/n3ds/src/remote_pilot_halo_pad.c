/* New3DS integration additions by Kevin Boggs. */
#include <string.h>
#include "remote_pilot_halo_pad.h"

/* Kevin Boggs: rp_pad_field - New3DS integration. */
static int rp_pad_field(const char **cursor, const char *name,
    int minimum, int maximum, int *out)
{
    size_t length = strlen(name);
    const char *p = *cursor;
    if (strncmp(p, name, length)) return 0;
    p += length;
    int negative = *p == '-';
    if (negative) ++p;
    if (*p < '0' || *p > '9') return 0;
    int value = 0, digits = 0;
    while (*p >= '0' && *p <= '9') {
        if (++digits > 4) return 0;
        value = value * 10 + *p++ - '0';
    }
    if (negative) value = -value;
    if (value < minimum || value > maximum) return 0;
    *out = value;
    *cursor = p;
    return 1;
}

/* Kevin Boggs: rp_pad_comma - New3DS integration. */
static int rp_pad_comma(const char **cursor)
{
    if (**cursor != ',') return 0;
    ++*cursor;
    return 1;
}

/* Kevin Boggs: halo_rp_pad_parse - New3DS integration. */
int halo_rp_pad_parse(const char *payload, uint64_t now_ms,
    struct halo_rp_pad_state *out)
{
    if (!payload || !out || strlen(payload) < 12 ||
        strncmp(payload, "buttons=", 8)) return 0;
    const char *p = payload + 8;
    unsigned buttons = 0;
    for (int i = 0; i < 4; ++i) {
        char c = p[i];
        unsigned digit;
        if (c >= '0' && c <= '9') digit = (unsigned)(c - '0');
        else if (c >= 'a' && c <= 'f') digit = (unsigned)(c - 'a' + 10);
        else return 0;
        buttons = (buttons << 4) | digit;
    }
    p += 4;
    struct halo_rp_pad_state parsed = {0};
    int frames;
    if (!rp_pad_comma(&p) || !rp_pad_field(&p, "lx=", -1000, 1000, &parsed.lx) ||
        !rp_pad_comma(&p) || !rp_pad_field(&p, "ly=", -1000, 1000, &parsed.ly) ||
        !rp_pad_comma(&p) || !rp_pad_field(&p, "rx=", -1000, 1000, &parsed.rx) ||
        !rp_pad_comma(&p) || !rp_pad_field(&p, "ry=", -1000, 1000, &parsed.ry) ||
        !rp_pad_comma(&p) || !rp_pad_field(&p, "lt=", 0, 1000, &parsed.lt) ||
        !rp_pad_comma(&p) || !rp_pad_field(&p, "rt=", 0, 1000, &parsed.rt) ||
        !rp_pad_comma(&p) || !rp_pad_field(&p, "frames=", 1, 30, &frames) || *p)
        return 0;
    parsed.buttons = buttons;
    parsed.frames = (unsigned)frames;
    parsed.expires_ms = now_ms + 350;
    *out = parsed;
    return 1;
}

/* Kevin Boggs: rp_pad_axis - New3DS integration. */
static short rp_pad_axis(short local, int remote)
{
    int scaled = remote * 156 / 1000;
    int local_magnitude = local < 0 ? -local : local;
    int remote_magnitude = scaled < 0 ? -scaled : scaled;
    return remote_magnitude > local_magnitude ? (short)scaled : local;
}

/* Kevin Boggs: halo_rp_pad_apply - New3DS integration. */
void halo_rp_pad_apply(struct halo_rp_pad_state *state,
    struct native_input_sample *sample, int original_model, uint64_t now_ms)
{
    if (!state || !sample || !state->frames) return;
    if (now_ms >= state->expires_ms) {
        memset(state, 0, sizeof(*state));
        return;
    }
    unsigned buttons = state->buttons;
    unsigned held = 0;
    if (buttons & (1u << 0)) held |= NATIVE_KEY_A;
    if (buttons & (1u << 1)) held |= NATIVE_KEY_B;
    if (buttons & (1u << 2)) held |= NATIVE_KEY_X;
    if (buttons & (1u << 3)) held |= NATIVE_KEY_Y;
    if (buttons & (1u << 4)) held |= NATIVE_KEY_L;
    if (buttons & (1u << 5)) held |= NATIVE_KEY_R;
    if ((buttons & (1u << 6)) || state->lt >= 500)
        held |= original_model ? NATIVE_KEY_R : NATIVE_KEY_ZL;
    if ((buttons & (1u << 7)) || state->rt >= 500)
        held |= original_model ? NATIVE_KEY_L : NATIVE_KEY_ZR;
    if (buttons & (1u << 8)) held |= NATIVE_KEY_SELECT;
    if (buttons & (1u << 9)) held |= NATIVE_KEY_START;
    if (buttons & (1u << 10)) held |= NATIVE_KEY_DDOWN;
    if (buttons & (1u << 11)) held |= NATIVE_KEY_DUP;
    if (buttons & (1u << 12)) held |= NATIVE_KEY_DUP;
    if (buttons & (1u << 13)) held |= NATIVE_KEY_DDOWN;
    if (buttons & (1u << 14)) held |= NATIVE_KEY_DLEFT;
    if (buttons & (1u << 15)) held |= NATIVE_KEY_DRIGHT;
    /* START+SELECT is the launcher exit chord. Never let remote input form it,
     * including a mixed remote/local combination. Local-only exit still works. */
    if ((held & NATIVE_KEY_START) && (sample->held & NATIVE_KEY_SELECT))
        held &= ~NATIVE_KEY_START;
    if ((held & NATIVE_KEY_SELECT) && (sample->held & NATIVE_KEY_START))
        held &= ~NATIVE_KEY_SELECT;
    if ((held & (NATIVE_KEY_START | NATIVE_KEY_SELECT)) ==
        (NATIVE_KEY_START | NATIVE_KEY_SELECT)) held &= ~NATIVE_KEY_SELECT;
    sample->held |= held;
    sample->circle_x = rp_pad_axis(sample->circle_x, state->lx);
    sample->circle_y = rp_pad_axis(sample->circle_y, state->ly);
    sample->cstick_x = rp_pad_axis(sample->cstick_x, state->rx);
    sample->cstick_y = rp_pad_axis(sample->cstick_y, state->ry);
    --state->frames;
    if (!state->frames) memset(state, 0, sizeof(*state));
}
