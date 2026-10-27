#include "layout.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "json_util.h"
#include "layout_default.h"
#include "nvs.h"

static const char *TAG = "layout";

static lnd_layout_t s_layout;
static uint32_t s_generation;
static SemaphoreHandle_t s_mutex;

#define AMBER 0xF5B800
#define WHITE 0xFFFFFF

static void parse_zones(const cJSON *arr, lnd_zone_t *zones, uint8_t *count, float lo, float hi, uint32_t color)
{
  const cJSON *it;
  *count = 0;
  cJSON_ArrayForEach(it, arr)
  {
    if (*count >= LND_LAYOUT_MAX_ZONES)
    {
      break;
    }
    lnd_zone_t *z = &zones[*count];
    z->from = lo;
    z->to = hi;
    z->color = color;
    z->fill = true;
    get_float(it, "from", &z->from);
    get_float(it, "to", &z->to);
    get_color(it, "color", &z->color);
    get_bool(it, "fill", &z->fill);
    if (z->to > z->from)
    {
      (*count)++;
    }
  }
}

// ------------------------------------------------------------------
// Signal
// ------------------------------------------------------------------

static bool parse_signal(const cJSON *j, lnd_signal_def_t *d)
{
  memset(d, 0, sizeof(*d));
  d->min = 0;
  d->max = 100;
  get_str(j, "name", d->name, sizeof(d->name));
  get_str(j, "title", d->title, sizeof(d->title));
  get_str(j, "unit", d->unit, sizeof(d->unit));
  char kind[8] = "";
  get_str(j, "pressure", kind, sizeof(kind));
  d->pressure = strcmp(kind, "abs") == 0 ? LND_PRESSURE_ABS : (strcmp(kind, "boost") == 0 ? LND_PRESSURE_BOOST : LND_PRESSURE_NONE);
  get_float(j, "min", &d->min);
  get_float(j, "max", &d->max);
  if (d->name[0] == '\0' || !(d->max > d->min))
  {
    ESP_LOGW(TAG, "signal needs \"name\" and min < max");
    return false;
  }
  int v = d->decimals;
  get_int(j, "decimals", 0, 3, &v);
  d->decimals = (uint8_t)v;
  parse_zones(cJSON_GetObjectItemCaseSensitive(j, "zones"), d->zones, &d->zone_count, d->min, d->max, WHITE);
  return true;
}

const lnd_signal_def_t *lnd_layout_find_signal(const lnd_layout_t *l, const char *name)
{
  for (int i = 0; i < l->signal_count; i++)
  {
    if (strcmp(l->signals[i].name, name) == 0)
    {
      return &l->signals[i];
    }
  }
  return NULL;
}

// ------------------------------------------------------------------
// Widget
// ------------------------------------------------------------------

static void get_font(const cJSON *obj, const char *key, char out[LND_FONT_NAME_LEN])
{
  get_str(obj, key, out, LND_FONT_NAME_LEN);
}

static void get_u8(const cJSON *obj, const char *key, int lo, int hi, uint8_t *out)
{
  int v = *out;
  get_int(obj, key, lo, hi, &v);
  *out = (uint8_t)v;
}

static void get_i8(const cJSON *obj, const char *key, int lo, int hi, int8_t *out)
{
  int v = *out;
  get_int(obj, key, lo, hi, &v);
  *out = (int8_t)v;
}

static void get_i16(const cJSON *obj, const char *key, int lo, int hi, int16_t *out)
{
  int v = *out;
  get_int(obj, key, lo, hi, &v);
  *out = (int16_t)v;
}

