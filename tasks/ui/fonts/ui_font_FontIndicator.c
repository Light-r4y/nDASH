/*******************************************************************************
 * Size: 18 px
 * Bpp: 1
 * Opts: --bpp 1 --size 18 --font D:/Users/Desktop/rusefi_dash_5 -lite/assets/Fussion-3zgAz.ttf -o D:/Users/Desktop/rusefi_dash_5 -lite/assets\ui_font_FontIndicator.c --format lvgl --symbols -.0123456789 --no-compress --no-prefilter
 ******************************************************************************/

#include "lvgl.h"

#ifndef UI_FONT_FONTINDICATOR
#define UI_FONT_FONTINDICATOR 1
#endif

#if UI_FONT_FONTINDICATOR

/*-----------------
 *    BITMAPS
 *----------------*/

/*Store the image of the glyphs*/
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    /* U+002D "-" */
    0xff, 0xff, 0xf0,

    /* U+002E "." */
    0xff,

    /* U+0030 "0" */
    0x1f, 0xff, 0x1f, 0xff, 0xde, 0x1, 0xfe, 0x0,
    0xff, 0x0, 0x7f, 0x80, 0x3f, 0xc0, 0x1f, 0xe0,
    0xf, 0xf0, 0x7, 0xf8, 0x7, 0xff, 0xff, 0xcf,
    0xff, 0xc0,

    /* U+0031 "1" */
    0xfd, 0xfc, 0x78, 0xf1, 0xe3, 0xc7, 0x8f, 0x1e,
    0x3c, 0x78, 0xf0,

    /* U+0032 "2" */
    0x7f, 0xfe, 0x3f, 0xff, 0x80, 0x3, 0xe0, 0x0,
    0xf0, 0x0, 0x79, 0xff, 0xfd, 0xff, 0xfd, 0xf0,
    0x0, 0xf0, 0x0, 0x78, 0x0, 0x3f, 0xff, 0xdf,
    0xff, 0xe0,

    /* U+0033 "3" */
    0xff, 0xfc, 0xff, 0xfe, 0x0, 0x1f, 0x0, 0xf,
    0x0, 0xf, 0x7f, 0xfe, 0x7f, 0xfe, 0x0, 0xf,
    0x0, 0xf, 0x0, 0x1f, 0xff, 0xfe, 0xff, 0xfc,

    /* U+0034 "4" */
    0x7, 0xff, 0x87, 0xff, 0xc7, 0x81, 0xe7, 0x80,
    0xf7, 0x80, 0x7f, 0x80, 0x3f, 0xc0, 0x1f, 0xff,
    0xff, 0xff, 0xff, 0x80, 0x3, 0xc0, 0x1, 0xe0,
    0x0, 0xf0,

    /* U+0035 "5" */
    0x7f, 0xff, 0x7f, 0xff, 0xbc, 0x0, 0x1e, 0x0,
    0xf, 0x0, 0x7, 0xff, 0xfd, 0xff, 0xfe, 0x0,
    0xf, 0x0, 0x7, 0x80, 0x7, 0xdf, 0xff, 0xcf,
    0xff, 0xc0,

    /* U+0036 "6" */
    0x1f, 0xff, 0x1f, 0xff, 0x9e, 0x0, 0x1e, 0x0,
    0xf, 0x0, 0x7, 0xff, 0xff, 0xff, 0xff, 0xe0,
    0xf, 0xf0, 0x7, 0xf8, 0x7, 0xff, 0xff, 0xcf,
    0xff, 0xc0,

    /* U+0037 "7" */
    0xff, 0xff, 0xff, 0xfc, 0x0, 0x78, 0x0, 0xf0,
    0x3, 0xe0, 0xf, 0x80, 0x3e, 0x0, 0xf8, 0x3,
    0xe0, 0xf, 0x80, 0x3e, 0x0, 0xf8, 0x0,

    /* U+0038 "8" */
    0x3f, 0xfe, 0x3f, 0xff, 0xbe, 0x3, 0xfe, 0x0,
    0xff, 0x0, 0x7f, 0xff, 0xff, 0xff, 0xff, 0xe0,
    0xf, 0xf0, 0x7, 0xfc, 0x7, 0xdf, 0xff, 0xc7,
    0xff, 0xc0,

    /* U+0039 "9" */
    0x3f, 0xfe, 0x3f, 0xff, 0xbe, 0x3, 0xfe, 0x0,
    0xff, 0x0, 0x7f, 0xff, 0xfd, 0xff, 0xfe, 0x0,
    0xf, 0x0, 0x7, 0x80, 0x7, 0xdf, 0xff, 0xcf,
    0xff, 0xc0
};


