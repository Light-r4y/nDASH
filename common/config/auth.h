#pragma once
#include <stdbool.h>

// Optional password protecting OTA and the modifying (POST) API requests - reading
// state (GET) stays open. Stored in NVS as SHA-256(salt + password) (see conf.h:
// LND_AUTH_SALT); an empty or missing hash means "protection off" (the default).
//
// A forgotten password is cleared by holding the BOOT button for LND_AUTH_RESET_HOLD_MS
// (buttons_task) on a running board. A new one is set on the "Access" page
// (POST /api/auth/password).
void lnd_auth_init(void);

// Is protection on (is a password set).
bool lnd_auth_is_enabled(void);

// Sets a new password (stored in NVS) or clears it (password == "" or NULL).
bool lnd_auth_set_password(const char *password);

// Checks a password against the stored hash. Protection off - always true.
bool lnd_auth_check(const char *password);
