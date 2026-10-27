#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// What lives on top of every screen: the card with the access point's name, password
// and address (LND_AP_CARD_MS after it comes up), then a Wi-Fi icon with the number of
// clients; and short messages ("Password reset"). LVGL lock held for both calls.
void lnd_overlay_init(void);
void lnd_overlay_update(void);  // call every few hundred ms

// A short message on the screen for LND_TOAST_MS. Safe from any task.
void lnd_ui_toast(const char *text);

#ifdef __cplusplus
} /*extern "C"*/
#endif
