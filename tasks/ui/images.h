#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// A picture read from the media partition into PSRAM. Free it with lnd_media_image_free() after
// the object that shows it is deleted.
typedef struct {
  lv_image_dsc_t dsc;
  void *buf;
} lnd_media_image_t;

// Loads a LVGL .bin (RGB565 or ARGB8888) of the media partition. fit > 0: the picture is
// scaled once to fit x fit px (a square), so that nothing has to be scaled while drawing.
// NULL - no such file, a bad file or no memory.
lnd_media_image_t *lnd_media_image_load(const char *name, int fit);
void lnd_media_image_free(lnd_media_image_t *m);

// The needle images built into the firmware (tasks/ui/images/needle_*.bin, ARGB8888, 199x44,
// pointing right, pivot at 34,22). Names: "classic", "neon", "stripe". NULL for an unknown name.
const lv_image_dsc_t *lnd_needle_image(const char *name);

#ifdef __cplusplus
} /*extern "C"*/
#endif
