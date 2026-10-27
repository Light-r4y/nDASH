idf_component_register(SRCS "board.c" "ch422g.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_driver_gpio esp_lcd esp_lcd_touch
                    PRIV_REQUIRES esp_driver_i2c esp_driver_ledc esp_driver_spi esp_driver_sdspi fatfs sdmmc vfs)

# The target board reaches the code (and the components that include board.h) from
# the NDASH_BOARD build option in the top-level CMakeLists.txt:
#   <BOARD>=1          exactly one of JC8048W550C / WAVESHARE_S3_LCD7 /
#                      WAVESHARE_S3_LCD5 / Sunton_S3_LCD7, what board.h switches on
#   NDASH_BOARD_NAME   board name string, printed at boot
target_compile_definitions(${COMPONENT_LIB} PUBLIC
    "${NDASH_BOARD}=1"
    "NDASH_BOARD_NAME=\"${NDASH_BOARD}\""
)
