#include "can_task.h"
#include "board.h"
#include "can_ids.h"
#include "can_map.h"
#include "conf.h"
#include "settings.h"
#include "signals.h"

#include <esp_log.h>
#include <esp_timer.h>
#include <esp_twai.h>
#include <esp_twai_onchip.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <string.h>

static const char *TAG = "can_task";

typedef struct {
  uint32_t id;
  bool ext;
  uint8_t length;
  uint8_t data[8];
} can_rx_item_t;

// ---------------------------------------------------------------------------
// ISR side: node, queue, ID filter
// ---------------------------------------------------------------------------
static twai_node_handle_t s_node;
static QueueHandle_t s_rx_queue;
static SemaphoreHandle_t s_node_mutex;  // guards s_node against a restart during lnd_can_send()

static volatile bool s_bus_off;
static volatile int64_t s_bus_off_ms;
static volatile uint32_t s_rx_frames, s_rx_dropped, s_bus_off_count;
static volatile bool s_tx_allowed;  // node up and in "normal" mode

// The ISR lets through only the IDs of the CAN table (so a busy bus does not wake the
// task for frames nobody wants), unless the web sniffer asked to see everything. The
// table is rebuilt in the task into the buffer the ISR is not using, then the pointer
// is switched.
typedef struct {
  uint8_t count;
  struct {
    uint32_t id;
    bool ext;
  } f[LND_CAN_MAP_MAX];
} id_filter_t;

static id_filter_t s_filters[2];
static volatile const id_filter_t *s_filter = &s_filters[0];
static volatile int64_t s_sniffer_until_ms;

static inline int64_t now_ms(void)
{
  return esp_timer_get_time() / 1000;
}

static bool filter_accepts(uint32_t id, bool ext)
{
  if (now_ms() < s_sniffer_until_ms)
  {
    return true;
  }
  const id_filter_t *f = (const id_filter_t *)s_filter;
  for (int i = 0; i < f->count; i++)
  {
    if (f->f[i].id == id && f->f[i].ext == ext)
    {
      return true;
    }
  }
  return false;
}

static bool on_rx_done(twai_node_handle_t node, const twai_rx_done_event_data_t *edata, void *user_ctx)
{
  (void)edata;
  (void)user_ctx;

  uint8_t buf[TWAI_FRAME_MAX_LEN];
  twai_frame_t frame = {};
  frame.buffer = buf;
  frame.buffer_len = sizeof(buf);
  if (twai_node_receive_from_isr(node, &frame) != ESP_OK)
  {
    return false;
  }
  if (frame.header.rtr || !filter_accepts(frame.header.id, frame.header.ide))
  {
    return false;
  }

  can_rx_item_t item = {};
  item.id = frame.header.id;
  item.ext = frame.header.ide;
  item.length = frame.header.dlc > 8 ? 8 : (uint8_t)frame.header.dlc;
  memcpy(item.data, buf, item.length);

  BaseType_t woken = pdFALSE;
  if (xQueueSendFromISR(s_rx_queue, &item, &woken) == pdTRUE)
  {
    s_rx_frames++;
  }
  else
  {
    s_rx_dropped++;
  }
  return woken == pdTRUE;
}

static bool on_state_change(twai_node_handle_t node, const twai_state_change_event_data_t *edata, void *user_ctx)
{
  (void)node;
  (void)user_ctx;
  if (edata->new_sta == TWAI_ERROR_BUS_OFF)
  {
    s_bus_off_ms = now_ms();
    s_bus_off_count++;
    s_bus_off = true;  // the task brings the node back after LND_CAN_BUSOFF_RECOVERY_MS
  }
  return false;
}

static void node_stop(void)
{
  xSemaphoreTake(s_node_mutex, portMAX_DELAY);
  s_tx_allowed = false;
  if (s_node != NULL)
  {
    twai_node_disable(s_node);
    twai_node_delete(s_node);
    s_node = NULL;
  }
  xSemaphoreGive(s_node_mutex);
  if (s_rx_queue != NULL)
  {
    vQueueDelete(s_rx_queue);
    s_rx_queue = NULL;
  }
}

