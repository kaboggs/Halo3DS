/* New3DS integration additions by Kevin Boggs. */
#include "remote_pilot_halo_touch.h"
#include <assert.h>
#include <string.h>

/* Kevin Boggs: main - New3DS integration. */
int main(void)
{
    struct halo_rp_touch_state state = {0};
    struct native_input_sample sample = {0};
    assert(halo_rp_touch_parse("x=0,y=0,down=1", 1000, &state));
    assert(state.down && state.expires_ms == 1900);
    sample.circle_x = 42;
    sample.cstick_y = -55;
    halo_rp_touch_apply(&state, &sample, 1000);
    assert((sample.held & NATIVE_KEY_TOUCH) && sample.touch_x == 0 &&
        sample.touch_y == 0 && sample.circle_x == 42 && sample.cstick_y == -55);

    /* A refreshed hold moves the pointer without adding another key press. */
    assert(halo_rp_touch_parse("x=319,y=239,down=1", 1500, &state));
    sample = (struct native_input_sample){0};
    halo_rp_touch_apply(&state, &sample, 1899);
    assert((sample.held & NATIVE_KEY_TOUCH) && sample.touch_x == 319 &&
        sample.touch_y == 239 && state.expires_ms == 2400);

    /* Explicit release and loss of refresh each remove held touch. */
    assert(halo_rp_touch_parse("x=319,y=239,down=0", 1900, &state));
    sample = (struct native_input_sample){0};
    halo_rp_touch_apply(&state, &sample, 1900);
    assert(!(sample.held & NATIVE_KEY_TOUCH) && !state.down);
    assert(halo_rp_touch_parse("x=123,y=90,down=1", 2000, &state));
    halo_rp_touch_apply(&state, &sample, 2900);
    assert(!(sample.held & NATIVE_KEY_TOUCH) && !state.down);

    /* A real finger owns the screen and cancels a concurrent remote hold. */
    assert(halo_rp_touch_parse("x=12,y=34,down=1", 3000, &state));
    sample = (struct native_input_sample){.held=NATIVE_KEY_TOUCH,
        .touch_x=210, .touch_y=160};
    halo_rp_touch_apply(&state, &sample, 3001);
    assert(sample.held == NATIVE_KEY_TOUCH && sample.touch_x == 210 &&
        sample.touch_y == 160 && !state.down);

    const char *invalid[] = {
        "x=-1,y=0,down=1", "x=320,y=0,down=1", "x=1,y=240,down=1",
        "x=1,y=-1,down=1", "x=1,y=1,down=2", "x=1,y=1,down=10",
        "x=1,y=1,down=1junk", "x=1, y=1,down=1", "x=,y=1,down=1",
        "x=1,y=,down=1", "x=1000,y=1,down=1", "y=1,x=1,down=1"
    };
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(*invalid); ++i) {
        struct halo_rp_touch_state prior = state;
        assert(!halo_rp_touch_parse(invalid[i], 4000, &state));
        assert(!memcmp(&state, &prior, sizeof(state)));
    }
    assert(halo_rp_touch_parse("x=10,y=20,down=1", 5000, &state));
    halo_rp_touch_clear(&state);
    assert(!state.down && !state.expires_ms);
    return 0;
}
