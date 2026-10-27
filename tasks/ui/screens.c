#include "screens.h"
#include "board.h"
#include "fonts.h"
#include "images.h"
#include "src/misc/cache/instance/lv_image_cache.h"
#include "settings.h"
#include "signals.h"
#include "theme.h"
#include "units.h"

#include <esp_log.h>
#include <esp_timer.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "screens";

// Internal resolution of the dial and bars: signal values are brought to 0..RES so the needle
// and the fill move smoothly whatever min/max are (LVGL scale/bar values are integers).
#define RES 1000
#define MAX_MARKS 24
#define FLASH_HALF_PERIOD_MS 125

// A signal as the widget follows it, resolved from the layout's signal table.
typedef struct {
  int id;
  float min, max;
  uint8_t decimals;
  const lnd_zone_t *zones;
  uint8_t zone_count;
  lnd_pressure_t pressure;  // the pressure units of the settings apply (see units.h)
} sig_t;

typedef struct {
  const lnd_widget_t *cfg;
  sig_t sig;
  lv_obj_t *obj;   // tile/value: the number; dial: the needle; bar: the bar
  lv_obj_t *obj2;  // dial: the number in the middle
  lv_obj_t *box;   // tile: the panel
  lv_obj_t *bar;   // tile: the side bar (NULL if none)
  lv_obj_t *unit_obj;        // tile: the unit label (NULL if hidden)
  const char *unit_now;      // tile with a pressure signal: the unit shown now
  // what is shown now, so that nothing is touched without a change
  bool shown;
  uint32_t next_ms;  // tile/value: the number is not looked at before this time (ms)
  int32_t key, key2;
  int zone;
  uint32_t colour;  // bar: fill colour now
  // dial
  lv_point_precise_t needle_pts[2];
  bool needle_image;  // the needle is a rotated picture, not a line
  lnd_media_image_t *media[2];  // pictures from the media partition (disc background, needle)
  float cx, cy, len;  // centre and needle length, screen px
  char labels[LND_LAYOUT_MAX_LABELS][12];
  const char *label_ptrs[LND_LAYOUT_MAX_LABELS + 1];
  // segments
  lv_obj_t *seg[LND_LAYOUT_MAX_SEGMENTS];
  float seg_thr[LND_LAYOUT_MAX_SEGMENTS];    // value above which the lamp is lit
  uint32_t seg_on[LND_LAYOUT_MAX_SEGMENTS];  // colour when lit
  uint32_t seg_shown;                        // lamps drawn lit now (bit mask)
} ui_widget_t;

typedef struct {
  lv_obj_t *scr;
  lnd_layout_t layout;  // widgets point into it
  ui_widget_t w[LND_LAYOUT_MAX_WIDGETS];
  int w_count;
} ui_screen_t;

static ui_screen_t *s_screen;
static int s_number_rr;  // the widget the next round of number updates starts from
static int s_current;
static lnd_display_settings_t s_units;  // the pressure units the screen was built for

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static void plain(lv_obj_t *o)
{
  lv_obj_remove_style_all(o);
  lv_obj_set_scrollable(o, false);
  lv_obj_set_clickable(o, false);
}

static lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, uint32_t color, const char *text)
{
  lv_obj_t *l = lv_label_create(parent);
  lv_obj_remove_style_all(l);  // no theme styles: nothing extra to draw or invalidate
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
  lv_label_set_text(l, text);
  return l;
}

// LVGL grows a label's invalid area by a quarter of the font height (for slanted glyphs that
// stick out). For the numbers that is about 45 % more pixels to redraw, so it is cut to nothing
// (the digits of the fonts used here do not leave their cells).
static void ext_draw_cb(lv_event_t *e)
{
  *(int32_t *)lv_event_get_param(e) = 0;
}

static void tight_label(lv_obj_t *l)
{
  lv_obj_add_event_cb(l, ext_draw_cb, LV_EVENT_REFR_EXT_DRAW_SIZE, NULL);
}

static void resolve_signal(const lnd_layout_t *l, const char *name, sig_t *out)
{
  const lnd_signal_def_t *d = lnd_layout_find_signal(l, name);
  memset(out, 0, sizeof(*out));
  out->id = -1;
  if (d == NULL)
  {
    return;
  }
  out->id = lnd_signal_register(d->name, d->min, d->max);
  out->min = d->min;
  out->max = d->max;
  out->decimals = d->decimals;
  out->zones = d->zones;
  out->zone_count = d->zone_count;
  out->pressure = d->pressure;
}

