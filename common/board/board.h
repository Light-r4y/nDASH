#pragma once

// Board support: pins, panel timings and the hardware bring-up. The target board
// is chosen at build time, not by editing this file:
//
//     idf.py -D NDASH_BOARD=WAVESHARE_S3_LCD5 build
//
// CMake (top-level CMakeLists.txt) then defines exactly one of the macros below,
// and the per-board tables switch on it. GPIO numbers appear only in this file.
//
//   JC8048W550C        JC8048W550C 5inch C-touch
//   WAVESHARE_S3_LCD7  Waveshare ESP32-S3-Touch-LCD-7
//   WAVESHARE_S3_LCD5  Waveshare ESP32-S3-Touch-LCD-5 (800x480)
//   Sunton_S3_LCD7     Sunton ESP32-S3 LCD7

#include <driver/gpio.h>
#include <stdbool.h>
#include <esp_lcd_types.h>
#include <esp_lcd_touch.h>

#if (defined(JC8048W550C) + defined(WAVESHARE_S3_LCD7) + defined(WAVESHARE_S3_LCD5) + defined(Sunton_S3_LCD7)) != 1
#error "Exactly one board must be selected: idf.py -D NDASH_BOARD=<JC8048W550C|WAVESHARE_S3_LCD7|WAVESHARE_S3_LCD5|Sunton_S3_LCD7> build"
#endif

#include <esp_lcd_types.h>
#include <esp_lcd_touch.h>

#ifdef JC8048W550C

#define LND_LCD_PIN_BACKLIGHT   GPIO_NUM_2

// CAN transceiver pins (TWAI)
#define LND_CAN_RX_PIN      GPIO_NUM_18
#define LND_CAN_TX_PIN      GPIO_NUM_17

// resolution
#define LND_LCD_WIDTH   800U
#define LND_LCD_HEIGHT   480U

#define LND_LCD_PIN_HENABLE     GPIO_NUM_40
#define LND_LCD_PIN_VSYNC       GPIO_NUM_41
#define LND_LCD_PIN_HSYNC       GPIO_NUM_39
#define LND_LCD_PIN_PCLK        GPIO_NUM_42

// RGB565 data pins
#define LND_LCD_PIN_DATA_R0     GPIO_NUM_45
#define LND_LCD_PIN_DATA_R1     GPIO_NUM_48
#define LND_LCD_PIN_DATA_R2     GPIO_NUM_47
#define LND_LCD_PIN_DATA_R3     GPIO_NUM_21
#define LND_LCD_PIN_DATA_R4     GPIO_NUM_14

#define LND_LCD_PIN_DATA_G0     GPIO_NUM_5
#define LND_LCD_PIN_DATA_G1     GPIO_NUM_6
#define LND_LCD_PIN_DATA_G2     GPIO_NUM_7
#define LND_LCD_PIN_DATA_G3     GPIO_NUM_15
#define LND_LCD_PIN_DATA_G4     GPIO_NUM_16
#define LND_LCD_PIN_DATA_G5     GPIO_NUM_4

#define LND_LCD_PIN_DATA_B0     GPIO_NUM_8
#define LND_LCD_PIN_DATA_B1     GPIO_NUM_3
#define LND_LCD_PIN_DATA_B2     GPIO_NUM_46
#define LND_LCD_PIN_DATA_B3     GPIO_NUM_9
#define LND_LCD_PIN_DATA_B4     GPIO_NUM_1

// timing and polarity
#define LND_LCD_FREQ                16000000U
#define LND_LCD_PCLK_ACTIVE_NEG     1
#define LND_LCD_DE_IDLE_HIGH        1
#define LND_LCD_PCLK_IDLE_HIGH      1

#define LND_LCD_HSYNC_POLARITY      0
#define LND_LCD_HSYNC_FRONT_PORCH   8
#define LND_LCD_HSYNC_PULSE_WIDTH   4
#define LND_LCD_HSYNC_BACK_PORCH    8

