#include "board.h"

#include <esp_lcd_panel_rgb.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_io_i2c.h>
#include <esp_lcd_touch_gt911.h>
#include <driver/i2c_master.h>
#include <driver/ledc.h>
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_check.h>
#include <string.h>
#include <esp_vfs_fat.h>
#include <sdmmc_cmd.h>
#include <driver/sdspi_host.h>
#include <driver/spi_common.h>

#if defined(WAVESHARE_S3_LCD7) || defined(WAVESHARE_S3_LCD5)
#include "ch422g.h"
#endif

static const char *TAG = "board";

#define LEDC_TIMER      LEDC_TIMER_0
#define LEDC_CHANNEL    LEDC_CHANNEL_0
#define LEDC_SPEED_MODE LEDC_LOW_SPEED_MODE

static esp_lcd_panel_handle_t s_panel = NULL;
static esp_lcd_touch_handle_t s_tp = NULL;
static i2c_master_bus_handle_t s_i2c_bus = NULL;

static int s_backlight = 205;

esp_lcd_panel_handle_t lnd_board_lcd_panel(void)
{
  return s_panel;
}

esp_lcd_touch_handle_t lnd_board_touch(void)
{
  return s_tp;
}

static esp_err_t i2c_bus_init(void)
{
  i2c_master_bus_config_t bus_cfg = {
      .i2c_port = -1,
      .sda_io_num = LND_TOUCH_PIN_SDA,
      .scl_io_num = LND_TOUCH_PIN_SCL,
      .clk_source = I2C_CLK_SRC_DEFAULT,
      .glitch_ignore_cnt = 7,
      .flags = {.enable_internal_pullup = true},
  };
  return i2c_new_master_bus(&bus_cfg, &s_i2c_bus);
}

static esp_err_t touch_init(void)
{
  esp_lcd_panel_io_i2c_config_t io_config = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
  // The component's default is 100 kHz; Waveshare's own board configuration runs
  // the touch bus at 400 kHz, which makes each touch read about 4x shorter.
  io_config.scl_speed_hz = LND_TOUCH_FREQ;
  esp_lcd_panel_io_handle_t tp_io_handle = NULL;
  ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(s_i2c_bus, &io_config, &tp_io_handle), TAG, "touch io init failed");

  esp_lcd_touch_config_t tp_cfg = {
      .x_max = LND_LCD_WIDTH - 1,
      .y_max = LND_LCD_HEIGHT - 1,
      .rst_gpio_num = LND_TOUCH_PIN_RST,
      .int_gpio_num = LND_TOUCH_PIN_INT,
  };
  return esp_lcd_touch_new_i2c_gt911(tp_io_handle, &tp_cfg, &s_tp);
}

#if defined(WAVESHARE_S3_LCD7) || defined(WAVESHARE_S3_LCD5)
static void io_expander_init(void)
{
  // GPIO4 is the GT911's interrupt line (TP_IRQ, per Waveshare's wiki and board
  // configuration). The GT911 samples INT while its reset is released and picks
  // its I2C address from it: low -> 0x5D. TP_RST is released below through the
  // CH422G, so INT has to be held low before that. (Not a CAN enable, as the old
  // Arduino code's unexplained pinMode(4, OUTPUT) might suggest.)
  gpio_config_t tp_int_cfg = {
      .pin_bit_mask = 1ULL << 4,
      .mode = GPIO_MODE_OUTPUT,
  };
  gpio_config(&tp_int_cfg);
  gpio_set_level(GPIO_NUM_4, 0);

  ESP_ERROR_CHECK(lnd_ch422g_init(s_i2c_bus));
  ESP_ERROR_CHECK(lnd_ch422g_enable_all_outputs());
  // Reset touch + LCD and enable the backlight (EXIO2 is a plain on/off switch on
  // these boards) before panel/touch bring-up.
  // The card is not selected (CS high) until it is mounted.
  uint8_t io = LND_CH422G_IO_TP_RST | LND_CH422G_IO_LCD_RST | LND_CH422G_IO_LCD_BL | LND_CH422G_IO_SD_CS;
#if defined(WAVESHARE_S3_LCD7)
  // GPIO19/20 are shared by the CAN transceiver and the USB: EXIO5 high picks CAN. (On the 5" EXIO5 is the
  // isolated input DI1, the CAN has its own pins.)
  io |= LND_CH422G_IO_CAN_SEL;
#endif
  ESP_ERROR_CHECK(lnd_ch422g_set_io(io, true));
  vTaskDelay(pdMS_TO_TICKS(100));
}
#endif

