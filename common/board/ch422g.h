#pragma once

// Minimal driver for the CH422G I2C IO-expander used on Waveshare
// ESP32-S3-Touch-LCD-7/5 boards to drive touch reset, LCD reset and
// backlight-enable lines.
//
// NOTE: the register model below (three synthetic I2C "addresses" instead
// of a normal device-address + register-index scheme) is reimplemented from
// esp-arduino-libs/ESP32_IO_Expander's CH422G driver behavior, not from a
// datasheet read in this session. Bench-verify against real hardware
// (logic analyzer / oscilloscope on SDA+SCL) before relying on it.

#include <driver/i2c_master.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Output bits on the WR_IO register used by this board's wiring.
#define LND_CH422G_IO_TP_RST  (1 << 1)
#define LND_CH422G_IO_LCD_BL  (1 << 2)
#define LND_CH422G_IO_LCD_RST (1 << 3)
#define LND_CH422G_IO_SD_CS   (1 << 4)  // the microSD card's chip select
#define LND_CH422G_IO_CAN_SEL (1 << 5)  // Waveshare 7" only: high = GPIO19/20 go to the CAN transceiver, low = to USB

// Attaches the CH422G's synthetic addresses as devices on an already-created
// shared I2C bus (the same bus the GT911 touch controller lives on).
esp_err_t lnd_ch422g_init(i2c_master_bus_handle_t bus);

// Puts all 8 WR_IO pins into push-pull output mode.
esp_err_t lnd_ch422g_enable_all_outputs(void);

// Sets (or clears) one or more LND_CH422G_IO_* bits on the WR_IO register,
// leaving the others at their last-written state.
esp_err_t lnd_ch422g_set_io(uint8_t mask, bool level);

#ifdef __cplusplus
} /*extern "C"*/
#endif
