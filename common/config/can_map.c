#include "can_map.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "can_map_default.h"
#include "conf.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "json_util.h"
#include "nvs.h"

static const char *TAG = "can_map";

static lnd_can_map_t s_map;
static uint32_t s_generation;
static SemaphoreHandle_t s_mutex;

#define TIMEOUT_DEFAULT_MS 2000
#define TIMEOUT_MIN_MS 100
#define TIMEOUT_MAX_MS 60000

// ------------------------------------------------------------------
// Bits
// ------------------------------------------------------------------

// Position of the next bit of a field, in DBC numbering (bit n is bit n % 8 of byte n / 8).
// Intel: the next bit is the next more significant one. Motorola: inside a byte the
// next bit is the less significant one, after bit 0 it continues from bit 7 of the
// next byte.
static int next_bit(int pos, bool motorola)
{
  if (!motorola)
  {
    return pos + 1;
  }
  return (pos % 8 == 0) ? pos + 15 : pos - 1;
}

// Does the field lie inside 8 bytes?
static bool field_fits(uint8_t start, uint8_t len, bool motorola)
{
  int pos = start;
  for (int i = 0; i < len; i++)
  {
    if (pos < 0 || pos > 63)
    {
      return false;
    }
    pos = next_bit(pos, motorola);
  }
  return true;
}

float lnd_can_map_extract(const lnd_can_map_entry_t *e, const uint8_t data[8])
{
  uint64_t raw = 0;
  int pos = e->start;
  for (int i = 0; i < e->len; i++)
  {
    const uint64_t bit = (data[pos / 8] >> (pos % 8)) & 1u;
    if (e->motorola)
    {
      raw = (raw << 1) | bit;  // the first bit is the most significant
    }
    else
    {
      raw |= bit << i;  // the first bit is the least significant
    }
    pos = next_bit(pos, e->motorola);
  }

  float value;
  if (e->is_signed && e->len < 64 && (raw >> (e->len - 1)) & 1u)
  {
    value = (float)((int64_t)raw - (int64_t)(1ULL << e->len));
  }
  else
  {
    value = (float)raw;
  }
  return value * e->factor + e->offset;
}

// ------------------------------------------------------------------
// JSON
// ------------------------------------------------------------------

static bool parse_id(const cJSON *j, uint32_t *id)
{
  const cJSON *v = cJSON_GetObjectItemCaseSensitive(j, "id");
  if (cJSON_IsNumber(v) && v->valuedouble >= 0)
  {
    *id = (uint32_t)v->valuedouble;
    return true;
  }
  if (cJSON_IsString(v) && v->valuestring != NULL && v->valuestring[0] != '\0')
  {
    char *end = NULL;
    unsigned long x = strtoul(v->valuestring, &end, 0);  // "0x201" or "513"
    if (end != NULL && *end == '\0')
    {
      *id = (uint32_t)x;
      return true;
    }
  }
  return false;
}

static bool parse_entry(const cJSON *j, lnd_can_map_entry_t *e)
{
  memset(e, 0, sizeof(*e));
  e->factor = 1.0f;
  e->timeout_ms = TIMEOUT_DEFAULT_MS;

  get_str(j, "signal", e->signal, sizeof(e->signal));
  if (e->signal[0] == '\0')
  {
    ESP_LOGW(TAG, "entry without \"signal\"");
    return false;
  }
  if (!parse_id(j, &e->id))
  {
    ESP_LOGW(TAG, "%s: bad \"id\"", e->signal);
    return false;
  }
  get_bool(j, "ext", &e->ext);
  if (e->id > (e->ext ? 0x1FFFFFFFu : 0x7FFu))
  {
    ESP_LOGW(TAG, "%s: id 0x%lx does not fit %s", e->signal, (unsigned long)e->id, e->ext ? "29 bits" : "11 bits");
    return false;
  }

  int v = 0;
  get_int(j, "start", 0, 63, &v);
  e->start = (uint8_t)v;
  v = 8;
  get_int(j, "len", 1, 32, &v);
  e->len = (uint8_t)v;
  char order[12] = "intel";
  get_str(j, "order", order, sizeof(order));
  e->motorola = strcmp(order, "motorola") == 0;
  if (!e->motorola && strcmp(order, "intel") != 0)
  {
    ESP_LOGW(TAG, "%s: \"order\" is intel or motorola", e->signal);
    return false;
  }
  if (!field_fits(e->start, e->len, e->motorola))
  {
    ESP_LOGW(TAG, "%s: the field does not fit in 8 bytes", e->signal);
    return false;
  }
  get_bool(j, "signed", &e->is_signed);
  get_float(j, "factor", &e->factor);
  get_float(j, "offset", &e->offset);
  v = (int)e->timeout_ms;
  get_int(j, "timeout", TIMEOUT_MIN_MS, TIMEOUT_MAX_MS, &v);
  e->timeout_ms = (uint32_t)v;
  return true;
}

