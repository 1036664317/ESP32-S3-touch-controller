#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_io.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    esp_lcd_panel_io_handle_t io_handle;
    esp_lcd_panel_handle_t panel_handle;
    uint16_t width;
    uint16_t height;
    spi_host_device_t spi_host;
    gpio_num_t lcd_cs;
    gpio_num_t lcd_dc;
    gpio_num_t lcd_rst;
    gpio_num_t lcd_bl;
} lcd_dev_t;

esp_err_t lcd_init(lcd_dev_t *dev, spi_host_device_t spi_host,
                   gpio_num_t cs, gpio_num_t dc, gpio_num_t rst, gpio_num_t bl,
                   uint16_t width, uint16_t height);
esp_err_t lcd_fill_rect(lcd_dev_t *dev, uint16_t x, uint16_t y,
                         uint16_t w, uint16_t h, uint16_t color);
esp_err_t lcd_set_brightness(lcd_dev_t *dev, uint8_t brightness);

#ifdef __cplusplus
}
#endif
