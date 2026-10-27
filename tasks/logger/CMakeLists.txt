idf_component_register(SRCS "logger_task.c" "logger_logic.c" "mlg.c"
                    INCLUDE_DIRS "."
                    REQUIRES freertos board config signals esp_timer esp_app_format)