static bool node_start(const lnd_can_settings_t *st)
{
  twai_onchip_node_config_t cfg = {};
  cfg.io_cfg.tx = LND_CAN_TX_PIN;
  cfg.io_cfg.rx = LND_CAN_RX_PIN;
  cfg.io_cfg.quanta_clk_out = GPIO_NUM_NC;
  cfg.io_cfg.bus_off_indicator = GPIO_NUM_NC;
  cfg.bit_timing.bitrate = st->bitrate;
  cfg.tx_queue_depth = 5;
  cfg.flags.enable_listen_only = st->mode == LND_CAN_MODE_LISTEN_ONLY;

  s_rx_queue = xQueueCreate(LND_CAN_RX_QUEUE_LEN, sizeof(can_rx_item_t));
  if (s_rx_queue == NULL)
  {
    return false;
  }

  twai_node_handle_t node = NULL;
  esp_err_t err = twai_new_node_onchip(&cfg, &node);
  if (err != ESP_OK)
  {
    ESP_LOGE(TAG, "twai_new_node_onchip failed: %d", err);
    vQueueDelete(s_rx_queue);
    s_rx_queue = NULL;
    return false;
  }

  twai_event_callbacks_t cbs = {};
  cbs.on_rx_done = on_rx_done;
  cbs.on_state_change = on_state_change;
  twai_node_register_event_callbacks(node, &cbs, NULL);

  err = twai_node_enable(node);
  if (err != ESP_OK)
  {
    ESP_LOGE(TAG, "twai_node_enable failed: %d", err);
    twai_node_delete(node);
    vQueueDelete(s_rx_queue);
    s_rx_queue = NULL;
    return false;
  }

  s_bus_off = false;
  xSemaphoreTake(s_node_mutex, portMAX_DELAY);
  s_node = node;
  s_tx_allowed = st->mode == LND_CAN_MODE_NORMAL;
  xSemaphoreGive(s_node_mutex);
  ESP_LOGI(TAG, "TWAI node up: %lu bit/s, %s", (unsigned long)st->bitrate,
           st->mode == LND_CAN_MODE_NORMAL ? "normal" : "listen only");
  return true;
}

bool lnd_can_send(uint32_t id, const uint8_t *data, uint8_t length, bool extended)
{
  if (length > TWAI_FRAME_MAX_LEN)
  {
    return false;
  }
  bool ok = false;
  xSemaphoreTake(s_node_mutex, portMAX_DELAY);
  if (s_node != NULL && s_tx_allowed)
  {
    twai_frame_t frame = {};
    frame.header.id = id;
    frame.header.dlc = length;
    frame.header.ide = extended ? 1 : 0;
    frame.buffer = (uint8_t *)data;
    frame.buffer_len = length;
    ok = twai_node_transmit(s_node, &frame, LND_CAN_POLL_MS) == ESP_OK;
  }
  xSemaphoreGive(s_node_mutex);
  return ok;
}

void lnd_can_get_status(lnd_can_status_t *out)
{
  memset(out, 0, sizeof(*out));
  out->rx_frames = s_rx_frames;
  out->rx_dropped = s_rx_dropped;
  out->bus_off_count = s_bus_off_count;
  xSemaphoreTake(s_node_mutex, portMAX_DELAY);
  if (s_node != NULL)
  {
    twai_node_status_t st;
    twai_node_record_t rec;
    if (twai_node_get_info(s_node, &st, &rec) == ESP_OK)
    {
      out->up = true;
      out->state = (uint8_t)st.state;
      out->tx_errors = st.tx_error_count;
      out->rx_errors = st.rx_error_count;
      out->bus_errors = rec.bus_err_num;
    }
  }
  out->listen_only = !s_tx_allowed;
  xSemaphoreGive(s_node_mutex);
}

// ---------------------------------------------------------------------------
// Sniffer: the last frame of each ID
// ---------------------------------------------------------------------------
typedef struct {
  uint32_t id;
  bool ext;
  uint8_t length;
  uint8_t data[8];
  uint32_t count;
  int64_t last_ms;
} sniff_slot_t;

static sniff_slot_t s_sniff[LND_CAN_SNIFFER_MAX];
static int s_sniff_count;
static SemaphoreHandle_t s_sniff_mutex;

static void sniffer_record(const can_rx_item_t *it)
{
  xSemaphoreTake(s_sniff_mutex, portMAX_DELAY);
  sniff_slot_t *slot = NULL;
  for (int i = 0; i < s_sniff_count; i++)
  {
    if (s_sniff[i].id == it->id && s_sniff[i].ext == it->ext)
    {
      slot = &s_sniff[i];
      break;
    }
  }
  if (slot == NULL && s_sniff_count < LND_CAN_SNIFFER_MAX)
  {
    slot = &s_sniff[s_sniff_count++];
    slot->id = it->id;
    slot->ext = it->ext;
    slot->count = 0;
  }
  if (slot != NULL)
  {
    slot->length = it->length;
    memcpy(slot->data, it->data, sizeof(slot->data));
    slot->count++;
    slot->last_ms = now_ms();
  }
  xSemaphoreGive(s_sniff_mutex);
}

size_t lnd_can_sniffer_get(lnd_can_sniff_frame_t *out, size_t max)
{
  s_sniffer_until_ms = now_ms() + LND_CAN_SNIFFER_ACTIVE_MS;
  const int64_t now = now_ms();
  xSemaphoreTake(s_sniff_mutex, portMAX_DELAY);
  size_t n = (size_t)s_sniff_count < max ? (size_t)s_sniff_count : max;
  for (size_t i = 0; i < n; i++)
  {
    out[i].id = s_sniff[i].id;
    out[i].ext = s_sniff[i].ext;
    out[i].length = s_sniff[i].length;
    memcpy(out[i].data, s_sniff[i].data, 8);
    out[i].count = s_sniff[i].count;
    out[i].age_ms = (uint32_t)(now - s_sniff[i].last_ms);
  }
  xSemaphoreGive(s_sniff_mutex);
  return n;
}

// ---------------------------------------------------------------------------
// Task side: the CAN table
// ---------------------------------------------------------------------------
static lnd_can_map_t s_map;
static int s_sig[LND_CAN_MAP_MAX];

