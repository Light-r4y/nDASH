#include "ch422g.h"

#include <esp_log.h>

static const char *TAG = "ch422g";

// CH422G exposes each internal register as its own 7-bit I2C address
// (no separate register-index byte).
#define CH422G_ADDR_WR_SET 0x24 // control register: bit0 = IO output-enable
#define CH422G_ADDR_WR_IO  0x38 // output data for the 8 push-pull IO pins

#define CH422G_XFER_TIMEOUT_MS 100

static i2c_master_dev_handle_t s_dev_set = NULL;
static i2c_master_dev_handle_t s_dev_io = NULL;
static uint8_t s_io_shadow = 0x00;

static esp_err_t add_device(i2c_master_bus_handle_t bus, uint16_t addr, i2c_master_dev_handle_t *out)
{
  i2c_device_config_t dev_cfg = {
      .dev_addr_length = I2C_ADDR_BIT_LEN_7,
      .device_address = addr,
      .scl_speed_hz = 400000,
  };
  return i2c_master_bus_add_device(bus, &dev_cfg, out);
}

esp_err_t lnd_ch422g_init(i2c_master_bus_handle_t bus)
{
  esp_err_t err = add_device(bus, CH422G_ADDR_WR_SET, &s_dev_set);
  if (err != ESP_OK)
  {
    ESP_LOGE(TAG, "failed to add WR_SET device: %d", err);
    return err;
  }

  err = add_device(bus, CH422G_ADDR_WR_IO, &s_dev_io);
  if (err != ESP_OK)
  {
    ESP_LOGE(TAG, "failed to add WR_IO device: %d", err);
    return err;
  }

  return ESP_OK;
}

esp_err_t lnd_ch422g_enable_all_outputs(void)
{
  uint8_t enable_output = 0x01;
  return i2c_master_transmit(s_dev_set, &enable_output, 1, CH422G_XFER_TIMEOUT_MS);
}

esp_err_t lnd_ch422g_set_io(uint8_t mask, bool level)
{
  if (level)
  {
    s_io_shadow |= mask;
  }
  else
  {
    s_io_shadow &= (uint8_t)~mask;
  }
  return i2c_master_transmit(s_dev_io, &s_io_shadow, 1, CH422G_XFER_TIMEOUT_MS);
}
