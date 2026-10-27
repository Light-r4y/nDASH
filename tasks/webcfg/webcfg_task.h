#pragma once
#include <stdbool.h>
#include <stdint.h>

// Configuration over Wi-Fi: an access point plus an HTTP server.
//
// While driving, Wi-Fi is off. The access point is brought up by holding the BOOT
// button (buttons_task) and goes down only with a restart. The SSID is nDash-XXXX (the
// tail of the MAC) or the one from the settings, WPA2.
//
// HTTP: static files from the www partition (LND_WWW_BASE_PATH); the /ota page is
// built into the firmware (ota_page.h); modifying requests and OTA are protected by
// an optional password (common/config/auth.h).

void lnd_webcfg_task_start(void);

// Asks for the access point to come up. Does not block, safe from any task: the
// webcfg task does it within LND_WEBCFG_TICK_MS.
void lnd_webcfg_start(void);

// true if the access point is up (or coming up).
bool lnd_webcfg_running(void);

// The state of the access point for the screen: off / starting (the button was held,
// the network is not up yet) / up. While the network is open the password is empty.
typedef enum {
  LND_AP_OFF = 0,
  LND_AP_STARTING,
  LND_AP_UP,
} lnd_ap_state_t;

typedef struct {
  lnd_ap_state_t state;
  char ssid[33];
  char password[65];
  uint8_t clients;  // devices connected right now
} lnd_ap_info_t;

void lnd_webcfg_get_ap_info(lnd_ap_info_t *out);
