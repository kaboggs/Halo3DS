/* New3DS integration additions by Kevin Boggs. */
#ifndef HALO_REMOTE_PILOT_TRANSPORT_H
#define HALO_REMOTE_PILOT_TRANSPORT_H
#include <stddef.h>
#include "engine_input.h"

/* Kevin Boggs: halo_rp_transport_start - New3DS integration. */
void halo_rp_transport_start(void);
/* Kevin Boggs: halo_rp_transport_stop - New3DS integration. */
void halo_rp_transport_stop(void);
#ifdef HALO_N3DS_REMOTE_PILOT_DEV_MENU
enum halo_rp_link_state {
    HALO_RP_NOT_ARMED,
    HALO_RP_WAITING,
    HALO_RP_AUTHENTICATED,
    HALO_RP_RETRYING,
    HALO_RP_NETWORK_ERROR
};
/* Kevin Boggs: halo_rp_transport_status - New3DS integration. */
enum halo_rp_link_state halo_rp_transport_status(void);
/* Kevin Boggs: halo_rp_transport_retry - New3DS integration. */
void halo_rp_transport_retry(void);
/* Kevin Boggs: halo_rp_transport_address - New3DS integration. */
int halo_rp_transport_address(char out[16]);
/* Kevin Boggs: halo_rp_transport_bug_it_request - New3DS integration. */
int halo_rp_transport_bug_it_request(void);
/* Kevin Boggs: halo_rp_transport_bug_it_sequence - New3DS integration. */
unsigned int halo_rp_transport_bug_it_sequence(void);
#endif
/* Kevin Boggs: halo_rp_take_console - New3DS integration. */
int halo_rp_take_console(char *out, size_t capacity);
/* Kevin Boggs: halo_rp_complete_console - New3DS integration. */
void halo_rp_complete_console(int succeeded);
/* Kevin Boggs: halo_rp_apply_input - New3DS integration. */
void halo_rp_apply_input(struct native_input_sample *sample);
/* Kevin Boggs: halo_rp_capture_if_requested - New3DS integration. */
void halo_rp_capture_if_requested(void);

#endif