#define LND_LCD_VSYNC_POLARITY      0
#define LND_LCD_VSYNC_FRONT_PORCH   8
#define LND_LCD_VSYNC_PULSE_WIDTH   4
#define LND_LCD_VSYNC_BACK_PORCH    8

// touch
#define LND_TOUCH_XMAX  LND_LCD_WIDTH - 1
#define LND_TOUCH_YMAX  LND_LCD_HEIGHT - 1

#define LND_TOUCH_PIN_INT   GPIO_NUM_NC
#define LND_TOUCH_PIN_RST   GPIO_NUM_38
#define LND_TOUCH_PIN_SDA   GPIO_NUM_19
#define LND_TOUCH_PIN_SCL   GPIO_NUM_20

#define LND_TOUCH_FREQ      400000U
#define LND_TOUCH_ROTATION  0

// JC8048W550C end

#elif defined(WAVESHARE_S3_LCD7)

#define LND_LCD_PIN_BACKLIGHT   GPIO_NUM_6

// CAN transceiver pins (TWAI)
#define LND_CAN_RX_PIN      GPIO_NUM_19
#define LND_CAN_TX_PIN      GPIO_NUM_20

// resolution
#define LND_LCD_WIDTH   800U
#define LND_LCD_HEIGHT   480U

#define LND_LCD_PIN_HENABLE     GPIO_NUM_5
#define LND_LCD_PIN_VSYNC       GPIO_NUM_3
#define LND_LCD_PIN_HSYNC       GPIO_NUM_46
#define LND_LCD_PIN_PCLK        GPIO_NUM_7

// RGB565 data pins
#define LND_LCD_PIN_DATA_R0     GPIO_NUM_1
#define LND_LCD_PIN_DATA_R1     GPIO_NUM_2
#define LND_LCD_PIN_DATA_R2     GPIO_NUM_42
#define LND_LCD_PIN_DATA_R3     GPIO_NUM_41
#define LND_LCD_PIN_DATA_R4     GPIO_NUM_40

#define LND_LCD_PIN_DATA_G0     GPIO_NUM_39
#define LND_LCD_PIN_DATA_G1     GPIO_NUM_0
#define LND_LCD_PIN_DATA_G2     GPIO_NUM_45
#define LND_LCD_PIN_DATA_G3     GPIO_NUM_48
#define LND_LCD_PIN_DATA_G4     GPIO_NUM_47
#define LND_LCD_PIN_DATA_G5     GPIO_NUM_21

#define LND_LCD_PIN_DATA_B0     GPIO_NUM_14
#define LND_LCD_PIN_DATA_B1     GPIO_NUM_38
#define LND_LCD_PIN_DATA_B2     GPIO_NUM_18
#define LND_LCD_PIN_DATA_B3     GPIO_NUM_17
#define LND_LCD_PIN_DATA_B4     GPIO_NUM_10

// timing and polarity
#define LND_LCD_FREQ                12000000U
#define LND_LCD_PCLK_ACTIVE_NEG     1
#define LND_LCD_DE_IDLE_HIGH        1
#define LND_LCD_PCLK_IDLE_HIGH      1

#define LND_LCD_HSYNC_POLARITY      0
#define LND_LCD_HSYNC_FRONT_PORCH   8
#define LND_LCD_HSYNC_PULSE_WIDTH   4
#define LND_LCD_HSYNC_BACK_PORCH    8

#define LND_LCD_VSYNC_POLARITY      0
#define LND_LCD_VSYNC_FRONT_PORCH   8
#define LND_LCD_VSYNC_PULSE_WIDTH   4
#define LND_LCD_VSYNC_BACK_PORCH    8

// touch
#define LND_TOUCH_XMAX  LND_LCD_WIDTH - 1
#define LND_TOUCH_YMAX  LND_LCD_HEIGHT - 1

#define LND_TOUCH_PIN_INT   GPIO_NUM_NC
#define LND_TOUCH_PIN_RST   GPIO_NUM_NC
#define LND_TOUCH_PIN_SDA   GPIO_NUM_8
#define LND_TOUCH_PIN_SCL   GPIO_NUM_9

