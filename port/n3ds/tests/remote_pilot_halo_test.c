/* New3DS integration additions by Kevin Boggs. */
#include "remote_pilot_halo.h"
#include <assert.h>
#include <string.h>

/* Kevin Boggs: main - New3DS integration. */
int main(void)
{
    char json[1024];
    assert(!halo_rp_format_perf(json, sizeof(json)));
    halo_rp_set_device_model(1);
    assert(halo_rp_format_meta(json, sizeof(json)));
    assert(strstr(json, "\"deviceModel\":\"new\""));
    assert(strstr(json, "\"console\":true"));
    assert(strstr(json, "\"pad\":true"));
    assert(strstr(json, "\"touch\":true"));
    assert(strstr(json, "\"shot\":true"));
    assert(strstr(json, "\"preview\":"));
    const uint64_t stages[5] = {100, 200, 300, 400, 500};
    halo_rp_record_frame(12345, 77, 62, "levels\\test\\bloodgulch\\bloodgulch",
        0, 1, stages, 3333, 100000, 0, 1, 2, 3);
    assert(halo_rp_format_perf(json, sizeof(json)));
    assert(strstr(json, "\"timestampMs\":12345"));
    assert(strstr(json, "\"map\":\"bloodgulch\""));
    assert(strstr(json, "\"mapSource\":\"pc\""));
    assert(strstr(json, "\"workMs\":15.000"));
    assert(strstr(json, "\"fps\":30.00"));
    assert(!strstr(json, "FreeBytes"));
    halo_rp_record_frame(12350, 78, 63, "bad\\map\"inject", 1, 0,
        stages, 3333, 100000, 1, 4, 5, 6);
    assert(halo_rp_format_perf(json, sizeof(json)));
    assert(strstr(json, "\"map\":\"map\""));
    assert(strstr(json, "\"heapFreeBytes\":4"));
    assert(!halo_rp_format_perf(json, 8));
    return 0;
}
