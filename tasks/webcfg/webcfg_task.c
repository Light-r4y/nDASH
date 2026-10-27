#include "webcfg_task.h"
#include <dirent.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include "auth.h"
#include "bench_cmds.h"
#include "board.h"
#include "can_map.h"
#include "can_task.h"
#include "cJSON.h"
#include "conf.h"
#include "esp_app_desc.h"
#include "esp_event.h"
#include "lwip/sockets.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_random.h"
#include "esp_spiffs.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "layout.h"
#include "logger_task.h"
#include "media.h"
#include "ota_page.h"
#include "settings.h"
#include "signals.h"

static const char *TAG = "webcfg";

// A number from a macro as a string (for error texts).
#define LND_STR_(x) #x
#define LND_STR(x) LND_STR_(x)

static atomic_bool s_start_requested = false;
static atomic_bool s_running = false;

// A new value at every start: the ETags of the static files differ between runs of the firmware, so
// a browser never keeps a page that a new www image has replaced.
static uint32_t s_boot_nonce;

#define OTA_CHUNK 4096
#define WWW_BLOCK 4096
#define RECV_IDLE_RETRIES 4
#define JSON_OK "{\"status\":\"success\"}"

// ---------------------------------------------------------------------------
// Common helpers
// ---------------------------------------------------------------------------
static esp_err_t send_text_err(httpd_req_t *req, const char *status, const char *msg)
{
  httpd_resp_set_status(req, status);
  httpd_resp_set_type(req, "text/plain");
  return httpd_resp_send(req, msg, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t send_json(httpd_req_t *req, const char *json)
{
  httpd_resp_set_type(req, "application/json");
  return httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
}

// Sends a heap string as JSON and frees it. NULL - out of memory.
static esp_err_t send_json_heap(httpd_req_t *req, char *json)
{
  if (json == NULL)
  {
    return send_text_err(req, "500 Internal Server Error", "No memory");
  }
  esp_err_t ret = send_json(req, json);
  free(json);
  return ret;
}

// Receives exactly len bytes of the body. Socket timeouts are retried: the AP can be slow.
static bool recv_exact(httpd_req_t *req, char *buf, size_t len)
{
  size_t got = 0;
  int idle = 0;
  while (got < len)
  {
    int r = httpd_req_recv(req, buf + got, len - got);
    if (r == HTTPD_SOCK_ERR_TIMEOUT)
    {
      if (++idle > RECV_IDLE_RETRIES)
      {
        return false;
      }
      continue;
    }
    if (r <= 0)
    {
      return false;
    }
    idle = 0;
    got += r;
  }
  return true;
}

// A small body on the heap (free() it); NULL - the reply has been sent.
static char *recv_body(httpd_req_t *req, size_t max)
{
  size_t len = req->content_len;
  if (len == 0 || len > max)
  {
    send_text_err(req, "400 Bad Request", "Bad body size");
    return NULL;
  }
  char *buf = malloc(len + 1);
  if (buf == NULL)
  {
    send_text_err(req, "500 Internal Server Error", "No memory");
    return NULL;
  }
  if (!recv_exact(req, buf, len))
  {
    free(buf);
    send_text_err(req, "400 Bad Request", "Receive failed");
    return NULL;
  }
  buf[len] = '\0';
  return buf;
}

// ---------------------------------------------------------------------------
// Password: Basic Auth, the login is ignored.
// ---------------------------------------------------------------------------
static int b64_val(char c)
{
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}

static size_t b64_decode(const char *in, char *out, size_t out_max)
{
  size_t n = 0;
  uint32_t acc = 0;
  int bits = 0;
  for (; *in && *in != '='; in++)
  {
    int v = b64_val(*in);
    if (v < 0) return 0;
    acc = (acc << 6) | (uint32_t)v;
    bits += 6;
    if (bits >= 8)
    {
      bits -= 8;
      if (n + 1 >= out_max) return 0;
      out[n++] = (char)(acc >> bits);
    }
  }
  out[n] = '\0';
  return n;
}

// true - go on; false - the 401 has been sent.
static bool check_auth(httpd_req_t *req)
{
  if (!lnd_auth_is_enabled())
  {
    return true;
  }
  char hdr[160];
  if (httpd_req_get_hdr_value_str(req, "Authorization", hdr, sizeof(hdr)) == ESP_OK && strncmp(hdr, "Basic ", 6) == 0)
  {
    char dec[128];
    if (b64_decode(hdr + 6, dec, sizeof(dec)) > 0)
    {
      const char *colon = strchr(dec, ':');
      if (colon && lnd_auth_check(colon + 1))
      {
        return true;
      }
    }
  }
  httpd_resp_set_status(req, "401 Unauthorized");
  httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"" LND_AUTH_REALM "\"");
  httpd_resp_send(req, NULL, 0);
  return false;
}

static esp_err_t auth_status_handler(httpd_req_t *req)
{
  return send_json(req, lnd_auth_is_enabled() ? "{\"enabled\":true}" : "{\"enabled\":false}");
}

static esp_err_t auth_verify_handler(httpd_req_t *req)
{
  if (!check_auth(req))
  {
    return ESP_OK;
  }
  return send_json(req, "{\"status\":\"ok\"}");
}

// Sets, changes or clears (empty) the settings password. While one exists the request
// itself needs the current one (check_auth); while there is none, anyone on the network
// can set the first.
static esp_err_t auth_password_handler(httpd_req_t *req)
{
  if (!check_auth(req))
  {
    return ESP_OK;
  }
  char *body = recv_body(req, 256);
  if (body == NULL)
  {
    return ESP_OK;
  }
  cJSON *root = cJSON_Parse(body);
  free(body);
  const cJSON *v = root ? cJSON_GetObjectItemCaseSensitive(root, "password") : NULL;
  if (!cJSON_IsString(v) || v->valuestring == NULL)
  {
    cJSON_Delete(root);
    return send_text_err(req, "400 Bad Request", "Invalid body");
  }
  size_t len = strlen(v->valuestring);
  if (len != 0 && (len < LND_AUTH_PASSWORD_MIN_LEN || len > LND_AUTH_PASSWORD_MAX_LEN))
  {
    cJSON_Delete(root);
    return send_text_err(req, "400 Bad Request",
                         "Password must be " LND_STR(LND_AUTH_PASSWORD_MIN_LEN) ".." LND_STR(LND_AUTH_PASSWORD_MAX_LEN) " characters");
  }
  bool ok = lnd_auth_set_password(len ? v->valuestring : NULL);
  cJSON_Delete(root);
  if (!ok)
  {
    return send_text_err(req, "500 Internal Server Error", "Save failed");
  }
  return send_json(req, lnd_auth_is_enabled() ? "{\"status\":\"success\",\"enabled\":true}"
                                              : "{\"status\":\"success\",\"enabled\":false}");
}

// ---------------------------------------------------------------------------
// OTA and /api/status
// ---------------------------------------------------------------------------
// The result of the last www update - the page reads it from /api/status.
static struct {
  bool valid, ok;
  char id[16];
  char error[48];
  uint32_t written, skipped, total_ms;
} s_www_update;
static SemaphoreHandle_t s_upd_mutex;

static esp_err_t status_handler(httpd_req_t *req)
{
  lnd_ap_info_t ap;
  lnd_webcfg_get_ap_info(&ap);
  char json[512];
  int n = snprintf(json, sizeof(json),
                   "{\"fw_version\":\"%s\",\"board\":\"%s\",\"uptime_s\":%lld,\"reset_reason\":%d,\"ap_clients\":%u,"
                   "\"heap\":{\"int_free\":%u,\"int_min\":%u,\"int_block\":%u,\"psram_free\":%u,\"psram_min\":%u}",
                   esp_app_get_description()->version, NDASH_BOARD_NAME, (long long)(esp_timer_get_time() / 1000000),
                   (int)esp_reset_reason(), (unsigned)ap.clients, (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                   (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
                   (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                   (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                   (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM));
  xSemaphoreTake(s_upd_mutex, portMAX_DELAY);
  if (s_www_update.valid)
  {
    n += snprintf(json + n, sizeof(json) - n, ",\"www_update\":{\"id\":\"%s\",\"done\":true,\"ok\":%s", s_www_update.id,
                  s_www_update.ok ? "true" : "false");
    if (!s_www_update.ok)
    {
      n += snprintf(json + n, sizeof(json) - n, ",\"error\":\"%s\"", s_www_update.error);
    }
    n += snprintf(json + n, sizeof(json) - n, ",\"blocks_written\":%u,\"blocks_skipped\":%u,\"total_ms\":%u}",
                  (unsigned)s_www_update.written, (unsigned)s_www_update.skipped, (unsigned)s_www_update.total_ms);
  }
  xSemaphoreGive(s_upd_mutex);
  snprintf(json + n, sizeof(json) - n, "}");
  return send_json(req, json);
}

static esp_err_t ota_update_handler(httpd_req_t *req)
{
  if (!check_auth(req))
  {
    return ESP_OK;
  }
  size_t total = req->content_len;
  const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);
  if (!part || total == 0 || total > part->size)
  {
    return send_text_err(req, "400 Bad Request", "Bad image size");
  }

  char *buf = malloc(OTA_CHUNK);
  if (!buf)
  {
    return send_text_err(req, "500 Internal Server Error", "No memory");
  }
  esp_ota_handle_t h = 0;
  esp_err_t err = esp_ota_begin(part, total, &h);
  if (err != ESP_OK)
  {
    free(buf);
    ESP_LOGE(TAG, "esp_ota_begin: %s", esp_err_to_name(err));
    return send_text_err(req, "500 Internal Server Error", "OTA begin failed");
  }

  const char *fail = NULL;
  for (size_t done = 0; done < total && !fail;)
  {
    size_t n = total - done < OTA_CHUNK ? total - done : OTA_CHUNK;
    if (!recv_exact(req, buf, n))
    {
      fail = "Receive failed";
    }
    else if (done == 0 && (uint8_t)buf[0] != 0xE9)
    {
      fail = "Not a firmware image";
    }
    else if (esp_ota_write(h, buf, n) != ESP_OK)
    {
      fail = "Write failed";
    }
    done += n;
  }
  free(buf);

  if (fail)
  {
    esp_ota_abort(h);
    ESP_LOGE(TAG, "OTA failed: %s", fail);
    return send_text_err(req, "400 Bad Request", fail);
  }
  if (esp_ota_end(h) != ESP_OK)
  {
    return send_text_err(req, "400 Bad Request", "Image check failed");
  }
  if (esp_ota_set_boot_partition(part) != ESP_OK)
  {
    return send_text_err(req, "500 Internal Server Error", "Set boot partition failed");
  }

  ESP_LOGW(TAG, "OTA ok (%u bytes) -> %s, rebooting", (unsigned)total, part->label);
  send_json(req, JSON_OK);
  vTaskDelay(pdMS_TO_TICKS(1000));
  esp_restart();
  return ESP_OK;
}

static void www_mount(void)
{
  if (s_boot_nonce == 0)
  {
    s_boot_nonce = esp_random() | 1;
  }
  esp_vfs_spiffs_conf_t fs = {
      .base_path = LND_WWW_BASE_PATH,
      .partition_label = LND_WWW_PARTITION,
      .max_files = 5,
      .format_if_mount_failed = false,
  };
  esp_err_t err = esp_vfs_spiffs_register(&fs);
  if (err != ESP_OK)
  {
    ESP_LOGE(TAG, "Mount %s failed: %s", LND_WWW_PARTITION, esp_err_to_name(err));
  }
}

// The whole www partition image. Blocks equal to what is already written are neither
// erased nor written: faster and kinder to the flash. The partition is mounted again
// afterwards, no restart needed.
static esp_err_t www_update_handler(httpd_req_t *req)
{
  if (!check_auth(req))
  {
    return ESP_OK;
  }
  char id[16] = "";
  char query[64];
  if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK)
  {
    httpd_query_key_value(query, "id", id, sizeof(id));
  }

  const esp_partition_t *part =
      esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, LND_WWW_PARTITION);
  size_t total = req->content_len;
  if (!part || total == 0 || total > part->size)
  {
    return send_text_err(req, "400 Bad Request", "Bad image size");
  }

  char *buf = malloc(WWW_BLOCK);
  char *cur = malloc(WWW_BLOCK);
  if (!buf || !cur)
  {
    free(buf);
    free(cur);
    return send_text_err(req, "500 Internal Server Error", "No memory");
  }

  int64_t t0 = esp_timer_get_time();
  esp_vfs_spiffs_unregister(LND_WWW_PARTITION);

  const char *fail = NULL;
  uint32_t written = 0, skipped = 0;
  for (size_t off = 0; off < total && !fail; off += WWW_BLOCK)
  {
    size_t n = total - off < WWW_BLOCK ? total - off : WWW_BLOCK;
    if (!recv_exact(req, buf, n))
    {
      fail = "Receive failed";
      break;
    }
    // The tail of a partial block is padded with 0xFF - what erased flash looks like.
    if (n < WWW_BLOCK) memset(buf + n, 0xFF, WWW_BLOCK - n);
    if (esp_partition_read(part, off, cur, WWW_BLOCK) == ESP_OK && memcmp(cur, buf, WWW_BLOCK) == 0)
    {
      skipped++;
    }
    else if (esp_partition_erase_range(part, off, WWW_BLOCK) != ESP_OK ||
             esp_partition_write(part, off, buf, WWW_BLOCK) != ESP_OK)
    {
      fail = "Write failed";
    }
    else
    {
      written++;
    }
  }
  free(buf);
  free(cur);
  www_mount();

  uint32_t ms = (uint32_t)((esp_timer_get_time() - t0) / 1000);
  xSemaphoreTake(s_upd_mutex, portMAX_DELAY);
  s_www_update.valid = id[0] != '\0';
  s_www_update.ok = fail == NULL;
  strlcpy(s_www_update.id, id, sizeof(s_www_update.id));
  strlcpy(s_www_update.error, fail ? fail : "", sizeof(s_www_update.error));
  s_www_update.written = written;
  s_www_update.skipped = skipped;
  s_www_update.total_ms = ms;
  xSemaphoreGive(s_upd_mutex);

  if (fail)
  {
    ESP_LOGE(TAG, "www update failed: %s", fail);
    return send_text_err(req, "500 Internal Server Error", fail);
  }
  ESP_LOGW(TAG, "www update ok: %u written, %u skipped, %u ms", (unsigned)written, (unsigned)skipped, (unsigned)ms);
  char json[128];
  snprintf(json, sizeof(json), "{\"status\":\"success\",\"blocks_written\":%u,\"blocks_skipped\":%u,\"total_ms\":%u}",
           (unsigned)written, (unsigned)skipped, (unsigned)ms);
  return send_json(req, json);
}

// ---------------------------------------------------------------------------
// Static files from www
// ---------------------------------------------------------------------------
static const char *mime_of(const char *path)
{
  static const struct
  {
    const char *ext, *type;
  } types[] = {
      {".html", "text/html"}, {".js", "application/javascript"}, {".css", "text/css"},
      {".woff2", "font/woff2"}, {".woff", "font/woff"},          {".json", "application/json"},
      {".png", "image/png"},   {".svg", "image/svg+xml"},        {".ico", "image/x-icon"},
  };
  const size_t plen = strlen(path);
  for (size_t i = 0; i < sizeof(types) / sizeof(types[0]); i++)
  {
    const size_t el = strlen(types[i].ext);
    if (plen > el && strcmp(path + plen - el, types[i].ext) == 0)
    {
      return types[i].type;
    }
  }
  return "application/octet-stream";
}

// Sends a file. is_www: a file of the web interface. Its text files are stored gzip-compressed
// (tools/pack_www.py, name.gz) and go out as they are; the fonts and the library styles that never change
// are cached by the browser for good, the rest is revalidated with an ETag (a 304 answer is a few bytes).
// A whole file that fits in memory goes in one piece (the browser sees its size), a big one in chunks.
static esp_err_t send_file(httpd_req_t *req, const char *path, bool is_www)
{
  const int64_t t0 = esp_timer_get_time();
  FILE *f = NULL;
  bool gz = false;
  if (is_www)
  {
    char gz_path[sizeof(LND_WWW_BASE_PATH) + 264];
    snprintf(gz_path, sizeof(gz_path), "%s.gz", path);
    f = fopen(gz_path, "rb");
    gz = f != NULL;
  }
  if (f == NULL)
  {
    f = fopen(path, "rb");
  }
  if (f == NULL)
  {
    return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not found");
  }
  fseek(f, 0, SEEK_END);
  const long size = ftell(f);
  fseek(f, 0, SEEK_SET);

  httpd_resp_set_type(req, mime_of(path));
  if (is_www)
  {
    char etag[32];
    snprintf(etag, sizeof(etag), "\"%08x-%ld\"", (unsigned)s_boot_nonce, size);
    httpd_resp_set_hdr(req, "ETag", etag);
    const bool fixed = strstr(path, ".min.") != NULL || strstr(path, ".woff") != NULL || strstr(path, ".png") != NULL;
    httpd_resp_set_hdr(req, "Cache-Control", fixed ? "public, max-age=31536000, immutable" : "no-cache");
    char inm[48];
    if (httpd_req_get_hdr_value_str(req, "If-None-Match", inm, sizeof(inm)) == ESP_OK && strcmp(inm, etag) == 0)
    {
      fclose(f);
      httpd_resp_set_status(req, "304 Not Modified");
      return httpd_resp_send(req, NULL, 0);
    }
  }
  if (gz)
  {
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
  }

  esp_err_t ret = ESP_OK;
  char *buf = size > 0 && size <= 262144 ? malloc((size_t)size) : NULL;
  if (buf != NULL && fread(buf, 1, (size_t)size, f) == (size_t)size)
  {
    ret = httpd_resp_send(req, buf, size);
  }
  else
  {
    free(buf);
    buf = malloc(4096);
    if (buf == NULL)
    {
      fclose(f);
      return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No memory");
    }
    fseek(f, 0, SEEK_SET);
    size_t r;
    while ((r = fread(buf, 1, 4096, f)) > 0)
    {
      if (httpd_resp_send_chunk(req, buf, r) != ESP_OK)
      {
        ret = ESP_FAIL;
        break;
      }
    }
    if (ret == ESP_OK)
    {
      httpd_resp_send_chunk(req, NULL, 0);
    }
  }
  free(buf);
  fclose(f);
  ESP_LOGI(TAG, "%s%s %ld B %d ms", path, gz ? ".gz" : "", size, (int)((esp_timer_get_time() - t0) / 1000));
  return ret;
}

// No Nagle on the connections of the web server: the header and the body of a reply leave in two
// pieces, and with Nagle the second one waits for the (delayed, 100+ ms on a phone) acknowledgement of
// the first - every request took 100 ms more than the data needed.
static esp_err_t nodelay_open(httpd_handle_t hd, int sockfd)
{
  (void)hd;
  const int one = 1;
  setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
  return ESP_OK;
}

static esp_err_t www_get_handler(httpd_req_t *req)
{
  char path[sizeof(LND_WWW_BASE_PATH) + 256];
  const char *uri = req->uri;
  size_t n = strcspn(uri, "?#");
  if (n == 1 && uri[0] == '/')
  {
    uri = "/index.html";
    n = strlen(uri);
  }
  if (n >= sizeof(path) - sizeof(LND_WWW_BASE_PATH) || strstr(uri, ".."))
  {
    return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not found");
  }
  snprintf(path, sizeof(path), LND_WWW_BASE_PATH "%.*s", (int)n, uri);
  return send_file(req, path, true);
}

static esp_err_t ota_page_handler(httpd_req_t *req)
{
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, LND_OTA_PAGE_HTML, HTTPD_RESP_USE_STRLEN);
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------
static esp_err_t layout_get_handler(httpd_req_t *req)
{
  return send_json_heap(req, lnd_layout_json_dup());
}

static esp_err_t layout_post_handler(httpd_req_t *req)
{
  if (!check_auth(req))
  {
    return ESP_OK;
  }
  char *body = recv_body(req, LND_LAYOUT_JSON_MAX);
  if (body == NULL)
  {
    return ESP_OK;
  }
  esp_err_t err = lnd_layout_apply_json(body);
  free(body);
  if (err == ESP_ERR_INVALID_ARG)
  {
    return send_text_err(req, "400 Bad Request", "Invalid layout");
  }
  if (err == ESP_ERR_INVALID_SIZE)
  {
    return send_text_err(req, "400 Bad Request",
                         "Too many widgets: up to " LND_STR(LND_LAYOUT_MAX_LIVE) " live ones and " LND_STR(LND_LAYOUT_MAX_DIALS) " dials per screen");
  }
  if (err == ESP_ERR_INVALID_STATE)
  {
    return send_text_err(req, "400 Bad Request", "A number or text cannot lie over a dial");
  }
  if (err != ESP_OK)
  {
    return send_text_err(req, "500 Internal Server Error", "Save failed");
  }
  return send_json(req, JSON_OK);
}

static esp_err_t layout_reset_handler(httpd_req_t *req)
{
  if (!check_auth(req))
  {
    return ESP_OK;
  }
  if (lnd_layout_reset() != ESP_OK)
  {
    return send_text_err(req, "500 Internal Server Error", "Reset failed");
  }
  return send_json(req, JSON_OK);
}

// ---------------------------------------------------------------------------
// Media: pictures of the dial backgrounds and needles
// ---------------------------------------------------------------------------
#define MEDIA_PATH_LEN (sizeof(LND_MEDIA_BASE_PATH) + 40)

// The file name from ?name=; false - none or unsafe (the reply has been sent).
static bool media_query_name(httpd_req_t *req, char *name, size_t size)
{
  char query[96];
  name[0] = '\0';
  if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
      httpd_query_key_value(query, "name", name, size) != ESP_OK || !lnd_media_name_ok(name))
  {
    send_text_err(req, "400 Bad Request", "Bad file name");
    return false;
  }
  return true;
}

// A LVGL .bin picture (12 byte header, RGB565 or ARGB8888 rows) sent N times smaller. Anything else goes as it is.
static esp_err_t send_scaled_picture(httpd_req_t *req, const char *path, int scale)
{
  FILE *f = fopen(path, "rb");
  if (f == NULL)
  {
    return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not found");
  }
  uint8_t head[12];
  if (fread(head, 1, sizeof(head), f) != sizeof(head) || head[0] != 0x19 || (head[1] != 0x12 && head[1] != 0x10 && head[1] != 0x11))
  {
    fclose(f);
    return send_file(req, path, false);
  }
  const int bpp = head[1] == 0x12 ? 2 : 4;
  const int w = head[4] | (head[5] << 8), h = head[6] | (head[7] << 8), stride = head[8] | (head[9] << 8);
  const int ow = w / scale, oh = h / scale;
  if (ow < 1 || oh < 1 || stride < w * bpp)
  {
    fclose(f);
    return send_file(req, path, false);
  }
  const size_t out_size = sizeof(head) + (size_t)ow * oh * bpp;
  uint8_t *out = malloc(out_size);
  uint8_t *row = malloc((size_t)stride);
  if (out == NULL || row == NULL)
  {
    free(out);
    free(row);
    fclose(f);
    return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No memory");
  }
  memcpy(out, head, sizeof(head));
  out[4] = (uint8_t)(ow & 0xFF);
  out[5] = (uint8_t)(ow >> 8);
  out[6] = (uint8_t)(oh & 0xFF);
  out[7] = (uint8_t)(oh >> 8);
  out[8] = (uint8_t)((ow * bpp) & 0xFF);
  out[9] = (uint8_t)((ow * bpp) >> 8);
  bool ok = true;
  for (int y = 0; y < oh * scale && ok; y++)
  {
    ok = fread(row, 1, (size_t)stride, f) == (size_t)stride;
    if (ok && y % scale == 0)
    {
      uint8_t *dst = out + sizeof(head) + (size_t)(y / scale) * ow * bpp;
      for (int x = 0; x < ow; x++)
      {
        memcpy(dst + x * bpp, row + (size_t)x * scale * bpp, (size_t)bpp);
      }
    }
  }
  fclose(f);
  free(row);
  esp_err_t ret = ok ? ESP_OK : ESP_FAIL;
  if (ok)
  {
    httpd_resp_set_type(req, "application/octet-stream");
    ret = httpd_resp_send(req, (const char *)out, (ssize_t)out_size);
  }
  else
  {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Read failed");
  }
  free(out);
  return ret;
}

static esp_err_t media_get_handler(httpd_req_t *req)
{
  // "/media/<name>" is also the file's path in the VFS
  char name[40];
  const size_t n = strcspn(req->uri, "?#");
  if (n <= sizeof(LND_MEDIA_BASE_PATH) || n - sizeof(LND_MEDIA_BASE_PATH) >= sizeof(name))
  {
    return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not found");
  }
  snprintf(name, sizeof(name), "%.*s", (int)(n - sizeof(LND_MEDIA_BASE_PATH)), req->uri + sizeof(LND_MEDIA_BASE_PATH));
  if (!lnd_media_name_ok(name))
  {
    return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not found");
  }
  char path[MEDIA_PATH_LEN];
  snprintf(path, sizeof(path), LND_MEDIA_BASE_PATH "/%s", name);
  httpd_resp_set_hdr(req, "Cache-Control", "no-cache");

  // ?s=2|4|8: the picture N times smaller (every N-th pixel of every N-th row), for the thumbnails and
  // the preview of the editor: the full 460 kB picture takes 2 s over the board's Wi-Fi.
  char query[16], sval[8];
  int scale = 1;
  if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK && httpd_query_key_value(query, "s", sval, sizeof(sval)) == ESP_OK)
  {
    scale = atoi(sval);
  }
  if (scale == 2 || scale == 4 || scale == 8)
  {
    return send_scaled_picture(req, path, scale);
  }
  // ?n=32: only the first bytes (the header of a font tells its size without loading all of it)
  if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK && httpd_query_key_value(query, "n", sval, sizeof(sval)) == ESP_OK)
  {
    const int want = atoi(sval);
    FILE *f = (want > 0 && want <= 256) ? fopen(path, "rb") : NULL;
    if (f != NULL)
    {
      char head[256];
      const size_t got = fread(head, 1, (size_t)want, f);
      fclose(f);
      httpd_resp_set_type(req, "application/octet-stream");
      return httpd_resp_send(req, head, (ssize_t)got);
    }
  }
  return send_file(req, path, false);
}

