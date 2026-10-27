#include "fps_overlay.h"
#include "conf.h"

#if NDASH_UI_SHOW_FPS

#include <esp_log.h>
#include <esp_timer.h>

static const char *TAG = "fps";

#define FPS_OVERLAY_PERIOD_MS 1000

// Written by the display event callback and read by the update timer. Both run
// inside the LVGL task, so no locking is needed.
static int64_t s_render_t0;   // RENDER_START of the current frame
static int64_t s_flush_t;     // start of the last flush of the current frame
static uint32_t s_frames;
static int64_t s_draw_us;     // sum over frames of  (last flush start - render start)
static int64_t s_wait_us;     // sum over frames of  (render ready - last flush start)
static int64_t s_max_draw_us;
static uint64_t s_px;
static lv_obj_t *s_label;

static void render_cb(lv_event_t *e)
{
  const int64_t now = esp_timer_get_time();
  switch (lv_event_get_code(e))
  {
  case LV_EVENT_RENDER_START:
    s_render_t0 = now;
    s_flush_t = now;
    break;
  case LV_EVENT_FLUSH_START:
    s_flush_t = now;
    s_px += (uint64_t)lv_area_get_size((const lv_area_t *)lv_event_get_param(e));
    break;
  default: // LV_EVENT_RENDER_READY
  {
    const int64_t draw = s_flush_t - s_render_t0;
    s_draw_us += draw;
    if (draw > s_max_draw_us)
    {
      s_max_draw_us = draw;
    }
    s_wait_us += now - s_flush_t;
    s_frames++;
    break;
  }
  }
}

static void update_cb(lv_timer_t *timer)
{
  (void)timer;
  const uint32_t frames = s_frames;
  const unsigned avg_r = frames ? (unsigned)(s_draw_us / frames / 1000) : 0;
  const unsigned max_r = (unsigned)(s_max_draw_us / 1000);
  const unsigned avg_w = frames ? (unsigned)(s_wait_us / frames / 1000) : 0;
  const unsigned kpx = frames ? (unsigned)(s_px / frames / 1000) : 0;

  lv_label_set_text_fmt(s_label, "%u fps  r %u (max %u) w %u ms  %u Kpx", (unsigned)frames, avg_r, max_r, avg_w, kpx);

  if (frames > 1)
  {
    ESP_LOGI(TAG, "%u fps  r %u (max %u) w %u ms  %u Kpx", (unsigned)frames, avg_r, max_r, avg_w, kpx);
  }

  s_frames = 0;
  s_draw_us = 0;
  s_wait_us = 0;
  s_max_draw_us = 0;
  s_px = 0;
}

void lnd_fps_overlay_start(lv_display_t *disp)
{
  lv_display_add_event_cb(disp, render_cb, LV_EVENT_RENDER_START, NULL);
  lv_display_add_event_cb(disp, render_cb, LV_EVENT_FLUSH_START, NULL);
  lv_display_add_event_cb(disp, render_cb, LV_EVENT_RENDER_READY, NULL);

  s_label = lv_label_create(lv_layer_top());
  lv_obj_set_style_text_color(s_label, lv_color_hex(0x00FF00), 0);
  lv_obj_set_style_bg_color(s_label, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(s_label, LV_OPA_70, 0);
  lv_obj_set_style_pad_all(s_label, 2, 0);
  lv_label_set_text(s_label, "-- fps");
  lv_obj_align(s_label, LV_ALIGN_BOTTOM_MID, 0, -2);

  lv_timer_create(update_cb, FPS_OVERLAY_PERIOD_MS, NULL);
}

#else  // NDASH_UI_SHOW_FPS

void lnd_fps_overlay_start(lv_display_t *disp)
{
  (void)disp;
}

#endif  // NDASH_UI_SHOW_FPS
