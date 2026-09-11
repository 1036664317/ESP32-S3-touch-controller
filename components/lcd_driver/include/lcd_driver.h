#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_lcd_panel_io.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LCD_PHYS_WIDTH   172
#define LCD_PHYS_HEIGHT  640

typedef struct {
    esp_lcd_panel_io_handle_t io_handle;
    uint16_t width;
    uint16_t height;
} lcd_dev_t;

esp_err_t lcd_init(lcd_dev_t *dev, uint16_t width, uint16_t height);
esp_err_t lcd_draw_bitmap(lcd_dev_t *dev, int x_start, int y_start, int x_end, int y_end, const void *color_data);
esp_err_t lcd_fill_screen(lcd_dev_t *dev, uint16_t color);
esp_err_t lcd_set_brightness(uint8_t brightness);

#ifdef __cplusplus
}
#endif