#define LND_TOUCH_FREQ      400000U
#define LND_TOUCH_ROTATION  0

// WAVESHARE_S3_LCD7 end

#elif defined(WAVESHARE_S3_LCD5)

#define LND_LCD_PIN_BACKLIGHT   GPIO_NUM_6

// CAN transceiver pins (TWAI)
#define LND_CAN_RX_PIN      GPIO_NUM_16
#define LND_CAN_TX_PIN      GPIO_NUM_15

// resolution
#define LND_LCD_WIDTH   800U
#define LND_LCD_HEIGHT   480U

#define LND_LCD_PIN_HENABLE     GPIO_NUM_5
#define LND_LCD_PIN_VSYNC       GPIO_NUM_3
#define LND_LCD_PIN_HSYNC       GPIO_NUM_46
#define LND_LCD_PIN_PCLK        GPIO_NUM_7

// RGB565 data pins
#define LND_LCD_PIN_DATA_R0     GPIO_NUM_1
#define LND_LCD_PIN_DATA_R1     GPIO_NUM_2
#define LND_LCD_PIN_DATA_R2     GPIO_NUM_42
#define LND_LCD_PIN_DATA_R3     GPIO_NUM_41
#define LND_LCD_PIN_DATA_R4     GPIO_NUM_40

#define LND_LCD_PIN_DATA_G0     GPIO_NUM_39
#define LND_LCD_PIN_DATA_G1     GPIO_NUM_0
#define LND_LCD_PIN_DATA_G2     GPIO_NUM_45
#define LND_LCD_PIN_DATA_G3     GPIO_NUM_48
#define LND_LCD_PIN_DATA_G4     GPIO_NUM_47
#define LND_LCD_PIN_DATA_G5     GPIO_NUM_21

#define LND_LCD_PIN_DATA_B0     GPIO_NUM_14
#define LND_LCD_PIN_DATA_B1     GPIO_NUM_38
#define LND_LCD_PIN_DATA_B2     GPIO_NUM_18
#define LND_LCD_PIN_DATA_B3     GPIO_NUM_17
#define LND_LCD_PIN_DATA_B4     GPIO_NUM_10

// timing and polarity
#define LND_LCD_FREQ                16000000U
#define LND_LCD_PCLK_ACTIVE_NEG     1
#define LND_LCD_DE_IDLE_HIGH        1
#define LND_LCD_PCLK_IDLE_HIGH      1

#define LND_LCD_HSYNC_POLARITY      0
#define LND_LCD_HSYNC_FRONT_PORCH   8
#define LND_LCD_HSYNC_PULSE_WIDTH   4
#define LND_LCD_HSYNC_BACK_PORCH    8

#define LND_LCD_VSYNC_POLARITY      0
#define LND_LCD_VSYNC_FRONT_PORCH   12
#define LND_LCD_VSYNC_PULSE_WIDTH   4
#define LND_LCD_VSYNC_BACK_PORCH    8

// touch
#define LND_TOUCH_XMAX  LND_LCD_WIDTH - 1
#define LND_TOUCH_YMAX  LND_LCD_HEIGHT - 1

#define LND_TOUCH_PIN_INT   GPIO_NUM_NC
#define LND_TOUCH_PIN_RST   GPIO_NUM_NC
#define LND_TOUCH_PIN_SDA   GPIO_NUM_8
#define LND_TOUCH_PIN_SCL   GPIO_NUM_9

#define LND_TOUCH_FREQ      400000U
#define LND_TOUCH_ROTATION  0

// WAVESHARE_S3_LCD5 end

#elif defined(Sunton_S3_LCD7)

#define LND_LCD_PIN_BACKLIGHT   GPIO_NUM_2

// CAN transceiver pins (TWAI)
#define LND_CAN_RX_PIN      GPIO_NUM_17
#define LND_CAN_TX_PIN      GPIO_NUM_18

