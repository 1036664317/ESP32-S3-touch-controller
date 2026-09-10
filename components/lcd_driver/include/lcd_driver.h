#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "esp_lcd_panel_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*lcd_flush_done_cb_t)(void);

typedef struct {
    esp_lcd_panel_io_handle_t io_handle;
    void *panel_handle;
    uint16_t width;
    uint16_t height;
} lcd_dev_t;

esp_err_t lcd_init(lcd_dev_t *dev, uint16_t width, uint16_t height);
esp_err_t lcd_fill_rect(lcd_dev_t *dev, uint16_t x, uint16_t y,
                         uint16_t w, uint16_t h, uint16_t color);
esp_err_t lcd_flush_area(lcd_dev_t *dev, uint16_t x1, uint16_t y1,
                          uint16_t x2, uint16_t y2, const uint16_t *color_p);
esp_err_t lcd_set_brightness(uint8_t brightness);
void lcd_set_flush_done_cb(lcd_flush_done_cb_t cb);

#ifdef __cplusplus
}
#endif