static esp_err_t media_list_handler(httpd_req_t *req)
{
  size_t total = 0, used = 0;
  esp_spiffs_info(LND_MEDIA_PARTITION, &total, &used);

  httpd_resp_set_type(req, "application/json");
  char line[128];
  snprintf(line, sizeof(line), "{\"total\":%u,\"used\":%u,\"max_file\":%d,\"files\":[", (unsigned)total, (unsigned)used,
           LND_MEDIA_MAX_FILE);
  httpd_resp_send_chunk(req, line, HTTPD_RESP_USE_STRLEN);

  DIR *d = opendir(LND_MEDIA_BASE_PATH);
  bool first = true;
  struct dirent *e;
  while (d != NULL && (e = readdir(d)) != NULL)
  {
    if (!lnd_media_name_ok(e->d_name))
    {
      continue;
    }
    char name[32];  // the check above guarantees up to 31 characters
    strlcpy(name, e->d_name, sizeof(name));
    char path[MEDIA_PATH_LEN];
    struct stat st;
    snprintf(path, sizeof(path), LND_MEDIA_BASE_PATH "/%s", name);
    if (stat(path, &st) != 0)
    {
      continue;
    }
    snprintf(line, sizeof(line), "%s{\"name\":\"%s\",\"size\":%ld}", first ? "" : ",", name, (long)st.st_size);
    first = false;
    httpd_resp_send_chunk(req, line, HTTPD_RESP_USE_STRLEN);
  }
  if (d != NULL)
  {
    closedir(d);
  }
  httpd_resp_send_chunk(req, "]}", HTTPD_RESP_USE_STRLEN);
  return httpd_resp_send_chunk(req, NULL, 0);
}