// resolution
#define LND_LCD_WIDTH   800U
#define LND_LCD_HEIGHT   480U

#define LND_LCD_PIN_HENABLE     GPIO_NUM_41
#define LND_LCD_PIN_VSYNC       GPIO_NUM_40
#define LND_LCD_PIN_HSYNC       GPIO_NUM_39
#define LND_LCD_PIN_PCLK        GPIO_NUM_42

// RGB565 data pins
#define LND_LCD_PIN_DATA_R0     GPIO_NUM_14
#define LND_LCD_PIN_DATA_R1     GPIO_NUM_21
#define LND_LCD_PIN_DATA_R2     GPIO_NUM_47
#define LND_LCD_PIN_DATA_R3     GPIO_NUM_48
#define LND_LCD_PIN_DATA_R4     GPIO_NUM_45

#define LND_LCD_PIN_DATA_G0     GPIO_NUM_9
#define LND_LCD_PIN_DATA_G1     GPIO_NUM_46
#define LND_LCD_PIN_DATA_G2     GPIO_NUM_3
#define LND_LCD_PIN_DATA_G3     GPIO_NUM_8
#define LND_LCD_PIN_DATA_G4     GPIO_NUM_16
#define LND_LCD_PIN_DATA_G5     GPIO_NUM_1

#define LND_LCD_PIN_DATA_B0     GPIO_NUM_15
#define LND_LCD_PIN_DATA_B1     GPIO_NUM_7
#define LND_LCD_PIN_DATA_B2     GPIO_NUM_6
#define LND_LCD_PIN_DATA_B3     GPIO_NUM_5
#define LND_LCD_PIN_DATA_B4     GPIO_NUM_4

// timing and polarity
#define LND_LCD_FREQ                14000000U
#define LND_LCD_PCLK_ACTIVE_NEG     1
#define LND_LCD_DE_IDLE_HIGH        0
#define LND_LCD_PCLK_IDLE_HIGH      0

#define LND_LCD_HSYNC_POLARITY      0
#define LND_LCD_HSYNC_FRONT_PORCH   20
#define LND_LCD_HSYNC_PULSE_WIDTH   30
#define LND_LCD_HSYNC_BACK_PORCH    16

#define LND_LCD_VSYNC_POLARITY      0
#define LND_LCD_VSYNC_FRONT_PORCH   22
#define LND_LCD_VSYNC_PULSE_WIDTH   13
#define LND_LCD_VSYNC_BACK_PORCH    10

// touch
#define LND_TOUCH_XMAX  LND_LCD_WIDTH - 1
#define LND_TOUCH_YMAX  LND_LCD_HEIGHT - 1

#define LND_TOUCH_PIN_INT   GPIO_NUM_NC
#define LND_TOUCH_PIN_RST   GPIO_NUM_38
#define LND_TOUCH_PIN_SDA   GPIO_NUM_19
#define LND_TOUCH_PIN_SCL   GPIO_NUM_20

#define LND_TOUCH_FREQ      400000U
#define LND_TOUCH_ROTATION  0

// Sunton_S3_LCD7 end

#else
#error "Unknown board - build with: idf.py -D NDASH_BOARD=<board> build (see common/board/board.h)"
#endif

// BOOT button (GPIO0): on every supported board. Pulled up, closes to GND. Do not hold it
// at power-on: it is the strapping pin that selects the download mode.
#define LND_BTN_BOOT_PIN GPIO_NUM_0

// 1 where GPIO0 is also an RGB data line of the panel (the Waveshare boards: G1). The BOOT button works there, but
// while the picture is drawn the pin also shows its data: brief false "presses" are possible. Fine for the holds
// (access point, password reset), not for a short press, so the BOOT button does not step the brightness there.
#if defined(WAVESHARE_S3_LCD7) || defined(WAVESHARE_S3_LCD5)
#define LND_BTN_BOOT_SHARED_WITH_LCD 1
#else
#define LND_BTN_BOOT_SHARED_WITH_LCD 0
#endif

