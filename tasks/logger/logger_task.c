#include "logger_task.h"
#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>
#include "board.h"
#include "conf.h"
#include "layout.h"
#include "logger_logic.h"
#include "mlg.h"
#include "settings.h"
#include "signals.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "logger";

static volatile bool s_req_start, s_req_stop, s_req_rescan, s_card_lost;
static volatile bool s_mounted;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static lnd_log_status_t s_status;

// The recording: the file, the fields and the buffer.
static int s_fd = -1;
static int s_sig_id[LND_LAYOUT_MAX_SIGNALS];
static int s_nfields;
static uint8_t *s_buf;
static size_t s_buf_len;
static size_t s_buf_cap;
static uint8_t s_counter;
static int64_t s_t0_us;
static uint32_t s_layout_gen;
static int s_next_num = 1;

static inline uint32_t now_ms(void)
{
  return (uint32_t)(esp_timer_get_time() / 1000);
}

void lnd_logger_request_start(void) { s_req_start = true; }
void lnd_logger_request_stop(void) { s_req_stop = true; }
void lnd_logger_request_rescan(void) { s_req_rescan = true; }
void lnd_logger_card_lost(void) { s_card_lost = true; }

const char *lnd_logger_root(void)
{
  return s_mounted ? LND_LOG_MOUNT_PATH : NULL;
}

void lnd_logger_get_status(lnd_log_status_t *out)
{
  portENTER_CRITICAL(&s_lock);
  *out = s_status;
  portEXIT_CRITICAL(&s_lock);
}

static void publish(lnd_log_state_t state, lnd_log_error_t error, bool set_error)
{
  portENTER_CRITICAL(&s_lock);
  s_status.state = state;
  if (set_error)
  {
    s_status.error = error;
  }
  if (state == LND_LOG_NO_CARD)
  {
    s_status.total_kb = 0;
    s_status.free_kb = 0;
  }
  portEXIT_CRITICAL(&s_lock);
}

// "log_0007.mlg" (in any case) -> 7, else -1.
static int file_number(const char *name)
{
  const size_t pl = strlen(LND_LOG_FILE_PREFIX), el = strlen(LND_LOG_FILE_EXT);
  if (strlen(name) != pl + 4 + el || strncasecmp(name, LND_LOG_FILE_PREFIX, pl) != 0 ||
      strcasecmp(name + pl + 4, LND_LOG_FILE_EXT) != 0)
  {
    return -1;
  }
  int n = 0;
  for (int i = 0; i < 4; i++)
  {
    const char c = name[pl + i];
    if (c < '0' || c > '9')
    {
      return -1;
    }
    n = n * 10 + (c - '0');
  }
  return n;
}

bool lnd_logger_valid_name(const char *name)
{
  return file_number(name) >= 0;
}

static void scan_next_number(void)
{
  int max = 0;
  DIR *d = opendir(LND_LOG_MOUNT_PATH);
  if (d != NULL)
  {
    struct dirent *e;
    while ((e = readdir(d)) != NULL)
    {
      const int n = file_number(e->d_name);
      if (n > max)
      {
        max = n;
      }
    }
    closedir(d);
  }
  s_next_num = max + 1;
}

static void refresh_space(void)
{
  uint64_t total = 0, free_kb = 0;
  if (lnd_board_sd_space(&total, &free_kb) == ESP_OK)
  {
    portENTER_CRITICAL(&s_lock);
    s_status.total_kb = total;
    s_status.free_kb = free_kb;
    portEXIT_CRITICAL(&s_lock);
  }
}

static void card_unmount(void)
{
  s_mounted = false;
  lnd_board_sd_unmount();
  publish(LND_LOG_NO_CARD, LND_LOG_ERR_NONE, false);
}

static bool card_mount(void)
{
  const esp_err_t err = lnd_board_sd_mount(LND_LOG_MOUNT_PATH);
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "No SD card: %s", esp_err_to_name(err));
    publish(LND_LOG_NO_CARD, LND_LOG_ERR_MOUNT, true);
    return false;
  }
  scan_next_number();
  s_mounted = true;
  publish(LND_LOG_IDLE, LND_LOG_ERR_NONE, true);
  refresh_space();
  return true;
}

