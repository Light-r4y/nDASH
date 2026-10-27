#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "conf.h"

// Dash layout: a table of signals and a list of screens. Stored in NVS as JSON
// (LND_LAYOUT_NVS_KEY), in memory as a parsed structure. The format:
//
// {
//   "version": 1,
//   "signals": [
//     { "name": "rpm", "title": "RPM", "unit": "rpm", "min": 0, "max": 8000, "decimals": 0, "pressure": "abs|boost" (kPa signals only),
//       "zones": [ { "from": 6500, "to": 8000, "color": "#ff0000", "fill": true } ] }
//   ],
//   "screens": [
//     { "bg_color": "#000000",
//       "widgets": [
//         { "type": "dial", "x": 230, "y": 36, "w": 340, "signal": "rpm", "label_div": 1000 },
//         { "type": "tile", "x": 0, "y": 34, "w": 210, "h": 100, "signal": "map",
//           "text": "Manifold Pressure", "unit": "kPa", "accent": "left", "zone_color": true },
//         { "type": "bar", "x": 0, "y": 0, "w": 800, "h": 80, "signal": "rpm", "mark_step": 1000, "mark_div": 1000 },
//         { "type": "segments", "x": 0, "y": 4, "w": 800, "h": 22, "signal": "rpm", "count": 14 },
//         { "type": "value", "x": 420, "y": 300, "w": 150, "signal": "speed", "font": "48" },
//         { "type": "text",  "x": 10, "y": 10, "text": "nDash", "font": "20" } ] }
//   ]
// }
//
// A screen is a flat list of widgets drawn in list order, the first one at the
// bottom. Coordinates x, y are pixels from the top-left corner of the screen.
// Widget types:
//   "tile"      the value with a title and a unit on a panel
//   "dial"      a round scale with coloured zones, a needle and, optionally, a number in the middle
//   "bar"       a horizontal or vertical bar filling with the value, with scale marks beside it
//   "segments"  a row of lamps (shift light) lighting up with the value
//   "value"     just the number
//   "text"      a static label
// Every widget can leave out any field: it takes the default of its type (the defaults are
// in layout.c, parse_widget, and repeated by the web editor).
//
// Range, unit and decimals come from the signal table; a widget can override unit
// ("unit"), decimals ("decimals") and show the value divided by "div" (colour zones
// still use the original value).
//
// Fonts ("font", "title_font", "label_font" ...) are names: "14", "20", "26", "36", "48"
// (Montserrat), the compiled-in custom ones and the embedded ones, see tasks/ui/fonts.c.

#define LND_LAYOUT_MAX_ZONES 4
#define LND_LAYOUT_MAX_LABELS 16
#define LND_LAYOUT_MAX_SIGNALS 16 // <= LND_SIGNAL_MAX_COUNT (common/signals)
#define LND_LAYOUT_MAX_SEGMENTS 32
#define LND_FONT_NAME_LEN 32  // a built-in font name, or the file name of a font of the media partition
#define LND_MEDIA_NAME_LEN 32  // a file name in the media partition (up to 31 characters)

typedef enum {
  LND_WIDGET_TILE = 0,
  LND_WIDGET_DIAL,
  LND_WIDGET_VALUE,
  LND_WIDGET_TEXT,
  LND_WIDGET_BAR,
  LND_WIDGET_SEGMENTS,
} lnd_widget_type_t;

typedef enum {
  LND_ACCENT_NONE = 0,
  LND_ACCENT_LEFT,
  LND_ACCENT_RIGHT,
  LND_ACCENT_TOP,
  LND_ACCENT_BOTTOM,
} lnd_accent_t;

typedef enum {
  LND_ALIGN_LEFT = 0,
  LND_ALIGN_CENTER,
  LND_ALIGN_RIGHT,
} lnd_align_t;

typedef struct {
  float from, to;
  uint32_t color; // 0xRRGGBB
  bool fill;      // a tile with zone_bg fills its background with this zone ("fill": false - only the number takes the colour)
} lnd_zone_t;

// What a signal measures, when the pressure units of the settings apply to it (units.h). The value in the
// signal is always kPa; "abs" - absolute pressure (baro, MAP as is), "boost" - MAP shown against the atmosphere.
typedef enum {
  LND_PRESSURE_NONE = 0,
  LND_PRESSURE_ABS,
  LND_PRESSURE_BOOST,
} lnd_pressure_t;

typedef struct {
  char name[16];
  char title[24];
  char unit[12];
  lnd_pressure_t pressure;
  float min, max;
  uint8_t decimals;
  uint8_t zone_count;
  lnd_zone_t zones[LND_LAYOUT_MAX_ZONES];
} lnd_signal_def_t;

// ---- per type settings ------------------------------------------------------
typedef struct {
  char title_font[LND_FONT_NAME_LEN], unit_font[LND_FONT_NAME_LEN];
  uint32_t title_color, unit_color;
  bool show_title, show_unit;
  uint8_t border_w, radius;
  uint32_t border_color;
  lnd_align_t align;     // of the number
  uint8_t pad;           // inside the panel, px
  int8_t value_dy;       // the number's offset from the top, px
  int16_t value_dx;      // ... and from where its alignment puts it, to the right
  int16_t title_dx, title_dy;  // the title's offset from its place (bottom left), px; dy down
  int16_t unit_dx, unit_dy;    // the unit's offset from its place (bottom right), px
  lnd_accent_t accent;
  uint8_t accent_w;
  uint32_t accent_color;
  bool zone_bg, zone_accent, zone_border;  // these parts take the colour of the signal's zone (none - the base colour)
} lnd_tile_t;

