idf_component_register(SRCS "ui_task.c" "screens.c" "fonts.c" "images.c" "overlay.c" "fps_overlay.c"
                            "fonts/ui_font_FontIndicator.c" "fonts/ui_font_FontIndicatorLabel.c"
                            "fonts/ui_font_FontLabel.c" "fonts/ui_font_FontRPM.c" "fonts/ui_font_FontSpeed.c"
                    INCLUDE_DIRS "."
                    EMBED_FILES "images/needle_classic.bin" "images/needle_neon.bin" "images/needle_stripe.bin"
                                "fonts/bin/blackops_28.fnt" "fonts/bin/blackops_40.fnt" "fonts/bin/blackops_56.fnt" "fonts/bin/blackops_96.fnt" "fonts/bin/jura_24.fnt" "fonts/bin/jura_36.fnt" "fonts/bin/michroma_24.fnt" "fonts/bin/michroma_36.fnt" "fonts/bin/orbitron_24.fnt" "fonts/bin/orbitron_36.fnt" "fonts/bin/orbitron_56.fnt" "fonts/bin/orbitron_96.fnt" "fonts/bin/russo_28.fnt" "fonts/bin/russo_40.fnt" "fonts/bin/russo_64.fnt" "fonts/bin/seg7_120.fnt" "fonts/bin/seg7_40.fnt" "fonts/bin/seg7_72.fnt"
                    REQUIRES freertos esp_timer esp_lcd_touch lvgl esp_lvgl_port board config signals media webcfg)

#   NDASH_UI_SHOW_FPS  1/0, FPS and frame-time overlay
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    "NDASH_UI_SHOW_FPS=${NDASH_UI_SHOW_FPS}"
)
