#include "settings.h"
#include <stdlib.h>
#include <string.h>
#include "conf.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "json_util.h"
#include "nvs.h"

static const char *TAG = "settings";

typedef struct {
  lnd_can_settings_t can;
  lnd_display_settings_t display;
  lnd_log_settings_t log;
  lnd_wifi_settings_t wifi;
} all_t;

static all_t s_all;
static uint32_t s_generation;
static SemaphoreHandle_t s_mutex;

static void defaults(all_t *a)
{
  memset(a, 0, sizeof(*a));
  a->can.bitrate = LND_CAN_BITRATE_DEFAULT;
  a->can.mode = LND_CAN_MODE_DEFAULT;
  a->can.heartbeat = true;
  a->can.demo = NDASH_DEMO != 0;
  a->display.brightness = LND_BRIGHTNESS_DEFAULT_PCT;
  a->log.mode = LND_LOG_MANUAL;
  strlcpy(a->log.signal, "rpm", sizeof(a->log.signal));
  a->log.value = 500.0f;
  a->log.stop_delay = 10;
  strlcpy(a->wifi.password, LND_WIFI_AP_PASS_DEFAULT, sizeof(a->wifi.password));
}

static bool wifi_valid(const lnd_wifi_settings_t *w)
{
  const size_t n = strlen(w->ssid), p = strlen(w->password);
  if (n > LND_WIFI_AP_SSID_MAX_LEN || p > LND_WIFI_AP_PASSWORD_MAX_LEN)
  {
    return false;
  }
  for (size_t i = 0; i < n; i++)
  {
    if ((unsigned char)w->ssid[i] < 0x20 || w->ssid[i] == 0x7f)
    {
      return false;
    }
  }
  return true;
}

static bool valid(const all_t *a)
{
  return a->can.bitrate >= 10000 && a->can.bitrate <= 1000000 &&
         (a->can.mode == LND_CAN_MODE_LISTEN_ONLY || a->can.mode == LND_CAN_MODE_NORMAL) &&
         a->display.brightness >= LND_BRIGHTNESS_MIN_PCT && a->display.brightness <= 100 &&
         a->display.baro <= LND_BARO_INHG && a->display.boost <= LND_BOOST_PSI && a->display.button_pin <= 48 && a->log.mode <= LND_LOG_CONDITION &&
         a->log.stop_delay <= 3600 && wifi_valid(&a->wifi);
}

static void parse(const char *json, all_t *a)
{
  cJSON *root = cJSON_Parse(json);
  if (root == NULL)
  {
    return;
  }
  const cJSON *c = cJSON_GetObjectItemCaseSensitive(root, "can");
  if (c != NULL)
  {
    int v = (int)a->can.bitrate;
    get_int(c, "bitrate", 10000, 1000000, &v);
    a->can.bitrate = (uint32_t)v;
    char mode[16] = "";
    get_str(c, "mode", mode, sizeof(mode));
    if (strcmp(mode, "listen_only") == 0)
    {
      a->can.mode = LND_CAN_MODE_LISTEN_ONLY;
    }
    else if (strcmp(mode, "normal") == 0)
    {
      a->can.mode = LND_CAN_MODE_NORMAL;
    }
    get_bool(c, "heartbeat", &a->can.heartbeat);
    get_bool(c, "demo", &a->can.demo);
  }
  const cJSON *d = cJSON_GetObjectItemCaseSensitive(root, "display");
  if (d != NULL)
  {
    int v = a->display.brightness;
    get_int(d, "brightness", 0, 255, &v);  // out-of-range values are rejected by valid()
    a->display.brightness = (uint8_t)v;
    char unit[8] = "";
    get_str(d, "baro", unit, sizeof(unit));
    if (strcmp(unit, "kpa") == 0) a->display.baro = LND_BARO_KPA;
    else if (strcmp(unit, "inhg") == 0) a->display.baro = LND_BARO_INHG;
    int pin = a->display.button_pin;
    get_int(d, "button_pin", 0, 255, &pin);  // out-of-range values are rejected by valid()
    a->display.button_pin = (uint8_t)pin;
    get_str(d, "boost", unit, sizeof(unit));
    if (strcmp(unit, "bar") == 0) a->display.boost = LND_BOOST_BAR;
    else if (strcmp(unit, "psi") == 0) a->display.boost = LND_BOOST_PSI;
  }
  const cJSON *lg = cJSON_GetObjectItemCaseSensitive(root, "log");
  if (lg != NULL)
  {
    char mode[16] = "";
    get_str(lg, "mode", mode, sizeof(mode));
    if (strcmp(mode, "manual") == 0) a->log.mode = LND_LOG_MANUAL;
    else if (strcmp(mode, "boot") == 0) a->log.mode = LND_LOG_BOOT;
    else if (strcmp(mode, "condition") == 0) a->log.mode = LND_LOG_CONDITION;
    get_str(lg, "signal", a->log.signal, sizeof(a->log.signal));
    char op[4] = "";
    get_str(lg, "op", op, sizeof(op));
    if (op[0] == '<' || op[0] == '>')
    {
      a->log.less = op[0] == '<';
    }
    get_float(lg, "value", &a->log.value);
    int sd = a->log.stop_delay;
    get_int(lg, "stop_delay", 0, 65535, &sd);  // out-of-range values are rejected by valid()
    a->log.stop_delay = (uint16_t)sd;
  }
  const cJSON *w = cJSON_GetObjectItemCaseSensitive(root, "wifi_ap");
  if (w != NULL)
  {
    get_str(w, "ssid", a->wifi.ssid, sizeof(a->wifi.ssid));
    get_str(w, "password", a->wifi.password, sizeof(a->wifi.password));
  }
  cJSON_Delete(root);
}