static void parse_tile(const cJSON *j, lnd_widget_t *w)
{
  lnd_tile_t *t = &w->tile;
  get_font(j, "title_font", t->title_font);
  get_font(j, "unit_font", t->unit_font);
  get_color(j, "title_color", &t->title_color);
  get_color(j, "unit_color", &t->unit_color);
  get_bool(j, "show_title", &t->show_title);
  get_bool(j, "show_unit", &t->show_unit);
  get_u8(j, "border_w", 0, 20, &t->border_w);
  get_u8(j, "radius", 0, 100, &t->radius);
  get_color(j, "border_color", &t->border_color);
  get_u8(j, "pad", 0, 100, &t->pad);
  get_i8(j, "value_dy", -100, 100, &t->value_dy);
  get_i16(j, "value_dx", -300, 300, &t->value_dx);
  get_i16(j, "title_dx", -300, 300, &t->title_dx);
  get_i16(j, "title_dy", -300, 300, &t->title_dy);
  get_i16(j, "unit_dx", -300, 300, &t->unit_dx);
  get_i16(j, "unit_dy", -300, 300, &t->unit_dy);
  get_u8(j, "accent_w", 1, 200, &t->accent_w);
  get_color(j, "accent_color", &t->accent_color);
  get_bool(j, "zone_bg", &t->zone_bg);
  get_bool(j, "zone_accent", &t->zone_accent);
  get_bool(j, "zone_border", &t->zone_border);

  char s[8] = "";
  get_str(j, "align", s, sizeof(s));
  if (strcmp(s, "left") == 0) t->align = LND_ALIGN_LEFT;
  else if (strcmp(s, "center") == 0) t->align = LND_ALIGN_CENTER;
  else if (strcmp(s, "right") == 0) t->align = LND_ALIGN_RIGHT;
  s[0] = '\0';
  get_str(j, "accent", s, sizeof(s));
  if (strcmp(s, "left") == 0) t->accent = LND_ACCENT_LEFT;
  else if (strcmp(s, "right") == 0) t->accent = LND_ACCENT_RIGHT;
  else if (strcmp(s, "top") == 0) t->accent = LND_ACCENT_TOP;
  else if (strcmp(s, "bottom") == 0) t->accent = LND_ACCENT_BOTTOM;
  else if (strcmp(s, "none") == 0) t->accent = LND_ACCENT_NONE;
}

static void parse_dial(const cJSON *j, lnd_widget_t *w)
{
  lnd_dial_t *d = &w->dial;
  int v = d->angle;
  get_int(j, "angle", 10, 360, &v);
  d->angle = (uint16_t)v;
  get_i16(j, "rotation", -360, 360, &d->rotation);
  get_u8(j, "ticks", 2, 101, &d->ticks);
  get_u8(j, "major_every", 1, 50, &d->major_every);
  get_float(j, "label_div", &d->label_div);
  if (d->label_div == 0.0f)
  {
    d->label_div = 1.0f;
  }
  get_font(j, "label_font", d->label_font);
  get_color(j, "label_color", &d->label_color);
  get_color(j, "tick_color", &d->tick_color);
  get_color(j, "minor_color", &d->minor_color);
  get_color(j, "bezel_color", &d->bezel_color);
  get_color(j, "needle_color", &d->needle_color);
  get_bool(j, "show_labels", &d->show_labels);
  get_i8(j, "label_gap", -100, 100, &d->label_gap);
  get_u8(j, "major_len", 0, 100, &d->major_len);
  get_u8(j, "minor_len", 0, 100, &d->minor_len);
  get_u8(j, "major_w", 1, 20, &d->major_w);
  get_u8(j, "minor_w", 1, 20, &d->minor_w);
  get_u8(j, "bezel_w", 0, 60, &d->bezel_w);
  get_u8(j, "needle_width", 1, 30, &d->needle_w);
  get_u8(j, "needle_margin", 0, 200, &d->needle_margin);
  get_str(j, "needle_image", d->needle_image, LND_MEDIA_NAME_LEN);
  get_str(j, "bg_image", d->bg_image, LND_MEDIA_NAME_LEN);
  get_i16(j, "pivot_x", -500, 500, &d->pivot_x);
  get_i16(j, "pivot_y", -500, 500, &d->pivot_y);
  v = d->needle_scale;
  get_int(j, "needle_scale", 32, 1024, &v);
  d->needle_scale = (uint16_t)v;
  get_bool(j, "show_hub", &d->show_hub);
  get_u8(j, "hub_d", 2, 100, &d->hub_d);
  get_u8(j, "zone_w", 1, 150, &d->zone_w);
  get_i8(j, "zone_offset", -100, 100, &d->zone_offset);
  get_u8(j, "zone_opa", 0, 255, &d->zone_opa);
  get_bool(j, "center_value", &d->center_value);
  get_font(j, "cv_font", d->cv_font);
  get_color(j, "cv_color", &d->cv_color);
  get_i16(j, "cv_dx", -300, 300, &d->cv_dx);
  get_i16(j, "cv_dy", -300, 300, &d->cv_dy);
  get_str(j, "center_text", d->center_text, sizeof(d->center_text));
  get_font(j, "ct_font", d->ct_font);
  get_color(j, "ct_color", &d->ct_color);
  get_i16(j, "ct_dx", -300, 300, &d->ct_dx);
  get_i16(j, "ct_dy", -300, 300, &d->ct_dy);

  // one label per major tick must fit
  if ((d->ticks - 1) / d->major_every + 1 > LND_LAYOUT_MAX_LABELS)
  {
    ESP_LOGW(TAG, "dial: too many labels, using fewer");
    d->major_every = (uint8_t)((d->ticks - 1) / (LND_LAYOUT_MAX_LABELS - 1) + 1);
  }
}

