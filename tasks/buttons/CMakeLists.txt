idf_component_register(SRCS "buttons_task.c"
                    INCLUDE_DIRS "."
                    REQUIRES freertos esp_driver_gpio board config webcfg ui)
