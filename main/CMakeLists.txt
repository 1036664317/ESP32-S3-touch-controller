idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES driver esp_timer esp_lcd nvs_flash bt freertos qmi8658 axs15231b lcd_driver ble_combo mahony ui
)
