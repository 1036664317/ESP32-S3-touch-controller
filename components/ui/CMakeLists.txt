idf_component_register(
    SRCS "ui.c"
    INCLUDE_DIRS "include"
    REQUIRES lvgl qmi8658 axs15231b lcd_driver ble_combo mahony esp_timer
)