static void backlight_init(void)
{
  ledc_timer_config_t timer_cfg = {
      .speed_mode = LEDC_SPEED_MODE,
      .duty_resolution = LEDC_TIMER_8_BIT,
      .timer_num = LEDC_TIMER,
      .freq_hz = 1000,
      .clk_cfg = LEDC_AUTO_CLK,
  };
  ESP_ERROR_CHECK(ledc_timer_config(&timer_cfg));

  ledc_channel_config_t channel_cfg = {
      .gpio_num = LND_LCD_PIN_BACKLIGHT,
      .speed_mode = LEDC_SPEED_MODE,
      .channel = LEDC_CHANNEL,
      .timer_sel = LEDC_TIMER,
      .duty = (uint32_t)s_backlight,
      .hpoint = 0,
  };
  ESP_ERROR_CHECK(ledc_channel_config(&channel_cfg));
}

void lnd_board_init(void)
{
  gpio_config_t btn = {
      .pin_bit_mask = 1ULL << LND_BTN_BOOT_PIN,
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_ENABLE,
  };
  gpio_config(&btn);

  ESP_ERROR_CHECK(i2c_bus_init());

#if defined(WAVESHARE_S3_LCD7) || defined(WAVESHARE_S3_LCD5)
  io_expander_init();
#endif

  backlight_init();

  // NOTE: hsync/vsync idle-level mapping below is derived from the previous
  // LovyanGFX *_POLARITY=0 config (all 4 boards), translated to esp_lcd's
  // idle-level flags. de_idle_high / pclk_active_neg / pclk_idle_high map
  // 1:1 by name from the original per-board defines.
  esp_lcd_rgb_panel_config_t panel_config = {
      .clk_src = LCD_CLK_SRC_DEFAULT,
      .timings = {
          .pclk_hz = LND_LCD_FREQ,
          .h_res = LND_LCD_WIDTH,
          .v_res = LND_LCD_HEIGHT,
          .hsync_pulse_width = LND_LCD_HSYNC_PULSE_WIDTH,
          .hsync_back_porch = LND_LCD_HSYNC_BACK_PORCH,
          .hsync_front_porch = LND_LCD_HSYNC_FRONT_PORCH,
          .vsync_pulse_width = LND_LCD_VSYNC_PULSE_WIDTH,
          .vsync_back_porch = LND_LCD_VSYNC_BACK_PORCH,
          .vsync_front_porch = LND_LCD_VSYNC_FRONT_PORCH,
          .flags = {
              .hsync_idle_low = LND_LCD_HSYNC_POLARITY != 0,
              .vsync_idle_low = LND_LCD_VSYNC_POLARITY != 0,
              .de_idle_high = LND_LCD_DE_IDLE_HIGH,
              .pclk_active_neg = LND_LCD_PCLK_ACTIVE_NEG,
              .pclk_idle_high = LND_LCD_PCLK_IDLE_HIGH,
          },
      },
      .data_width = 16,
      .in_color_format = LCD_COLOR_FMT_RGB565,
      .out_color_format = LCD_COLOR_FMT_RGB565,
      .num_fbs = LND_LCD_NUM_FBS,
      .bounce_buffer_size_px = LND_LCD_WIDTH * LND_BOUNCE_BUFFER_LINES,
      .dma_burst_size = 64,
      .hsync_gpio_num = LND_LCD_PIN_HSYNC,
      .vsync_gpio_num = LND_LCD_PIN_VSYNC,
      .de_gpio_num = LND_LCD_PIN_HENABLE,
      .pclk_gpio_num = LND_LCD_PIN_PCLK,
      .disp_gpio_num = GPIO_NUM_NC,
      .data_gpio_nums = {
          LND_LCD_PIN_DATA_B0, LND_LCD_PIN_DATA_B1, LND_LCD_PIN_DATA_B2, LND_LCD_PIN_DATA_B3, LND_LCD_PIN_DATA_B4,
          LND_LCD_PIN_DATA_G0, LND_LCD_PIN_DATA_G1, LND_LCD_PIN_DATA_G2, LND_LCD_PIN_DATA_G3, LND_LCD_PIN_DATA_G4, LND_LCD_PIN_DATA_G5,
          LND_LCD_PIN_DATA_R0, LND_LCD_PIN_DATA_R1, LND_LCD_PIN_DATA_R2, LND_LCD_PIN_DATA_R3, LND_LCD_PIN_DATA_R4,
      },
      .flags = {
          .fb_in_psram = 1,
      },
  };

  // No event callbacks are registered here: esp_lvgl_port installs its own on
  // the panel when the display is added (render_task.c).
  ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&panel_config, &s_panel));
  ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
  ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));

  esp_err_t tp_err = touch_init();
  if (tp_err != ESP_OK)
  {
    ESP_LOGE(TAG, "touch_init failed: %d (continuing without touch)", tp_err);
    s_tp = NULL;
  }
}