// ASCII only: MegaLogViewer shows names and units as one-byte strings, UTF-8 ("°C") would turn into
// garbage. Units without a letter or a digit left ("км/ч" -> "/") are made empty.
static void ascii_copy(char *dst, size_t size, const char *src, bool units)
{
  size_t n = 0;
  bool alnum = false;
  for (; *src && n + 1 < size; src++)
  {
    const unsigned char c = (unsigned char)*src;
    if (c < 0x20 || c >= 0x7F)
    {
      continue;
    }
    dst[n++] = (char)c;
    alnum = alnum || isalnum(c);
  }
  dst[n] = '\0';
  if (units && !alnum)
  {
    dst[0] = '\0';
  }
}

static void file_close(void)
{
  if (s_fd >= 0)
  {
    if (s_buf_len > 0)
    {
      write(s_fd, s_buf, s_buf_len);
    }
    fsync(s_fd);
    close(s_fd);
    s_fd = -1;
  }
  s_buf_len = 0;
}

// Creates the next file and writes the header. Returns the error code.
static lnd_log_error_t file_open(void)
{
  lnd_layout_t *layout = heap_caps_malloc(sizeof(*layout), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (layout == NULL)
  {
    return LND_LOG_ERR_OPEN;
  }
  s_layout_gen = lnd_layout_generation();
  lnd_layout_copy(layout);

  mlg_field_t *fields = calloc(LND_LAYOUT_MAX_SIGNALS, sizeof(*fields));
  int n = 0;
  for (int i = 0; fields != NULL && i < layout->signal_count && n < LND_LAYOUT_MAX_SIGNALS; i++)
  {
    const lnd_signal_def_t *s = &layout->signals[i];
    const int id = s->name[0] != '\0' ? lnd_signal_find(s->name) : -1;
    if (id < 0)
    {
      continue;
    }
    s_sig_id[n] = id;
    ascii_copy(fields[n].name, sizeof(fields[n].name), s->name, false);
    ascii_copy(fields[n].units, sizeof(fields[n].units), s->unit, true);
    fields[n].digits = (int8_t)(s->decimals > 6 ? 6 : s->decimals);
    n++;
  }
  free(layout);
  if (n == 0)
  {
    free(fields);
    return LND_LOG_ERR_NO_SIGNALS;
  }

  char info[48];
  const esp_app_desc_t *app = esp_app_get_description();
  snprintf(info, sizeof(info), "nDash %s", app != NULL ? app->version : "");
  const time_t now = time(NULL);
  const size_t hsize = mlg_header_size(n, info);
  uint8_t *hdr = malloc(hsize);
  if (hdr == NULL)
  {
    free(fields);
    return LND_LOG_ERR_OPEN;
  }
  mlg_build_header(hdr, hsize, fields, n, info, now > 1700000000 ? (uint32_t)now : 0);
  free(fields);

  char path[32];
  snprintf(path, sizeof(path), LND_LOG_MOUNT_PATH "/" LND_LOG_FILE_PREFIX "%04d" LND_LOG_FILE_EXT, s_next_num);
  const int fd = s_next_num <= 9999 ? open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666) : -1;
  if (fd < 0 || write(fd, hdr, hsize) != (ssize_t)hsize)
  {
    ESP_LOGE(TAG, "Cannot create %s", path);
    if (fd >= 0)
    {
      close(fd);
      unlink(path);
    }
    free(hdr);
    return LND_LOG_ERR_OPEN;
  }
  free(hdr);

  s_fd = fd;
  s_nfields = n;
  s_counter = 0;
  s_buf_len = 0;
  s_t0_us = esp_timer_get_time();
  portENTER_CRITICAL(&s_lock);
  snprintf(s_status.file, sizeof(s_status.file), LND_LOG_FILE_PREFIX "%04d" LND_LOG_FILE_EXT, s_next_num);
  s_status.records = 0;
  s_status.bytes = (uint32_t)hsize;
  s_status.seconds = 0;
  portEXIT_CRITICAL(&s_lock);
  s_next_num++;
  ESP_LOGI(TAG, "Recording to %s (%d fields)", path, n);
  return LND_LOG_ERR_NONE;
}

