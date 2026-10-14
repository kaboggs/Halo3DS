/* New3DS integration additions by Kevin Boggs. */
#include "remote_pilot_halo_pad.h"
#include <assert.h>
#include <string.h>

/* Kevin Boggs: main - New3DS integration. */
int main(void)
{
    struct halo_rp_pad_state state = {0};
    const char *valid =
        "buttons=ffff,lx=-1000,ly=1000,rx=750,ry=-500,lt=500,rt=499,frames=12";
    assert(halo_rp_pad_parse(valid, 1000, &state));
    assert(state.buttons == 0xffff && state.expires_ms == 1350 &&
        state.lx == -1000 && state.lt == 500 && state.frames == 12);
    const char *invalid[] = {
        "buttons=fff,lx=0,ly=0,rx=0,ry=0,lt=0,rt=0,frames=1",
        "buttons=gggg,lx=0,ly=0,rx=0,ry=0,lt=0,rt=0,frames=1",
        "buttons=0000,lx=1001,ly=0,rx=0,ry=0,lt=0,rt=0,frames=1",
        "buttons=0000,lx=0,ly=0,rx=0,ry=0,lt=-1,rt=0,frames=1",
        "buttons=0000,lx=0,ly=0,rx=0,ry=0,lt=0,rt=0,frames=31",
        "buttons=0000,lx=0,ly=0,rx=0,ry=0,lt=0,rt=0,frames=1junk",
        "buttons=0000, lx=0,ly=0,rx=0,ry=0,lt=0,rt=0,frames=1",
    };
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(*invalid); ++i) {
        struct halo_rp_pad_state prior = state;
        assert(!halo_rp_pad_parse(invalid[i], 1001, &state));
        assert(!memcmp(&prior, &state, sizeof(state)));
    }
    struct native_input_sample sample = {0};
    sample.held = NATIVE_KEY_B;
    sample.circle_x = 80;
    sample.circle_y = -120;
    sample.cstick_x = -130;
    halo_rp_pad_apply(&state, &sample, 0, 1200);
    assert((sample.held & (NATIVE_KEY_A | NATIVE_KEY_B | NATIVE_KEY_X |
        NATIVE_KEY_Y | NATIVE_KEY_L | NATIVE_KEY_R | NATIVE_KEY_ZL |
        NATIVE_KEY_ZR | NATIVE_KEY_START | NATIVE_KEY_DUP |
        NATIVE_KEY_DDOWN | NATIVE_KEY_DLEFT | NATIVE_KEY_DRIGHT)) ==
        (NATIVE_KEY_A | NATIVE_KEY_B | NATIVE_KEY_X | NATIVE_KEY_Y |
        NATIVE_KEY_L | NATIVE_KEY_R | NATIVE_KEY_ZL | NATIVE_KEY_ZR |
        NATIVE_KEY_START | NATIVE_KEY_DUP | NATIVE_KEY_DDOWN |
        NATIVE_KEY_DLEFT | NATIVE_KEY_DRIGHT));
    assert(!(sample.held & NATIVE_KEY_SELECT));
    assert(sample.circle_x == -156 && sample.circle_y == 156);
    assert(sample.cstick_x == -130 && sample.cstick_y == -78);
    assert(state.frames == 11);
    sample = (struct native_input_sample){0};
    halo_rp_pad_apply(&state, &sample, 0, 1350);
    assert(!sample.held && !sample.circle_x && !state.frames);

    assert(halo_rp_pad_parse(
        "buttons=0000,lx=0,ly=0,rx=1000,ry=-1000,lt=500,rt=500,frames=1",
        2000, &state));
    sample = (struct native_input_sample){0};
    halo_rp_pad_apply(&state, &sample, 1, 2001);
    assert((sample.held & (NATIVE_KEY_L | NATIVE_KEY_R)) ==
        (NATIVE_KEY_L | NATIVE_KEY_R));
    assert(!(sample.held & (NATIVE_KEY_ZL | NATIVE_KEY_ZR)));
    assert(sample.cstick_x == 156 && sample.cstick_y == -156);
    assert(!state.frames);

    assert(halo_rp_pad_parse(
        "buttons=0200,lx=0,ly=0,rx=0,ry=0,lt=0,rt=0,frames=1",
        3000, &state));
    sample = (struct native_input_sample){0};
    sample.held = NATIVE_KEY_SELECT;
    halo_rp_pad_apply(&state, &sample, 0, 3001);
    assert((sample.held & (NATIVE_KEY_START | NATIVE_KEY_SELECT)) ==
        NATIVE_KEY_SELECT);
    return 0;
}
