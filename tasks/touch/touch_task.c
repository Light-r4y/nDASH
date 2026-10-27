#include "touch_task.h"
#include "board.h"
#include "conf.h"
#include "settings.h"
#include "ui_task.h"

#include <esp_lcd_touch.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <stdio.h>
#include <stdlib.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const char *TAG = "touch";

typedef enum {
  GESTURE_NONE,
  GESTURE_LEFT,
  GESTURE_RIGHT,
  GESTURE_UP,
  GESTURE_DOWN,
} gesture_t;

// One read of the panel: true - a finger is down at (x, y). A failed read counts as "no finger".
static bool read_touch(esp_lcd_touch_handle_t tp, int *x, int *y)
{
  if (esp_lcd_touch_read_data(tp) != ESP_OK)
  {
    return false;
  }
  esp_lcd_touch_point_data_t p;
  uint8_t n = 0;
  if (esp_lcd_touch_get_data(tp, &p, &n, 1) != ESP_OK || n == 0)
  {
    return false;
  }
  *x = p.x;
  *y = p.y;
  return true;
}

static gesture_t classify(int dx, int dy, int64_t ms)
{
  const int ax = abs(dx), ay = abs(dy);
  if (ms > LND_SWIPE_MAX_MS || (ax < LND_SWIPE_MIN_PX && ay < LND_SWIPE_MIN_PX))
  {
    return GESTURE_NONE;
  }
  if (ax >= ay * 3 / 2)
  {
    return dx < 0 ? GESTURE_LEFT : GESTURE_RIGHT;
  }
  if (ay >= ax * 3 / 2)
  {
    return dy < 0 ? GESTURE_UP : GESTURE_DOWN;
  }
  return GESTURE_NONE;  // diagonal: neither
}

static void change_brightness(int delta_pct, bool *dirty, int64_t *changed_ms)
{
  if (!LND_HAS_BRIGHTNESS)
  {
    return;
  }
  int pct = lnd_board_backlight_get_pct() + delta_pct;
  if (pct < LND_BRIGHTNESS_MIN_PCT) pct = LND_BRIGHTNESS_MIN_PCT;
  if (pct > 100) pct = 100;
  lnd_board_backlight_set_pct(pct);
  char msg[32];
  snprintf(msg, sizeof(msg), "Brightness %d%%", pct);
  lnd_ui_toast(msg);
  *dirty = true;
  *changed_ms = esp_timer_get_time() / 1000;
}

static void touch_task(void *arg)
{
  (void)arg;
  esp_lcd_touch_handle_t tp = lnd_board_touch();
  if (tp == NULL)
  {
    ESP_LOGW(TAG, "no touch panel, the task ends");
    vTaskDelete(NULL);
    return;
  }

  bool down = false;
  int sx = 0, sy = 0, lx = 0, ly = 0, lost = 0;
  int64_t t0 = 0, brightness_changed_ms = 0;
  bool brightness_dirty = false;
  TickType_t last = xTaskGetTickCount();

  while (1)
  {
    vTaskDelayUntil(&last, pdMS_TO_TICKS(LND_TOUCH_TICK_MS));
    const int64_t now = esp_timer_get_time() / 1000;

    int x, y;
    if (read_touch(tp, &x, &y))
    {
      lost = 0;
      if (!down)
      {
        down = true;
        sx = x;
        sy = y;
        t0 = now;
      }
      lx = x;
      ly = y;
    }
    else if (down && ++lost >= LND_TOUCH_RELEASE_TICKS)
    {
      // a few empty reads in a row: the finger is up
      down = false;
      switch (classify(lx - sx, ly - sy, now - t0))
      {
      case GESTURE_LEFT:
        lnd_ui_screen_step(1);
        break;
      case GESTURE_RIGHT:
        lnd_ui_screen_step(-1);
        break;
      case GESTURE_UP:
        change_brightness(LND_BRIGHTNESS_SWIPE_PCT, &brightness_dirty, &brightness_changed_ms);
        break;
      case GESTURE_DOWN:
        change_brightness(-LND_BRIGHTNESS_SWIPE_PCT, &brightness_dirty, &brightness_changed_ms);
        break;
      default:
        break;
      }
    }

    // The brightness is written to NVS once the swipes have stopped, not on every one.
    if (brightness_dirty && now - brightness_changed_ms >= LND_BRIGHTNESS_SAVE_DELAY_MS)
    {
      brightness_dirty = false;
      lnd_display_settings_t d;
      lnd_settings_get_display(&d);
      d.brightness = (uint8_t)lnd_board_backlight_get_pct();
      if (lnd_settings_set_display(&d) != ESP_OK)
      {
        ESP_LOGW(TAG, "brightness not saved");
      }
    }
  }
}

void lnd_touch_task_start(void)
{
  BaseType_t ok = xTaskCreatePinnedToCore(touch_task, "touch_task", LND_TOUCH_TASK_STACK, NULL, LND_TOUCH_TASK_PRIORITY,
                                          NULL, LND_TOUCH_TASK_CORE);
  configASSERT(ok == pdPASS);
}