// The body is a ready LVGL .bin. It goes to a temporary file first and replaces the real
// one only when complete: a broken upload does not spoil the old picture.
static esp_err_t media_upload_handler(httpd_req_t *req)
{
  if (!check_auth(req))
  {
    return ESP_OK;
  }
  char name[40];
  if (!media_query_name(req, name, sizeof(name)))
  {
    return ESP_OK;
  }
  const size_t len = req->content_len;
  if (len < 12 || len > LND_MEDIA_MAX_FILE)
  {
    return send_text_err(req, "400 Bad Request", "Bad file size");
  }

  char path[MEDIA_PATH_LEN];
  snprintf(path, sizeof(path), LND_MEDIA_BASE_PATH "/%s", name);
  struct stat st;
  const size_t old_size = stat(path, &st) == 0 ? (size_t)st.st_size : 0;
  size_t total = 0, used = 0;
  esp_spiffs_info(LND_MEDIA_PARTITION, &total, &used);
  // SPIFFS takes more than the file (pages, bookkeeping): keep a margin
  if (total - used + old_size < len + len / 8 + 32768)
  {
    return send_text_err(req, "507 Insufficient Storage", "Not enough space");
  }

  const char *tmp_path = LND_MEDIA_BASE_PATH "/.upload";
  FILE *f = fopen(tmp_path, "wb");
  char *buf = malloc(OTA_CHUNK);
  if (f == NULL || buf == NULL)
  {
    if (f)
    {
      fclose(f);
    }
    free(buf);
    return send_text_err(req, "500 Internal Server Error", "Cannot create file");
  }
  const char *fail = NULL;
  for (size_t done = 0; done < len && !fail;)
  {
    const size_t n = len - done < OTA_CHUNK ? len - done : OTA_CHUNK;
    if (!recv_exact(req, buf, n))
    {
      fail = "Receive failed";
    }
    else if (fwrite(buf, 1, n, f) != n)
    {
      fail = "Write failed";
    }
    done += n;
  }
  free(buf);
  if (fclose(f) != 0 && !fail)
  {
    fail = "Write failed";
  }
  if (fail)
  {
    remove(tmp_path);
    return send_text_err(req, "500 Internal Server Error", fail);
  }
  remove(path);
  if (rename(tmp_path, path) != 0)
  {
    remove(tmp_path);
    return send_text_err(req, "500 Internal Server Error", "Rename failed");
  }
  ESP_LOGW(TAG, "media: %s, %u bytes", name, (unsigned)len);
  lnd_layout_touch();  // the screen picks up the new picture
  return send_json(req, JSON_OK);
}

