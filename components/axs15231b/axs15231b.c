#include "axs15231b.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "AXS15231B";

static i2c_master_dev_handle_t touch_handle;

esp_err_t axs15231b_init(axs15231b_dev_t *dev, i2c_port_t port, uint8_t addr)
{
    dev->i2c_port = port;
    dev->addr = addr;
    dev->max_x = 172;
    dev->max_y = 640;
    dev->touched = false;

    i2c_master_bus_config_t bus_config = {
        .i2c_port = port,
        .sda_io_num = 17,
        .scl_io_num = 18,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus_handle;
    esp_err_t ret = i2c_new_master_bus(&bus_config, &bus_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create I2C bus: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = i2c_master_bus_add_device(bus_handle, &((i2c_device_config_t){
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = 400000,
    }), &touch_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add device: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "AXS15231B touch initialized on I2C%d", port);
    return ESP_OK;
}

esp_err_t axs15231b_read_touch(axs15231b_dev_t *dev, axs15231b_touch_data_t *data)
{
    uint8_t cmd[11] = {0xb5, 0xab, 0xa5, 0x5a, 0x00, 0x00, 0x00, 0x0e, 0x00, 0x00, 0x00};
    uint8_t buf[32] = {0};
    memset(data, 0, sizeof(axs15231b_touch_data_t));

    esp_err_t ret = i2c_master_transmit_receive(touch_handle, cmd, 11, buf, sizeof(buf), pdMS_TO_TICKS(50));
    if (ret != ESP_OK) {
        dev->touched = false;
        return ret;
    }

    // Parse touch: buf[1] > 0 && buf[1] < 5 means touched
    if (buf[1] > 0 && buf[1] < 5) {
        data->pressed = true;
        dev->touched = true;
        data->x = (((uint16_t)buf[2] & 0x0f) << 8) | (uint16_t)buf[3];
        data->y = (((uint16_t)buf[4] & 0x0f) << 8) | (uint16_t)buf[5];
        dev->x = data->x;
        dev->y = data->y;
    } else {
        data->pressed = false;
        dev->touched = false;
    }

    return ESP_OK;
}