static void parse_bar(const cJSON *j, lnd_widget_t *w)
{
  lnd_bar_t *b = &w->bar;
  get_bool(j, "vertical", &b->vertical);
  get_u8(j, "border_w", 0, 20, &b->border_w);
  get_u8(j, "radius", 0, 100, &b->radius);
  get_color(j, "border_color", &b->border_color);
  get_u8(j, "bg_opa", 0, 255, &b->bg_opa);
  get_float(j, "mark_step", &b->mark_step);
  get_float(j, "mark_div", &b->mark_div);
  if (b->mark_div == 0.0f)
  {
    b->mark_div = 1.0f;
  }
  get_font(j, "mark_font", b->mark_font);
  get_color(j, "mark_color", &b->mark_color);
  get_i8(j, "mark_gap", -100, 100, &b->mark_gap);
}

static void parse_segments(const cJSON *j, lnd_widget_t *w)
{
  lnd_segments_t *s = &w->seg;
  get_u8(j, "count", 1, LND_LAYOUT_MAX_SEGMENTS, &s->count);
  get_u8(j, "gap", 0, 50, &s->gap);
  get_u8(j, "radius", 0, 50, &s->radius);
  get_float(j, "start", &s->start);
  get_float(j, "end", &s->end);
  get_color(j, "off_color", &s->off_color);
  get_float(j, "flash_above", &s->flash_above);
}

static void set_font(char dst[LND_FONT_NAME_LEN], const char *name)
{
  strlcpy(dst, name, LND_FONT_NAME_LEN);
}