// false - the JSON is broken or an entry is wrong; the result is not to be used.
static bool parse(const char *json, lnd_can_map_t *m)
{
  memset(m, 0, sizeof(*m));
  cJSON *root = cJSON_Parse(json);
  if (root == NULL)
  {
    ESP_LOGW(TAG, "JSON does not parse");
    return false;
  }
  bool ok = true;
  const cJSON *it;
  cJSON_ArrayForEach(it, cJSON_GetObjectItemCaseSensitive(root, "entries"))
  {
    if (m->count >= LND_CAN_MAP_MAX)
    {
      ESP_LOGW(TAG, "more than %d entries", LND_CAN_MAP_MAX);
      ok = false;
      break;
    }
    lnd_can_map_entry_t *e = &m->e[m->count];
    if (!parse_entry(it, e))
    {
      ok = false;
      break;
    }
    for (int i = 0; i < m->count; i++)
    {
      if (strcmp(m->e[i].signal, e->signal) == 0)
      {
        ESP_LOGW(TAG, "signal \"%s\" appears twice", e->signal);
        ok = false;
      }
    }
    if (!ok)
    {
      break;
    }
    m->count++;
  }
  cJSON_Delete(root);
  return ok;
}

// ------------------------------------------------------------------
// NVS
// ------------------------------------------------------------------

static char *nvs_load_json(void)
{
  nvs_handle_t h;
  if (nvs_open(LND_CONFIG_NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK)
  {
    return NULL;
  }
  size_t len = 0;
  char *buf = NULL;
  if (nvs_get_blob(h, LND_CAN_MAP_NVS_KEY, NULL, &len) == ESP_OK && len > 0 && len <= LND_CAN_MAP_JSON_MAX)
  {
    buf = malloc(len + 1);
    if (buf && nvs_get_blob(h, LND_CAN_MAP_NVS_KEY, buf, &len) == ESP_OK)
    {
      buf[len] = '\0';
    }
    else
    {
      free(buf);
      buf = NULL;
    }
  }
  nvs_close(h);
  return buf;
}

static esp_err_t nvs_save_json(const char *json)
{
  nvs_handle_t h;
  esp_err_t err = nvs_open(LND_CONFIG_NVS_NAMESPACE, NVS_READWRITE, &h);
  if (err != ESP_OK)
  {
    return err;
  }
  err = nvs_set_blob(h, LND_CAN_MAP_NVS_KEY, json, strlen(json));
  if (err == ESP_OK)
  {
    err = nvs_commit(h);
  }
  nvs_close(h);
  return err;
}

static void set_current(const lnd_can_map_t *m)
{
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  s_map = *m;
  s_generation++;
  xSemaphoreGive(s_mutex);
}

void lnd_can_map_init(void)
{
  s_mutex = xSemaphoreCreateMutex();
  configASSERT(s_mutex != NULL);

  lnd_can_map_t *tmp = malloc(sizeof(*tmp));
  configASSERT(tmp != NULL);
  char *json = nvs_load_json();
  if (json != NULL && parse(json, tmp))
  {
    ESP_LOGI(TAG, "CAN table from NVS: %u entries", (unsigned)tmp->count);
  }
  else
  {
    if (json != NULL)
    {
      ESP_LOGW(TAG, "stored CAN table is broken, using the built-in preset");
    }
    bool ok = parse(LND_CAN_MAP_DEFAULT_JSON, tmp);
    configASSERT(ok);
    ESP_LOGI(TAG, "built-in CAN table: %u entries", (unsigned)tmp->count);
  }
  free(json);
  set_current(tmp);
  free(tmp);
}

void lnd_can_map_copy(lnd_can_map_t *out)
{
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  *out = s_map;
  xSemaphoreGive(s_mutex);
}

uint32_t lnd_can_map_generation(void)
{
  return s_generation;
}

esp_err_t lnd_can_map_apply_json(const char *json)
{
  size_t len = json ? strlen(json) : 0;
  if (len == 0 || len > LND_CAN_MAP_JSON_MAX)
  {
    return ESP_ERR_INVALID_ARG;
  }
  lnd_can_map_t *m = malloc(sizeof(*m));
  if (m == NULL)
  {
    return ESP_ERR_NO_MEM;
  }
  esp_err_t err = ESP_ERR_INVALID_ARG;
  if (parse(json, m))
  {
    err = nvs_save_json(json);
    if (err == ESP_OK)
    {
      set_current(m);
      ESP_LOGI(TAG, "CAN table applied: %u entries", (unsigned)m->count);
    }
  }
  free(m);
  return err;
}

char *lnd_can_map_json_dup(void)
{
  char *json = nvs_load_json();
  if (json == NULL)
  {
    json = strdup(LND_CAN_MAP_DEFAULT_JSON);
  }
  return json;
}

esp_err_t lnd_can_map_reset(void)
{
  nvs_handle_t h;
  esp_err_t err = nvs_open(LND_CONFIG_NVS_NAMESPACE, NVS_READWRITE, &h);
  if (err != ESP_OK)
  {
    return err;
  }
  err = nvs_erase_key(h, LND_CAN_MAP_NVS_KEY);
  if (err == ESP_ERR_NVS_NOT_FOUND)
  {
    err = ESP_OK;
  }
  if (err == ESP_OK)
  {
    err = nvs_commit(h);
  }
  nvs_close(h);
  if (err == ESP_OK)
  {
    lnd_can_map_t *tmp = malloc(sizeof(*tmp));
    if (tmp == NULL)
    {
      return ESP_ERR_NO_MEM;
    }
    bool ok = parse(LND_CAN_MAP_DEFAULT_JSON, tmp);
    configASSERT(ok);
    set_current(tmp);
    free(tmp);
  }
  return err;
}
