#include "fonts.h"
#include "theme.h"

#include <esp_log.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <strings.h>

#include "media.h"

static const char *TAG = "fonts";

// The embedded LVGL bin fonts (tasks/ui/fonts/bin/*.fnt, made with lv_font_conv --format bin
// --bpp 4 --no-compress). Files named *_56, *_64, *_96, seg7_* hold only digits and
// ". : - + % /"; the others are Latin (and Cyrillic where the family has it).
// The licences are in the README (all SIL OFL 1.1).
#define FONTS(X) \
  X(blackops_28) X(blackops_40) X(blackops_56) X(blackops_96) \
  X(jura_24) X(jura_36) \
  X(michroma_24) X(michroma_36) \
  X(orbitron_24) X(orbitron_36) X(orbitron_56) X(orbitron_96) \
  X(russo_28) X(russo_40) X(russo_64) \
  X(seg7_40) X(seg7_72) X(seg7_120)

#define DECLARE(n)                                                         \
  extern const uint8_t n##_start[] asm("_binary_" #n "_fnt_start");        \
  extern const uint8_t n##_end[] asm("_binary_" #n "_fnt_end");
FONTS(DECLARE)

typedef struct {
  const char *name;
  const uint8_t *start, *end;
  lv_font_t *font;  // created on first use
} embedded_t;

#define ENTRY(n) {#n, n##_start, n##_end, NULL},
static embedded_t s_embedded[] = {FONTS(ENTRY)};

static const struct {
  const char *name;
  const lv_font_t *font;
} s_builtin[] = {
    {"14", &lv_font_montserrat_14},
    {"20", &lv_font_montserrat_20},
    {"26", &lv_font_montserrat_26},
    {"36", &lv_font_montserrat_36},
    {"48", &lv_font_montserrat_48},
    {"digits18", &ui_font_FontIndicator},
    {"tick10", &ui_font_FontIndicatorLabel},
    {"label16", &ui_font_FontLabel},
    {"digits26", &ui_font_FontRPM},
    {"speed160", &ui_font_FontSpeed},
};

// ---------------------------------------------------------------------------
// Fonts uploaded to the media partition
// ---------------------------------------------------------------------------
#define MEDIA_FONTS_MAX 8

typedef struct {
  char name[32];
  void *buf;  // the file, in PSRAM: lv_binfont_create_from_buffer() uses it in place
  lv_font_t *font;
  uint32_t gen;  // the build that asked for it last
} media_font_t;

static media_font_t s_media[MEDIA_FONTS_MAX];
static uint32_t s_gen;

// Names that have no file in the media partition (checked once per screen build, a failed fopen is slow).
#define MISSES_MAX 24
static char s_miss[MISSES_MAX][32];
static int s_miss_n;

// The file of an uploaded font for a layout name: "orbitron_36" and "orbitron_36.fnt" are the same file.
static bool media_file_name(const char *name, char *file, size_t size)
{
  const size_t n = strlen(name);
  if (n > 4 && strcasecmp(name + n - 4, ".fnt") == 0)
  {
    return n < size && snprintf(file, size, "%s", name) > 0;
  }
  return n + 4 < size && snprintf(file, size, "%s.fnt", name) > 0;
}

static void media_free(media_font_t *m)
{
  if (m->font != NULL)
  {
    lv_binfont_destroy(m->font);
  }
  free(m->buf);
  memset(m, 0, sizeof(*m));
}

void lnd_fonts_begin(void)
{
  s_gen++;
  s_miss_n = 0;
}

void lnd_fonts_sweep(void)
{
  for (int i = 0; i < MEDIA_FONTS_MAX; i++)
  {
    if (s_media[i].font != NULL && s_media[i].gen != s_gen)
    {
      media_free(&s_media[i]);
    }
  }
}

// The font of an uploaded file, loaded on the first use. NULL: there is no such file (or it is not a font).
static const lv_font_t *media_font(const char *name)
{
  char file[32];
  if (!media_file_name(name, file, sizeof(file)))
  {
    return NULL;
  }
  media_font_t *free_slot = NULL;
  for (int i = 0; i < MEDIA_FONTS_MAX; i++)
  {
    media_font_t *m = &s_media[i];
    if (m->font != NULL && strcmp(m->name, file) == 0)
    {
      m->gen = s_gen;
      return m->font;
    }
    if (m->font == NULL && free_slot == NULL)
    {
      free_slot = m;
    }
  }
  for (int i = 0; i < s_miss_n; i++)
  {
    if (strcmp(s_miss[i], file) == 0)
    {
      return NULL;
    }
  }
  if (free_slot == NULL)
  {
    ESP_LOGW(TAG, "too many uploaded fonts in use, \"%s\" is not loaded", file);
    return NULL;
  }
  size_t size = 0;
  void *buf = lnd_media_read(file, &size);
  lv_font_t *font = buf != NULL ? lv_binfont_create_from_buffer(buf, (uint32_t)size) : NULL;
  if (font == NULL)
  {
    free(buf);
    if (s_miss_n < MISSES_MAX)
    {
      strlcpy(s_miss[s_miss_n++], file, sizeof(s_miss[0]));
    }
    return NULL;
  }
  font->fallback = &lv_font_montserrat_14;  // the symbols the font lacks
  strlcpy(free_slot->name, file, sizeof(free_slot->name));
  free_slot->buf = buf;
  free_slot->font = font;
  free_slot->gen = s_gen;
  ESP_LOGI(TAG, "font \"%s\" loaded, %u bytes", file, (unsigned)size);
  return font;
}

const lv_font_t *lnd_font_get(const char *name)
{
  if (name == NULL || name[0] == '\0')
  {
    return &lv_font_montserrat_14;
  }
  for (size_t i = 0; i < sizeof(s_builtin) / sizeof(s_builtin[0]); i++)
  {
    if (strcmp(s_builtin[i].name, name) == 0)
    {
      return s_builtin[i].font;
    }
  }
  // An uploaded file wins over the copy built into the firmware: a font can be replaced or deleted on the web page "Media"
  // (the files of the RD set are flashed there with the firmware).
  const lv_font_t *uploaded = media_font(name);
  if (uploaded != NULL)
  {
    return uploaded;
  }
  for (size_t i = 0; i < sizeof(s_embedded) / sizeof(s_embedded[0]); i++)
  {
    embedded_t *e = &s_embedded[i];
    if (strcmp(e->name, name) != 0)
    {
      continue;
    }
    if (e->font == NULL)
    {
      e->font = lv_binfont_create_from_buffer((void *)e->start, (uint32_t)(e->end - e->start));
      if (e->font == NULL)
      {
        ESP_LOGW(TAG, "font \"%s\" did not load", name);
        return &lv_font_montserrat_14;
      }
      e->font->fallback = &lv_font_montserrat_14;  // symbols the font lacks (e.g. letters in a digits-only one)
    }
    return e->font;
  }
  ESP_LOGW(TAG, "unknown font \"%s\", using 14", name);
  return &lv_font_montserrat_14;
}
