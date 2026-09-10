#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "driver/spi_master.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    spi_device_handle_t spi_dev;
    uint16_t width;
    uint16_t height;
} lcd_dev_t;

esp_err_t lcd_init(lcd_dev_t *dev, uint16_t width, uint16_t height);
esp_err_t lcd_flush_area(lcd_dev_t *dev, uint16_t x1, uint16_t y1,
                          uint16_t x2, uint16_t y2, const uint16_t *color_p);
esp_err_t lcd_set_brightness(uint8_t brightness);
esp_err_t lcd_fill_screen(lcd_dev_t *dev, uint16_t color);

#ifdef __cplusplus
}
#endif