static char *to_json(const all_t *a, bool with_secrets)
{
  cJSON *root = cJSON_CreateObject();
  cJSON_AddNumberToObject(root, "version", LND_SETTINGS_VERSION);
  cJSON *can = cJSON_AddObjectToObject(root, "can");
  cJSON_AddNumberToObject(can, "bitrate", a->can.bitrate);
  cJSON_AddStringToObject(can, "mode", a->can.mode == LND_CAN_MODE_NORMAL ? "normal" : "listen_only");
  cJSON_AddBoolToObject(can, "heartbeat", a->can.heartbeat);
  cJSON_AddBoolToObject(can, "demo", a->can.demo);
  cJSON *d = cJSON_AddObjectToObject(root, "display");
  cJSON_AddNumberToObject(d, "brightness", a->display.brightness);
  cJSON_AddStringToObject(d, "baro", a->display.baro == LND_BARO_INHG ? "inhg" : "kpa");
  cJSON_AddStringToObject(d, "boost", a->display.boost == LND_BOOST_PSI ? "psi" : "bar");
  cJSON_AddNumberToObject(d, "button_pin", a->display.button_pin);
  cJSON *lg = cJSON_AddObjectToObject(root, "log");
  static const char *const log_modes[] = {"manual", "boot", "condition"};
  cJSON_AddStringToObject(lg, "mode", log_modes[a->log.mode <= LND_LOG_CONDITION ? a->log.mode : 0]);
  cJSON_AddStringToObject(lg, "signal", a->log.signal);
  cJSON_AddStringToObject(lg, "op", a->log.less ? "<" : ">");
  cJSON_AddNumberToObject(lg, "value", a->log.value);
  cJSON_AddNumberToObject(lg, "stop_delay", a->log.stop_delay);
  cJSON *w = cJSON_AddObjectToObject(root, "wifi_ap");
  cJSON_AddStringToObject(w, "ssid", a->wifi.ssid);
  if (with_secrets)
  {
    cJSON_AddStringToObject(w, "password", a->wifi.password);
  }
  char *s = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  return s;
}

static esp_err_t nvs_save(const char *json)
{
  nvs_handle_t h;
  esp_err_t err = nvs_open(LND_CONFIG_NVS_NAMESPACE, NVS_READWRITE, &h);
  if (err != ESP_OK)
  {
    return err;
  }
  err = nvs_set_blob(h, LND_SETTINGS_NVS_KEY, json, strlen(json));
  if (err == ESP_OK)
  {
    err = nvs_commit(h);
  }
  nvs_close(h);
  return err;
}