static bool parse_widget(const cJSON *j, lnd_widget_t *w)
{
  memset(w, 0, sizeof(*w));
  const cJSON *type = cJSON_GetObjectItemCaseSensitive(j, "type");
  if (!cJSON_IsString(type) || type->valuestring == NULL)
  {
    return false;
  }
  static const char *const names[] = {
      [LND_WIDGET_TILE] = "tile",   [LND_WIDGET_DIAL] = "dial", [LND_WIDGET_VALUE] = "value",
      [LND_WIDGET_TEXT] = "text",   [LND_WIDGET_BAR] = "bar",   [LND_WIDGET_SEGMENTS] = "segments",
  };
  int t = -1;
  for (int i = 0; i < (int)(sizeof(names) / sizeof(names[0])); i++)
  {
    if (strcmp(type->valuestring, names[i]) == 0)
    {
      t = i;
    }
  }
  if (t < 0)
  {
    ESP_LOGW(TAG, "unknown widget type \"%s\"", type->valuestring);
    return false;
  }
  w->type = (lnd_widget_type_t)t;

  // Defaults by type (the web editor repeats them).
  w->color = AMBER;
  w->bg_color = 0x3A3A3A;
  w->decimals = -1;
  w->div = 1.0f;
  switch (w->type)
  {
  case LND_WIDGET_TILE:
    w->w = 240;
    w->h = 104;
    set_font(w->font, "48");
    w->tile.title_color = WHITE;
    w->tile.unit_color = AMBER;
    set_font(w->tile.title_font, "14");
    set_font(w->tile.unit_font, "14");
    w->tile.show_title = w->tile.show_unit = true;
    w->tile.border_w = 2;
    w->tile.border_color = 0x5A5A5A;
    w->tile.radius = 8;
    w->tile.align = LND_ALIGN_CENTER;
    w->tile.pad = 10;
    w->tile.value_dy = 2;
    w->tile.accent = LND_ACCENT_NONE;
    w->tile.accent_w = 26;
    w->tile.accent_color = 0x9A9A9A;
    break;
  case LND_WIDGET_VALUE:
    w->w = 150;
    set_font(w->font, "48");
    break;
  case LND_WIDGET_TEXT:
    set_font(w->font, "20");
    w->color = WHITE;
    break;
  case LND_WIDGET_DIAL:
    w->w = 460;
    w->bg_color = 0x1C1C1C;
    w->dial.angle = 270;
    w->dial.rotation = 90;
    w->dial.ticks = 41;
    w->dial.major_every = 5;
    w->dial.label_div = 1.0f;
    set_font(w->dial.label_font, "36");
    w->dial.label_color = AMBER;
    w->dial.tick_color = WHITE;
    w->dial.minor_color = 0x808080;
    w->dial.bezel_color = 0x555555;
    w->dial.needle_color = 0xC00000;
    w->dial.show_labels = true;
    w->dial.major_len = 24;
    w->dial.minor_len = 12;
    w->dial.major_w = 4;
    w->dial.minor_w = 2;
    w->dial.bezel_w = 14;
    w->dial.needle_w = 8;
    w->dial.needle_margin = 70;
    w->dial.pivot_x = 34;
    w->dial.pivot_y = 22;
    w->dial.needle_scale = 256;
    w->dial.show_hub = true;
    w->dial.hub_d = 30;
    w->dial.zone_w = 66;
    w->dial.zone_opa = 255;
    set_font(w->dial.cv_font, "36");
    w->dial.cv_color = WHITE;
    set_font(w->dial.ct_font, "20");
    w->dial.ct_color = 0x9E9E9E;
    break;
  case LND_WIDGET_BAR:
    w->w = 300;
    w->h = 40;
    w->color = 0xE0FF00;
    w->bg_color = 0x393839;
    w->bar.border_w = 3;
    w->bar.border_color = WHITE;
    w->bar.bg_opa = 0;
    w->bar.mark_div = 1.0f;
    set_font(w->bar.mark_font, "26");
    w->bar.mark_color = WHITE;
    w->bar.mark_gap = 6;
    break;
  case LND_WIDGET_SEGMENTS:
    w->w = 400;
    w->h = 24;
    w->color = 0x35D07F;
    w->seg.count = 14;
    w->seg.gap = 4;
    w->seg.radius = 3;
    w->seg.off_color = 0x2A2A2A;
    break;
  }

  int v;
  v = 0;
  get_int(j, "x", -2000, 2000, &v);
  w->x = (int16_t)v;
  v = 0;
  get_int(j, "y", -2000, 2000, &v);
  w->y = (int16_t)v;
  v = w->w;
  get_int(j, "w", 0, 2000, &v);
  w->w = (uint16_t)v;
  v = w->h;
  get_int(j, "h", 0, 2000, &v);
  w->h = (uint16_t)v;

  get_str(j, "signal", w->signal, sizeof(w->signal));
  get_font(j, "font", w->font);
  get_str(j, "text", w->text, sizeof(w->text));
  get_str(j, "unit", w->unit, sizeof(w->unit));
  get_color(j, "color", &w->color);
  get_color(j, "bg_color", &w->bg_color);
  v = w->decimals;
  get_int(j, "decimals", -1, 3, &v);
  w->decimals = (int8_t)v;
  get_float(j, "div", &w->div);
  get_float(j, "round", &w->round_step);
  if (!(w->round_step > 0.0f))
  {
    w->round_step = 0.0f;
  }
  if (w->div == 0.0f)
  {
    w->div = 1.0f;
  }
  get_bool(j, "zone_color", &w->zone_color);

  switch (w->type)
  {
  case LND_WIDGET_TILE:
    parse_tile(j, w);
    break;
  case LND_WIDGET_DIAL:
    parse_dial(j, w);
    break;
  case LND_WIDGET_BAR:
    parse_bar(j, w);
    break;
  case LND_WIDGET_SEGMENTS:
    parse_segments(j, w);
    break;
  default:
    break;
  }
  return true;
}

// ------------------------------------------------------------------
// Whole layout
// ------------------------------------------------------------------

