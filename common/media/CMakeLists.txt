idf_component_register(SRCS "media.c"
                    INCLUDE_DIRS "."
                    REQUIRES config
                    PRIV_REQUIRES spiffs esp_partition)
