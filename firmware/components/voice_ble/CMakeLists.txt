idf_component_register(
    SRCS "voice_ble.c" "voice_hid.c"
    INCLUDE_DIRS "include"
    REQUIRES app_update bt nvs_flash esp_timer
)
