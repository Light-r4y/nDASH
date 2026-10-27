#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Starts the CAN task (brings up the TWAI node from the settings, decodes frames into
// signals by the CAN table) and the heartbeat task (periodic DASH_ALIVE frame to the
// ECU). Needs lnd_settings_init() and lnd_can_map_init() done. Settings and table
// changes are picked up on the fly.
void lnd_can_task_start(void);

// Sends a frame (blocks up to LND_CAN_POLL_MS for a free TX slot). false - the node is
// not up, the mode is "listen_only" or the frame was not accepted.
bool lnd_can_send(uint32_t id, const uint8_t *data, uint8_t length, bool extended);

typedef struct {
  bool up;               // the node is running
  bool listen_only;
  uint8_t state;         // 0 error active, 1 warning, 2 error passive, 3 bus-off
  uint16_t tx_errors, rx_errors;
  uint32_t rx_frames;    // frames the ISR let through
  uint32_t rx_dropped;   // ... and the queue had no room for
  uint32_t bus_off_count;
  uint32_t bus_errors;   // since the node was started
} lnd_can_status_t;

void lnd_can_get_status(lnd_can_status_t *out);

typedef struct {
  uint32_t id;
  bool ext;
  uint8_t length;
  uint8_t data[8];
  uint32_t count;   // frames seen with this ID
  uint32_t age_ms;  // since the last one
} lnd_can_sniff_frame_t;

// The last frame of every ID seen (up to LND_CAN_SNIFFER_MAX), for the web page. Each
// call also lets frames of IDs outside the CAN table through the ISR filter for
// LND_CAN_SNIFFER_ACTIVE_MS. Returns how many were stored in `out`.
size_t lnd_can_sniffer_get(lnd_can_sniff_frame_t *out, size_t max);

#ifdef __cplusplus
} /*extern "C"*/
#endif