typedef struct {
  uint16_t angle;
  int16_t rotation;
  uint8_t ticks, major_every;
  float label_div;
  char label_font[LND_FONT_NAME_LEN];
  uint32_t label_color, tick_color, minor_color, bezel_color, needle_color;
  bool show_labels;
  int8_t label_gap;                  // moves the labels along the radius, px (+ inwards)
  uint8_t major_len, minor_len, major_w, minor_w;
  uint8_t bezel_w;
  uint8_t needle_w, needle_margin;   // the needle stops this far from the dial's edge
  char needle_image[LND_MEDIA_NAME_LEN];  // "classic" | "neon" | "stripe" (built in) or a .bin file of the media partition; empty - a line
  char bg_image[LND_MEDIA_NAME_LEN];      // a .bin file of the media partition drawn in the disc; empty - none
  int16_t pivot_x, pivot_y;          // the image's pivot, px from its top-left corner (it points right)
  uint16_t needle_scale;             // image scale, 256 = 100 %
  bool show_hub;
  uint8_t hub_d;
  uint8_t zone_w;                    // width of the coloured band
  int8_t zone_offset;                // moves the band along the radius, px (+ inwards)
  uint8_t zone_opa;                  // 0..255
  bool center_value;                 // the number in the middle
  char cv_font[LND_FONT_NAME_LEN];
  uint32_t cv_color;
  int16_t cv_dx, cv_dy;              // offset of the number from the centre
  char center_text[24];              // caption under it (empty - none)
  char ct_font[LND_FONT_NAME_LEN];
  uint32_t ct_color;
  int16_t ct_dx, ct_dy;
} lnd_dial_t;

typedef struct {
  bool vertical;
  uint8_t border_w, radius;
  uint32_t border_color;
  uint8_t bg_opa;        // of the empty part, 0..255
  float mark_step;       // a scale mark every this many signal units (0 - no marks)
  float mark_div;        // the mark shows value / mark_div
  char mark_font[LND_FONT_NAME_LEN];
  uint32_t mark_color;
  int8_t mark_gap;       // from the bar's edge (below / to the right of it), px
} lnd_bar_t;

typedef struct {
  uint8_t count, gap, radius;
  float start, end;      // the signal values where the first lamp lights and the last one
  uint32_t off_color;
  float flash_above;     // all lamps flash above this value (0 - never)
} lnd_segments_t;

typedef struct {
  lnd_widget_type_t type;
  int16_t x, y;
  uint16_t w, h;     // dial: w is the diameter; value: w is the text width
  char signal[16];
  char font[LND_FONT_NAME_LEN];  // value / text font; empty - default for the type
  char text[24];     // text: the label; tile: the title (empty - the signal's)
  char unit[12];     // tile: unit (empty - the signal's)
  uint32_t color;    // text / value colour; bar: fill; segments: lit lamps
  uint32_t bg_color; // tile: panel; dial: disc; bar: empty part
  int8_t decimals;   // value, tile, dial centre: -1 - as in the signal
  float div;         // value, tile, dial centre: the shown number = value / div (1 by default)
  float round_step;  // ... then rounded to a multiple of this ("round"; 0 - not rounded): the last digits of a fast signal do not flicker
  bool zone_color;   // value, tile, bar, segments, dial centre: colour by the signal's zone
  union {
    lnd_tile_t tile;
    lnd_dial_t dial;
    lnd_bar_t bar;
    lnd_segments_t seg;
  };
} lnd_widget_t;

typedef struct {
  uint32_t bg_color;
  uint8_t widget_count;
  lnd_widget_t widgets[LND_LAYOUT_MAX_WIDGETS];
} lnd_screen_t;

typedef struct {
  uint8_t signal_count;
  lnd_signal_def_t signals[LND_LAYOUT_MAX_SIGNALS];
  uint8_t count; // screens
  lnd_screen_t screens[LND_UI_MAX_SCREENS];
} lnd_layout_t;

// A signal definition by name, or NULL.
const lnd_signal_def_t *lnd_layout_find_signal(const lnd_layout_t *l, const char *name);

// Reads the layout from NVS, takes the built-in one if there is none or it is
// broken. Call after nvs_flash_init().
void lnd_layout_init(void);

// A copy of the current layout (it can change on the fly, do not keep a pointer).
// The structure is big: put it on the heap.
void lnd_layout_copy(lnd_layout_t *out);

// What a layout is refused for (lnd_layout_check). Texts and numbers have no background of their
// own: over a dial the disc, the scale and the picture under them would be redrawn with every number.
typedef enum {
  LND_LAYOUT_OK = 0,
  LND_LAYOUT_TOO_MANY,       // more than LND_LAYOUT_MAX_LIVE live widgets on a screen
  LND_LAYOUT_TOO_MANY_DIALS, // more than LND_LAYOUT_MAX_DIALS dials on a screen
  LND_LAYOUT_LABEL_OVER_DIAL // a value or text widget overlaps a dial
} lnd_layout_problem_t;

// The first problem found (screen and widget index in *screen, *widget when not NULL).
lnd_layout_problem_t lnd_layout_check(const lnd_layout_t *l, int *screen, int *widget);

// Parses the JSON, checks it, stores it in NVS and makes it current. ESP_ERR_INVALID_ARG -
// the JSON did not parse or has no usable screen (the old layout stays).
esp_err_t lnd_layout_apply_json(const char *json);

// The JSON of the current layout on the heap (free() it); NULL on out of memory.
char *lnd_layout_json_dup(void);

// Brings the built-in layout back and erases the stored one.
esp_err_t lnd_layout_reset(void);

// Grows on every layout change; ui_task rebuilds the screen when it sees a new value.
uint32_t lnd_layout_generation(void);

// Makes ui_task rebuild the screen without a layout change (after the media files changed).
void lnd_layout_touch(void);
