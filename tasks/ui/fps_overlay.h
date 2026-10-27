#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// FPS / frame-time overlay, drawn on LVGL's top layer so it stays above every
// screen and screen-change animation. Compiled in only when the build option
// NDASH_UI_SHOW_FPS is 1 (idf.py -D NDASH_UI_SHOW_FPS=0 build turns it off;
// tools/build_release.py does so for release builds). With it off the function
// below is empty.
//
// What it shows, refreshed once a second:
//     38 fps  r 14 (max 31) w 10 ms  25 Kpx
//   fps  - frames completed in the last second. LVGL only redraws what changed,
//          so a static screen reads ~1 (the overlay's own label updating) and the
//          number only means something while something moves, e.g. a screen-change
//          animation;
//   r    - mean time spent rendering one frame (CPU work), worst one in brackets -
//          the worst case is what shows up as a stutter;
//   w    - mean time waiting after the last area was handed to the panel: in the
//          tear-free mode esp_lvgl_port holds a frame until the panel's VSYNC, so
//          this is not CPU work. r + w is what bounds the frame rate;
//   Kpx  - mean number of pixels redrawn per frame.
// The same line is logged (tag "fps") whenever more than one frame was drawn.

// Creates the label, the 1 s update timer and the display event hooks. Call with
// the LVGL lock held, once the display exists.
void lnd_fps_overlay_start(lv_display_t *disp);

#ifdef __cplusplus
} /*extern "C"*/
#endif