// One record into the buffer; true - the write to the card went well (or was not needed).
static bool sample(uint32_t *last_flush_ms, uint32_t *last_sync_ms, uint32_t now)
{
  float v[LND_LAYOUT_MAX_SIGNALS];
  for (int i = 0; i < s_nfields; i++)
  {
    float x = 0.0f;
    if (!lnd_signal_get(s_sig_id[i], &x) || !isfinite(x))
    {
      x = 0.0f;  // no data: the MLG format has no "empty", a zero is written
    }
    v[i] = x;
  }
  const size_t rs = mlg_record_size(s_nfields);
  if (s_buf_len + rs > s_buf_cap)
  {
    if (write(s_fd, s_buf, s_buf_len) != (ssize_t)s_buf_len)
    {
      return false;
    }
    s_buf_len = 0;
    *last_flush_ms = now;
  }
  s_buf_len += mlg_build_record(s_buf + s_buf_len, s_buf_cap - s_buf_len, s_counter++,
                                (uint64_t)(esp_timer_get_time() - s_t0_us), v, s_nfields);
  portENTER_CRITICAL(&s_lock);
  s_status.records++;
  s_status.bytes += (uint32_t)rs;
  s_status.seconds = (uint32_t)((esp_timer_get_time() - s_t0_us) / 1000000);
  portEXIT_CRITICAL(&s_lock);

  if ((uint32_t)(now - *last_flush_ms) >= LND_LOGGER_FLUSH_MS)
  {
    if (s_buf_len > 0 && write(s_fd, s_buf, s_buf_len) != (ssize_t)s_buf_len)
    {
      return false;
    }
    s_buf_len = 0;
    *last_flush_ms = now;
  }
  if ((uint32_t)(now - *last_sync_ms) >= LND_LOGGER_SYNC_MS)
  {
    if (fsync(s_fd) != 0)
    {
      return false;
    }
    *last_sync_ms = now;
  }
  return true;
}

