#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// The font behind a name used in a layout. Names:
//   "14" "20" "26" "36" "48"          built-in Montserrat (Latin, digits, symbols)
//   "digits18" "tick10" "label16" "digits26" "speed160"   the compiled-in custom fonts
//   "orbitron_24" ... "seg7_120"       the LVGL bin fonts of the RD set: files of the media partition (flashed with the
//                                      firmware, can be replaced or deleted on the web page "Media"); the copies built into the
//                                      firmware (see fonts.c) are used when the file is not there
//   "name" or "name.fnt"               any LVGL bin font uploaded to the media partition
// An unknown or empty name gives Montserrat 14. Embedded fonts are loaded into RAM the
// first time they are asked for and then kept. LVGL lock held.
const lv_font_t *lnd_font_get(const char *name);

// The uploaded fonts live in RAM only while a screen uses them. Call lnd_fonts_begin() before a screen is
// built and lnd_fonts_sweep() after the old one is deleted: the fonts the new screen did not ask for are freed.
void lnd_fonts_begin(void);
void lnd_fonts_sweep(void);

#ifdef __cplusplus
} /*extern "C"*/
#endif
