idf_component_register(SRCS "layout.c" "settings.c" "can_map.c" "auth.c" "bench_cmds.c" "units.c"
                    INCLUDE_DIRS "."
                    REQUIRES nvs_flash freertos espressif__cjson)

# NDASH_DEMO build option (top-level CMakeLists.txt): the default of can.demo.
target_compile_definitions(${COMPONENT_LIB} PRIVATE "NDASH_DEMO=${NDASH_DEMO}" "NDASH_LAYOUT_BUILTIN=${NDASH_LAYOUT_BUILTIN}")
