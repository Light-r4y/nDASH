#include "bench_cmds.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "conf.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "json_util.h"
#include "nvs.h"

static const char *TAG = "bench_cmds";

static lnd_bench_cmds_t s_cmds;
static SemaphoreHandle_t s_mutex;

// ------------------------------------------------------------------
// The preset. The numbers come from the rusEFI sources (java_console/models/.../enums):
//   ECU_CAN_BUS_USER_CONTROL = 0x77000C (extended ID), header bench_test_magic_numbers_e = 0x66,
//   ts_command_e: TS_IGNITION_CATEGORY 0x12, TS_INJECTOR_CATEGORY 0x13, TS_X14 0x14 (index 9 -
//   start/stop the engine, as the old dash sent it), TS_BENCH_CATEGORY 0x16, TS_STOP_ENGINE 0x24,
//   bench_mode_e (the index of TS_BENCH_CATEGORY): MAIN_RELAY 0, FUEL_PUMP 1, STARTER_ENABLE_RELAY 2,
//   FAN_RELAY 4, FAN_RELAY_2 5, AC_COMPRESSOR_RELAY 6, CHECK_ENGINE_LIGHT 7, IDLE_VALVE 8,
//   CANCEL 15, BOOST_VALVE 43. The values are positions in the enums of the current rusEFI master:
//   a much older ECU firmware may number them differently - edit the table then.
// ------------------------------------------------------------------
#define TS_IGNITION 0x12
#define TS_INJECTOR 0x13
#define TS_X14 0x14
#define TS_BENCH 0x16
#define TS_STOP_ENGINE 0x24

static void add(lnd_bench_cmds_t *t, const char *name, const char *label, const char *group, uint16_t sub, uint16_t idx,
                uint8_t dlc, bool stopped_only)
{
  if (t->count >= LND_BENCH_CMDS_MAX)
  {
    return;
  }
  lnd_bench_cmd_t *c = &t->c[t->count++];
  memset(c, 0, sizeof(*c));
  strlcpy(c->name, name, sizeof(c->name));
  strlcpy(c->label, label, sizeof(c->label));
  strlcpy(c->group, group, sizeof(c->group));
  c->id = LND_BENCH_DEFAULT_ID;
  c->ext = true;
  c->sub = sub;
  c->idx = idx;
  c->dlc = dlc;
  c->stopped_only = stopped_only;
}

static void preset(lnd_bench_cmds_t *t)
{
  memset(t, 0, sizeof(*t));
  char name[LND_BENCH_NAME_LEN], label[LND_BENCH_LABEL_LEN];
  for (int i = 1; i <= 8; i++)
  {
    snprintf(name, sizeof(name), "ign%d", i);
    snprintf(label, sizeof(label), "Coil %d", i);
    add(t, name, label, "Ignition", TS_IGNITION, i, 8, true);
  }
  for (int i = 1; i <= 8; i++)
  {
    snprintf(name, sizeof(name), "inj%d", i);
    snprintf(label, sizeof(label), "Injector %d", i);
    add(t, name, label, "Injectors", TS_INJECTOR, i, 8, true);
  }
  add(t, "fuel_pump", "Fuel pump", "Outputs", TS_BENCH, 1, 6, true);
  add(t, "fan1", "Fan 1", "Outputs", TS_BENCH, 4, 6, true);
  add(t, "fan2", "Fan 2", "Outputs", TS_BENCH, 5, 6, true);
  add(t, "main_relay", "Main relay", "Outputs", TS_BENCH, 0, 6, true);
  add(t, "ac_relay", "A/C relay", "Outputs", TS_BENCH, 6, 6, true);
  add(t, "mil", "Check engine light", "Outputs", TS_BENCH, 7, 6, true);
  add(t, "starter", "Starter relay", "Outputs", TS_BENCH, 2, 6, true);
  add(t, "idle_valve", "Idle valve", "Outputs", TS_BENCH, 8, 6, true);
  add(t, "boost_valve", "Boost valve", "Outputs", TS_BENCH, 43, 6, true);
  add(t, "start_stop", "Start / stop engine", "Engine", TS_X14, 9, 6, false);
  add(t, "stop_engine", "Stop engine", "Engine", TS_STOP_ENGINE, 0, 6, false);
  add(t, "cancel", "Abort test", "Engine", TS_BENCH, 15, 6, false);
}