static esp_err_t media_delete_handler(httpd_req_t *req)
{
  if (!check_auth(req))
  {
    return ESP_OK;
  }
  char name[40];
  if (!media_query_name(req, name, sizeof(name)))
  {
    return ESP_OK;
  }
  char path[MEDIA_PATH_LEN];
  snprintf(path, sizeof(path), LND_MEDIA_BASE_PATH "/%s", name);
  if (remove(path) != 0)
  {
    return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not found");
  }
  lnd_layout_touch();
  return send_json(req, JSON_OK);
}

// ---------------------------------------------------------------------------
// Bench: the live signals, values pinned by hand, the demo generator, a CAN frame sent by hand
// ---------------------------------------------------------------------------
static esp_err_t bench_get_handler(httpd_req_t *req)
{
  httpd_resp_set_type(req, "application/json");
  char head[64];
  snprintf(head, sizeof(head), "{\"demo\":%s,\"hold_ms\":%d,\"signals\":[", lnd_signal_sim_is_enabled() ? "true" : "false",
           LND_BENCH_HOLD_MS);
  httpd_resp_send_chunk(req, head, HTTPD_RESP_USE_STRLEN);
  for (int i = 0; i < lnd_signal_count(); i++)
  {
    lnd_signal_info_t s;
    if (!lnd_signal_info(i, &s))
    {
      break;
    }
    char row[160];
    int n = snprintf(row, sizeof(row), "%s{\"name\":\"%s\",\"min\":%g,\"max\":%g,\"held\":%s,\"age\":%u", i ? "," : "", s.name,
                     (double)s.min, (double)s.max, s.held ? "true" : "false", (unsigned)s.age_ms);
    n += s.valid ? snprintf(row + n, sizeof(row) - n, ",\"value\":%g}", (double)s.value)
                 : snprintf(row + n, sizeof(row) - n, ",\"value\":null}");
    httpd_resp_send_chunk(req, row, n);
  }
  httpd_resp_send_chunk(req, "]}", HTTPD_RESP_USE_STRLEN);
  return httpd_resp_send_chunk(req, NULL, 0);
}

// The bench commands table (the buttons of the page).
static esp_err_t bench_cmds_get_handler(httpd_req_t *req)
{
  return send_json_heap(req, lnd_bench_cmds_json_dup());
}

static esp_err_t bench_cmds_post_handler(httpd_req_t *req)
{
  if (!check_auth(req))
  {
    return ESP_OK;
  }
  char *body = recv_body(req, LND_BENCH_CMDS_JSON_MAX);
  if (body == NULL)
  {
    return ESP_OK;
  }
  const esp_err_t err = lnd_bench_cmds_apply_json(body);
  free(body);
  if (err == ESP_ERR_INVALID_ARG)
  {
    return send_text_err(req, "400 Bad Request", "Invalid command table");
  }
  return err == ESP_OK ? send_json(req, JSON_OK) : send_text_err(req, "500 Internal Server Error", "Save failed");
}

static esp_err_t bench_cmds_reset_handler(httpd_req_t *req)
{
  if (!check_auth(req))
  {
    return ESP_OK;
  }
  if (lnd_bench_cmds_reset() != ESP_OK)
  {
    return send_text_err(req, "500 Internal Server Error", "Reset failed");
  }
  return send_json(req, JSON_OK);
}

// {"name":"fan1"}: sends the command. A "stopped only" command is refused while the engine runs.
static esp_err_t bench_send_handler(httpd_req_t *req)
{
  if (!check_auth(req))
  {
    return ESP_OK;
  }
  char *body = recv_body(req, 96);
  if (body == NULL)
  {
    return ESP_OK;
  }
  cJSON *root = cJSON_Parse(body);
  free(body);
  const cJSON *name = root ? cJSON_GetObjectItemCaseSensitive(root, "name") : NULL;
  lnd_bench_cmd_t cmd;
  const bool found = cJSON_IsString(name) && lnd_bench_cmd_find(name->valuestring, &cmd);
  cJSON_Delete(root);
  if (!found)
  {
    return send_text_err(req, "404 Not Found", "Unknown command");
  }
  float rpm = 0;
  if (cmd.stopped_only && lnd_signal_get(lnd_signal_find("rpm"), &rpm) && rpm > LND_BENCH_RUNNING_RPM)
  {
    return send_text_err(req, "409 Conflict", "The engine is running");
  }
  uint8_t data[8];
  const uint8_t dlc = lnd_bench_cmd_frame(&cmd, data);
  const bool sent = lnd_can_send(cmd.id, data, dlc, cmd.ext);
  ESP_LOGW(TAG, "bench %s -> %s", cmd.name, sent ? "sent" : "not sent");
  return send_json(req, sent ? "{\"sent\":true}" : "{\"sent\":false}");
}

// {"name":"rpm","value":3000} pins the value; {"name":"rpm","release":true} gives it back.
static esp_err_t bench_hold_handler(httpd_req_t *req)
{
  if (!check_auth(req))
  {
    return ESP_OK;
  }
  char *body = recv_body(req, 160);
  if (body == NULL)
  {
    return ESP_OK;
  }
  cJSON *root = cJSON_Parse(body);
  free(body);
  const cJSON *name = root ? cJSON_GetObjectItemCaseSensitive(root, "name") : NULL;
  const cJSON *value = root ? cJSON_GetObjectItemCaseSensitive(root, "value") : NULL;
  const cJSON *release = root ? cJSON_GetObjectItemCaseSensitive(root, "release") : NULL;
  const int id = cJSON_IsString(name) ? lnd_signal_find(name->valuestring) : -1;
  if (id < 0 || (!cJSON_IsNumber(value) && !cJSON_IsTrue(release)))
  {
    cJSON_Delete(root);
    return send_text_err(req, "400 Bad Request", "Unknown signal or no value");
  }
  if (cJSON_IsTrue(release))
  {
    lnd_signal_release(id);
  }
  else
  {
    lnd_signal_hold(id, (float)value->valuedouble, LND_BENCH_HOLD_MS);
  }
  cJSON_Delete(root);
  return send_json(req, JSON_OK);
}

static esp_err_t bench_demo_handler(httpd_req_t *req)
{
  if (!check_auth(req))
  {
    return ESP_OK;
  }
  char *body = recv_body(req, 64);
  if (body == NULL)
  {
    return ESP_OK;
  }
  cJSON *root = cJSON_Parse(body);
  free(body);
  const cJSON *on = root ? cJSON_GetObjectItemCaseSensitive(root, "on") : NULL;
  if (!cJSON_IsBool(on))
  {
    cJSON_Delete(root);
    return send_text_err(req, "400 Bad Request", "\"on\" must be true or false");
  }
  lnd_signal_sim_enable(cJSON_IsTrue(on));  // until the next restart; the saved setting is on the CAN page
  cJSON_Delete(root);
  return send_json(req, JSON_OK);
}

