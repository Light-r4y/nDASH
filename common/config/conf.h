#pragma once

// Project constants. GPIO numbers do not appear here: they live only in
// common/board/board.h.

// ------------------------------------------------------------------
// Tasks: stack, priority, core
// ------------------------------------------------------------------
// Core 1 is rendering only; everything else runs on core 0 together with Wi-Fi.
// Wi-Fi is switched on only on request, so while driving CAN and touch share
// core 0 with nobody else.

// esp_lvgl_port task: runs lv_timer_handler and the rendering.
#define LND_LVGL_TASK_STACK 8192
#define LND_LVGL_TASK_PRIORITY 4
#define LND_LVGL_TASK_CORE 1

// ui_task: moves signal values into widgets under lvgl_port_lock().
#define LND_UI_TASK_STACK 8192
#define LND_UI_TASK_PRIORITY 3
#define LND_UI_TASK_CORE 1
#define LND_UI_TASK_PERIOD_MS 16
// Numbers (tiles, bare values) are redrawn at most this often: faster digits cannot be read, and
// every redraw costs a few milliseconds of rendering. At most LND_NUMBERS_PER_UPDATE of them are
// refreshed in one pass of the UI task (the rest wait for the next frames).
#define LND_NUMBER_UPDATE_MS 100
#define LND_NUMBERS_PER_UPDATE 3

#define LND_CAN_TASK_STACK 4096
#define LND_CAN_TASK_PRIORITY 8
#define LND_CAN_TASK_CORE 0

// The DASH_ALIVE frame sent back to the ECU.
#define LND_CAN_HEARTBEAT_TASK_STACK 2048
#define LND_CAN_HEARTBEAT_PERIOD_MS 500

// ------------------------------------------------------------------
// CAN
// ------------------------------------------------------------------
// buttons_task: the BOOT button (GPIO0, on every supported board).
#define LND_BUTTONS_TASK_STACK 2560
#define LND_BUTTONS_TASK_PRIORITY 5
#define LND_BUTTONS_TASK_CORE 0
#define LND_BUTTONS_TICK_MS 10
// Holding the button this long (ms) brings the access point up (once per press).
#define LND_BTN_AP_HOLD_MS 2000
// A short press (shorter than LND_BTN_AP_HOLD_MS, longer than LND_BTN_DEBOUNCE_MS) steps the backlight down through these
// levels (descending) and, after the last one, back to the first; saved LND_BRIGHTNESS_SAVE_DELAY_MS after the last press.
#define LND_BTN_DEBOUNCE_MS 40
// The BOOT pin shared with the panel (LND_BTN_BOOT_SHARED_WITH_LCD) also shows picture data, so a short press
// there counts only when the pin was high for LND_BTN_SHARED_QUIET_MS before it and stays low for
// LND_BTN_SHARED_MIN_MS (instead of LND_BTN_DEBOUNCE_MS). Raise both if the brightness still jumps on its own.
#define LND_BTN_SHARED_QUIET_MS 300
#define LND_BTN_SHARED_MIN_MS 150
#define LND_BTN_BRIGHTNESS_STEPS {100, 60, 30, 10}
// Holding it this long clears a forgotten settings password; from the warning moment
// on, the screen tells what is about to happen (releasing earlier cancels). Do not hold
// the button at power-on: GPIO0 is the BOOT strapping pin.
#define LND_AUTH_RESET_HOLD_MS 10000
#define LND_AUTH_RESET_WARN_MS 7000

// touch_task: the touch panel is polled and the touches become gestures. A swipe is a move of at
// least LND_SWIPE_MIN_PX in at most LND_SWIPE_MAX_MS; left/right change the screen, up/down the
// backlight by LND_BRIGHTNESS_SWIPE_PCT (written to NVS LND_BRIGHTNESS_SAVE_DELAY_MS after the
// last swipe). A finger is up after LND_TOUCH_RELEASE_TICKS empty reads in a row.
#define LND_TOUCH_TASK_STACK 3072
#define LND_TOUCH_TASK_PRIORITY 4
#define LND_TOUCH_TASK_CORE 0
#define LND_TOUCH_TICK_MS 20
#define LND_TOUCH_RELEASE_TICKS 3
#define LND_SWIPE_MIN_PX 100
#define LND_SWIPE_MAX_MS 900
#define LND_BRIGHTNESS_SWIPE_PCT 10
#define LND_BRIGHTNESS_SAVE_DELAY_MS 3000

// logger_task: the signals of the layout are recorded to the microSD card in the MegaLogViewer
// format (tasks/logger), LND_LOGGER_PERIOD_MS apart; the buffer goes to the card every
// LND_LOGGER_FLUSH_MS and is synced (fsync) every LND_LOGGER_SYNC_MS. Recording stops when less than
// LND_LOGGER_MIN_FREE_KB is left. The card is looked for LND_LOGGER_MOUNT_DELAY_MS after power-on
// (the screen finishes its start first) and then every LND_LOGGER_RETRY_MS in the modes that need it.
#define LND_LOGGER_TASK_STACK 6144
#define LND_LOGGER_TASK_PRIORITY 2
#define LND_LOGGER_TASK_CORE 0
#define LND_LOGGER_PERIOD_MS 50
#define LND_LOGGER_FLUSH_MS 1000
#define LND_LOGGER_SYNC_MS 2000
#define LND_LOGGER_BUF_BYTES 4096
#define LND_LOGGER_MOUNT_DELAY_MS 3000
#define LND_LOGGER_RETRY_MS 15000
#define LND_LOGGER_MIN_FREE_KB 4096
#define LND_LOG_MOUNT_PATH "/sdcard"
#define LND_LOG_FILE_PREFIX "log_"  // log_0001.mlg: an 8.3 name, long FAT names are not needed
#define LND_LOG_FILE_EXT ".mlg"

