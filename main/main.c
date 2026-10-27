#include "auth.h"
#include "board.h"
#include "buttons_task.h"
#include "bench_cmds.h"
#include "can_map.h"
#include "can_task.h"
#include "layout.h"
#include "logger_task.h"
#include "media.h"
#include "settings.h"
#include "touch_task.h"
#include "ui_task.h"
#include "webcfg_task.h"

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_ota_ops.h>
#include <nvs_flash.h>

static const char *TAG = "main";

void app_main(void)
{
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
  {
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  ESP_ERROR_CHECK(err);

  lnd_settings_init();
  lnd_auth_init();
  lnd_can_map_init();
  lnd_bench_cmds_init();
  lnd_layout_init();
  lnd_board_init();
  lnd_display_settings_t display;
  lnd_settings_get_display(&display);
  lnd_board_backlight_set_pct(display.brightness);
  lnd_media_mount();
  lnd_ui_task_start();
  lnd_touch_task_start();
  lnd_logger_task_start();  // the card is mounted on request or by the settings
  lnd_can_task_start();
  lnd_webcfg_task_start();  // the access point comes up on request

  // Everything is up: confirm the new firmware, otherwise a restart with rollback
  // enabled (CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE) brings the old one back.
  esp_ota_mark_app_valid_cancel_rollback();

  // Last: a long press goes straight to the webcfg task.
  lnd_buttons_task_start();

  ESP_LOGI(TAG, "nDash started, board %s, internal heap free %u (largest block %u)", NDASH_BOARD_NAME,
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL), (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
}
