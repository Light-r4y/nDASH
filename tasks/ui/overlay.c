#include "overlay.h"
#include "conf.h"
#include "lvgl.h"
#include "webcfg_task.h"

#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <stdio.h>
#include <string.h>

#define CARD_W 440
#define CARD_H 214
#define COLOR_ACCENT 0x00C0FF
#define COLOR_PANEL  0x0B1015

static lv_obj_t *s_ap_card, *s_ap_title, *s_ap_l1, *s_ap_l2, *s_ap_l3, *s_ap_badge, *s_toast;

// A short message: set from any task, shown by the UI task.
static char s_toast_text[48];
static volatile bool s_toast_new;
static portMUX_TYPE s_toast_lock = portMUX_INITIALIZER_UNLOCKED;

void lnd_ui_toast(const char *text)
{
  portENTER_CRITICAL(&s_toast_lock);
  strlcpy(s_toast_text, text, sizeof(s_toast_text));
  s_toast_new = true;
  portEXIT_CRITICAL(&s_toast_lock);
}

static lv_obj_t *card_label(lv_obj_t *parent, int y, uint32_t color)
{
  lv_obj_t *l = lv_label_create(parent);
  lv_obj_set_width(l, CARD_W - 20);
  lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(l, &lv_font_montserrat_26, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
  lv_obj_align(l, LV_ALIGN_TOP_MID, 0, y);
  lv_label_set_text(l, "");
  return l;
}

void lnd_overlay_init(void)
{
  lv_obj_t *top = lv_layer_top();

  s_toast = lv_label_create(top);
  lv_obj_set_style_text_font(s_toast, &lv_font_montserrat_26, 0);
  lv_obj_set_style_text_color(s_toast, lv_color_white(), 0);
  lv_obj_set_style_bg_color(s_toast, lv_color_hex(COLOR_PANEL), 0);
  lv_obj_set_style_bg_opa(s_toast, LV_OPA_90, 0);
  lv_obj_set_style_border_color(s_toast, lv_color_hex(COLOR_ACCENT), 0);
  lv_obj_set_style_border_width(s_toast, 2, 0);
  lv_obj_set_style_radius(s_toast, 12, 0);
  lv_obj_set_style_pad_all(s_toast, 10, 0);
  lv_obj_align(s_toast, LV_ALIGN_BOTTOM_MID, 0, -20);
  lv_obj_set_hidden(s_toast, true);

  s_ap_card = lv_obj_create(top);
  lv_obj_set_size(s_ap_card, CARD_W, CARD_H);
  lv_obj_center(s_ap_card);
  lv_obj_set_clickable(s_ap_card, false);
  lv_obj_set_scrollable(s_ap_card, false);
  lv_obj_set_style_bg_color(s_ap_card, lv_color_hex(COLOR_PANEL), 0);
  lv_obj_set_style_bg_opa(s_ap_card, LV_OPA_90, 0);
  lv_obj_set_style_border_color(s_ap_card, lv_color_hex(COLOR_ACCENT), 0);
  lv_obj_set_style_border_width(s_ap_card, 2, 0);
  lv_obj_set_style_radius(s_ap_card, 18, 0);
  lv_obj_set_style_pad_all(s_ap_card, 8, 0);
  s_ap_title = card_label(s_ap_card, 6, COLOR_ACCENT);
  s_ap_l1 = card_label(s_ap_card, 52, 0xFFFFFF);
  s_ap_l2 = card_label(s_ap_card, 94, 0xFFFFFF);
  s_ap_l3 = card_label(s_ap_card, 136, 0x9E9E9E);
  lv_obj_set_hidden(s_ap_card, true);

  s_ap_badge = lv_label_create(top);
  lv_obj_set_style_text_font(s_ap_badge, &lv_font_montserrat_26, 0);
  lv_obj_set_style_text_color(s_ap_badge, lv_color_hex(COLOR_ACCENT), 0);
  lv_obj_align(s_ap_badge, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
  lv_obj_set_hidden(s_ap_badge, true);
}

void lnd_overlay_update(void)
{
  static lnd_ap_state_t last_state = LND_AP_OFF;
  static int last_clients = -1;
  static int64_t up_ms;
  static bool card_shown;
  static int64_t toast_until;

  lnd_ap_info_t info;
  lnd_webcfg_get_ap_info(&info);
  const int64_t now = esp_timer_get_time() / 1000;

  if (s_toast_new)
  {
    char txt[sizeof(s_toast_text)];
    portENTER_CRITICAL(&s_toast_lock);
    strlcpy(txt, s_toast_text, sizeof(txt));
    s_toast_new = false;
    portEXIT_CRITICAL(&s_toast_lock);
    lv_label_set_text(s_toast, txt);
    lv_obj_set_hidden(s_toast, false);
    toast_until = now + LND_TOAST_MS;
  }
  else if (toast_until != 0 && now >= toast_until)
  {
    lv_obj_set_hidden(s_toast, true);
    toast_until = 0;
  }

  if (info.state != last_state)
  {
    last_state = info.state;
    if (info.state == LND_AP_STARTING)
    {
      lv_label_set_text(s_ap_title, LV_SYMBOL_WIFI "  Wi-Fi");
      lv_label_set_text(s_ap_l1, "Starting...");
      lv_label_set_text(s_ap_l2, "");
      lv_label_set_text(s_ap_l3, "");
    }
    else if (info.state == LND_AP_UP)
    {
      up_ms = now;
      char pass[80];
      snprintf(pass, sizeof(pass), info.password[0] ? "Pass: %s" : "Open network", info.password);
      lv_label_set_text(s_ap_title, LV_SYMBOL_WIFI "  Wi-Fi");
      lv_label_set_text(s_ap_l1, info.ssid);
      lv_label_set_text(s_ap_l2, pass);
      lv_label_set_text(s_ap_l3, "http://192.168.4.1");
    }
  }

  const bool card = info.state == LND_AP_STARTING || (info.state == LND_AP_UP && now - up_ms < LND_AP_CARD_MS);
  if (card != card_shown)
  {
    card_shown = card;
    lv_obj_set_hidden(s_ap_card, !card);
  }

  if (info.state == LND_AP_UP)
  {
    if (info.clients != last_clients)
    {
      last_clients = info.clients;
      char txt[24];
      snprintf(txt, sizeof(txt), LV_SYMBOL_WIFI " %u", (unsigned)info.clients);
      lv_label_set_text(s_ap_badge, txt);
    }
    lv_obj_set_hidden(s_ap_badge, false);
  }
}
