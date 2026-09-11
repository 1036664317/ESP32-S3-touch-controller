#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "driver/i2c.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "lcd_driver.h"
#include "qmi8658.h"
#include "axs15231b.h"
#include "ble_combo.h"
#include "mahony.h"
#include "ui.h"

static const char *TAG = "MAIN";

// Global handles for LVGL callbacks
lcd_dev_t g_lcd;
axs15231b_dev_t g_touch;
qmi8658_dev_t g_imu;
mahony_state_t g_mahony;
ble_combo_state_t g_ble;

// GPIO pins for ESP32-S3-Touch-LCD-3.49 V1
// LCD QSPI: CS=9, PCLK=10, D0=11, D1=12, D2=13, D3=14, RST=21, BL=8
// I2C0 (RTC+IMU): SDA=47, SCL=48
// I2C1 (Touch): SDA=17, SCL=18

static bool g_touch_ok = false;
static bool g_imu_ok = false;

static void on_ble_connected(void *ctx)
{
    ESP_LOGI(TAG, "BLE connected - notifying UI");
    ui_set_bt_status(true);
}

static void on_ble_disconnected(void *ctx)
{
    ESP_LOGI(TAG, "BLE disconnected - notifying UI");
    ui_set_bt_status(false);
}

static void imu_task(void *arg)
{
    TickType_t last_wake = xTaskGetTickCount();

    while (1) {
        if (g_imu_ok) {
            if (g_ui.air_mouse_active) {
                ui_update_imu(&g_mahony, &g_imu);
            }
            ui_detect_gesture(&g_imu);
            ui_check_sleep(&g_imu);
            ui_wake_from_imu(&g_imu);
        }
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(10));
    }
}

static void ui_task(void *arg)
{
    TickType_t last_wake = xTaskGetTickCount();

    while (1) {
        // Update LVGL (automatically polls touch_read_cb)
        ui_update();

        // Send BLE reports
        ui_send_reports(&g_ble);

        // Update battery (simulated for now)
        static int bat_counter = 0;
        if (++bat_counter >= 100) {
            bat_counter = 0;
            ui_set_battery(85);  // TODO: implement actual ADC reading
        }

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(16));  // ~60Hz
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== ESP32-S3 VR Controller ===");

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize LCD (QSPI, pins handled internally)
    ESP_LOGI(TAG, "Initializing LCD...");
    ret = lcd_init(&g_lcd, LCD_PHYS_WIDTH, LCD_PHYS_HEIGHT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LCD init failed: %s", esp_err_to_name(ret));
        return;
    }

    // === TEST: Fill screen with solid red BEFORE LVGL init ===
    ESP_LOGI(TAG, "TEST: Filling screen with red...");
    lcd_fill_screen(&g_lcd, 0xF800);  // RGB565 red
    ESP_LOGI(TAG, "TEST: Red fill done. Waiting 2 seconds...");
    vTaskDelay(pdMS_TO_TICKS(2000));
    ESP_LOGI(TAG, "TEST: Now filling screen with green...");
    lcd_fill_screen(&g_lcd, 0x07E0);  // RGB565 green
    ESP_LOGI(TAG, "TEST: Green fill done. Waiting 2 seconds...");
    vTaskDelay(pdMS_TO_TICKS(2000));

    // Initialize touch (I2C1 on GPIO17/18)
    ESP_LOGI(TAG, "Initializing touch...");
    ret = axs15231b_init(&g_touch, I2C_NUM_1, AXS15231B_I2C_ADDR);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Touch init failed: %s", esp_err_to_name(ret));
    } else {
        g_touch_ok = true;
    }

    // Initialize IMU (I2C0 on GPIO47/48)
    ESP_LOGI(TAG, "Initializing IMU...");
    ret = qmi8658_init(&g_imu, I2C_NUM_0, QMI8658_I2C_ADDR);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "IMU init failed: %s", esp_err_to_name(ret));
    } else {
        g_imu_ok = true;
        qmi8658_calibrate(&g_imu, 50);
    }

    // Initialize Mahony filter
    mahony_init(&g_mahony, 0.01f);  // 100Hz

    // Initialize UI
    ESP_LOGI(TAG, "Initializing UI...");
    ret = ui_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "UI init failed: %s", esp_err_to_name(ret));
        return;
    }

    // Initialize BLE
    ESP_LOGI(TAG, "Initializing BLE Combo...");
    ret = ble_combo_init(BLE_COMBO_DEVICE_ALL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "BLE init failed: %s", esp_err_to_name(ret));
    }
    ble_combo_register_callbacks(on_ble_connected, on_ble_disconnected, NULL);

    // Start BLE advertising
    vTaskDelay(pdMS_TO_TICKS(500));
    ble_combo_start();

    // Set initial UI state
    ui_set_mode(MODE_JOYSTICK);
    ui_set_bt_status(false);
    ui_set_battery(100);

    // Create tasks
    xTaskCreatePinnedToCore(imu_task, "imu_task", 4096, NULL, 5, NULL, 1);
    xTaskCreatePinnedToCore(ui_task, "ui_task", 8192, NULL, 5, NULL, 0);

    ESP_LOGI(TAG, "All systems initialized. Starting main loop.");

    // Main loop (idle task for background cleanup)
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