static int zone_index(const sig_t *s, float v)
{
  for (int i = 0; i < s->zone_count; i++)
  {
    if (v >= s->zones[i].from && v <= s->zones[i].to)
    {
      return i;
    }
  }
  return -1;
}

static int32_t to_res(float min, float max, float v)
{
  float f = (v - min) / (max - min);
  f = f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f);
  return (int32_t)lroundf(f * RES);
}

static const float P10[] = {1, 10, 100, 1000};

// ---------------------------------------------------------------------------
// Tile: a panel with the number, a title and a unit
// ---------------------------------------------------------------------------
static lv_text_align_t text_align(lnd_align_t a)
{
  return a == LND_ALIGN_LEFT ? LV_TEXT_ALIGN_LEFT : (a == LND_ALIGN_RIGHT ? LV_TEXT_ALIGN_RIGHT : LV_TEXT_ALIGN_CENTER);
}

static void build_tile(ui_screen_t *u, ui_widget_t *w, const lnd_widget_t *c)
{
  const lnd_tile_t *t = &c->tile;
  const lnd_signal_def_t *sd = lnd_layout_find_signal(&u->layout, c->signal);

  lv_obj_t *box = lv_obj_create(u->scr);
  w->box = box;
  plain(box);
  lv_obj_set_size(box, c->w, c->h);
  lv_obj_set_pos(box, c->x, c->y);
  lv_obj_set_style_radius(box, t->radius, 0);
  lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(box, lv_color_hex(c->bg_color), 0);
  lv_obj_set_style_border_width(box, t->border_w, 0);
  lv_obj_set_style_border_color(box, lv_color_hex(t->border_color), 0);

  int left = t->pad, right = t->pad, top = 0, bottom = 0;
  if (t->accent != LND_ACCENT_NONE)
  {
    lv_obj_t *bar = lv_obj_create(box);
    w->bar = bar;
    plain(bar);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(t->accent_color), 0);
    const int aw = t->accent_w;
    switch (t->accent)
    {
    case LND_ACCENT_LEFT:
      lv_obj_set_size(bar, aw, c->h - 2 * t->border_w);
      lv_obj_align(bar, LV_ALIGN_TOP_LEFT, 0, 0);
      left += aw;
      break;
    case LND_ACCENT_RIGHT:
      lv_obj_set_size(bar, aw, c->h - 2 * t->border_w);
      lv_obj_align(bar, LV_ALIGN_TOP_RIGHT, 0, 0);
      right += aw;
      break;
    case LND_ACCENT_TOP:
      lv_obj_set_size(bar, c->w - 2 * t->border_w, aw);
      lv_obj_align(bar, LV_ALIGN_TOP_LEFT, 0, 0);
      top += aw;
      break;
    default:  // bottom
      lv_obj_set_size(bar, c->w - 2 * t->border_w, aw);
      lv_obj_align(bar, LV_ALIGN_BOTTOM_LEFT, 0, 0);
      bottom += aw;
      break;
    }
  }

  const int inner_w = (int)c->w - 2 * t->border_w - left - right;
  w->obj = label(box, lnd_font_get(c->font), c->color, "--");
  lv_obj_set_size(w->obj, inner_w > 1 ? inner_w : 1, lv_font_get_line_height(lnd_font_get(c->font)));  // a fixed size: no re-layout when the text changes
  lv_obj_set_style_text_align(w->obj, text_align(t->align), 0);
  lv_label_set_long_mode(w->obj, LV_LABEL_LONG_MODE_CLIP);
  lv_obj_align(w->obj, LV_ALIGN_TOP_LEFT, left + t->value_dx, top + t->value_dy);
  // Opaque, in the panel's colour, and no bigger than its own area: when the number changes LVGL
  // sees that the label covers the whole dirty area and draws only the label, not the screen,
  // the rounded panel and its border under it (most of the frame time before).
  tight_label(w->obj);
  lv_obj_set_style_bg_opa(w->obj, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(w->obj, lv_color_hex(c->bg_color), 0);

  if (t->show_title)
  {
    const char *title = c->text[0] ? c->text : (sd ? sd->title : "");
    lv_obj_t *l = label(box, lnd_font_get(t->title_font), t->title_color, title);
    lv_obj_align(l, LV_ALIGN_BOTTOM_LEFT, left + t->title_dx, -(bottom + 4) + t->title_dy);
  }
  if (t->show_unit)
  {
    const char *unit = c->unit[0] ? c->unit : (sd ? sd->unit : "");
    lv_obj_t *l = label(box, lnd_font_get(t->unit_font), t->unit_color, unit);
    lv_obj_align(l, LV_ALIGN_BOTTOM_RIGHT, -right + t->unit_dx, -(bottom + 4) + t->unit_dy);
    w->unit_obj = l;
  }
}

