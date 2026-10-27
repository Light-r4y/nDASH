#pragma once

#include "overlay.h"

#ifdef __cplusplus
extern "C" {
#endif

// Starts the UI: esp_lvgl_port (LVGL task, display, touch), the screens, and the
// task that feeds signal values into the widgets every LND_UI_TASK_PERIOD_MS.
// The board must be initialised (lnd_board_init) first.
void lnd_ui_task_start(void);

// Asks for another screen: +1 the next, -1 the previous (wraps around). Safe from any task; the
// UI task does it on its next pass.
void lnd_ui_screen_step(int delta);

#ifdef __cplusplus
} /*extern "C"*/
#endif