static char *nvs_load(void)
{
  nvs_handle_t h;
  if (nvs_open(LND_CONFIG_NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK)
  {
    return NULL;
  }
  size_t len = 0;
  char *buf = NULL;
  if (nvs_get_blob(h, LND_SETTINGS_NVS_KEY, NULL, &len) == ESP_OK && len > 0 && len <= LND_SETTINGS_JSON_MAX)
  {
    buf = malloc(len + 1);
    if (buf && nvs_get_blob(h, LND_SETTINGS_NVS_KEY, buf, &len) == ESP_OK)
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

// cJSON allocates a 64-byte node for every key and value, and ALWAYSINTERNAL keeps small blocks in internal
// RAM, which is the scarce one. A layout of four screens would take over 100 KB of it while it is parsed, so
// the JSON of the whole firmware (settings, layout, CAN table, the web requests) goes to PSRAM.
static void *json_malloc(size_t size)
{
  void *p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return p != NULL ? p : malloc(size);
}

void lnd_settings_init(void)
{
  static cJSON_Hooks hooks = {.malloc_fn = json_malloc, .free_fn = free};
  cJSON_InitHooks(&hooks);  // before the first JSON is parsed: this is the first call of main
  s_mutex = xSemaphoreCreateMutex();
  configASSERT(s_mutex != NULL);

  defaults(&s_all);
  char *json = nvs_load();
  if (json != NULL)
  {
    parse(json, &s_all);
    free(json);
  }
  if (!valid(&s_all))
  {
    ESP_LOGW(TAG, "stored settings are out of range, using defaults");
    defaults(&s_all);
  }
  s_generation++;
  ESP_LOGI(TAG, "CAN: %lu bit/s, %s, heartbeat %s, demo %s; brightness %u%%", (unsigned long)s_all.can.bitrate,
           s_all.can.mode == LND_CAN_MODE_NORMAL ? "normal" : "listen_only", s_all.can.heartbeat ? "on" : "off",
           s_all.can.demo ? "on" : "off", (unsigned)s_all.display.brightness);
}

static void get(void *out, const void *field, size_t size)
{
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  memcpy(out, field, size);
  xSemaphoreGive(s_mutex);
}

void lnd_settings_get_can(lnd_can_settings_t *out)
{
  get(out, &s_all.can, sizeof(*out));
}

void lnd_settings_get_display(lnd_display_settings_t *out)
{
  get(out, &s_all.display, sizeof(*out));
}

void lnd_settings_get_log(lnd_log_settings_t *out)
{
  get(out, &s_all.log, sizeof(*out));
}

void lnd_settings_get_wifi(lnd_wifi_settings_t *out)
{
  get(out, &s_all.wifi, sizeof(*out));
}

// Stores a whole new set of settings if it is valid.
static esp_err_t commit(const all_t *a)
{
  if (!valid(a))
  {
    return ESP_ERR_INVALID_ARG;
  }
  char *json = to_json(a, true);
  if (json == NULL)
  {
    return ESP_ERR_NO_MEM;
  }
  esp_err_t err = nvs_save(json);
  free(json);
  if (err == ESP_OK)
  {
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_all = *a;
    s_generation++;
    xSemaphoreGive(s_mutex);
  }
  return err;
}

static esp_err_t set_part(void (*apply)(all_t *, const void *), const void *in)
{
  all_t a;
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  a = s_all;
  xSemaphoreGive(s_mutex);
  apply(&a, in);
  return commit(&a);
}

static void apply_can(all_t *a, const void *in)
{
  a->can = *(const lnd_can_settings_t *)in;
}

static void apply_display(all_t *a, const void *in)
{
  a->display = *(const lnd_display_settings_t *)in;
}

static void apply_log(all_t *a, const void *in)
{
  a->log = *(const lnd_log_settings_t *)in;
}

static void apply_wifi(all_t *a, const void *in)
{
  a->wifi = *(const lnd_wifi_settings_t *)in;
}

esp_err_t lnd_settings_set_can(const lnd_can_settings_t *in)
{
  return set_part(apply_can, in);
}

esp_err_t lnd_settings_set_display(const lnd_display_settings_t *in)
{
  return set_part(apply_display, in);
}

esp_err_t lnd_settings_set_log(const lnd_log_settings_t *in)
{
  return set_part(apply_log, in);
}

esp_err_t lnd_settings_set_wifi(const lnd_wifi_settings_t *in)
{
  return set_part(apply_wifi, in);
}

char *lnd_settings_json_dup(void)
{
  all_t a;
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  a = s_all;
  xSemaphoreGive(s_mutex);
  return to_json(&a, false);
}

esp_err_t lnd_settings_apply_json(const char *json)
{
  if (json == NULL || strlen(json) > LND_SETTINGS_JSON_MAX)
  {
    return ESP_ERR_INVALID_ARG;
  }
  all_t a;
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  a = s_all;
  xSemaphoreGive(s_mutex);
  parse(json, &a);
  return commit(&a);
}

uint32_t lnd_settings_generation(void)
{
  return s_generation;
}