void lnd_board_backlight_set(int duty)
{
  if (duty < LND_BACKLIGHT_MIN)
  {
    duty = LND_BACKLIGHT_MIN;
  }
  else if (duty > LND_BACKLIGHT_MAX)
  {
    duty = LND_BACKLIGHT_MAX;
  }
  s_backlight = duty;
  ledc_set_duty(LEDC_SPEED_MODE, LEDC_CHANNEL, (uint32_t)s_backlight);
  ledc_update_duty(LEDC_SPEED_MODE, LEDC_CHANNEL);
}

int lnd_board_backlight_get(void)
{
  return s_backlight;
}

void lnd_board_backlight_set_pct(int pct)
{
  if (pct < 0)
  {
    pct = 0;
  }
  else if (pct > 100)
  {
    pct = 100;
  }
  lnd_board_backlight_set(LND_BACKLIGHT_MIN + (LND_BACKLIGHT_MAX - LND_BACKLIGHT_MIN) * pct / 100);
}

int lnd_board_backlight_get_pct(void)
{
  return (s_backlight - LND_BACKLIGHT_MIN) * 100 / (LND_BACKLIGHT_MAX - LND_BACKLIGHT_MIN);
}

// ---------------------------------------------------------------------------
// microSD
// ---------------------------------------------------------------------------
#define SD_HOST SPI2_HOST

static sdmmc_card_t *s_sd_card = NULL;
static char s_sd_path[16];

#if defined(WAVESHARE_S3_LCD7) || defined(WAVESHARE_S3_LCD5)
static void sd_select(bool on)
{
  lnd_ch422g_set_io(LND_CH422G_IO_SD_CS, !on);  // low = selected
}
#else
static void sd_select(bool on)
{
  (void)on;  // the CS is driven by the SPI driver
}
#endif

esp_err_t lnd_board_sd_mount(const char *mount_path)
{
  if (s_sd_card != NULL)
  {
    return ESP_OK;
  }
  if (strlen(mount_path) >= sizeof(s_sd_path))
  {
    return ESP_ERR_INVALID_ARG;
  }
  // Waveshare: the CS is on the expander, low for the whole time the card is mounted (the SPI driver
  // is told there is no CS pin); before that it stays high and the card is not selected.
  sd_select(true);

  const spi_bus_config_t bus = {
      .mosi_io_num = LND_SD_PIN_MOSI,
      .miso_io_num = LND_SD_PIN_MISO,
      .sclk_io_num = LND_SD_PIN_SCK,
      .quadwp_io_num = -1,
      .quadhd_io_num = -1,
      .max_transfer_sz = 4096,
  };
  esp_err_t err = spi_bus_initialize(SD_HOST, &bus, SPI_DMA_CH_AUTO);
  if (err != ESP_OK)
  {
    sd_select(false);
    return err;
  }

  sdmmc_host_t host = SDSPI_HOST_DEFAULT();
  host.slot = SD_HOST;
  host.max_freq_khz = LND_SD_FREQ_KHZ;
  sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
#ifdef LND_SD_PIN_CS
  slot.gpio_cs = LND_SD_PIN_CS;
#else
  slot.gpio_cs = GPIO_NUM_NC;
#endif
  slot.host_id = SD_HOST;
  const esp_vfs_fat_mount_config_t mount = {
      .format_if_mount_failed = false,  // somebody else's card is not formatted
      .max_files = 3,
      .allocation_unit_size = 16 * 1024,
  };
  err = esp_vfs_fat_sdspi_mount(mount_path, &host, &slot, &mount, &s_sd_card);
  if (err != ESP_OK)
  {
    s_sd_card = NULL;
    spi_bus_free(SD_HOST);
    sd_select(false);
    return err;
  }
  strlcpy(s_sd_path, mount_path, sizeof(s_sd_path));
  ESP_LOGI(TAG, "SD mounted: %s, %llu MB", s_sd_card->cid.name,
           (unsigned long long)s_sd_card->csd.capacity * s_sd_card->csd.sector_size / (1024 * 1024));
  return ESP_OK;
}

