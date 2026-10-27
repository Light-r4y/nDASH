idf_component_register(SRCS "can_task.c"
                    INCLUDE_DIRS "."
                    REQUIRES freertos esp_driver_twai esp_driver_gpio esp_timer board config signals)