// ------------------------------------------------------------------
// JSON
// ------------------------------------------------------------------
static bool name_ok(const char *n)
{
  const size_t len = strlen(n);
  if (len == 0 || len >= LND_BENCH_NAME_LEN)
  {
    return false;
  }
  for (; *n; n++)
  {
    const bool ok = (*n >= 'a' && *n <= 'z') || (*n >= 'A' && *n <= 'Z') || (*n >= '0' && *n <= '9') || *n == '_';
    if (!ok)
    {
      return false;
    }
  }
  return true;
}

// Text that goes into the JSON as it is: no quotes, backslashes or control characters.
static bool text_ok(const char *s)
{
  for (; *s; s++)
  {
    if (*s == '"' || *s == '\\' || (unsigned char)*s < 0x20)
    {
      return false;
    }
  }
  return true;
}

// "id" is a number or a string ("0x77000C").
static bool read_id(const cJSON *j, uint32_t *out)
{
  const cJSON *v = cJSON_GetObjectItemCaseSensitive(j, "id");
  if (cJSON_IsNumber(v) && v->valuedouble >= 0 && v->valuedouble <= 0x1FFFFFFF)
  {
    *out = (uint32_t)v->valuedouble;
    return true;
  }
  if (cJSON_IsString(v) && v->valuestring != NULL)
  {
    char *end = NULL;
    const unsigned long x = strtoul(v->valuestring, &end, 0);
    if (end != NULL && end != v->valuestring && *end == '\0' && x <= 0x1FFFFFFF)
    {
      *out = (uint32_t)x;
      return true;
    }
  }
  return false;
}

static bool parse(const char *json, lnd_bench_cmds_t *out)
{
  memset(out, 0, sizeof(*out));
  cJSON *root = cJSON_Parse(json);
  const cJSON *list = root ? cJSON_GetObjectItemCaseSensitive(root, "cmds") : NULL;
  if (!cJSON_IsArray(list) || cJSON_GetArraySize(list) > LND_BENCH_CMDS_MAX)
  {
    cJSON_Delete(root);
    return false;
  }
  bool ok = true;
  const cJSON *it;
  cJSON_ArrayForEach(it, list)
  {
    lnd_bench_cmd_t *c = &out->c[out->count];
    memset(c, 0, sizeof(*c));
    c->id = LND_BENCH_DEFAULT_ID;
    c->ext = true;
    c->dlc = 6;
    c->stopped_only = true;
    get_str(it, "name", c->name, sizeof(c->name));
    get_str(it, "label", c->label, sizeof(c->label));
    get_str(it, "group", c->group, sizeof(c->group));
    get_bool(it, "ext", &c->ext);
    get_bool(it, "stopped_only", &c->stopped_only);
    int v = 0;
    get_int(it, "sub", 0, 65535, &v);
    c->sub = (uint16_t)v;
    v = 0;
    get_int(it, "idx", 0, 65535, &v);
    c->idx = (uint16_t)v;
    v = c->dlc;
    get_int(it, "dlc", 6, 8, &v);
    c->dlc = (uint8_t)v;
    if (!name_ok(c->name) || !text_ok(c->label) || !text_ok(c->group) ||
        (cJSON_GetObjectItemCaseSensitive(it, "id") && !read_id(it, &c->id)) || c->id > (c->ext ? 0x1FFFFFFFu : 0x7FFu) ||
        (c->dlc != 6 && c->dlc != 8))
    {
      ok = false;
      break;
    }
    for (int k = 0; k < out->count; k++)
    {
      ok = ok && strcmp(out->c[k].name, c->name) != 0;  // names are unique
    }
    if (!ok)
    {
      break;
    }
    if (c->label[0] == '\0')
    {
      strlcpy(c->label, c->name, sizeof(c->label));
    }
    out->count++;
  }
  cJSON_Delete(root);
  return ok;
}