// A bare number.
static void build_value(ui_screen_t *u, ui_widget_t *w, const lnd_widget_t *c)
{
  w->obj = label(u->scr, lnd_font_get(c->font), c->color, "--");
  lv_obj_set_width(w->obj, c->w);
  lv_obj_set_style_text_align(w->obj, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(w->obj, LV_LABEL_LONG_MODE_CLIP);
  lv_obj_set_pos(w->obj, c->x, c->y);
}

static void build_text(ui_screen_t *u, const lnd_widget_t *c)
{
  lv_obj_t *t = label(u->scr, lnd_font_get(c->font), c->color, c->text);
  lv_obj_set_pos(t, c->x, c->y);
}

// ---------------------------------------------------------------------------
// Dial: round scale with coloured zones, a needle and, if wanted, a number in the middle
// ---------------------------------------------------------------------------
// A label of the given width centred on (cx, cy + dy).
static lv_obj_t *centred_label(lv_obj_t *parent, const char *font, uint32_t color, const char *text, int cx, int cy, int dy, int width)
{
  const lv_font_t *f = lnd_font_get(font);
  lv_obj_t *l = label(parent, f, color, text);
  lv_obj_set_width(l, width);
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_CLIP);
  lv_obj_set_pos(l, cx - width / 2, cy + dy - lv_font_get_line_height(f) / 2);
  return l;
}

