#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

// General settings: one JSON blob in NVS (LND_SETTINGS_NVS_KEY). Adding fields does
// not reset the stored ones: what is missing from the JSON takes the default.
//
// {
//   "version": 1,
//   "can":     { "bitrate": 500000, "mode": "normal", "heartbeat": true, "demo": false },
//   "display": { "brightness": 80, "baro": "kpa", "boost": "bar", "button_pin": 0 },
//   "log":     { "mode": "manual", "signal": "rpm", "op": ">", "value": 500, "stop_delay": 10 },
//   "wifi_ap": { "ssid": "", "password": "ndash1234" }
// }
//
// can.bitrate         bit/s, 10000..1000000
// can.mode            "listen_only" - the node only monitors the bus: it neither acknowledges
//                     frames nor transmits (so no heartbeat and no bench commands);
//                     "normal" - acknowledges frames and may transmit
// can.heartbeat       send the DASH_ALIVE frame to the ECU (only in "normal" mode)
// can.demo            the signals are driven by the generator, CAN frames are ignored
//                     (for tuning the look on the desk). Default: the NDASH_DEMO build option.
// display.brightness  backlight, LND_BRIGHTNESS_MIN_PCT..100 %
// display.button_pin  GPIO of the button (0 - the BOOT button of the board); an external one goes between the GPIO and GND
// display.baro        "kpa" | "inhg": signals marked "pressure": "abs" (absolute pressure, kPa)
// display.boost       "bar" | "psi": signals marked "pressure": "boost" (absolute MAP shown against the
//                     atmosphere); in "psi" the vacuum is shown in inHg, see units.h
// log.mode            "manual" - only by the button of the web page "Logs"; "boot" - once after power-on,
//                     as soon as the card is ready; "condition" - while the signal is above (op ">") or
//                     below ("<") the value, and stop_delay seconds after that
// log.stop_delay      0..3600 s
// wifi_ap.ssid        empty = "nDash-XXXX" from the MAC
// wifi_ap.password    8..64 characters, WPA2; shorter than 8 - an open network

typedef enum {
  LND_CAN_MODE_LISTEN_ONLY = 0,
  LND_CAN_MODE_NORMAL,
} lnd_can_mode_t;

typedef struct {
  uint32_t bitrate;
  lnd_can_mode_t mode;
  bool heartbeat;
  bool demo;
} lnd_can_settings_t;

typedef enum {
  LND_BARO_KPA = 0,
  LND_BARO_INHG,
} lnd_baro_unit_t;

typedef enum {
  LND_BOOST_BAR = 0,
  LND_BOOST_PSI,
} lnd_boost_unit_t;

typedef struct {
  uint8_t brightness;  // %
  lnd_baro_unit_t baro;
  lnd_boost_unit_t boost;
  uint8_t button_pin;  // GPIO of the button
} lnd_display_settings_t;

typedef enum {
  LND_LOG_MANUAL = 0,
  LND_LOG_BOOT,
  LND_LOG_CONDITION,
} lnd_log_mode_t;

#define LND_LOG_SIGNAL_MAX_LEN 15  // = the length of a signal name in the layout

typedef struct {
  lnd_log_mode_t mode;
  char signal[LND_LOG_SIGNAL_MAX_LEN + 1];  // the signal of the condition ("condition" mode)
  bool less;                                // false: signal > value, true: signal < value
  float value;
  uint16_t stop_delay;                      // s the condition must stay false for the recording to stop
} lnd_log_settings_t;

typedef struct {
  char ssid[33];
  char password[65];
} lnd_wifi_settings_t;

// Loads the settings from NVS (defaults if none). Call after nvs_flash_init().
void lnd_settings_init(void);

void lnd_settings_get_can(lnd_can_settings_t *out);
void lnd_settings_get_display(lnd_display_settings_t *out);
void lnd_settings_get_wifi(lnd_wifi_settings_t *out);
void lnd_settings_get_log(lnd_log_settings_t *out);

// Validate, store and apply. ESP_ERR_INVALID_ARG - out of range.
esp_err_t lnd_settings_set_can(const lnd_can_settings_t *in);
esp_err_t lnd_settings_set_display(const lnd_display_settings_t *in);
esp_err_t lnd_settings_set_log(const lnd_log_settings_t *in);  // the conditions of the recording, at once
esp_err_t lnd_settings_set_wifi(const lnd_wifi_settings_t *in);  // takes effect after a restart

// The JSON of the settings on the heap (free() it); NULL on out of memory. The Wi-Fi
// password is left out: reading is open and the password is what protects the network.
char *lnd_settings_json_dup(void);

// Takes a JSON (possibly with only some of the fields), merges it over the current
// settings, validates, stores and applies.
esp_err_t lnd_settings_apply_json(const char *json);

// Grows on every change; tasks that own the settings' effect watch it.
uint32_t lnd_settings_generation(void);
