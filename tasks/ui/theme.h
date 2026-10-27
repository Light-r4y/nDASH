#pragma once

#include "lvgl.h"

// The compiled-in custom fonts (their names for a layout are in fonts.c). The look of the
// widgets is not fixed here: it is set per widget in the layout (common/config/layout.h),
// whose defaults are in layout.c.
LV_FONT_DECLARE(ui_font_FontIndicator);       // "digits18": 18 px digits
LV_FONT_DECLARE(ui_font_FontIndicatorLabel);  // "tick10": 10 px
LV_FONT_DECLARE(ui_font_FontLabel);           // "label16": 16 px, ASCII
LV_FONT_DECLARE(ui_font_FontRPM);             // "digits26": 26 px digits
LV_FONT_DECLARE(ui_font_FontSpeed);           // "speed160": 160 px digits

// ---- screen change ---------------------------------------------------------
#define LND_SCREEN_ANIM_MS 500