// Bench page: a value pinned from the web lasts this long (ms) after the last request; the page
// repeats it while open, so closing the page gives the signal back.
#define LND_BENCH_HOLD_MS 6000
// A bench command marked "stopped only" is refused while the rpm signal shows more than this.
#define LND_BENCH_RUNNING_RPM 100

// webcfg_task: the access point and the HTTP server, brought up on request.
#define LND_WEBCFG_TASK_STACK 4096
#define LND_WEBCFG_TASK_PRIORITY 2
#define LND_WEBCFG_TASK_CORE 0
#define LND_WEBCFG_TICK_MS 100
// The httpd task is created by esp_http_server; these are its parameters.
#define LND_HTTPD_STACK 8192
#define LND_HTTPD_PRIORITY 3  // below the touch task (4): a long page transfer must not starve the gestures
#define LND_HTTPD_CORE 0

// ------------------------------------------------------------------
// Wi-Fi: the access point only comes up on request (BOOT held)
// ------------------------------------------------------------------
// The default SSID is the prefix plus the end of the MAC (nDash-XXXX), so every dash
// has its own. SSID and password are set from the web and kept in the settings.
#define LND_WIFI_AP_SSID_PREFIX "nDash-"
#define LND_WIFI_AP_PASS_DEFAULT "ndash1234"
#define LND_WIFI_AP_SSID_MAX_LEN 32
// WPA2-PSK needs a password of 8..64 characters.
#define LND_WIFI_AP_PASSWORD_MIN_LEN 8
#define LND_WIFI_AP_PASSWORD_MAX_LEN 64

// ------------------------------------------------------------------
// Web interface and OTA
// ------------------------------------------------------------------
#define LND_WWW_PARTITION "www"
#define LND_WWW_BASE_PATH "/www"
#define LND_MEDIA_PARTITION "media"
#define LND_MEDIA_BASE_PATH "/media"
#define LND_MEDIA_MAX_FILE 1000000  // bytes in one picture file
// Password on OTA and the modifying API requests (common/config/auth.c).
#define LND_AUTH_NVS_KEY "auth_hash"          // SHA-256(salt + password); empty/missing = protection off
#define LND_AUTH_SALT "nDash-auth-v1-"        // a fixed salt, compiled into the firmware
#define LND_AUTH_PASSWORD_MIN_LEN 4
#define LND_AUTH_PASSWORD_MAX_LEN 64
#define LND_AUTH_REALM "nDash"                // Basic Auth realm (what the browser shows in its dialog)

// Frames travel from the ISR to can_task through a queue.
#define LND_CAN_RX_QUEUE_LEN 64
// How long receive()/transmit() wait for an RX frame / a TX slot.
#define LND_CAN_POLL_MS 20
#define LND_CAN_BITRATE_DEFAULT 500000
// "normal" acknowledges frames and may transmit (heartbeat, bench); "listen_only"
// only monitors. The dash has always sent the heartbeat, so "normal" is the default.
#define LND_CAN_MODE_DEFAULT 1  // LND_CAN_MODE_NORMAL (common/config/settings.h)
// After a bus-off the node is brought back this many ms later.
#define LND_CAN_BUSOFF_RECOVERY_MS 1000
// Sniffer for the web: how many different IDs it remembers, and for how many ms after
// the last request the frames of IDs outside the table are still let through (without
// the sniffer the ISR drops everything that is not in the table).
#define LND_CAN_SNIFFER_MAX 64
#define LND_CAN_SNIFFER_ACTIVE_MS 3000

// ------------------------------------------------------------------
// Screen
// ------------------------------------------------------------------
// Backlight in the settings, %. Below the minimum the screen is practically dark, so
// the web and NVS do not allow it.
#define LND_BRIGHTNESS_DEFAULT_PCT 80
#define LND_BRIGHTNESS_MIN_PCT 5
// How long (ms) the card with the access point's name and password stays on the
// screen; after that only a Wi-Fi icon with the number of clients is left.
#define LND_AP_CARD_MS 10000
// How long (ms) a short message ("Password reset") stays on the screen.
#define LND_TOAST_MS 3000
// Layout limits (common/config/layout.h)
#define LND_UI_MAX_SCREENS 8
#define LND_LAYOUT_MAX_WIDGETS 24
// Widgets that change (everything but static text) on one screen, and dials: more than this does not
// keep the frame rate comfortable (README, "Производительность"); the layout is refused above it.
#define LND_LAYOUT_MAX_LIVE 14
#define LND_LAYOUT_MAX_DIALS 2
// The layout, the settings and the CAN table are one JSON blob each in NVS.
#define LND_CONFIG_NVS_NAMESPACE "lnd_cfg"
#define LND_LAYOUT_NVS_KEY "layout"
#define LND_LAYOUT_JSON_MAX 16384
#define LND_SETTINGS_NVS_KEY "cfg"
#define LND_SETTINGS_JSON_MAX 2048
#define LND_SETTINGS_VERSION 1
#define LND_CAN_MAP_NVS_KEY "can_map"
#define LND_BENCH_CMDS_NVS_KEY "bench_cmds"
#define LND_BENCH_CMDS_JSON_MAX 8192
#define LND_CAN_MAP_JSON_MAX 4096

// FPS / frame-time overlay. Set at build time: idf.py -D NDASH_UI_SHOW_FPS=0 build
// (1 by default; tools/build_release.py passes 0).
#ifndef NDASH_UI_SHOW_FPS
#define NDASH_UI_SHOW_FPS 1
#endif
// Drive the signals with the generator instead of waiting for CAN (debugging aid).
// Set at build time: idf.py -D NDASH_DEMO=1 build
#ifndef NDASH_DEMO
#define NDASH_DEMO 0
#endif
