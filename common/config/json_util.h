#pragma once

// Small helpers for reading cJSON objects. A missing or unusable field leaves
// the value alone, so the caller sets the defaults first.

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"

// ------------------------------------------------------------------

static inline void get_float(const cJSON *obj, const char *key, float *out)
{
  const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
  if (cJSON_IsNumber(v))
  {
    *out = (float)v->valuedouble;
  }
}

static inline void get_int(const cJSON *obj, const char *key, int lo, int hi, int *out)
{
  const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
  if (cJSON_IsNumber(v))
  {
    int x = (int)v->valuedouble;
    *out = x < lo ? lo : (x > hi ? hi : x);
  }
}

static inline void get_bool(const cJSON *obj, const char *key, bool *out)
{
  const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
  if (cJSON_IsBool(v))
  {
    *out = cJSON_IsTrue(v);
  }
}

static inline void get_str(const cJSON *obj, const char *key, char *out, size_t size)
{
  const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
  if (cJSON_IsString(v) && v->valuestring != NULL)
  {
    strlcpy(out, v->valuestring, size);
  }
}

// "#rrggbb" -> 0xRRGGBB; on an error the value is left alone.
static inline void get_color(const cJSON *obj, const char *key, uint32_t *out)
{
  const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
  if (!cJSON_IsString(v) || v->valuestring == NULL || v->valuestring[0] != '#' || strlen(v->valuestring) != 7)
  {
    return;
  }
  char *end = NULL;
  unsigned long c = strtoul(v->valuestring + 1, &end, 16);
  if (end != NULL && *end == '\0')
  {
    *out = (uint32_t)c;
  }
}