// Takes the current table: signals get their ids and timeouts, the ISR filter is rebuilt.
static void map_reload(void)
{
  lnd_can_map_copy(&s_map);
  for (int i = 0; i < s_map.count; i++)
  {
    s_sig[i] = lnd_signal_id(s_map.e[i].signal);
    lnd_signal_set_timeout(s_sig[i], s_map.e[i].timeout_ms);
  }

  id_filter_t *next = (id_filter_t *)s_filter == &s_filters[0] ? &s_filters[1] : &s_filters[0];
  next->count = 0;
  for (int i = 0; i < s_map.count; i++)
  {
    bool seen = false;
    for (int k = 0; k < next->count && !seen; k++)
    {
      seen = next->f[k].id == s_map.e[i].id && next->f[k].ext == s_map.e[i].ext;
    }
    if (!seen)
    {
      next->f[next->count].id = s_map.e[i].id;
      next->f[next->count].ext = s_map.e[i].ext;
      next->count++;
    }
  }
  s_filter = next;
  ESP_LOGI(TAG, "CAN table: %u entries, %u IDs in the ISR filter", (unsigned)s_map.count, (unsigned)next->count);
}

static void decode(const can_rx_item_t *it)
{
  for (int i = 0; i < s_map.count; i++)
  {
    const lnd_can_map_entry_t *e = &s_map.e[i];
    if (e->id == it->id && e->ext == it->ext)
    {
      lnd_signal_set(s_sig[i], lnd_can_map_extract(e, it->data));
    }
  }
}

// ---------------------------------------------------------------------------
// Tasks
// ---------------------------------------------------------------------------
static void heartbeat_task(void *arg)
{
  (void)arg;
  uint8_t data[] = {LND_BENCH_HEADER, 0x00, 0x55, 0xAA, 0x00};
  uint8_t count = 0;
  while (1)
  {
    lnd_can_settings_t st;
    lnd_settings_get_can(&st);
    if (st.heartbeat && !st.demo)
    {
      data[4] = count++;
      // 0x77000F 0x66 0x00 0x55 0xAA count -- heartbeat; refused (false) in listen-only mode
      lnd_can_send(LND_BENCH_DASH_ALIVE, data, sizeof(data), true);
    }
    vTaskDelay(pdMS_TO_TICKS(LND_CAN_HEARTBEAT_PERIOD_MS));
  }
}

static void can_task(void *arg)
{
  (void)arg;
  lnd_can_settings_t st;
  uint32_t settings_gen = 0, map_gen = 0;
  int64_t retry_ms = 0;
  lnd_can_settings_t running = {};  // what the node was started with
  bool node_up = false;

  while (1)
  {
    // Settings changed (web): demo on/off, bitrate or mode - restart the node.
    if (settings_gen != lnd_settings_generation())
    {
      settings_gen = lnd_settings_generation();
      lnd_settings_get_can(&st);
      lnd_signal_sim_enable(st.demo);
      if (node_up && (st.bitrate != running.bitrate || st.mode != running.mode))
      {
        node_stop();
        node_up = false;
        retry_ms = 0;
      }
    }
    if (map_gen != lnd_can_map_generation())
    {
      map_gen = lnd_can_map_generation();
      map_reload();
    }

    if (!node_up && now_ms() >= retry_ms)
    {
      node_up = node_start(&st);
      if (node_up)
      {
        running = st;
      }
      else
      {
        retry_ms = now_ms() + 1000;
      }
    }
    if (!node_up)
    {
      vTaskDelay(pdMS_TO_TICKS(LND_CAN_POLL_MS));
      continue;
    }

    // Bus-off: the node is brought back a moment later, not at once.
    if (s_bus_off && now_ms() - s_bus_off_ms >= LND_CAN_BUSOFF_RECOVERY_MS)
    {
      s_bus_off = false;
      ESP_LOGW(TAG, "recovering from bus-off");
      twai_node_recover(s_node);
    }

    can_rx_item_t item;
    if (xQueueReceive(s_rx_queue, &item, pdMS_TO_TICKS(LND_CAN_POLL_MS)) == pdTRUE)
    {
      sniffer_record(&item);
      if (!lnd_signal_sim_is_enabled())
      {
        decode(&item);
      }
    }
  }
}

void lnd_can_task_start(void)
{
  s_node_mutex = xSemaphoreCreateMutex();
  s_sniff_mutex = xSemaphoreCreateMutex();
  configASSERT(s_node_mutex != NULL && s_sniff_mutex != NULL);

  BaseType_t ok = xTaskCreatePinnedToCore(can_task, "can_task", LND_CAN_TASK_STACK, NULL, LND_CAN_TASK_PRIORITY, NULL,
                                          LND_CAN_TASK_CORE);
  configASSERT(ok == pdPASS);
  ok = xTaskCreatePinnedToCore(heartbeat_task, "can_alive", LND_CAN_HEARTBEAT_TASK_STACK, NULL, LND_CAN_TASK_PRIORITY,
                               NULL, LND_CAN_TASK_CORE);
  configASSERT(ok == pdPASS);
}
