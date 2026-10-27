#pragma once

#include "layout.h"

#ifdef __cplusplus
extern "C" {
#endif

// Builds the current screen from the layout and shows it; the previous one is
// deleted, so only the active screen lives in memory. Call with the LVGL lock held.
void lnd_screens_rebuild(const lnd_layout_t *layout);

// Shows another screen of the layout (index wraps around); the old one is deleted. LVGL lock held.
void lnd_screens_select(const lnd_layout_t *layout, int index);

// The index of the screen shown now.
int lnd_screens_current(void);

// Moves the current signal values into the widgets; only what changed is touched.
// LVGL lock held.
void lnd_screens_update(void);

#ifdef __cplusplus
} /*extern "C"*/
#endif
