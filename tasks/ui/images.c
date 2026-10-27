#include "images.h"

#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "media.h"

// LVGL .bin files: a 12-byte header (lv_image_header_t) and then the pixels. They are used in
// place, from flash, through an image descriptor built once.
#define IMAGES(X) X(classic) X(neon) X(stripe)

#define DECLARE(n)                                                              \
  extern const uint8_t needle_##n##_start[] asm("_binary_needle_" #n "_bin_start"); \
  extern const uint8_t needle_##n##_end[] asm("_binary_needle_" #n "_bin_end");
IMAGES(DECLARE)

typedef struct {
  const char *name;
  const uint8_t *start, *end;
  lv_image_dsc_t dsc;
  bool ready;
} needle_t;

#define ENTRY(n) {.name = #n, .start = needle_##n##_start, .end = needle_##n##_end},
static needle_t s_needles[] = {IMAGES(ENTRY)};

#define BIN_HEADER_SIZE 12

const lv_image_dsc_t *lnd_needle_image(const char *name)
{
  if (name == NULL || name[0] == '\0')
  {
    return NULL;
  }
  for (size_t i = 0; i < sizeof(s_needles) / sizeof(s_needles[0]); i++)
  {
    needle_t *n = &s_needles[i];
    if (strcmp(n->name, name) != 0)
    {
      continue;
    }
    if (!n->ready)
    {
      const lv_image_header_t *h = (const lv_image_header_t *)n->start;
      n->dsc.header = *h;
      n->dsc.data = n->start + BIN_HEADER_SIZE;
      n->dsc.data_size = (uint32_t)(n->end - n->start) - BIN_HEADER_SIZE;
      n->ready = true;
    }
    return &n->dsc;
  }
  return NULL;
}

// ---------------------------------------------------------------------------
// Pictures of the media partition
// ---------------------------------------------------------------------------
static const char *TAG = "images";

static uint16_t lerp565(uint16_t a, uint16_t b, uint16_t c, uint16_t d, uint32_t fx, uint32_t fy)
{
  // fx, fy: 0..256; channels are interpolated separately
  uint32_t out = 0;
  static const struct
  {
    uint8_t shift, bits;
  } ch[3] = {{11, 5}, {5, 6}, {0, 5}};
  for (int i = 0; i < 3; i++)
  {
    const uint32_t m = (1u << ch[i].bits) - 1;
    const uint32_t ca = (a >> ch[i].shift) & m, cb = (b >> ch[i].shift) & m;
    const uint32_t cc = (c >> ch[i].shift) & m, cd = (d >> ch[i].shift) & m;
    const uint32_t top = ca * (256 - fx) + cb * fx;
    const uint32_t bot = cc * (256 - fx) + cd * fx;
    const uint32_t v = (top * (256 - fy) + bot * fy + 32768) >> 16;
    out |= (v > m ? m : v) << ch[i].shift;
  }
  return (uint16_t)out;
}

lnd_media_image_t *lnd_media_image_load(const char *name, int fit)
{
  size_t size = 0;
  uint8_t *file = lnd_media_read(name, &size);
  if (file == NULL)
  {
    return NULL;
  }
  const lv_image_header_t *h = (const lv_image_header_t *)file;
  const bool rgb565 = size > BIN_HEADER_SIZE && h->magic == LV_IMAGE_HEADER_MAGIC && h->cf == LV_COLOR_FORMAT_RGB565;
  const bool argb = size > BIN_HEADER_SIZE && h->magic == LV_IMAGE_HEADER_MAGIC && h->cf == LV_COLOR_FORMAT_ARGB8888;
  const int bpp = rgb565 ? 2 : 4;
  if ((!rgb565 && !argb) || h->w == 0 || h->h == 0 || h->stride < h->w * bpp ||
      size < BIN_HEADER_SIZE + (size_t)h->stride * h->h)
  {
    ESP_LOGW(TAG, "%s: not a usable .bin", name);
    free(file);
    return NULL;
  }
  lnd_media_image_t *m = calloc(1, sizeof(*m));
  if (m == NULL)
  {
    free(file);
    return NULL;
  }

  if (fit <= 0 || (h->w == fit && h->h == fit))
  {
    m->buf = file;
    m->dsc.header = *h;
    m->dsc.data = file + BIN_HEADER_SIZE;
    m->dsc.data_size = (uint32_t)(size - BIN_HEADER_SIZE);
    return m;
  }

  uint8_t *out = heap_caps_malloc((size_t)fit * fit * bpp, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (out == NULL)
  {
    free(file);
    free(m);
    return NULL;
  }
  const uint8_t *px = file + BIN_HEADER_SIZE;
  const uint32_t sw = h->w, sh = h->h, stride = h->stride;
  for (int y = 0; y < fit; y++)
  {
    // source position of the pixel centre, 8 bits of fraction
    int32_t fy = (int32_t)(((y * 2 + 1) * (int64_t)sh * 256) / (fit * 2)) - 128;
    if (fy < 0) fy = 0;
    uint32_t y0 = fy >> 8, y1 = y0 + 1 < sh ? y0 + 1 : y0;
    const uint32_t wy = fy & 255;
    for (int x = 0; x < fit; x++)
    {
      int32_t fx = (int32_t)(((x * 2 + 1) * (int64_t)sw * 256) / (fit * 2)) - 128;
      if (fx < 0) fx = 0;
      uint32_t x0 = fx >> 8, x1 = x0 + 1 < sw ? x0 + 1 : x0;
      const uint32_t wx = fx & 255;
      if (rgb565)
      {
        const uint16_t *r0 = (const uint16_t *)(px + y0 * stride), *r1 = (const uint16_t *)(px + y1 * stride);
        ((uint16_t *)out)[y * fit + x] = lerp565(r0[x0], r0[x1], r1[x0], r1[x1], wx, wy);
      }
      else
      {
        ((uint32_t *)out)[y * fit + x] = ((const uint32_t *)(px + y0 * stride))[x0];  // nearest
      }
    }
  }
  free(file);
  m->buf = out;
  m->dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
  m->dsc.header.cf = rgb565 ? LV_COLOR_FORMAT_RGB565 : LV_COLOR_FORMAT_ARGB8888;
  m->dsc.header.w = fit;
  m->dsc.header.h = fit;
  m->dsc.header.stride = fit * bpp;
  m->dsc.data = out;
  m->dsc.data_size = (uint32_t)fit * fit * bpp;
  return m;
}

void lnd_media_image_free(lnd_media_image_t *m)
{
  if (m != NULL)
  {
    free(m->buf);
    free(m);
  }
}
