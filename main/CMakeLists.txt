idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS "."
                    REQUIRES nvs_flash app_update board config media ui can webcfg buttons touch logger)