static void logger_task(void *arg)
{
  (void)arg;
  s_buf_cap = LND_LOGGER_BUF_BYTES;
  s_buf = malloc(s_buf_cap);
  configASSERT(s_buf != NULL);

  vTaskDelay(pdMS_TO_TICKS(LND_LOGGER_MOUNT_DELAY_MS));

  lnd_trig_t trig;
  memset(&trig, 0, sizeof(trig));
  uint32_t next_retry = 0, next_check = 0, next_space = 0, last_flush = 0, last_sync = 0;
  int cond_id = -1;
  char cond_name[LND_LOG_SIGNAL_MAX_LEN + 1] = "";

  TickType_t last = xTaskGetTickCount();
  for (;;)
  {
    if (xTaskDelayUntil(&last, pdMS_TO_TICKS(LND_LOGGER_PERIOD_MS)) == pdFALSE)
    {
      last = xTaskGetTickCount();  // the card stalled the previous pass: no catching up with a burst of equal records
    }
    const uint32_t now = now_ms();

    lnd_log_settings_t cfg;
    lnd_settings_get_log(&cfg);

    const bool rescan = s_req_rescan;
    s_req_rescan = false;
    const bool want_start = s_req_start;
    s_req_start = false;
    const bool want_stop = s_req_stop;
    s_req_stop = false;
    const bool lost = s_card_lost;
    s_card_lost = false;

    // The card: mounted on request and (in the "boot" / "condition" modes) every now and then.
    if (lost && s_mounted && s_fd < 0)
    {
      card_unmount();
    }
    if (!s_mounted)
    {
      const bool auto_retry = cfg.mode != LND_LOG_MANUAL && (int32_t)(now - next_retry) >= 0;
      if (rescan || auto_retry || want_start)
      {
        next_retry = now + LND_LOGGER_RETRY_MS;
        card_mount();
      }
    }
    else if (s_fd < 0 && (int32_t)(now - next_check) >= 0)
    {
      next_check = now + 2000;
      if (!lnd_board_sd_present())
      {
        ESP_LOGW(TAG, "SD card removed");
        card_unmount();
      }
      else if ((int32_t)(now - next_space) >= 0)
      {
        next_space = now + 10000;
        refresh_space();
      }
    }

    // The signal of the condition.
    bool sig_valid = false;
    float sig = 0.0f;
    if (cfg.mode == LND_LOG_CONDITION && cfg.signal[0] != '\0')
    {
      if (cond_id < 0 || strcmp(cond_name, cfg.signal) != 0)
      {
        strlcpy(cond_name, cfg.signal, sizeof(cond_name));
        cond_id = lnd_signal_find(cond_name);
      }
      sig_valid = cond_id >= 0 && lnd_signal_get(cond_id, &sig);
    }

    // The layout changed: the fields of the file are out of date, a new file begins.
    const bool restart = s_fd >= 0 && lnd_layout_generation() != s_layout_gen;

    const lnd_trig_cfg_t tc = {(int)cfg.mode, cfg.less, cfg.value, cfg.stop_delay};
    lnd_trig_act_t act = lnd_trig_step(&trig, &tc, s_mounted, sig_valid, sig, want_start, want_stop, now);

    if (act == LND_TRIG_STOP)
    {
      file_close();
      ESP_LOGI(TAG, "Recording stopped");
      if (s_mounted)
      {
        publish(LND_LOG_IDLE, LND_LOG_ERR_NONE, false);
        refresh_space();
      }
      else
      {
        publish(LND_LOG_NO_CARD, LND_LOG_ERR_WRITE, true);
      }
    }
    else if (restart && trig.rec)
    {
      file_close();
      act = LND_TRIG_START;
    }

    if (act == LND_TRIG_START)
    {
      const lnd_log_error_t err = s_mounted ? file_open() : LND_LOG_ERR_MOUNT;
      if (err != LND_LOG_ERR_NONE)
      {
        lnd_trig_forced_stop(&trig);
        publish(s_mounted ? LND_LOG_IDLE : LND_LOG_NO_CARD, err, true);
      }
      else
      {
        last_flush = last_sync = now;
        next_space = now + 10000;
        publish(LND_LOG_RECORDING, LND_LOG_ERR_NONE, true);
      }
    }

    if (s_fd >= 0)
    {
      lnd_log_error_t err = LND_LOG_ERR_NONE;
      if (!sample(&last_flush, &last_sync, now))
      {
        err = LND_LOG_ERR_WRITE;
      }
      else if ((int32_t)(now - next_space) >= 0)
      {
        next_space = now + 10000;
        refresh_space();
        lnd_log_status_t st;
        lnd_logger_get_status(&st);
        if (st.total_kb > 0 && st.free_kb < LND_LOGGER_MIN_FREE_KB)
        {
          err = LND_LOG_ERR_FULL;
        }
      }
      if (err != LND_LOG_ERR_NONE)
      {
        ESP_LOGE(TAG, "Recording failed (%d)", (int)err);
        s_buf_len = 0;  // what was not written is lost
        file_close();
        lnd_trig_forced_stop(&trig);
        if (err == LND_LOG_ERR_WRITE)
        {
          card_unmount();  // try to mount again by the timer / on request
          next_retry = now + LND_LOGGER_RETRY_MS;
          publish(LND_LOG_NO_CARD, err, true);
        }
        else
        {
          publish(LND_LOG_IDLE, err, true);
        }
      }
    }
  }
}

void lnd_logger_task_start(void)
{
  const BaseType_t ok = xTaskCreatePinnedToCore(logger_task, "logger_task", LND_LOGGER_TASK_STACK, NULL,
                                                LND_LOGGER_TASK_PRIORITY, NULL, LND_LOGGER_TASK_CORE);
  configASSERT(ok == pdPASS);
  ESP_LOGI(TAG, "Logger task started");
}