// {"id":1816,"ext":false,"data":"0011223344556677"} - up to 8 bytes as hex.
static esp_err_t bench_can_handler(httpd_req_t *req)
{
  if (!check_auth(req))
  {
    return ESP_OK;
  }
  char *body = recv_body(req, 160);
  if (body == NULL)
  {
    return ESP_OK;
  }
  cJSON *root = cJSON_Parse(body);
  free(body);
  const cJSON *id = root ? cJSON_GetObjectItemCaseSensitive(root, "id") : NULL;
  const cJSON *ext = root ? cJSON_GetObjectItemCaseSensitive(root, "ext") : NULL;
  const cJSON *data = root ? cJSON_GetObjectItemCaseSensitive(root, "data") : NULL;
  const bool is_ext = cJSON_IsTrue(ext);
  const size_t hex_len = cJSON_IsString(data) ? strlen(data->valuestring) : 1;
  if (!cJSON_IsNumber(id) || id->valuedouble < 0 || id->valuedouble > (is_ext ? 0x1FFFFFFF : 0x7FF) || hex_len % 2 != 0 ||
      hex_len > 16)
  {
    cJSON_Delete(root);
    return send_text_err(req, "400 Bad Request", "id (11 or 29 bit) and data (up to 16 hex digits) needed");
  }
  uint8_t bytes[8] = {0};
  const uint8_t dlc = (uint8_t)(hex_len / 2);
  for (uint8_t i = 0; i < dlc; i++)
  {
    char pair[3] = {data->valuestring[2 * i], data->valuestring[2 * i + 1], 0};
    char *end = NULL;
    bytes[i] = (uint8_t)strtoul(pair, &end, 16);
    if (end == NULL || *end != '\0')
    {
      cJSON_Delete(root);
      return send_text_err(req, "400 Bad Request", "data is not hex");
    }
  }
  const uint32_t can_id = (uint32_t)id->valuedouble;
  cJSON_Delete(root);
  const bool sent = lnd_can_send(can_id, bytes, dlc, is_ext);
  return send_json(req, sent ? "{\"sent\":true}" : "{\"sent\":false}");
}

// ---------------------------------------------------------------------------
// Logs: the recording to the microSD card (tasks/logger)
// ---------------------------------------------------------------------------
static const char *const LOG_STATES[] = {"no_card", "idle", "recording"};
static const char *const LOG_ERRORS[] = {"", "mount", "open", "write", "full", "no_signals"};
static const char *const LOG_MODES[] = {"manual", "boot", "condition"};

static esp_err_t log_get_handler(httpd_req_t *req)
{
  lnd_log_status_t st;
  lnd_logger_get_status(&st);
  lnd_log_settings_t cfg;
  lnd_settings_get_log(&cfg);

  cJSON *root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "state", LOG_STATES[(int)st.state <= 2 ? (int)st.state : 0]);
  cJSON_AddStringToObject(root, "error", LOG_ERRORS[(int)st.error <= 5 ? (int)st.error : 0]);
  cJSON_AddStringToObject(root, "file", st.file);
  cJSON_AddNumberToObject(root, "records", st.records);
  cJSON_AddNumberToObject(root, "bytes", st.bytes);
  cJSON_AddNumberToObject(root, "seconds", st.seconds);
  cJSON_AddNumberToObject(root, "total_kb", (double)st.total_kb);
  cJSON_AddNumberToObject(root, "free_kb", (double)st.free_kb);
  cJSON_AddStringToObject(root, "mode", LOG_MODES[(int)cfg.mode <= 2 ? (int)cfg.mode : 0]);
  cJSON_AddStringToObject(root, "signal", cfg.signal);
  cJSON_AddStringToObject(root, "op", cfg.less ? "<" : ">");
  cJSON_AddNumberToObject(root, "value", cfg.value);
  cJSON_AddNumberToObject(root, "stop_delay", cfg.stop_delay);
  char *json = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if (json == NULL)
  {
    return send_text_err(req, "500 Internal Server Error", "No memory");
  }
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  const esp_err_t ret = send_json(req, json);
  cJSON_free(json);
  return ret;
}