static char *serialise(const lnd_bench_cmds_t *t)
{
  const size_t cap = 64 + (size_t)t->count * 220;
  char *s = malloc(cap);
  if (s == NULL)
  {
    return NULL;
  }
  size_t n = (size_t)snprintf(s, cap, "{\"version\":1,\"cmds\":[");
  for (int i = 0; i < t->count; i++)
  {
    const lnd_bench_cmd_t *c = &t->c[i];
    n += (size_t)snprintf(s + n, cap - n,
                          "%s{\"name\":\"%s\",\"label\":\"%s\",\"group\":\"%s\",\"id\":%u,\"ext\":%s,\"sub\":%u,\"idx\":%u,"
                          "\"dlc\":%u,\"stopped_only\":%s}",
                          i ? "," : "", c->name, c->label, c->group, (unsigned)c->id, c->ext ? "true" : "false",
                          (unsigned)c->sub, (unsigned)c->idx, (unsigned)c->dlc, c->stopped_only ? "true" : "false");
  }
  snprintf(s + n, cap - n, "]}");
  return s;
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
  if (nvs_get_blob(h, LND_BENCH_CMDS_NVS_KEY, NULL, &len) == ESP_OK && len > 0 && len <= LND_BENCH_CMDS_JSON_MAX)
  {
    buf = malloc(len + 1);
    if (buf != NULL && nvs_get_blob(h, LND_BENCH_CMDS_NVS_KEY, buf, &len) == ESP_OK)
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
  err = nvs_set_blob(h, LND_BENCH_CMDS_NVS_KEY, json, strlen(json));
  if (err == ESP_OK)
  {
    err = nvs_commit(h);
  }
  nvs_close(h);
  return err;
}

static void set_current(const lnd_bench_cmds_t *t)
{
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  s_cmds = *t;
  xSemaphoreGive(s_mutex);
}

// ------------------------------------------------------------------
// API
// ------------------------------------------------------------------
void lnd_bench_cmds_init(void)
{
  s_mutex = xSemaphoreCreateMutex();
  configASSERT(s_mutex != NULL);
  lnd_bench_cmds_t *tmp = malloc(sizeof(*tmp));
  configASSERT(tmp != NULL);
  char *json = nvs_load_json();
  if (json != NULL && parse(json, tmp))
  {
    ESP_LOGI(TAG, "bench commands from NVS: %u", (unsigned)tmp->count);
  }
  else
  {
    if (json != NULL)
    {
      ESP_LOGW(TAG, "stored bench commands are broken, using the preset");
    }
    preset(tmp);
    ESP_LOGI(TAG, "preset bench commands: %u", (unsigned)tmp->count);
  }
  free(json);
  set_current(tmp);
  free(tmp);
}

bool lnd_bench_cmd_find(const char *name, lnd_bench_cmd_t *out)
{
  bool found = false;
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  for (int i = 0; i < s_cmds.count; i++)
  {
    if (strcmp(s_cmds.c[i].name, name) == 0)
    {
      *out = s_cmds.c[i];
      found = true;
      break;
    }
  }
  xSemaphoreGive(s_mutex);
  return found;
}

uint8_t lnd_bench_cmd_frame(const lnd_bench_cmd_t *c, uint8_t data[8])
{
  memset(data, 0, 8);
  data[0] = LND_BENCH_FRAME_HEADER;
  data[2] = (uint8_t)(c->sub & 0xFF);
  data[3] = (uint8_t)(c->sub >> 8);
  data[4] = (uint8_t)(c->idx & 0xFF);
  data[5] = (uint8_t)(c->idx >> 8);
  return c->dlc;
}

esp_err_t lnd_bench_cmds_apply_json(const char *json)
{
  const size_t len = json ? strlen(json) : 0;
  if (len == 0 || len > LND_BENCH_CMDS_JSON_MAX)
  {
    return ESP_ERR_INVALID_ARG;
  }
  lnd_bench_cmds_t *t = malloc(sizeof(*t));
  if (t == NULL)
  {
    return ESP_ERR_NO_MEM;
  }
  esp_err_t err = ESP_ERR_INVALID_ARG;
  if (parse(json, t))
  {
    char *clean = serialise(t);  // what is stored is what was understood
    err = clean ? nvs_save_json(clean) : ESP_ERR_NO_MEM;
    free(clean);
    if (err == ESP_OK)
    {
      set_current(t);
      ESP_LOGI(TAG, "bench commands applied: %u", (unsigned)t->count);
    }
  }
  free(t);
  return err;
}

char *lnd_bench_cmds_json_dup(void)
{
  lnd_bench_cmds_t *t = malloc(sizeof(*t));
  if (t == NULL)
  {
    return NULL;
  }
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  *t = s_cmds;
  xSemaphoreGive(s_mutex);
  char *json = serialise(t);
  free(t);
  return json;
}

esp_err_t lnd_bench_cmds_reset(void)
{
  nvs_handle_t h;
  esp_err_t err = nvs_open(LND_CONFIG_NVS_NAMESPACE, NVS_READWRITE, &h);
  if (err != ESP_OK)
  {
    return err;
  }
  err = nvs_erase_key(h, LND_BENCH_CMDS_NVS_KEY);
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
    lnd_bench_cmds_t *t = malloc(sizeof(*t));
    if (t == NULL)
    {
      return ESP_ERR_NO_MEM;
    }
    preset(t);
    set_current(t);
    free(t);
  }
  return err;
}
