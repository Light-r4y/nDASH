#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Polls the BOOT button (GPIO0). Held for LND_BTN_AP_HOLD_MS - brings the access point
// up; held for LND_AUTH_RESET_HOLD_MS - clears the forgotten settings password.
void lnd_buttons_task_start(void);

#ifdef __cplusplus
} /*extern "C"*/
#endif