static void build_dial(ui_screen_t *u, ui_widget_t *w, const lnd_widget_t *c)
{
  const lnd_dial_t *dl = &c->dial;
  const int d = c->w;
  const int inner = d - 2 * dl->bezel_w;

  // disc with the bezel
  lv_obj_t *disc = lv_obj_create(u->scr);
  plain(disc);
  lv_obj_set_size(disc, d, d);
  lv_obj_set_pos(disc, c->x, c->y);
  lv_obj_set_style_radius(disc, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(disc, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(disc, lv_color_hex(c->bg_color), 0);
  lv_obj_set_style_border_width(disc, dl->bezel_w, 0);
  lv_obj_set_style_border_color(disc, lv_color_hex(dl->bezel_color), 0);
  if (dl->bg_image[0] != '\0')
  {
    // scaled once to the disc, LVGL clips it to the circle
    w->media[0] = lnd_media_image_load(dl->bg_image, d);
    if (w->media[0] != NULL)
    {
      lv_obj_set_style_bg_image_src(disc, &w->media[0]->dsc, 0);
    }
    else
    {
      ESP_LOGW(TAG, "background \"%s\" not found", dl->bg_image);
    }
  }

  // coloured bands of the signal's zones, behind the numbers
  const int zone_d = inner + 2 * dl->zone_offset;
  for (int i = 0; i < w->sig.zone_count && zone_d > 4; i++)
  {
    lv_obj_t *arc = lv_arc_create(u->scr);
    lv_obj_set_size(arc, zone_d, zone_d);
    lv_obj_set_pos(arc, c->x + dl->bezel_w - dl->zone_offset, c->y + dl->bezel_w - dl->zone_offset);
    lv_obj_set_clickable(arc, false);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
    lv_obj_remove_style(arc, NULL, LV_PART_INDICATOR);
    lv_arc_set_rotation(arc, dl->rotation);
    const int a0 = to_res(w->sig.min, w->sig.max, w->sig.zones[i].from) * dl->angle / RES;
    const int a1 = to_res(w->sig.min, w->sig.max, w->sig.zones[i].to) * dl->angle / RES;
    lv_arc_set_bg_angles(arc, a0, a1);
    lv_obj_set_style_arc_width(arc, dl->zone_w, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, lv_color_hex(w->sig.zones[i].color), LV_PART_MAIN);
    lv_obj_set_style_arc_opa(arc, dl->zone_opa, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(arc, false, LV_PART_MAIN);
  }

  lv_obj_t *scale = lv_scale_create(u->scr);
  lv_obj_set_size(scale, inner, inner);
  lv_obj_set_pos(scale, c->x + dl->bezel_w, c->y + dl->bezel_w);
  lv_obj_set_clickable(scale, false);
  lv_scale_set_mode(scale, LV_SCALE_MODE_ROUND_INNER);
  lv_scale_set_range(scale, 0, RES);
  lv_scale_set_total_tick_count(scale, dl->ticks);
  lv_scale_set_major_tick_every(scale, dl->major_every);
  lv_scale_set_label_show(scale, dl->show_labels);
  lv_scale_set_angle_range(scale, dl->angle);
  lv_scale_set_rotation(scale, dl->rotation);

  // Labels of the major ticks: signal values divided by label_div.
  const int nlab = (dl->ticks - 1) / dl->major_every + 1;
  for (int i = 0; i < nlab; i++)
  {
    const float frac = nlab > 1 ? (float)(i * dl->major_every) / (float)(dl->ticks - 1) : 0;
    float x = (w->sig.min + (w->sig.max - w->sig.min) * frac) / dl->label_div;
    lnd_shown_t cv;
    if (lnd_units_convert(w->sig.pressure, &s_units, w->sig.min + (w->sig.max - w->sig.min) * frac, &cv))
    {
      x = cv.value;  // a pressure scale is in the units of the settings, label_div does not apply
    }
    snprintf(w->labels[i], sizeof(w->labels[i]), fabsf(x - roundf(x)) < 0.05f ? "%.0f" : "%.1f", (double)x);
    w->label_ptrs[i] = w->labels[i];
  }
  w->label_ptrs[nlab] = NULL;
  lv_scale_set_text_src(scale, w->label_ptrs);

  lv_obj_set_style_bg_opa(scale, LV_OPA_TRANSP, 0);
  lv_obj_set_style_arc_width(scale, 0, LV_PART_MAIN);
  lv_obj_set_style_text_font(scale, lnd_font_get(dl->label_font), LV_PART_INDICATOR);
  lv_obj_set_style_text_color(scale, lv_color_hex(dl->label_color), LV_PART_INDICATOR);
  lv_obj_set_style_pad_radial(scale, dl->label_gap, LV_PART_INDICATOR);
  lv_obj_set_style_length(scale, dl->major_len, LV_PART_INDICATOR);
  lv_obj_set_style_length(scale, dl->minor_len, LV_PART_ITEMS);
  lv_obj_set_style_line_width(scale, dl->major_w, LV_PART_INDICATOR);
  lv_obj_set_style_line_width(scale, dl->minor_w, LV_PART_ITEMS);
  lv_obj_set_style_line_color(scale, lv_color_hex(dl->tick_color), LV_PART_INDICATOR);
  lv_obj_set_style_line_color(scale, lv_color_hex(dl->minor_color), LV_PART_ITEMS);

  w->cx = c->x + d / 2.0f;
  w->cy = c->y + d / 2.0f;

  // numbers in the middle (under the needle, so the hub stays on top)
  if (dl->center_value)
  {
    w->obj2 = centred_label(u->scr, dl->cv_font, dl->cv_color, "--", (int)w->cx + dl->cv_dx, (int)w->cy, dl->cv_dy, inner / 2);
    tight_label(w->obj2);
  }
  if (dl->center_text[0])
  {
    centred_label(u->scr, dl->ct_font, dl->ct_color, dl->center_text, (int)w->cx + dl->ct_dx, (int)w->cy, dl->ct_dy, inner / 2);
  }

  // The needle is a picture rotated about its pivot, or a line whose points are computed by hand:
  // the line's object is only the bounding rectangle of the segment, so only that is redrawn.
  w->len = d / 2.0f - dl->needle_margin;
  const lv_image_dsc_t *img = lnd_needle_image(dl->needle_image);
  if (img == NULL && dl->needle_image[0] != '\0')
  {
    w->media[1] = lnd_media_image_load(dl->needle_image, 0);  // a file of the media partition
    img = w->media[1] != NULL ? &w->media[1]->dsc : NULL;
  }
  if (dl->needle_image[0] != '\0' && img == NULL)
  {
    ESP_LOGW(TAG, "needle image \"%s\" not found, drawing a line", dl->needle_image);
  }
  if (img != NULL)
  {
    lv_obj_t *o = lv_image_create(u->scr);
    lv_obj_set_clickable(o, false);
    lv_image_set_src(o, img);
    lv_image_set_pivot(o, dl->pivot_x, dl->pivot_y);
    lv_image_set_scale(o, dl->needle_scale);
    lv_image_set_antialias(o, false);
    lv_obj_set_pos(o, (int32_t)w->cx - dl->pivot_x, (int32_t)w->cy - dl->pivot_y);
    w->obj = o;
    w->needle_image = true;
  }
  else
  {
    lv_obj_t *needle = lv_line_create(u->scr);
    lv_obj_set_clickable(needle, false);
    lv_obj_set_style_line_width(needle, dl->needle_w, 0);
    lv_obj_set_style_line_rounded(needle, true, 0);
    lv_obj_set_style_line_color(needle, lv_color_hex(dl->needle_color), 0);
    w->obj = needle;
  }

  if (dl->show_hub)
  {
    lv_obj_t *hub = lv_obj_create(u->scr);
    plain(hub);
    lv_obj_set_size(hub, dl->hub_d, dl->hub_d);
    lv_obj_set_pos(hub, (int32_t)w->cx - dl->hub_d / 2, (int32_t)w->cy - dl->hub_d / 2);
    lv_obj_set_style_radius(hub, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(hub, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(hub, lv_color_hex(dl->needle_color), 0);
    lv_obj_set_style_border_width(hub, 3, 0);
    lv_obj_set_style_border_color(hub, lv_color_hex(0x300000), 0);
  }
}

static void needle_set(ui_widget_t *w, int32_t pos)
{
  const lnd_dial_t *dl = &w->cfg->dial;
  if (w->needle_image)
  {
    // LVGL rotates in tenths of a degree
    lv_image_set_rotation(w->obj, (int32_t)lroundf((dl->rotation + dl->angle * pos / (float)RES) * 10.0f));
    return;
  }
  const float ang = (float)(dl->rotation + dl->angle * pos / (float)RES) * (float)M_PI / 180.0f;
  const float tx = w->cx + w->len * cosf(ang), ty = w->cy + w->len * sinf(ang);
  const float x0 = fminf(w->cx, tx), y0 = fminf(w->cy, ty);

  w->needle_pts[0] = (lv_point_precise_t){w->cx - x0, w->cy - y0};
  w->needle_pts[1] = (lv_point_precise_t){tx - x0, ty - y0};
  lv_line_set_points_mutable(w->obj, w->needle_pts, 2);
  lv_obj_set_pos(w->obj, (int32_t)x0, (int32_t)y0);
}

// ---------------------------------------------------------------------------
// Bar: fills with the value, scale marks beside it
// ---------------------------------------------------------------------------
static void build_bar(ui_screen_t *u, ui_widget_t *w, const lnd_widget_t *c)
{
  const lnd_bar_t *b = &c->bar;
  lv_obj_t *bar = lv_bar_create(u->scr);
  w->obj = bar;
  w->colour = c->color;
  lv_bar_set_orientation(bar, b->vertical ? LV_BAR_ORIENTATION_VERTICAL : LV_BAR_ORIENTATION_HORIZONTAL);
  lv_bar_set_range(bar, 0, RES);
  lv_bar_set_value(bar, 0, LV_ANIM_OFF);
  lv_obj_set_size(bar, c->w, c->h);
  lv_obj_set_pos(bar, c->x, c->y);
  lv_obj_set_style_radius(bar, b->radius, LV_PART_MAIN);
  lv_obj_set_style_bg_color(bar, lv_color_hex(c->bg_color), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(bar, b->bg_opa, LV_PART_MAIN);
  lv_obj_set_style_border_width(bar, b->border_w, LV_PART_MAIN);
  lv_obj_set_style_border_color(bar, lv_color_hex(b->border_color), LV_PART_MAIN);
  lv_obj_set_style_border_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(bar, b->radius, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(bar, lv_color_hex(c->color), LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);

  // marks strictly between min and max, one every mark_step signal units
  if (b->mark_step > 0.0f && w->sig.max > w->sig.min)
  {
    const lv_font_t *f = lnd_font_get(b->mark_font);
    int n = 0;
    for (float m = floorf(w->sig.min / b->mark_step + 1.0f) * b->mark_step; m < w->sig.max && n < MAX_MARKS; m += b->mark_step, n++)
    {
      if (m <= w->sig.min)
      {
        continue;
      }
      const float frac = (m - w->sig.min) / (w->sig.max - w->sig.min);
      char txt[16];
      float shown = m / b->mark_div;
      lnd_shown_t cv;
      if (lnd_units_convert(w->sig.pressure, &s_units, m, &cv))
      {
        shown = cv.value;
      }
      snprintf(txt, sizeof(txt), fabsf(shown - roundf(shown)) < 0.05f ? "%.0f" : "%.1f", (double)shown);
      lv_obj_t *l = label(u->scr, f, b->mark_color, txt);
      if (b->vertical)
      {
        lv_obj_align(l, LV_ALIGN_TOP_LEFT, c->x + c->w + b->mark_gap, c->y + (int)((1.0f - frac) * c->h));
        lv_obj_set_style_translate_y(l, lv_pct(-50), 0);
      }
      else
      {
        lv_obj_align(l, LV_ALIGN_TOP_LEFT, c->x + (int)(frac * c->w), c->y + c->h + b->mark_gap);
        lv_obj_set_style_translate_x(l, lv_pct(-50), 0);
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Segments: a row of lamps lighting with the value (shift light)
// ---------------------------------------------------------------------------
static void build_segments(ui_screen_t *u, ui_widget_t *w, const lnd_widget_t *c)
{
  const lnd_segments_t *s = &c->seg;
  const int n = s->count;
  const float start = s->end > s->start ? s->start : w->sig.min;
  const float end = s->end > s->start ? s->end : w->sig.max;
  const int seg_w = ((int)c->w - (n - 1) * s->gap) / n;

  for (int i = 0; i < n; i++)
  {
    lv_obj_t *o = lv_obj_create(u->scr);
    plain(o);
    lv_obj_set_size(o, seg_w > 1 ? seg_w : 1, c->h);
    lv_obj_set_pos(o, c->x + i * (seg_w + s->gap), c->y);
    lv_obj_set_style_radius(o, s->radius, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(s->off_color), 0);
    w->seg[i] = o;
    w->seg_thr[i] = start + (end - start) * (float)i / (float)n;
    uint32_t on = c->color;
    if (c->zone_color)
    {
      const int z = zone_index(&w->sig, start + (end - start) * (float)(i + 1) / (float)n);
      if (z >= 0)
      {
        on = w->sig.zones[z].color;
      }
    }
    w->seg_on[i] = on;
  }
}

// ---------------------------------------------------------------------------
// Screen
// ---------------------------------------------------------------------------
static void destroy_screen(ui_screen_t *u)
{
  if (u == NULL)
  {
    return;
  }
  if (u->scr != NULL)
  {
    lv_obj_delete(u->scr);
  }
  bool had_media = false;
  for (int i = 0; i < u->w_count; i++)
  {
    for (int k = 0; k < 2; k++)
    {
      had_media |= u->w[i].media[k] != NULL;
      lnd_media_image_free(u->w[i].media[k]);
    }
  }
  if (had_media)
  {
    lv_image_cache_drop(NULL);  // the cache is keyed by the address: a new picture may get the old one
  }
  free(u);
}

static ui_screen_t *build_screen(const lnd_layout_t *layout, int index)
{
  ui_screen_t *u = calloc(1, sizeof(*u));
  if (u == NULL)
  {
    ESP_LOGE(TAG, "no memory for the screen");
    return NULL;
  }
  u->layout = *layout;
  const lnd_screen_t *sc = &u->layout.screens[index];

  u->scr = lv_obj_create(NULL);
  lv_obj_set_scrollable(u->scr, false);
  lv_obj_set_style_bg_color(u->scr, lv_color_hex(sc->bg_color), 0);
  lv_obj_set_style_bg_opa(u->scr, LV_OPA_COVER, 0);

  for (int i = 0; i < sc->widget_count; i++)
  {
    const lnd_widget_t *c = &sc->widgets[i];
    ui_widget_t *w = &u->w[u->w_count];
    memset(w, 0, sizeof(*w));
    w->cfg = c;
    w->zone = -2;
    w->key = w->key2 = INT32_MIN;
    if (c->type != LND_WIDGET_TEXT)
    {
      resolve_signal(&u->layout, c->signal, &w->sig);
    }
    switch (c->type)
    {
    case LND_WIDGET_TILE:
      build_tile(u, w, c);
      break;
    case LND_WIDGET_VALUE:
      build_value(u, w, c);
      break;
    case LND_WIDGET_DIAL:
      build_dial(u, w, c);
      break;
    case LND_WIDGET_BAR:
      build_bar(u, w, c);
      break;
    case LND_WIDGET_SEGMENTS:
      build_segments(u, w, c);
      break;
    case LND_WIDGET_TEXT:
      build_text(u, c);
      continue;  // static, nothing to update
    }
    u->w_count++;
  }
  return u;
}

void lnd_screens_select(const lnd_layout_t *layout, int index)
{
  if (layout->count > 0)
  {
    s_current = ((index % layout->count) + layout->count) % layout->count;
  }
  lnd_screens_rebuild(layout);
}

int lnd_screens_current(void)
{
  return s_current;
}

void lnd_screens_rebuild(const lnd_layout_t *layout)
{
  // The signals of the table go to the store: their ranges are what the demo
  // generator sweeps over.
  for (int i = 0; i < layout->signal_count; i++)
  {
    lnd_signal_register(layout->signals[i].name, layout->signals[i].min, layout->signals[i].max);
  }

  if (s_current >= layout->count)
  {
    s_current = layout->count - 1;
  }
  lnd_settings_get_display(&s_units);
  lnd_fonts_begin();  // the uploaded fonts the new screen does not use are freed after the old one is gone
  ui_screen_t *ns = build_screen(layout, s_current);
  if (ns == NULL)
  {
    return;
  }
  lv_screen_load(ns->scr);
  ui_screen_t *old = s_screen;
  s_screen = ns;
  destroy_screen(old);
  lnd_fonts_sweep();
  ESP_LOGI(TAG, "screen %d of %d built: %d live widgets", s_current + 1, layout->count, ns->w_count);
}

// ---------------------------------------------------------------------------
// Updating
// ---------------------------------------------------------------------------
// The parts of a tile that follow the zone of the signal (the settings zone_bg / zone_accent / zone_border).
// Outside every zone they go back to their own colours. The number's own background is the panel's colour
// (see build_tile), so it follows the panel.
static void apply_tile_zone(ui_widget_t *w, bool in_zone, bool fill_bg, uint32_t zone_colour)
{
  const lnd_widget_t *c = w->cfg;
  const lnd_tile_t *t = &c->tile;
  if (t->zone_bg)
  {
    const lv_color_t col = lv_color_hex(in_zone && fill_bg ? zone_colour : c->bg_color);  // only a zone with "fill" paints the panel
    lv_obj_set_style_bg_color(w->box, col, 0);
    lv_obj_set_style_bg_color(w->obj, col, 0);
  }
  if (t->zone_accent && w->bar != NULL)
  {
    lv_obj_set_style_bg_color(w->bar, lv_color_hex(in_zone ? zone_colour : t->accent_color), 0);
  }
  if (t->zone_border)
  {
    lv_obj_set_style_border_color(w->box, lv_color_hex(in_zone ? zone_colour : t->border_color), 0);
  }
}

static void set_number(ui_widget_t *w, lv_obj_t *obj, bool zone_color, uint32_t base_colour, int32_t *key_store)
{
  const lnd_widget_t *c = w->cfg;
  float v = 0;
  const bool ok = lnd_signal_get(w->sig.id, &v);
  int dec = c->decimals >= 0 ? c->decimals : w->sig.decimals;
  float shown = v / c->div;
  if (c->round_step > 0.0f)
  {
    shown = roundf(shown / c->round_step) * c->round_step;  // the key below then changes only when the rounded number does
  }
  lnd_shown_t cv;
  if (ok && lnd_units_convert(w->sig.pressure, &s_units, v, &cv))
  {
    shown = cv.value;  // pressure: the units of the settings, the widget's div and unit do not apply
    dec = cv.decimals;
    if (obj == w->obj && w->unit_obj != NULL && w->unit_now != cv.unit)
    {
      w->unit_now = cv.unit;  // psi <-> inHg changes with the sign of the boost
      lv_label_set_text_static(w->unit_obj, cv.unit);
    }
  }
  const int32_t key = ok ? (int32_t)lroundf(shown * P10[dec]) : INT32_MIN;
  const bool first = !w->shown;
  if (!first && key == *key_store)
  {
    return;
  }
  *key_store = key;

  if (ok)
  {
    // libc snprintf: LVGL's built-in one has no %f
    char txt[24];
    snprintf(txt, sizeof(txt), "%.*f", dec, (double)shown);
    lv_label_set_text(obj, txt);
  }
  else
  {
    lv_label_set_text(obj, "--");
  }

  const lnd_tile_t *tile = (c->type == LND_WIDGET_TILE && obj == w->obj) ? &c->tile : NULL;
  const bool tile_zone = tile != NULL && (tile->zone_bg || tile->zone_accent || tile->zone_border);
  if (zone_color || tile_zone)
  {
    const int zone = ok ? zone_index(&w->sig, v) : -1;
    if (first || zone != w->zone)
    {
      w->zone = zone;
      // a number the colour of its own background could not be read: on a panel the zone fills it keeps the base
      // colour; in a zone without "fill" (a warning) the number takes the zone's colour instead
      const bool fills = tile != NULL && tile->zone_bg && zone >= 0 && w->sig.zones[zone].fill;
      if (zone_color)
      {
        lv_obj_set_style_text_color(obj, lv_color_hex(zone >= 0 && !fills ? w->sig.zones[zone].color : base_colour), 0);
      }
      if (tile_zone)
      {
        apply_tile_zone(w, zone >= 0, fills, zone >= 0 ? w->sig.zones[zone].color : 0);
      }
    }
  }
}

static void update_number(ui_widget_t *w)
{
  set_number(w, w->obj, w->cfg->zone_color, w->cfg->color, &w->key);
  w->shown = true;
}

static void update_dial(ui_widget_t *w, uint32_t now_ms)
{
  float v = 0;
  const bool ok = lnd_signal_get(w->sig.id, &v);
  const int32_t pos = ok ? to_res(w->sig.min, w->sig.max, v) : 0;
  if (!w->shown || pos != w->key)
  {
    w->key = pos;
    needle_set(w, pos);
  }
  if (w->obj2 != NULL && (!w->shown || (int32_t)(now_ms - w->next_ms) >= 0))
  {
    set_number(w, w->obj2, w->cfg->zone_color, w->cfg->dial.cv_color, &w->key2);
    w->next_ms = now_ms + LND_NUMBER_UPDATE_MS;  // the number in the middle is slowed down like the others
  }
  w->shown = true;
}

static void update_bar(ui_widget_t *w)
{
  float v = 0;
  const bool ok = lnd_signal_get(w->sig.id, &v);
  const int32_t pos = ok ? to_res(w->sig.min, w->sig.max, v) : 0;
  if (w->shown && pos == w->key)
  {
    return;
  }
  w->shown = true;
  w->key = pos;
  lv_bar_set_value(w->obj, pos, LV_ANIM_OFF);
  if (w->cfg->zone_color)
  {
    const int zone = ok ? zone_index(&w->sig, v) : -1;
    const uint32_t colour = zone >= 0 ? w->sig.zones[zone].color : w->cfg->color;
    if (colour != w->colour)
    {
      w->colour = colour;
      lv_obj_set_style_bg_color(w->obj, lv_color_hex(colour), LV_PART_INDICATOR);
    }
  }
}

static void update_segments(ui_widget_t *w)
{
  const lnd_segments_t *s = &w->cfg->seg;
  float v = 0;
  const bool ok = lnd_signal_get(w->sig.id, &v);
  uint32_t lit = 0;
  for (int i = 0; ok && i < s->count; i++)
  {
    if (v > w->seg_thr[i])
    {
      lit |= 1u << i;
    }
  }
  // above flash_above all the lit lamps blink
  if (ok && s->flash_above > 0.0f && v >= s->flash_above && ((esp_timer_get_time() / 1000 / FLASH_HALF_PERIOD_MS) & 1) != 0)
  {
    lit = 0;
  }
  if (w->shown && lit == w->seg_shown)
  {
    return;
  }
  for (int i = 0; i < s->count; i++)
  {
    const bool on = (lit >> i) & 1u;
    if (!w->shown || on != (bool)((w->seg_shown >> i) & 1u))
    {
      lv_obj_set_style_bg_color(w->seg[i], lv_color_hex(on ? w->seg_on[i] : s->off_color), 0);
    }
  }
  w->shown = true;
  w->seg_shown = lit;
}

void lnd_screens_update(void)
{
  if (s_screen == NULL)
  {
    return;
  }
  const uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
  const int n = s_screen->w_count;
  for (int i = 0; i < n; i++)
  {
    ui_widget_t *w = &s_screen->w[i];
    switch (w->cfg->type)
    {
    case LND_WIDGET_DIAL:
      update_dial(w, now_ms);
      break;
    case LND_WIDGET_BAR:
      update_bar(w);
      break;
    case LND_WIDGET_SEGMENTS:
      update_segments(w);
      break;
    default:
      break;
    }
  }

  // Numbers: each one at most every LND_NUMBER_UPDATE_MS, and only a few per call, in turn. A
  // frame then holds the needle and a couple of numbers, not all of them at once, so the
  // needle keeps moving smoothly however many numbers are on the screen.
  int budget = LND_NUMBERS_PER_UPDATE;
  for (int k = 0; k < n && budget > 0; k++)
  {
    const int i = (s_number_rr + k) % n;
    ui_widget_t *w = &s_screen->w[i];
    if ((w->cfg->type == LND_WIDGET_TILE || w->cfg->type == LND_WIDGET_VALUE) &&
        (!w->shown || (int32_t)(now_ms - w->next_ms) >= 0))
    {
      update_number(w);
      w->next_ms = now_ms + LND_NUMBER_UPDATE_MS;
      s_number_rr = (i + 1) % n;
      budget--;
    }
  }
}