/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 191, .box_w = 10, .box_h = 2, .ofs_x = 1, .ofs_y = 5},
    {.bitmap_index = 3, .adv_w = 96, .box_w = 4, .box_h = 2, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 4, .adv_w = 296, .box_w = 17, .box_h = 12, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 30, .adv_w = 134, .box_w = 7, .box_h = 12, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 41, .adv_w = 296, .box_w = 17, .box_h = 12, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 67, .adv_w = 286, .box_w = 16, .box_h = 12, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 91, .adv_w = 295, .box_w = 17, .box_h = 12, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 117, .adv_w = 296, .box_w = 17, .box_h = 12, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 143, .adv_w = 296, .box_w = 17, .box_h = 12, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 169, .adv_w = 267, .box_w = 15, .box_h = 12, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 192, .adv_w = 305, .box_w = 17, .box_h = 12, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 218, .adv_w = 296, .box_w = 17, .box_h = 12, .ofs_x = 1, .ofs_y = 0}
};

/*---------------------
 *  CHARACTER MAPPING
 *--------------------*/

static const uint8_t glyph_id_ofs_list_0[] = {
    0, 1, 0, 2, 3, 4, 5, 6,
    7, 8, 9, 10, 11
};

/*Collect the unicode lists and glyph_id offsets*/
static const lv_font_fmt_txt_cmap_t cmaps[] =
{
    {
        .range_start = 45, .range_length = 13, .glyph_id_start = 1,
        .unicode_list = NULL, .glyph_id_ofs_list = glyph_id_ofs_list_0, .list_length = 13, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_FULL
    }
};

/*-----------------
 *    KERNING
 *----------------*/


/*Map glyph_ids to kern left classes*/
static const uint8_t kern_left_class_mapping[] =
{
    0, 0, 0, 0, 1, 2, 3, 4,
    5, 6, 7, 3, 8
};

/*Map glyph_ids to kern right classes*/
static const uint8_t kern_right_class_mapping[] =
{
    0, 0, 0, 1, 2, 3, 4, 5,
    6, 1, 7, 8, 9
};

/*Kern values between classes*/
static const int8_t kern_class_values[] =
{
    -1, 0, -3, -5, -5, -3, -11, -3,
    -3, -6, -13, 0, -11, -11, -5, -15,
    -9, -5, -3, -13, -6, 0, -8, -5,
    -15, -5, -6, -1, -10, -3, -5, 0,
    -3, -11, -3, -3, -4, -13, -6, -11,
    -7, 0, -15, -5, -11, -4, -13, -6,
    -11, -7, -10, -15, -5, -11, -10, -11,
    -13, -11, -14, -6, 0, -12, -8, -2,
    -13, -6, -11, -6, -4, -14, -4, 0
};


/*Collect the kern class' data in one place*/
static const lv_font_fmt_txt_kern_classes_t kern_classes =
{
    .class_pair_values   = kern_class_values,
    .left_class_mapping  = kern_left_class_mapping,
    .right_class_mapping = kern_right_class_mapping,
    .left_class_cnt      = 8,
    .right_class_cnt     = 9,
};

/*--------------------
 *  ALL CUSTOM DATA
 *--------------------*/

#if LVGL_VERSION_MAJOR == 8
/*Store all the custom data of the font*/
static  lv_font_fmt_txt_glyph_cache_t cache;
#endif

#if LVGL_VERSION_MAJOR >= 8
static const lv_font_fmt_txt_dsc_t font_dsc = {
#else
static lv_font_fmt_txt_dsc_t font_dsc = {
#endif
    .glyph_bitmap = glyph_bitmap,
    .glyph_dsc = glyph_dsc,
    .cmaps = cmaps,
    .kern_dsc = &kern_classes,
    .kern_scale = 16,
    .cmap_num = 1,
    .bpp = 1,
    .kern_classes = 1,
    .bitmap_format = 0,
#if LVGL_VERSION_MAJOR == 8
    .cache = &cache
#endif
};



/*-----------------
 *  PUBLIC FONT
 *----------------*/

/*Initialize a public general font descriptor*/
#if LVGL_VERSION_MAJOR >= 8
const lv_font_t ui_font_FontIndicator = {
#else
lv_font_t ui_font_FontIndicator = {
#endif
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,    /*Function pointer to get glyph's data*/
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,    /*Function pointer to get glyph's bitmap*/
    .line_height = 12,          /*The maximum line height required by the font*/
    .base_line = 0,             /*Baseline measured from the bottom of the line*/
#if !(LVGL_VERSION_MAJOR == 6 && LVGL_VERSION_MINOR == 0)
    .subpx = LV_FONT_SUBPX_NONE,
#endif
#if LV_VERSION_CHECK(7, 4, 0) || LVGL_VERSION_MAJOR >= 8
    .underline_position = 0,
    .underline_thickness = 0,
#endif
    .dsc = &font_dsc,          /*The custom font data. Will be accessed by `get_glyph_bitmap/dsc` */
#if LV_VERSION_CHECK(8, 2, 0) || LVGL_VERSION_MAJOR >= 9
    .fallback = NULL,
#endif
    .user_data = NULL,
};



#endif /*#if UI_FONT_FONTINDICATOR*/