// Fields missing from the request keep their values.
static esp_err_t log_post_handler(httpd_req_t *req)
{
  if (!check_auth(req))
  {
    return ESP_OK;
  }
  char *body = recv_body(req, 256);
  if (body == NULL)
  {
    return ESP_OK;
  }
  cJSON *root = cJSON_Parse(body);
  free(body);
  if (root == NULL)
  {
    return send_text_err(req, "400 Bad Request", "Bad JSON");
  }
  lnd_log_settings_t cfg;
  lnd_settings_get_log(&cfg);

  const cJSON *m = cJSON_GetObjectItemCaseSensitive(root, "mode");
  if (cJSON_IsString(m) && m->valuestring != NULL)
  {
    int mode = -1;
    for (int i = 0; i < 3; i++)
    {
      if (strcmp(m->valuestring, LOG_MODES[i]) == 0)
      {
        mode = i;
      }
    }
    if (mode < 0)
    {
      cJSON_Delete(root);
      return send_text_err(req, "400 Bad Request", "Mode must be manual, boot or condition");
    }
    cfg.mode = (lnd_log_mode_t)mode;
  }
  const cJSON *sg = cJSON_GetObjectItemCaseSensitive(root, "signal");
  if (cJSON_IsString(sg) && sg->valuestring != NULL)
  {
    if (strlen(sg->valuestring) > LND_LOG_SIGNAL_MAX_LEN)
    {
      cJSON_Delete(root);
      return send_text_err(req, "400 Bad Request", "Signal name too long");
    }
    strlcpy(cfg.signal, sg->valuestring, sizeof(cfg.signal));
  }
  const cJSON *op = cJSON_GetObjectItemCaseSensitive(root, "op");
  if (cJSON_IsString(op) && op->valuestring != NULL)
  {
    cfg.less = strcmp(op->valuestring, "<") == 0;
  }
  const cJSON *v = cJSON_GetObjectItemCaseSensitive(root, "value");
  if (cJSON_IsNumber(v))
  {
    cfg.value = (float)v->valuedouble;
  }
  const cJSON *d = cJSON_GetObjectItemCaseSensitive(root, "stop_delay");
  if (cJSON_IsNumber(d))
  {
    if (d->valuedouble < 0 || d->valuedouble > 3600)
    {
      cJSON_Delete(root);
      return send_text_err(req, "400 Bad Request", "stop_delay must be 0..3600");
    }
    cfg.stop_delay = (uint16_t)d->valuedouble;
  }
  cJSON_Delete(root);

  // The signal of the condition has to be in the layout, else the signal table would get a stray entry.
  if (cfg.mode == LND_LOG_CONDITION)
  {
    lnd_layout_t *layout = heap_caps_malloc(sizeof(*layout), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (layout == NULL)
    {
      return send_text_err(req, "500 Internal Server Error", "No memory");
    }
    lnd_layout_copy(layout);
    const bool known = lnd_layout_find_signal(layout, cfg.signal) != NULL;
    free(layout);
    if (!known)
    {
      return send_text_err(req, "400 Bad Request", "Condition signal is not in the layout");
    }
  }
  if (lnd_settings_set_log(&cfg) != ESP_OK)
  {
    return send_text_err(req, "500 Internal Server Error", "Save failed");
  }
  return send_json(req, JSON_OK);
}

static esp_err_t log_start_handler(httpd_req_t *req)
{
  if (!check_auth(req))
  {
    return ESP_OK;
  }
  lnd_logger_request_start();
  return send_json(req, JSON_OK);
}

static esp_err_t log_stop_handler(httpd_req_t *req)
{
  if (!check_auth(req))
  {
    return ESP_OK;
  }
  lnd_logger_request_stop();
  return send_json(req, JSON_OK);
}

static esp_err_t log_rescan_handler(httpd_req_t *req)
{
  lnd_logger_request_rescan();
  return send_json(req, JSON_OK);
}

// The page sends the time of the browser: the board has no clock with its own power, and the files need
// a time (the date in the list and the stamp in the log header). An open request: otherwise the files
// would not be dated until the password is entered.
static esp_err_t log_time_handler(httpd_req_t *req)
{
  char *body = recv_body(req, 64);
  if (body == NULL)
  {
    return ESP_OK;
  }
  cJSON *root = cJSON_Parse(body);
  free(body);
  const cJSON *u = root ? cJSON_GetObjectItemCaseSensitive(root, "unix") : NULL;
  const bool ok = cJSON_IsNumber(u) && u->valuedouble > 1700000000.0 && u->valuedouble < 4100000000.0;
  if (ok)
  {
    const struct timeval tv = {.tv_sec = (time_t)u->valuedouble, .tv_usec = 0};
    settimeofday(&tv, NULL);
  }
  cJSON_Delete(root);
  return ok ? send_json(req, JSON_OK) : send_text_err(req, "400 Bad Request", "Bad time");
}

static int name_cmp_desc(const void *a, const void *b)
{
  return strcmp(*(const char *const *)b, *(const char *const *)a);
}

static esp_err_t log_files_handler(httpd_req_t *req)
{
  const char *root_dir = lnd_logger_root();
  if (root_dir == NULL)
  {
    return send_text_err(req, "409 Conflict", "No SD card");
  }
  DIR *d = opendir(root_dir);
  if (d == NULL)
  {
    lnd_logger_card_lost();
    return send_text_err(req, "409 Conflict", "SD card not readable");
  }
  // The names first (newest first), then sizes and dates: a stat of every file is a trip to the card.
  enum { MAX_FILES = 200 };
  char(*names)[16] = malloc(sizeof(*names) * MAX_FILES);
  const char **order = malloc(sizeof(*order) * MAX_FILES);
  if (names == NULL || order == NULL)
  {
    free(names);
    free(order);
    closedir(d);
    return send_text_err(req, "500 Internal Server Error", "No memory");
  }
  int n = 0;
  struct dirent *e;
  while (n < MAX_FILES && (e = readdir(d)) != NULL)
  {
    if (lnd_logger_valid_name(e->d_name))
    {
      strlcpy(names[n], e->d_name, sizeof(names[n]));
      order[n] = names[n];
      n++;
    }
  }
  closedir(d);
  qsort(order, n, sizeof(order[0]), name_cmp_desc);

  cJSON *arr = cJSON_CreateArray();
  for (int i = 0; i < n; i++)
  {
    char path[48];
    snprintf(path, sizeof(path), "%s/%s", root_dir, order[i]);
    struct stat sb;
    if (stat(path, &sb) != 0)
    {
      continue;
    }
    cJSON *o = cJSON_CreateObject();
    char lower[16];
    strlcpy(lower, order[i], sizeof(lower));
    for (char *c = lower; *c; c++)
    {
      if (*c >= 'A' && *c <= 'Z')
      {
        *c = (char)(*c - 'A' + 'a');
      }
    }
    cJSON_AddStringToObject(o, "name", lower);
    cJSON_AddNumberToObject(o, "size", (double)sb.st_size);
    cJSON_AddNumberToObject(o, "time", (double)sb.st_mtime);
    cJSON_AddItemToArray(arr, o);
  }
  free(names);
  free(order);
  cJSON *root = cJSON_CreateObject();
  cJSON_AddItemToObject(root, "files", arr);
  char *json = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if (json == NULL)
  {
    return send_text_err(req, "500 Internal Server Error", "No memory");
  }
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  const esp_err_t ret = send_json(req, json);
  cJSON_free(json);
  return ret;
}

// GET /log/log_0001.mlg - download as a file.
static esp_err_t log_download_handler(httpd_req_t *req)
{
  const char *root_dir = lnd_logger_root();
  const char *name = req->uri + strlen("/log/");
  char clean[16];
  const size_t n = strcspn(name, "?#");
  if (root_dir == NULL)
  {
    return send_text_err(req, "409 Conflict", "No SD card");
  }
  if (n == 0 || n >= sizeof(clean))
  {
    return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not found");
  }
  memcpy(clean, name, n);
  clean[n] = '\0';
  if (!lnd_logger_valid_name(clean))
  {
    return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not found");
  }
  char path[48];
  snprintf(path, sizeof(path), "%s/%s", root_dir, clean);
  FILE *f = fopen(path, "rb");
  if (f == NULL)
  {
    return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not found");
  }
  char *buf = malloc(2048);
  if (buf == NULL)
  {
    fclose(f);
    return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No memory");
  }
  char disp[64];
  snprintf(disp, sizeof(disp), "attachment; filename=\"%s\"", clean);
  httpd_resp_set_type(req, "application/octet-stream");
  httpd_resp_set_hdr(req, "Content-Disposition", disp);
  esp_err_t ret = ESP_OK;
  size_t r;
  while ((r = fread(buf, 1, 2048, f)) > 0)
  {
    if (httpd_resp_send_chunk(req, buf, r) != ESP_OK)
    {
      ret = ESP_FAIL;
      break;
    }
  }
  free(buf);
  fclose(f);
  if (ret == ESP_OK)
  {
    httpd_resp_send_chunk(req, NULL, 0);
  }
  return ret;
}

static esp_err_t log_delete_handler(httpd_req_t *req)
{
  if (!check_auth(req))
  {
    return ESP_OK;
  }
  const char *root_dir = lnd_logger_root();
  char name[24];
  char query[64];
  if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
      httpd_query_key_value(query, "name", name, sizeof(name)) != ESP_OK || !lnd_logger_valid_name(name))
  {
    return send_text_err(req, "400 Bad Request", "Bad file name");
  }
  if (root_dir == NULL)
  {
    return send_text_err(req, "409 Conflict", "No SD card");
  }
  lnd_log_status_t st;
  lnd_logger_get_status(&st);
  if (st.state == LND_LOG_RECORDING && strcasecmp(st.file, name) == 0)
  {
    return send_text_err(req, "409 Conflict", "File is being recorded");
  }
  char path[48];
  snprintf(path, sizeof(path), "%s/%s", root_dir, name);
  if (unlink(path) != 0)
  {
    return send_text_err(req, "404 Not Found", "File not found");
  }
  return send_json(req, JSON_OK);
}

// ---------------------------------------------------------------------------
// CAN: settings, table, sniffer
// ---------------------------------------------------------------------------
static esp_err_t can_get_handler(httpd_req_t *req)
{
  lnd_can_settings_t c;
  lnd_settings_get_can(&c);
  lnd_can_status_t st;
  lnd_can_get_status(&st);
  char json[384];
  snprintf(json, sizeof(json),
           "{\"bitrate\":%u,\"mode\":\"%s\",\"heartbeat\":%s,\"demo\":%s,\"node_up\":%s,\"state\":%u,\"rx\":%u,"
           "\"dropped\":%u,\"tx_err\":%u,\"rx_err\":%u,\"bus_err\":%u,\"busoff\":%u}",
           (unsigned)c.bitrate, c.mode == LND_CAN_MODE_LISTEN_ONLY ? "listen_only" : "normal",
           c.heartbeat ? "true" : "false", c.demo ? "true" : "false", st.up ? "true" : "false", (unsigned)st.state,
           (unsigned)st.rx_frames, (unsigned)st.rx_dropped, (unsigned)st.tx_errors, (unsigned)st.rx_errors,
           (unsigned)st.bus_errors, (unsigned)st.bus_off_count);
  return send_json(req, json);
}

// Fields missing from the request keep their values.
static esp_err_t can_post_handler(httpd_req_t *req)
{
  if (!check_auth(req))
  {
    return ESP_OK;
  }
  char *body = recv_body(req, 256);
  if (body == NULL)
  {
    return ESP_OK;
  }
  cJSON *root = cJSON_Parse(body);
  free(body);
  if (root == NULL)
  {
    return send_text_err(req, "400 Bad Request", "Invalid JSON");
  }
  lnd_can_settings_t c;
  lnd_settings_get_can(&c);
  const cJSON *v = cJSON_GetObjectItemCaseSensitive(root, "bitrate");
  if (cJSON_IsNumber(v))
  {
    if (v->valuedouble < 10000 || v->valuedouble > 1000000)
    {
      cJSON_Delete(root);
      return send_text_err(req, "400 Bad Request", "Bitrate must be 10000..1000000");
    }
    c.bitrate = (uint32_t)v->valuedouble;
  }
  v = cJSON_GetObjectItemCaseSensitive(root, "mode");
  if (cJSON_IsString(v) && v->valuestring != NULL)
  {
    if (strcmp(v->valuestring, "listen_only") == 0)
    {
      c.mode = LND_CAN_MODE_LISTEN_ONLY;
    }
    else if (strcmp(v->valuestring, "normal") == 0)
    {
      c.mode = LND_CAN_MODE_NORMAL;
    }
    else
    {
      cJSON_Delete(root);
      return send_text_err(req, "400 Bad Request", "Unknown mode");
    }
  }
  v = cJSON_GetObjectItemCaseSensitive(root, "heartbeat");
  if (cJSON_IsBool(v))
  {
    c.heartbeat = cJSON_IsTrue(v);
  }
  v = cJSON_GetObjectItemCaseSensitive(root, "demo");
  if (cJSON_IsBool(v))
  {
    c.demo = cJSON_IsTrue(v);
  }
  cJSON_Delete(root);

  if (lnd_settings_set_can(&c) != ESP_OK)
  {
    return send_text_err(req, "500 Internal Server Error", "Save failed");
  }
  return send_json(req, JSON_OK);  // can_task picks the change up on its own
}

static esp_err_t can_map_get_handler(httpd_req_t *req)
{
  return send_json_heap(req, lnd_can_map_json_dup());
}

static esp_err_t can_map_post_handler(httpd_req_t *req)
{
  if (!check_auth(req))
  {
    return ESP_OK;
  }
  char *body = recv_body(req, LND_CAN_MAP_JSON_MAX);
  if (body == NULL)
  {
    return ESP_OK;
  }
  esp_err_t err = lnd_can_map_apply_json(body);
  free(body);
  if (err == ESP_ERR_INVALID_ARG)
  {
    return send_text_err(req, "400 Bad Request", "Invalid CAN table");
  }
  if (err != ESP_OK)
  {
    return send_text_err(req, "500 Internal Server Error", "Save failed");
  }
  return send_json(req, JSON_OK);
}