void lnd_board_sd_unmount(void)
{
  if (s_sd_card == NULL)
  {
    return;
  }
  esp_vfs_fat_sdcard_unmount(s_sd_path, s_sd_card);
  s_sd_card = NULL;
  spi_bus_free(SD_HOST);
  sd_select(false);
  ESP_LOGI(TAG, "SD unmounted");
}

bool lnd_board_sd_present(void)
{
  return s_sd_card != NULL && sdmmc_get_status(s_sd_card) == ESP_OK;
}

esp_err_t lnd_board_sd_space(uint64_t *total_kb, uint64_t *free_kb)
{
  if (s_sd_card == NULL)
  {
    return ESP_ERR_INVALID_STATE;
  }
  uint64_t total = 0, free_b = 0;
  ESP_RETURN_ON_ERROR(esp_vfs_fat_info(s_sd_path, &total, &free_b), TAG, "fat info");
  *total_kb = total / 1024;
  *free_kb = free_b / 1024;
  return ESP_OK;
}

static int s_btn_pin = LND_BTN_BOOT_PIN;

bool lnd_board_btn_pressed(void)
{
  return gpio_get_level(s_btn_pin) == 0;
}

int lnd_board_btn_get_pin(void)
{
  return s_btn_pin;
}

bool lnd_board_pin_reserved(int gpio)
{
  if (gpio == LND_BTN_BOOT_PIN)
  {
    return false;  // the default, allowed everywhere
  }
  if (gpio < 0 || gpio > 48 || (gpio >= 22 && gpio <= 25))
  {
    return true;  // there is no such GPIO
  }
  if (gpio >= 26 && gpio <= 37)
  {
    return true;  // flash and octal PSRAM
  }
  if (gpio == 19 || gpio == 20)
  {
    return true;  // USB (the console of the boards with native USB)
  }
  static const int used[] = {
      LND_LCD_PIN_BACKLIGHT, LND_CAN_RX_PIN, LND_CAN_TX_PIN, LND_LCD_PIN_HENABLE, LND_LCD_PIN_VSYNC, LND_LCD_PIN_HSYNC,
      LND_LCD_PIN_PCLK, LND_LCD_PIN_DATA_R0, LND_LCD_PIN_DATA_R1, LND_LCD_PIN_DATA_R2, LND_LCD_PIN_DATA_R3,
      LND_LCD_PIN_DATA_R4, LND_LCD_PIN_DATA_G0, LND_LCD_PIN_DATA_G1, LND_LCD_PIN_DATA_G2, LND_LCD_PIN_DATA_G3,
      LND_LCD_PIN_DATA_G4, LND_LCD_PIN_DATA_G5, LND_LCD_PIN_DATA_B0, LND_LCD_PIN_DATA_B1, LND_LCD_PIN_DATA_B2,
      LND_LCD_PIN_DATA_B3, LND_LCD_PIN_DATA_B4, LND_TOUCH_PIN_INT, LND_TOUCH_PIN_RST, LND_TOUCH_PIN_SDA,
      LND_TOUCH_PIN_SCL, LND_SD_PIN_MOSI, LND_SD_PIN_SCK, LND_SD_PIN_MISO,
#ifdef LND_SD_PIN_CS
      LND_SD_PIN_CS,
#endif
#if defined(WAVESHARE_S3_LCD7) || defined(WAVESHARE_S3_LCD5)
      4,  // GT911 INT, driven at start-up to pick the I2C address
#endif
  };
  for (size_t i = 0; i < sizeof(used) / sizeof(used[0]); i++)
  {
    if (used[i] == gpio)
    {
      return true;
    }
  }
  return false;
}

bool lnd_board_btn_set_pin(int gpio)
{
  if (lnd_board_pin_reserved(gpio))
  {
    return false;
  }
  gpio_config_t btn = {
      .pin_bit_mask = 1ULL << gpio,
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_ENABLE,
  };
  if (gpio_config(&btn) != ESP_OK)
  {
    return false;
  }
  s_btn_pin = gpio;
  return true;
}