static bool parse(const char *json, lnd_layout_t *l)
{
  memset(l, 0, sizeof(*l));
  cJSON *root = cJSON_Parse(json);
  if (root == NULL)
  {
    ESP_LOGW(TAG, "JSON does not parse");
    return false;
  }

  const cJSON *sigs = cJSON_GetObjectItemCaseSensitive(root, "signals");
  const cJSON *it;
  cJSON_ArrayForEach(it, sigs)
  {
    if (l->signal_count >= LND_LAYOUT_MAX_SIGNALS)
    {
      break;
    }
    if (parse_signal(it, &l->signals[l->signal_count]))
    {
      l->signal_count++;
    }
  }

  const cJSON *screens = cJSON_GetObjectItemCaseSensitive(root, "screens");
  cJSON_ArrayForEach(it, screens)
  {
    if (l->count >= LND_UI_MAX_SCREENS)
    {
      break;
    }
    lnd_screen_t *s = &l->screens[l->count];
    s->bg_color = 0x000000;
    get_color(it, "bg_color", &s->bg_color);
    const cJSON *wj;
    cJSON_ArrayForEach(wj, cJSON_GetObjectItemCaseSensitive(it, "widgets"))
    {
      if (s->widget_count >= LND_LAYOUT_MAX_WIDGETS)
      {
        break;
      }
      lnd_widget_t *w = &s->widgets[s->widget_count];
      if (!parse_widget(wj, w))
      {
        continue;
      }
      // a widget showing a signal needs the signal in the table
      if (w->type != LND_WIDGET_TEXT && lnd_layout_find_signal(l, w->signal) == NULL)
      {
        ESP_LOGW(TAG, "widget refers to unknown signal \"%s\", skipped", w->signal);
        continue;
      }
      s->widget_count++;
    }
    l->count++;
  }
  cJSON_Delete(root);
  return l->count > 0;
}

// ------------------------------------------------------------------
// NVS
// ------------------------------------------------------------------