// Back to the rusEFI preset (erases the saved table).
static esp_err_t can_map_reset_handler(httpd_req_t *req)
{
  if (!check_auth(req))
  {
    return ESP_OK;
  }
  if (lnd_can_map_reset() != ESP_OK)
  {
    return send_text_err(req, "500 Internal Server Error", "Reset failed");
  }
  return send_json(req, JSON_OK);
}

// Sniffer: the last frame of each ID. The request itself turns on reception of other
// IDs for LND_CAN_SNIFFER_ACTIVE_MS, so the first reply may be empty.
static esp_err_t can_frames_handler(httpd_req_t *req)
{
  lnd_can_sniff_frame_t *fr = malloc(sizeof(*fr) * LND_CAN_SNIFFER_MAX);
  if (fr == NULL)
  {
    return send_text_err(req, "500 Internal Server Error", "No memory");
  }
  size_t n = lnd_can_sniffer_get(fr, LND_CAN_SNIFFER_MAX);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_send_chunk(req, "[", 1);
  for (size_t i = 0; i < n; i++)
  {
    char hex[17] = "";
    for (int b = 0; b < fr[i].length && b < 8; b++)
    {
      snprintf(hex + b * 2, 3, "%02x", fr[i].data[b]);
    }
    char row[128];
    int len = snprintf(row, sizeof(row), "%s{\"id\":%u,\"ext\":%s,\"dlc\":%u,\"data\":\"%s\",\"count\":%u,\"age\":%u}",
                       i ? "," : "", (unsigned)fr[i].id, fr[i].ext ? "true" : "false", (unsigned)fr[i].length, hex,
                       (unsigned)fr[i].count, (unsigned)fr[i].age_ms);
    httpd_resp_send_chunk(req, row, len);
  }
  free(fr);
  httpd_resp_send_chunk(req, "]", 1);
  return httpd_resp_send_chunk(req, NULL, 0);
}

// ---------------------------------------------------------------------------
// Brightness and the Wi-Fi network
// ---------------------------------------------------------------------------
static esp_err_t display_get_handler(httpd_req_t *req)
{
  lnd_display_settings_t d;
  lnd_settings_get_display(&d);
  char json[160];
  snprintf(json, sizeof(json), "{\"brightness\":%u,\"has_brightness\":%s,\"baro\":\"%s\",\"boost\":\"%s\",\"button_pin\":%u}",
           (unsigned)d.brightness, LND_HAS_BRIGHTNESS ? "true" : "false", d.baro == LND_BARO_INHG ? "inhg" : "kpa",
           d.boost == LND_BOOST_PSI ? "psi" : "bar", (unsigned)d.button_pin);
  return send_json(req, json);
}

static esp_err_t display_post_handler(httpd_req_t *req)
{
  if (!check_auth(req))
  {
    return ESP_OK;
  }
  char *body = recv_body(req, 128);
  if (body == NULL)
  {
    return ESP_OK;
  }
  cJSON *root = cJSON_Parse(body);
  free(body);
  // Any of the fields: the brightness and the pressure units are set separately.
  lnd_display_settings_t d;
  lnd_settings_get_display(&d);
  const cJSON *v = root ? cJSON_GetObjectItemCaseSensitive(root, "brightness") : NULL;
  const cJSON *baro = root ? cJSON_GetObjectItemCaseSensitive(root, "baro") : NULL;
  const cJSON *boost = root ? cJSON_GetObjectItemCaseSensitive(root, "boost") : NULL;
  const cJSON *pin = root ? cJSON_GetObjectItemCaseSensitive(root, "button_pin") : NULL;
  const bool brightness_given = v != NULL;
  if (v == NULL && baro == NULL && boost == NULL && pin == NULL)
  {
    cJSON_Delete(root);
    return send_text_err(req, "400 Bad Request", "Nothing to set");
  }
  if (v != NULL)
  {
    if (!cJSON_IsNumber(v) || v->valuedouble < LND_BRIGHTNESS_MIN_PCT || v->valuedouble > 100)
    {
      cJSON_Delete(root);
      return send_text_err(req, "400 Bad Request", "Brightness must be " LND_STR(LND_BRIGHTNESS_MIN_PCT) "..100");
    }
    d.brightness = (uint8_t)v->valuedouble;
  }
  if (baro != NULL)
  {
    if (cJSON_IsString(baro) && strcmp(baro->valuestring, "kpa") == 0) d.baro = LND_BARO_KPA;
    else if (cJSON_IsString(baro) && strcmp(baro->valuestring, "inhg") == 0) d.baro = LND_BARO_INHG;
    else
    {
      cJSON_Delete(root);
      return send_text_err(req, "400 Bad Request", "baro must be kpa or inhg");
    }
  }
  if (boost != NULL)
  {
    if (cJSON_IsString(boost) && strcmp(boost->valuestring, "bar") == 0) d.boost = LND_BOOST_BAR;
    else if (cJSON_IsString(boost) && strcmp(boost->valuestring, "psi") == 0) d.boost = LND_BOOST_PSI;
    else
    {
      cJSON_Delete(root);
      return send_text_err(req, "400 Bad Request", "boost must be bar or psi");
    }
  }
  if (pin != NULL)
  {
    if (!cJSON_IsNumber(pin) || pin->valuedouble != (int)pin->valuedouble || pin->valuedouble < 0 || pin->valuedouble > 48 ||
        lnd_board_pin_reserved((int)pin->valuedouble))
    {
      cJSON_Delete(root);
      return send_text_err(req, "400 Bad Request", "This GPIO is used by the board or the chip (panel, touch, CAN, SD, flash, USB)");
    }
    d.button_pin = (uint8_t)pin->valuedouble;
  }
  cJSON_Delete(root);
  if (lnd_settings_set_display(&d) != ESP_OK)
  {
    return send_text_err(req, "500 Internal Server Error", "Save failed");
  }
  if (brightness_given && LND_HAS_BRIGHTNESS)
  {
    lnd_board_backlight_set_pct(d.brightness);  // at once, no restart
  }
  return send_json(req, JSON_OK);
}

// The default name: the prefix and the end of the MAC - every dash has its own.
static void default_ap_ssid(char *out, size_t n)
{
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
  snprintf(out, n, LND_WIFI_AP_SSID_PREFIX "%02X%02X", mac[4], mac[5]);
}

// The network password is not given out: reading is open and the password is what
// protects the network.
static esp_err_t wifi_get_handler(httpd_req_t *req)
{
  lnd_wifi_settings_t w;
  lnd_settings_get_wifi(&w);
  char def[LND_WIFI_AP_SSID_MAX_LEN + 1];
  default_ap_ssid(def, sizeof(def));
  cJSON *root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "ssid", w.ssid[0] ? w.ssid : def);
  cJSON_AddStringToObject(root, "default_ssid", def);
  cJSON_AddBoolToObject(root, "custom_ssid", w.ssid[0] != '\0');
  cJSON_AddBoolToObject(root, "password_default", strcmp(w.password, LND_WIFI_AP_PASS_DEFAULT) == 0);
  char *json = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  return send_json_heap(req, json);
}

// The Wi-Fi can only be changed with a settings password set: otherwise anyone on the
// access point could change the network's own password. Takes effect after a restart.
static esp_err_t wifi_post_handler(httpd_req_t *req)
{
  if (!lnd_auth_is_enabled())
  {
    return send_text_err(req, "403 Forbidden", "Set a settings password first");
  }
  if (!check_auth(req))
  {
    return ESP_OK;
  }
  char *body = recv_body(req, 512);
  if (body == NULL)
  {
    return ESP_OK;
  }
  cJSON *root = cJSON_Parse(body);
  free(body);
  const cJSON *ssid = root ? cJSON_GetObjectItemCaseSensitive(root, "ssid") : NULL;
  const cJSON *pass = root ? cJSON_GetObjectItemCaseSensitive(root, "password") : NULL;
  if (!cJSON_IsString(pass) || pass->valuestring == NULL || (ssid != NULL && !cJSON_IsString(ssid)))
  {
    cJSON_Delete(root);
    return send_text_err(req, "400 Bad Request", "Invalid body");
  }
  lnd_wifi_settings_t w = {0};
  const char *name = ssid ? ssid->valuestring : "";
  size_t plen = strlen(pass->valuestring);
  if (strlen(name) > LND_WIFI_AP_SSID_MAX_LEN || plen < LND_WIFI_AP_PASSWORD_MIN_LEN || plen > LND_WIFI_AP_PASSWORD_MAX_LEN)
  {
    cJSON_Delete(root);
    return send_text_err(req, "400 Bad Request",
                         "SSID up to " LND_STR(LND_WIFI_AP_SSID_MAX_LEN) " bytes, password " LND_STR(LND_WIFI_AP_PASSWORD_MIN_LEN) ".." LND_STR(LND_WIFI_AP_PASSWORD_MAX_LEN) " characters");
  }
  strlcpy(w.ssid, name, sizeof(w.ssid));
  strlcpy(w.password, pass->valuestring, sizeof(w.password));
  cJSON_Delete(root);
  if (lnd_settings_set_wifi(&w) != ESP_OK)
  {
    return send_text_err(req, "400 Bad Request", "Invalid Wi-Fi settings");
  }
  ESP_LOGI(TAG, "Wi-Fi settings saved, they apply after a restart");
  return send_json(req, "{\"status\":\"success\",\"restart\":true}");
}

