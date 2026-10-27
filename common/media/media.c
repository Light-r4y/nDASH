#include "media.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "conf.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_spiffs.h"

static const char *TAG = "media";

void lnd_media_mount(void)
{
  esp_vfs_spiffs_conf_t fs = {
      .base_path = LND_MEDIA_BASE_PATH,
      .partition_label = LND_MEDIA_PARTITION,
      .max_files = 4,
      .format_if_mount_failed = true,  // it only holds pictures; an unformatted partition is just empty
  };
  esp_err_t err = esp_vfs_spiffs_register(&fs);
  if (err != ESP_OK)
  {
    ESP_LOGE(TAG, "Mount %s failed: %s", LND_MEDIA_PARTITION, esp_err_to_name(err));
  }
}

bool lnd_media_name_ok(const char *n)
{
  const size_t len = strlen(n);
  if (len == 0 || len > 31 || n[0] == '.')
  {
    return false;
  }
  for (; *n; n++)
  {
    const bool ok = (*n >= 'a' && *n <= 'z') || (*n >= 'A' && *n <= 'Z') || (*n >= '0' && *n <= '9') || *n == '.' ||
                    *n == '_' || *n == '-';
    if (!ok)
    {
      return false;
    }
  }
  return true;
}

void *lnd_media_read(const char *name, size_t *size)
{
  if (!lnd_media_name_ok(name))
  {
    return NULL;
  }
  char path[sizeof(LND_MEDIA_BASE_PATH) + 40];
  snprintf(path, sizeof(path), LND_MEDIA_BASE_PATH "/%s", name);
  FILE *f = fopen(path, "rb");
  if (f == NULL)
  {
    return NULL;
  }
  fseek(f, 0, SEEK_END);
  const long len = ftell(f);
  fseek(f, 0, SEEK_SET);
  void *buf = len > 0 ? heap_caps_malloc((size_t)len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) : NULL;
  if (buf != NULL && fread(buf, 1, (size_t)len, f) != (size_t)len)
  {
    free(buf);
    buf = NULL;
  }
  fclose(f);
  if (buf != NULL && size != NULL)
  {
    *size = (size_t)len;
  }
  return buf;
}