// Two full-screen frame buffers in PSRAM: esp_lvgl_port renders into one in
// "direct mode" while the other is on screen and swaps them at VSYNC, which is
// what makes the picture tear-free. The same buffers are LVGL's draw buffers, so
// no internal RAM is spent on them.
#define LND_LCD_NUM_FBS 2

// Number of scanlines held in the RGB peripheral's internal-SRAM bounce buffer
// (refilled from the PSRAM frame buffer). It keeps the picture stable when PSRAM
// is busy with something else.
#define LND_BOUNCE_BUFFER_LINES 16

// ---- microSD card (SPI mode) -----------------------------------------------------------------------
// The card slot of every supported board: MOSI / MISO / SCK on the GPIOs below, chip select on a GPIO
// (LND_SD_PIN_CS) or, on the Waveshare boards, on the CH422G expander (LND_CH422G_IO_SD_CS). The pins
// are not used by anything else on these boards. Not checked against the schematics of the JC8048W550C
// and Sunton boards: confirm on the first card.
#define LND_SD_PIN_MOSI GPIO_NUM_11
#define LND_SD_PIN_SCK  GPIO_NUM_12
#define LND_SD_PIN_MISO GPIO_NUM_13
#define LND_SD_FREQ_KHZ 20000
#if !defined(WAVESHARE_S3_LCD7) && !defined(WAVESHARE_S3_LCD5)
#define LND_SD_PIN_CS   GPIO_NUM_10
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Hardware bring-up: I2C bus, IO expander (Waveshare boards), backlight, RGB panel
// and GT911 touch. The panel is created but nothing is drawn: LVGL is attached to
// it by the ui task.
void lnd_board_init(void);

// Handles for esp_lvgl_port. The touch handle is NULL if the controller did not
// answer (the UI then simply has no touch).
esp_lcd_panel_handle_t lnd_board_lcd_panel(void);
esp_lcd_touch_handle_t lnd_board_touch(void);

// 1 when the backlight can be dimmed. The Waveshare 5" has only the on/off switch (EXIO2 of the CH422G),
// no brightness control: swipes, the button and the web page leave the brightness alone there.
#if defined(WAVESHARE_S3_LCD5)
#define LND_HAS_BRIGHTNESS 0
#else
#define LND_HAS_BRIGHTNESS 1
#endif

// Backlight duty, LND_BACKLIGHT_MIN..255 (values outside are clamped).
#define LND_BACKLIGHT_MIN 55
#define LND_BACKLIGHT_MAX 255
void lnd_board_backlight_set(int duty);
int lnd_board_backlight_get(void);

// Backlight in percent, 0..100 mapped onto LND_BACKLIGHT_MIN..LND_BACKLIGHT_MAX.
void lnd_board_backlight_set_pct(int pct);
int lnd_board_backlight_get_pct(void);

// The microSD card. Mounts a FAT32 card at mount_path; a card that is not FAT32 is not formatted.
// ESP_ERR_* when there is no card or it does not answer. Not for the LVGL task: it blocks on the card.
esp_err_t lnd_board_sd_mount(const char *mount_path);
void lnd_board_sd_unmount(void);
bool lnd_board_sd_present(void);                                   // the mounted card still answers
esp_err_t lnd_board_sd_space(uint64_t *total_kb, uint64_t *free_kb);

// Is the BOOT button pressed right now.
bool lnd_board_btn_pressed(void);

// The button is LND_BTN_BOOT_PIN until another pin is set (from the settings). A pin the board itself uses (panel, touch,
// CAN, SD, backlight), the chip (flash, PSRAM, USB) or that does not exist is refused; the BOOT pin is always allowed.
bool lnd_board_pin_reserved(int gpio);
bool lnd_board_btn_set_pin(int gpio);  // false: refused, the pin stays
int lnd_board_btn_get_pin(void);

#ifdef __cplusplus
} /*extern "C"*/
#endif
