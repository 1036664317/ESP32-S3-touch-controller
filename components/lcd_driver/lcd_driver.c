#include "lcd_driver.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "esp_lcd_panel_vendor.h"
#include <string.h>

static const char *TAG = "LCD";

#define LCD_SPI_CLOCK_HZ     (40 * 1000 * 1000)
#define LCD_CMD_BITS         8
#define LCD_PARAM_BITS       8

esp_err_t lcd_init(lcd_dev_t *dev, spi_host_device_t spi_host,
                   gpio_num_t cs, gpio_num_t dc, gpio_num_t rst, gpio_num_t bl,
                   uint16_t width, uint16_t height)
{
    dev->spi_host = spi_host;
    dev->lcd_cs = cs;
    dev->lcd_dc = dc;
    dev->lcd_rst = rst;
    dev->lcd_bl = bl;
    dev->width = width;
    dev->height = height;

    ESP_LOGI(TAG, "Initializing LCD %dx%d", width, height);

    // SPI bus configuration
    spi_bus_config_t buscfg = {
        .mosi_io_num = 11,
        .miso_io_num = -1,
        .sclk_io_num = 12,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = width * height * sizeof(uint16_t),
    };
    esp_err_t ret = spi_bus_initialize(spi_host, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI bus init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // LCD panel IO
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = dc,
        .cs_gpio_num = cs,
        .pclk_hz = LCD_SPI_CLOCK_HZ,
        .lcd_cmd_bits = LCD_CMD_BITS,
        .lcd_param_bits = LCD_PARAM_BITS,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };
    ret = esp_lcd_new_panel_io_spi(spi_host, &io_config, &io_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LCD IO init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    dev->io_handle = io_handle;

    // LCD panel handle
    esp_lcd_panel_handle_t panel_handle = NULL;
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = rst,
        .rgb_endian = LCD_RGB_ENDIAN_RGB,
        .bits_per_pixel = 16,
    };
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

    // Turn on backlight
    if (bl >= 0) {
        ledc_timer_config_t ledc_timer = {
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .duty_resolution = LEDC_TIMER_8_BIT,
            .timer_num = LEDC_TIMER_0,
            .freq_hz = 5000,
            .clk_cfg = LEDC_AUTO_CLK,
        };
        ledc_timer_config(&ledc_timer);

        ledc_channel_config_t ledc_channel = {
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .channel = LEDC_CHANNEL_0,
            .timer_sel = LEDC_TIMER_0,
            .gpio_num = bl,
            .duty = 128,
            .hpoint = 0,
        };
        ledc_channel_config(&ledc_channel);
    }

    ESP_LOGI(TAG, "LCD initialized successfully");
    return ESP_OK;
}

esp_err_t lcd_fill_rect(lcd_dev_t *dev, uint16_t x, uint16_t y,
                         uint16_t w, uint16_t h, uint16_t color)
{
    esp_lcd_panel_set_window(dev->panel_handle, x, y, x + w - 1, y + h - 1);
    uint16_t *buf = heap_caps_malloc(w * h * sizeof(uint16_t), MALLOC_CAP_DMA);
    if (!buf) return ESP_ERR_NO_MEM;
    for (int i = 0; i < w * h; i++) buf[i] = color;
    esp_lcd_panel_draw_bitmap(dev->panel_handle, x, y, x + w, y + h, buf);
    heap_caps_free(buf);
    return ESP_OK;
}

esp_err_t lcd_set_brightness(lcd_dev_t *dev, uint8_t brightness)
{
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, brightness);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    return ESP_OK;
}
