#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Reads the touch panel in a task of its own (not in the LVGL task, where a slow I2C transfer
// would cost frames) and turns the touches into gestures:
//   swipe left / right  - the next / previous screen
//   swipe up / down     - the backlight brighter / dimmer (kept in the settings)
// Call after lnd_ui_task_start().
void lnd_touch_task_start(void);

#ifdef __cplusplus
} /*extern "C"*/
#endif
