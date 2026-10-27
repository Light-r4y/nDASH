idf_component_register(SRCS "touch_task.c"
                    INCLUDE_DIRS "."
                    REQUIRES freertos board config ui
                    PRIV_REQUIRES esp_lcd_touch)
