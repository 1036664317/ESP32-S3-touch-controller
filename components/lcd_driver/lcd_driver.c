#include "lcd_driver.h"
#include "driver/ledc.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "LCD";

// V1 Pin definitions for ESP32-S3-Touch-LCD-3.49
#define LCD_CS_PIN      9
#define LCD_PCLK_PIN    10
#define LCD_D0_PIN      11
#define LCD_D1_PIN      12
#define LCD_D2_PIN      13
#define LCD_D3_PIN      14
#define LCD_RST_PIN     21
#define LCD_BL_PIN      8
#define LCD_HOST        SPI3_HOST

#define LCD_PCLK_HZ     (40 * 1000 * 1000)

static void lcd_send_cmd(esp_lcd_panel_io_handle_t io, uint32_t cmd, const uint8_t *data, int len)
{
    esp_lcd_panel_io_tx_param(io, cmd, data, len);
}

static void lcd_reset(void)
{
    gpio_set_direction(LCD_RST_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(LCD_RST_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(30));
    gpio_set_level(LCD_RST_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(250));
    gpio_set_level(LCD_RST_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(30));
}

esp_err_t lcd_init(lcd_dev_t *dev, uint16_t width, uint16_t height)
{
    dev->width = width;
    dev->height = height;

    ESP_LOGI(TAG, "Initializing QSPI LCD %dx%d", width, height);

    // Reset LCD
    lcd_reset();

    // QSPI bus configuration
    spi_bus_config_t buscfg = {0};
    buscfg.data0_io_num = LCD_D0_PIN;
    buscfg.data1_io_num = LCD_D1_PIN;
    buscfg.sclk_io_num = LCD_PCLK_PIN;
    buscfg.data2_io_num = LCD_D2_PIN;
    buscfg.data3_io_num = LCD_D3_PIN;
    buscfg.max_transfer_sz = width * height * sizeof(uint16_t);

    esp_err_t ret = spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI bus init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Panel IO configuration (QSPI mode)
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_spi_config_t io_config = {0};
    io_config.cs_gpio_num = LCD_CS_PIN;
    io_config.dc_gpio_num = -1;  // No DC pin for QSPI
    io_config.spi_mode = 3;
    io_config.pclk_hz = LCD_PCLK_HZ;
    io_config.trans_queue_depth = 10;
    io_config.lcd_cmd_bits = 32;  // 32-bit command for AXS15231B
    io_config.lcd_param_bits = 8;
    io_config.flags.quad_mode = true;

    ret = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &io_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LCD IO init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    dev->io_handle = io_handle;

    // Use ST7789 panel driver (AXS15231B is command-compatible)
    esp_lcd_panel_handle_t panel_handle = NULL;
    esp_lcd_panel_dev_config_t panel_config = {0};
    panel_config.reset_gpio_num = -1;  // RST handled manually
    panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
    panel_config.bits_per_pixel = 16;

    ret = esp_lcd_new_panel_st7789(io_handle, &panel_config, &panel_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LCD panel init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    dev->panel_handle = panel_handle;

    esp_lcd_panel_reset(panel_handle);
    esp_lcd_panel_init(panel_handle);
    esp_lcd_panel_set_gap(panel_handle, 0, 0);
    esp_lcd_panel_invert_color(panel_handle, true);
    esp_lcd_panel_mirror(panel_handle, false, false);

    // Send AXS15231B specific QSPI init commands via IO handle
    uint8_t zero = 0x00;
    lcd_send_cmd(io_handle, 0x11, &zero, 0);  // Sleep Out
    vTaskDelay(pdMS_TO_TICKS(120));
    lcd_send_cmd(io_handle, 0x29, &zero, 0);  // Display ON
    vTaskDelay(pdMS_TO_TICKS(20));

    // Turn on backlight
    ledc_timer_config_t ledc_timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .timer_num = LEDC_TIMER_3,
        .freq_hz = 50000,
        .clk_cfg = LEDC_SLOW_CLK_RC_FAST,
    };
    ledc_timer_config(&ledc_timer);

    ledc_channel_config_t ledc_channel = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_1,
        .timer_sel = LEDC_TIMER_3,
        .gpio_num = LCD_BL_PIN,
        .duty = 255,
        .hpoint = 0,
    };
    ledc_channel_config(&ledc_channel);

    ESP_LOGI(TAG, "QSPI LCD initialized successfully");
    return ESP_OK;
}

esp_err_t lcd_fill_rect(lcd_dev_t *dev, uint16_t x, uint16_t y,
                         uint16_t w, uint16_t h, uint16_t color)
{
    uint16_t *buf = heap_caps_malloc(w * h * sizeof(uint16_t), MALLOC_CAP_DMA);
    if (!buf) return ESP_ERR_NO_MEM;
    for (int i = 0; i < w * h; i++) buf[i] = color;
    esp_lcd_panel_draw_bitmap(dev->panel_handle, x, y, x + w, y + h, buf);
    heap_caps_free(buf);
    return ESP_OK;
}

esp_err_t lcd_set_brightness(uint8_t brightness)
{
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, brightness);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1);
    return ESP_OK;
}
