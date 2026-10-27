#include "buttons_task.h"
#include "auth.h"
#include "board.h"
#include "conf.h"
#include "settings.h"
#include "ui_task.h"
#include "webcfg_task.h"

#include <esp_log.h>
#include <esp_timer.h>
#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const char *TAG = "buttons";

// The next backlight level of the cycle: the first one below the current, the first of all after the last.
static int next_brightness(int current)
{
  static const uint8_t levels[] = LND_BTN_BRIGHTNESS_STEPS;
  for (size_t i = 0; i < sizeof(levels); i++)
  {
    if (levels[i] < current)
    {
      return levels[i];
    }
  }
  return levels[0];
}

// The pin of the button from the settings (the BOOT pin when it is refused).
static void apply_pin(int pin)
{
  if (pin != lnd_board_btn_get_pin() && !lnd_board_btn_set_pin(pin))
  {
    ESP_LOGW(TAG, "GPIO%d cannot be the button, staying on GPIO%d", pin, lnd_board_btn_get_pin());
  }
}

static void buttons_task(void *arg)
{
  (void)arg;
  TickType_t last = xTaskGetTickCount();
  uint32_t held_ms = 0;
  uint32_t high_ms = 0;      // how long the pin has been high without a break (saturates)
  bool press_clean = false;  // the pin was quiet (high) before this press began
  bool ap_fired = false;     // the access point has been started for this press
  bool warn_fired = false;
  bool reset_fired = false;
  bool brightness_dirty = false;
  int64_t brightness_changed_ms = 0;
  uint32_t settings_gen = 0;

  while (1)
  {
    vTaskDelayUntil(&last, pdMS_TO_TICKS(LND_BUTTONS_TICK_MS));

    if (settings_gen != lnd_settings_generation())
    {
      settings_gen = lnd_settings_generation();
      lnd_display_settings_t d;
      lnd_settings_get_display(&d);
      apply_pin(d.button_pin);
    }
    // The level is written to NVS once the presses have stopped, as after the swipes.
    const int64_t now_ms = esp_timer_get_time() / 1000;
    if (brightness_dirty && now_ms - brightness_changed_ms >= LND_BRIGHTNESS_SAVE_DELAY_MS)
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

    if (lnd_board_btn_pressed())
    {
      if (held_ms == 0)
      {
        press_clean = high_ms >= LND_BTN_SHARED_QUIET_MS;
      }
      high_ms = 0;
      if (held_ms < LND_AUTH_RESET_HOLD_MS)
      {
        held_ms += LND_BUTTONS_TICK_MS;
      }
      if (held_ms >= LND_BTN_AP_HOLD_MS && !ap_fired)
      {
        ap_fired = true;
        if (!lnd_webcfg_running())
        {
          ESP_LOGI(TAG, "BOOT held - starting the access point");
          lnd_webcfg_start();
        }
      }
      // Held on: clear a forgotten settings password. The Wi-Fi password cannot be
      // forgotten: it is shown on the screen with the access point. A warning comes
      // first; releasing the button before the end cancels.
      if (held_ms >= LND_AUTH_RESET_WARN_MS && !warn_fired)
      {
        warn_fired = true;
        lnd_ui_toast("Hold to reset password");
      }
      if (held_ms >= LND_AUTH_RESET_HOLD_MS && !reset_fired)
      {
        reset_fired = true;
        const bool had = lnd_auth_is_enabled();
        lnd_auth_set_password(NULL);
        ESP_LOGW(TAG, "BOOT held %d ms - web password cleared", LND_AUTH_RESET_HOLD_MS);
        lnd_ui_toast(had ? "Password reset" : "No password set");
      }
    }
    else
    {
      // A short press, released before the access point or the password reset: the next backlight level.
      // On the shared BOOT pin (GPIO0 is also a panel data line there) the press must be longer and come after a quiet
      // pin, so that bursts of picture data are not taken for a press.
      const bool shared_pin = LND_BTN_BOOT_SHARED_WITH_LCD && lnd_board_btn_get_pin() == LND_BTN_BOOT_PIN;
      const uint32_t min_ms = shared_pin ? LND_BTN_SHARED_MIN_MS : LND_BTN_DEBOUNCE_MS;
      if (high_ms < UINT32_MAX - LND_BUTTONS_TICK_MS)
      {
        high_ms += LND_BUTTONS_TICK_MS;
      }
      if (shared_pin && held_ms >= LND_BTN_DEBOUNCE_MS && held_ms < LND_BTN_AP_HOLD_MS && !press_clean)
      {
        ESP_LOGD(TAG, "short press ignored: the pin was not quiet before it (%u ms)", (unsigned)held_ms);
      }
      if (LND_HAS_BRIGHTNESS && (!shared_pin || press_clean) && held_ms >= min_ms && held_ms < LND_BTN_AP_HOLD_MS && !ap_fired)
      {
        const int level = next_brightness(lnd_board_backlight_get_pct());
        lnd_board_backlight_set_pct(level);
        brightness_dirty = true;
        brightness_changed_ms = now_ms;
        char msg[32];
        snprintf(msg, sizeof(msg), "Brightness %d %%", level);
        lnd_ui_toast(msg);
      }
      held_ms = 0;
      ap_fired = false;
      warn_fired = false;
      reset_fired = false;
    }
  }
}

void lnd_buttons_task_start(void)
{
  BaseType_t ok = xTaskCreatePinnedToCore(buttons_task, "buttons_task", LND_BUTTONS_TASK_STACK, NULL,
                                          LND_BUTTONS_TASK_PRIORITY, NULL, LND_BUTTONS_TASK_CORE);
  configASSERT(ok == pdPASS);
}
