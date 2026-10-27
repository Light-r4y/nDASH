#include "ui_task.h"
#include "board.h"
#include "conf.h"
#include "fps_overlay.h"
#include "layout.h"
#include "overlay.h"
#include "screens.h"
#include "settings.h"
#include "signals.h"
#include "units.h"

#include <esp_log.h>
#include <esp_lvgl_port.h>
#include <esp_timer.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const char *TAG = "ui_task";

#define LVGL_TICK_PERIOD_MS 5
#define LVGL_MAX_SLEEP_MS   500
#define OVERLAY_PERIOD_MS   250

static atomic_int s_screen_step;  // asked for by lnd_ui_screen_step(), taken by the UI task

static lv_display_t *lvgl_start(void)
{
  const lvgl_port_cfg_t port_cfg = {
      .task_priority = LND_LVGL_TASK_PRIORITY,
      .task_stack = LND_LVGL_TASK_STACK,
      .task_affinity = LND_LVGL_TASK_CORE,
      .task_max_sleep_ms = LVGL_MAX_SLEEP_MS,
      .task_stack_caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_DEFAULT,
      .timer_period_ms = LVGL_TICK_PERIOD_MS,
  };
  ESP_ERROR_CHECK(lvgl_port_init(&port_cfg));

  // Tear-free mode: LVGL draws straight into the panel's two PSRAM frame buffers
  // ("direct mode") and esp_lvgl_port swaps them at VSYNC. Needs the panel to be
  // created with LND_LCD_NUM_FBS == 2 (board.c).
  const lvgl_port_display_cfg_t disp_cfg = {
      .panel_handle = lnd_board_lcd_panel(),
      .buffer_size = LND_LCD_WIDTH * LND_LCD_HEIGHT,
      .double_buffer = true,
      .hres = LND_LCD_WIDTH,
      .vres = LND_LCD_HEIGHT,
      .color_format = LV_COLOR_FORMAT_RGB565,
      .flags = {
          .direct_mode = true,
      },
  };
  const lvgl_port_display_rgb_cfg_t rgb_cfg = {
      .flags = {
          .bb_mode = true,
          .avoid_tearing = true,
      },
  };
  lv_display_t *disp = lvgl_port_add_disp_rgb(&disp_cfg, &rgb_cfg);
  configASSERT(disp != NULL);

  // No LVGL input device: nothing on the screen is touchable, the gestures come from touch_task.
  return disp;
}

static void ui_task(void *arg)
{
  (void)arg;
  lv_display_t *disp = lvgl_start();

  // The layout structure is ~20 KB: on the heap, not on the task's stack.
  lnd_layout_t *layout = malloc(sizeof(*layout));
  configASSERT(layout != NULL);
  uint32_t layout_gen = lnd_layout_generation();
  lnd_display_settings_t shown_units;
  lnd_settings_get_display(&shown_units);
  lnd_layout_copy(layout);
  lvgl_port_lock(0);
  lnd_screens_rebuild(layout);
  lnd_fps_overlay_start(disp);
  lnd_overlay_init();
  lvgl_port_unlock();

  ESP_LOGI(TAG, "UI task started (LVGL %d.%d.%d, %dx%d, direct mode, %d frame buffers)", LVGL_VERSION_MAJOR,
           LVGL_VERSION_MINOR, LVGL_VERSION_PATCH, (int)LND_LCD_WIDTH, (int)LND_LCD_HEIGHT, (int)LND_LCD_NUM_FBS);

  TickType_t last = xTaskGetTickCount();
  int64_t last_overlay_ms = 0;
  while (1)
  {
    vTaskDelayUntil(&last, pdMS_TO_TICKS(LND_UI_TASK_PERIOD_MS));

    const int64_t now_ms = esp_timer_get_time() / 1000;
    lnd_signal_sim_step((uint32_t)now_ms);  // only while the demo is on

    // The layout was changed (from the web): build the screen again.
    // The pressure units were changed: the numbers and the scales are built in them.
    lnd_display_settings_t units;
    lnd_settings_get_display(&units);
    const bool units_changed = lnd_units_differ(&units, &shown_units);
    shown_units = units;
    const uint32_t gen = lnd_layout_generation();
    if (gen != layout_gen || units_changed)
    {
      layout_gen = gen;
      lnd_layout_copy(layout);
      lvgl_port_lock(0);
      lnd_screens_rebuild(layout);
      lvgl_port_unlock();
    }

    const int step = atomic_exchange(&s_screen_step, 0);
    if (step != 0 && layout->count > 1)
    {
      lvgl_port_lock(0);
      lnd_screens_select(layout, lnd_screens_current() + step);
      lvgl_port_unlock();
      char msg[40];
      snprintf(msg, sizeof(msg), "Screen %d / %d", lnd_screens_current() + 1, (int)layout->count);
      lnd_ui_toast(msg);
    }

    lvgl_port_lock(0);
    lnd_screens_update();
    if (now_ms - last_overlay_ms >= OVERLAY_PERIOD_MS)
    {
      last_overlay_ms = now_ms;
      lnd_overlay_update();
    }
    lvgl_port_unlock();
  }
}

void lnd_ui_screen_step(int delta)
{
  atomic_fetch_add(&s_screen_step, delta);
}

void lnd_ui_task_start(void)
{
  BaseType_t ok = xTaskCreatePinnedToCore(ui_task, "ui_task", LND_UI_TASK_STACK, NULL, LND_UI_TASK_PRIORITY, NULL,
                                          LND_UI_TASK_CORE);
  configASSERT(ok == pdPASS);
}
