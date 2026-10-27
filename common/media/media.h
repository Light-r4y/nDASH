#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// The media partition: picture files (LVGL .bin) for the dial backgrounds and needles,
// uploaded from the web or flashed with the firmware (media/ in the project).

// Mounts the partition at LND_MEDIA_BASE_PATH. A failure is logged, the dash works without it.
void lnd_media_mount(void);

// A file name that is safe to use: [A-Za-z0-9._-], 1..31 characters, not starting with a dot.
bool lnd_media_name_ok(const char *name);

// Reads a whole file to a PSRAM buffer (free() it). NULL: no such file, a bad name or no memory.
void *lnd_media_read(const char *name, size_t *size);

#ifdef __cplusplus
} /*extern "C"*/
#endif
