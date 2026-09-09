#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "driver/i2c.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AXS15231B_I2C_ADDR  0x3B

typedef struct {
    i2c_port_t i2c_port;
    uint8_t addr;
    uint16_t max_x;
    uint16_t max_y;
    bool touched;
    uint16_t x;
    uint16_t y;
} axs15231b_dev_t;

typedef struct {
    bool pressed;
    uint16_t x;
    uint16_t y;
} axs15231b_touch_data_t;

esp_err_t axs15231b_init(axs15231b_dev_t *dev, i2c_port_t port, uint8_t addr);
esp_err_t axs15231b_read_touch(axs15231b_dev_t *dev, axs15231b_touch_data_t *data);

#ifdef __cplusplus
}
#endif