static char *nvs_load_json(void)
{
  nvs_handle_t h;
  if (nvs_open(LND_CONFIG_NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK)
  {
    return NULL;
  }
  size_t len = 0;
  char *buf = NULL;
  if (nvs_get_blob(h, LND_LAYOUT_NVS_KEY, NULL, &len) == ESP_OK && len > 0 && len <= LND_LAYOUT_JSON_MAX)
  {
    buf = malloc(len + 1);
    if (buf && nvs_get_blob(h, LND_LAYOUT_NVS_KEY, buf, &len) == ESP_OK)
    {
      buf[len] = '\0';
    }
    else
    {
      free(buf);
      buf = NULL;
    }
  }
  nvs_close(h);
  return buf;
}

static esp_err_t nvs_save_json(const char *json)
{
  nvs_handle_t h;
  esp_err_t err = nvs_open(LND_CONFIG_NVS_NAMESPACE, NVS_READWRITE, &h);
  if (err != ESP_OK)
  {
    return err;
  }
  err = nvs_set_blob(h, LND_LAYOUT_NVS_KEY, json, strlen(json));
  if (err == ESP_OK)
  {
    err = nvs_commit(h);
  }
  nvs_close(h);
  return err;
}

static void set_current(const lnd_layout_t *l)
{
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  s_layout = *l;
  s_generation++;
  xSemaphoreGive(s_mutex);
}

void lnd_layout_init(void)
{
  s_mutex = xSemaphoreCreateMutex();
  configASSERT(s_mutex != NULL);

  lnd_layout_t *tmp = malloc(sizeof(*tmp));
  configASSERT(tmp != NULL);
  // NDASH_LAYOUT_BUILTIN=1 (build option, for tests): ignore the saved layout.
  char *json = NDASH_LAYOUT_BUILTIN ? NULL : nvs_load_json();
  if (json != NULL && parse(json, tmp))
  {
    ESP_LOGI(TAG, "Layout from NVS: %u screens, %u signals", (unsigned)tmp->count, (unsigned)tmp->signal_count);
  }
  else
  {
    if (json != NULL)
    {
      ESP_LOGW(TAG, "Stored layout is broken, using built-in");
    }
    bool ok = parse(LND_LAYOUT_DEFAULT_JSON, tmp);
    configASSERT(ok);
    ESP_LOGI(TAG, "Built-in layout: %u screens, %u signals", (unsigned)tmp->count, (unsigned)tmp->signal_count);
  }
  free(json);
  set_current(tmp);
  free(tmp);
}

void lnd_layout_copy(lnd_layout_t *out)
{
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  *out = s_layout;
  xSemaphoreGive(s_mutex);
}

void lnd_layout_touch(void)
{
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  s_generation++;
  xSemaphoreGive(s_mutex);
}

uint32_t lnd_layout_generation(void)
{
  return s_generation;
}

// The pixel height of a font from its name: the number it ends with (48, orbitron_56, digits18).
static int font_px(const char *name, int dflt)
{
  const size_t n = strlen(name);
  size_t i = n;
  while (i > 0 && name[i - 1] >= '0' && name[i - 1] <= '9')
  {
    i--;
  }
  return i < n ? atoi(name + i) : dflt;
}

// Does the rectangle of a value or text widget touch the dial's disc?
static bool label_touches_dial(const lnd_widget_t *l, const lnd_widget_t *d)
{
  const int px = font_px(l->font, l->type == LND_WIDGET_VALUE ? 48 : 20);
  const float w = l->type == LND_WIDGET_VALUE ? (float)l->w : strlen(l->text) * px * 0.6f;
  const float h = px * 1.25f;
  const float r = d->w / 2.0f, cx = d->x + r, cy = d->y + r;
  const float nx = fminf(fmaxf(cx, l->x), l->x + w);  // the point of the rectangle nearest to the centre
  const float ny = fminf(fmaxf(cy, l->y), l->y + h);
  return (nx - cx) * (nx - cx) + (ny - cy) * (ny - cy) < r * r;
}

lnd_layout_problem_t lnd_layout_check(const lnd_layout_t *l, int *screen, int *widget)
{
  for (int si = 0; si < l->count; si++)
  {
    const lnd_screen_t *s = &l->screens[si];
    int live = 0, dials = 0;
    for (int i = 0; i < s->widget_count; i++)
    {
      const lnd_widget_t *w = &s->widgets[i];
      live += w->type != LND_WIDGET_TEXT;
      dials += w->type == LND_WIDGET_DIAL;
      lnd_layout_problem_t p = LND_LAYOUT_OK;
      if (live > LND_LAYOUT_MAX_LIVE)
      {
        p = LND_LAYOUT_TOO_MANY;
      }
      else if (dials > LND_LAYOUT_MAX_DIALS)
      {
        p = LND_LAYOUT_TOO_MANY_DIALS;
      }
      else if (w->type == LND_WIDGET_VALUE || w->type == LND_WIDGET_TEXT)
      {
        for (int k = 0; k < s->widget_count; k++)
        {
          if (s->widgets[k].type == LND_WIDGET_DIAL && label_touches_dial(w, &s->widgets[k]))
          {
            p = LND_LAYOUT_LABEL_OVER_DIAL;
            break;
          }
        }
      }
      if (p != LND_LAYOUT_OK)
      {
        if (screen) *screen = si;
        if (widget) *widget = i;
        return p;
      }
    }
  }
  return LND_LAYOUT_OK;
}

esp_err_t lnd_layout_apply_json(const char *json)
{
  size_t len = json ? strlen(json) : 0;
  if (len == 0 || len > LND_LAYOUT_JSON_MAX)
  {
    return ESP_ERR_INVALID_ARG;
  }
  lnd_layout_t *l = malloc(sizeof(*l));
  if (l == NULL)
  {
    return ESP_ERR_NO_MEM;
  }
  esp_err_t err = ESP_ERR_INVALID_ARG;
  if (parse(json, l))
  {
    const lnd_layout_problem_t p = lnd_layout_check(l, NULL, NULL);
    if (p != LND_LAYOUT_OK)
    {
      free(l);
      return p == LND_LAYOUT_LABEL_OVER_DIAL ? ESP_ERR_INVALID_STATE : ESP_ERR_INVALID_SIZE;
    }
    err = nvs_save_json(json);
    if (err == ESP_OK)
    {
      set_current(l);
      ESP_LOGI(TAG, "Layout applied: %u screens, %u signals", (unsigned)l->count, (unsigned)l->signal_count);
    }
  }
  free(l);
  return err;
}

char *lnd_layout_json_dup(void)
{
  char *json = nvs_load_json();
  if (json == NULL)
  {
    json = strdup(LND_LAYOUT_DEFAULT_JSON);
  }
  return json;
}

esp_err_t lnd_layout_reset(void)
{
  nvs_handle_t h;
  esp_err_t err = nvs_open(LND_CONFIG_NVS_NAMESPACE, NVS_READWRITE, &h);
  if (err != ESP_OK)
  {
    return err;
  }
  err = nvs_erase_key(h, LND_LAYOUT_NVS_KEY);
  if (err == ESP_ERR_NVS_NOT_FOUND)
  {
    err = ESP_OK;
  }
  if (err == ESP_OK)
  {
    err = nvs_commit(h);
  }
  nvs_close(h);
  if (err == ESP_OK)
  {
    lnd_layout_t *tmp = malloc(sizeof(*tmp));
    if (tmp == NULL)
    {
      return ESP_ERR_NO_MEM;
    }
    bool ok = parse(LND_LAYOUT_DEFAULT_JSON, tmp);
    configASSERT(ok);
    set_current(tmp);
    free(tmp);
  }
  return err;
}