// ---------------------------------------------------------------------------
// HTTP server
// ---------------------------------------------------------------------------
static void start_http_server(void)
{
  s_upd_mutex = xSemaphoreCreateMutex();
  www_mount();

  httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
  cfg.stack_size = LND_HTTPD_STACK;
  cfg.task_priority = LND_HTTPD_PRIORITY;
  cfg.core_id = LND_HTTPD_CORE;
  cfg.uri_match_fn = httpd_uri_match_wildcard;
  cfg.max_uri_handlers = 64;
  cfg.recv_wait_timeout = 10;
  cfg.open_fn = nodelay_open;

  httpd_handle_t server = NULL;
  esp_err_t err = httpd_start(&server, &cfg);
  if (err != ESP_OK)
  {
    ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(err));
    return;
  }
  // The specific handlers go before the "/*" pattern.
  static const httpd_uri_t uris[] = {
      {.uri = "/ota", .method = HTTP_GET, .handler = ota_page_handler},
      {.uri = "/api/status", .method = HTTP_GET, .handler = status_handler},
      {.uri = "/api/auth/status", .method = HTTP_GET, .handler = auth_status_handler},
      {.uri = "/api/auth/verify", .method = HTTP_POST, .handler = auth_verify_handler},
      {.uri = "/api/auth/password", .method = HTTP_POST, .handler = auth_password_handler},
      {.uri = "/api/ota/update", .method = HTTP_POST, .handler = ota_update_handler},
      {.uri = "/api/www/update", .method = HTTP_POST, .handler = www_update_handler},
      {.uri = "/api/layout", .method = HTTP_GET, .handler = layout_get_handler},
      {.uri = "/api/layout", .method = HTTP_POST, .handler = layout_post_handler},
      {.uri = "/api/layout/reset", .method = HTTP_POST, .handler = layout_reset_handler},
      {.uri = "/api/media", .method = HTTP_GET, .handler = media_list_handler},
      {.uri = "/api/media", .method = HTTP_POST, .handler = media_upload_handler},
      {.uri = "/api/media/delete", .method = HTTP_POST, .handler = media_delete_handler},
      {.uri = "/media/*", .method = HTTP_GET, .handler = media_get_handler},
      {.uri = "/api/bench", .method = HTTP_GET, .handler = bench_get_handler},
      {.uri = "/api/bench/cmds", .method = HTTP_GET, .handler = bench_cmds_get_handler},
      {.uri = "/api/bench/cmds", .method = HTTP_POST, .handler = bench_cmds_post_handler},
      {.uri = "/api/bench/cmds/reset", .method = HTTP_POST, .handler = bench_cmds_reset_handler},
      {.uri = "/api/bench/send", .method = HTTP_POST, .handler = bench_send_handler},
      {.uri = "/api/bench/hold", .method = HTTP_POST, .handler = bench_hold_handler},
      {.uri = "/api/bench/demo", .method = HTTP_POST, .handler = bench_demo_handler},
      {.uri = "/api/bench/can", .method = HTTP_POST, .handler = bench_can_handler},
      {.uri = "/api/log", .method = HTTP_GET, .handler = log_get_handler},
      {.uri = "/api/log", .method = HTTP_POST, .handler = log_post_handler},
      {.uri = "/api/log/start", .method = HTTP_POST, .handler = log_start_handler},
      {.uri = "/api/log/stop", .method = HTTP_POST, .handler = log_stop_handler},
      {.uri = "/api/log/rescan", .method = HTTP_POST, .handler = log_rescan_handler},
      {.uri = "/api/log/time", .method = HTTP_POST, .handler = log_time_handler},
      {.uri = "/api/log/files", .method = HTTP_GET, .handler = log_files_handler},
      {.uri = "/api/log/delete", .method = HTTP_POST, .handler = log_delete_handler},
      {.uri = "/log/*", .method = HTTP_GET, .handler = log_download_handler},
      {.uri = "/api/display", .method = HTTP_GET, .handler = display_get_handler},
      {.uri = "/api/display", .method = HTTP_POST, .handler = display_post_handler},
      {.uri = "/api/wifi", .method = HTTP_GET, .handler = wifi_get_handler},
      {.uri = "/api/wifi", .method = HTTP_POST, .handler = wifi_post_handler},
      {.uri = "/api/can", .method = HTTP_GET, .handler = can_get_handler},
      {.uri = "/api/can", .method = HTTP_POST, .handler = can_post_handler},
      {.uri = "/api/can/map", .method = HTTP_GET, .handler = can_map_get_handler},
      {.uri = "/api/can/map", .method = HTTP_POST, .handler = can_map_post_handler},
      {.uri = "/api/can/map/reset", .method = HTTP_POST, .handler = can_map_reset_handler},
      {.uri = "/api/can/frames", .method = HTTP_GET, .handler = can_frames_handler},
      {.uri = "/*", .method = HTTP_GET, .handler = www_get_handler},
  };
  for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++)
  {
    httpd_register_uri_handler(server, &uris[i]);
  }
}

// ---------------------------------------------------------------------------
// The access point and its state for the screen
// ---------------------------------------------------------------------------
static lnd_ap_info_t s_ap_info;
static portMUX_TYPE s_ap_lock = portMUX_INITIALIZER_UNLOCKED;

void lnd_webcfg_get_ap_info(lnd_ap_info_t *out)
{
  portENTER_CRITICAL(&s_ap_lock);
  *out = s_ap_info;
  portEXIT_CRITICAL(&s_ap_lock);
}

static void ap_set_state(lnd_ap_state_t state, const char *ssid, const char *password)
{
  portENTER_CRITICAL(&s_ap_lock);
  s_ap_info.state = state;
  if (ssid != NULL)
  {
    strlcpy(s_ap_info.ssid, ssid, sizeof(s_ap_info.ssid));
    strlcpy(s_ap_info.password, password ? password : "", sizeof(s_ap_info.password));
  }
  portEXIT_CRITICAL(&s_ap_lock);
}

// Who connected and who left - for the client count on the icon.
static void ap_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
  portENTER_CRITICAL(&s_ap_lock);
  if (id == WIFI_EVENT_AP_STACONNECTED)
  {
    s_ap_info.clients++;
  }
  else if (id == WIFI_EVENT_AP_STADISCONNECTED && s_ap_info.clients > 0)
  {
    s_ap_info.clients--;
  }
  portEXIT_CRITICAL(&s_ap_lock);
}

static void start_access_point(void)
{
  lnd_wifi_settings_t w;
  lnd_settings_get_wifi(&w);

  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  esp_netif_create_default_wifi_ap();
  wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&init));

  wifi_config_t wc = {0};
  if (w.ssid[0])
  {
    strlcpy((char *)wc.ap.ssid, w.ssid, sizeof(wc.ap.ssid));
  }
  else
  {
    default_ap_ssid((char *)wc.ap.ssid, sizeof(wc.ap.ssid));
  }
  wc.ap.ssid_len = strlen((char *)wc.ap.ssid);
  wc.ap.channel = 1;
  wc.ap.max_connection = 4;
  if (strlen(w.password) >= LND_WIFI_AP_PASSWORD_MIN_LEN)
  {
    strlcpy((char *)wc.ap.password, w.password, sizeof(wc.ap.password));
    wc.ap.authmode = WIFI_AUTH_WPA2_PSK;
  }
  else
  {
    ESP_LOGW(TAG, "AP password shorter than %d - open network", LND_WIFI_AP_PASSWORD_MIN_LEN);
    wc.ap.authmode = WIFI_AUTH_OPEN;
  }

  ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_AP_STACONNECTED, ap_event_handler, NULL));
  ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_AP_STADISCONNECTED, ap_event_handler, NULL));
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wc));
  ESP_ERROR_CHECK(esp_wifi_start());
  ESP_LOGW(TAG, "Access point \"%s\" up, open http://192.168.4.1/", wc.ap.ssid);

  start_http_server();

  // Only now are the network and the site ready - the screen shows the connection data.
  ap_set_state(LND_AP_UP, (const char *)wc.ap.ssid, wc.ap.authmode == WIFI_AUTH_OPEN ? "" : (const char *)wc.ap.password);
}

static void webcfg_task(void *arg)
{
  (void)arg;
  ESP_LOGI(TAG, "Web config task started, the access point is off");

  while (1)
  {
    vTaskDelay(pdMS_TO_TICKS(LND_WEBCFG_TICK_MS));

    if (atomic_exchange(&s_start_requested, false) && !atomic_load(&s_running))
    {
      atomic_store(&s_running, true);
      start_access_point();
    }
  }
}

void lnd_webcfg_task_start(void)
{
  BaseType_t ok = xTaskCreatePinnedToCore(webcfg_task, "webcfg_task", LND_WEBCFG_TASK_STACK, NULL,
                                          LND_WEBCFG_TASK_PRIORITY, NULL, LND_WEBCFG_TASK_CORE);
  configASSERT(ok == pdPASS);
}

void lnd_webcfg_start(void)
{
  // The screen shows "starting" at once, without waiting for the webcfg task to pick the request up.
  portENTER_CRITICAL(&s_ap_lock);
  if (s_ap_info.state == LND_AP_OFF)
  {
    s_ap_info.state = LND_AP_STARTING;
  }
  portEXIT_CRITICAL(&s_ap_lock);
  atomic_store(&s_start_requested, true);
}

bool lnd_webcfg_running(void)
{
  return atomic_load(&s_running);
}
