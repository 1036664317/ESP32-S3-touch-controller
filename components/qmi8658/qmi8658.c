#include "qmi8658.h"
#include "driver/i2c_master.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <string.h>
#include <math.h>

static const char *TAG = "QMI8658";

// Register addresses
#define QMI8658_WHO_AM_I        0x00
#define QMI8658_REVISION_ID     0x01
#define QMI8658_CTRL1           0x02
#define QMI8658_CTRL2           0x03
#define QMI8658_CTRL3           0x04
#define QMI8658_CTRL4           0x05
#define QMI8658_CTRL5           0x06
#define QMI8658_CTRL7           0x08
#define QMI8658_CTRL8           0x09
#define QMI8658_CTRL9           0x0A
#define QMI8658_AX_L            0x35
#define QMI8658_AX_H            0x36
#define QMI8658_AY_L            0x37
#define QMI8658_AY_H            0x38
#define QMI8658_AZ_L            0x39
#define QMI8658_AZ_H            0x3A
#define QMI8658_GX_L            0x3B
#define QMI8658_GX_H            0x3C
#define QMI8658_GY_L            0x3D
#define QMI8658_GY_H            0x3E
#define QMI8658_GZ_L            0x3F
#define QMI8658_GZ_H            0x40

static i2c_master_dev_handle_t dev_handle;

static esp_err_t qmi8658_write_reg(qmi8658_dev_t *dev, uint8_t reg, uint8_t val)
{
    return i2c_master_transmit(dev_handle, &reg, 1, pdMS_TO_TICKS(100));
}

static esp_err_t qmi8658_read_reg(qmi8658_dev_t *dev, uint8_t reg, uint8_t *val, size_t len)
{
    return i2c_master_transmit_receive(dev_handle, &reg, 1, val, len, pdMS_TO_TICKS(100));
}

static esp_err_t qmi8658_write_reg_with_data(qmi8658_dev_t *dev, uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(dev_handle, buf, 2, pdMS_TO_TICKS(100));
}

esp_err_t qmi8658_init(qmi8658_dev_t *dev, i2c_port_t port, uint8_t addr)
{
    esp_err_t ret;
    uint8_t whoami = 0;

    dev->i2c_port = port;
    dev->addr = addr;

    i2c_master_bus_config_t bus_config = {
        .i2c_port = port,
        .sda_io_num = 47,
        .scl_io_num = 48,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus_handle;
    ret = i2c_new_master_bus(&bus_config, &bus_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create I2C bus: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = i2c_master_bus_add_device(bus_handle, &((i2c_device_config_t){
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = 300000,
    }), &dev_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add device: %s", esp_err_to_name(ret));
        return ret;
    }

    // Read WHO_AM_I
    ret = qmi8658_read_reg(dev, QMI8658_WHO_AM_I, &whoami, 1);
    if (ret != ESP_OK || whoami != 0x05) {
        ESP_LOGE(TAG, "Invalid WHO_AM_I: 0x%02X (expected 0x05)", whoami);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "QMI8658 detected, WHO_AM_I=0x%02X", whoami);

    // Reset
    qmi8658_write_reg_with_data(dev, QMI8658_CTRL7, 0x80);
    vTaskDelay(pdMS_TO_TICKS(100));

    // Configure accelerometer: 250Hz ODR, +/-8g range
    qmi8658_write_reg_with_data(dev, QMI8658_CTRL2, 0x93);  // ODR=250Hz, Range=8g, enable
    dev->accel_scale = 8.0f / 32768.0f;

    // Configure gyroscope: 250Hz ODR, +/-500dps range
    qmi8658_write_reg_with_data(dev, QMI8658_CTRL3, 0x95);  // ODR=250Hz, Range=500dps, enable
    dev->gyro_scale = 500.0f / 32768.0f;
    dev->gyro_bias_x = 0.0f;
    dev->gyro_bias_y = 0.0f;
    dev->gyro_bias_z = 0.0f;

    // Enable both sensors
    qmi8658_write_reg_with_data(dev, QMI8658_CTRL7, 0x03);

    ESP_LOGI(TAG, "QMI8658 initialized successfully");
    return ESP_OK;
}

esp_err_t qmi8658_calibrate(qmi8658_dev_t *dev, int samples)
{
    if (samples <= 0) samples = 100;
    float sum_gx = 0, sum_gy = 0, sum_gz = 0;
    int valid = 0;

    for (int i = 0; i < samples; i++) {
        uint8_t buf[12];
        if (qmi8658_read_reg(dev, QMI8658_AX_L, buf, 12) == ESP_OK) {
            int16_t raw_gx = (int16_t)(buf[7] << 8 | buf[6]);
            int16_t raw_gy = (int16_t)(buf[9] << 8 | buf[8]);
            int16_t raw_gz = (int16_t)(buf[11] << 8 | buf[10]);
            sum_gx += raw_gx * dev->gyro_scale;
            sum_gy += raw_gy * dev->gyro_scale;
            sum_gz += raw_gz * dev->gyro_scale;
            valid++;
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    if (valid > 0) {
        dev->gyro_bias_x = sum_gx / valid;
        dev->gyro_bias_y = sum_gy / valid;
        dev->gyro_bias_z = sum_gz / valid;
        ESP_LOGI(TAG, "Gyro calibrated (%d samples): bias=(%.2f, %.2f, %.2f) dps",
                 valid, dev->gyro_bias_x, dev->gyro_bias_y, dev->gyro_bias_z);
    }
    return ESP_OK;
}

esp_err_t qmi8658_read_data(qmi8658_dev_t *dev, qmi8658_data_t *data)
{
    uint8_t buf[12];
    esp_err_t ret = qmi8658_read_reg(dev, QMI8658_AX_L, buf, 12);
    if (ret != ESP_OK) return ret;

    int16_t raw_ax = (int16_t)(buf[1] << 8 | buf[0]);
    int16_t raw_ay = (int16_t)(buf[3] << 8 | buf[2]);
    int16_t raw_az = (int16_t)(buf[5] << 8 | buf[4]);
    int16_t raw_gx = (int16_t)(buf[7] << 8 | buf[6]);
    int16_t raw_gy = (int16_t)(buf[9] << 8 | buf[8]);
    int16_t raw_gz = (int16_t)(buf[11] << 8 | buf[10]);

    data->accel.x = raw_ax * dev->accel_scale;
    data->accel.y = raw_ay * dev->accel_scale;
    data->accel.z = raw_az * dev->accel_scale;
    data->gyro.x = (raw_gx * dev->gyro_scale) - dev->gyro_bias_x;
    data->gyro.y = (raw_gy * dev->gyro_scale) - dev->gyro_bias_y;
    data->gyro.z = (raw_gz * dev->gyro_scale) - dev->gyro_bias_z;
    data->timestamp_us = esp_timer_get_time();

    return ESP_OK;
}

esp_err_t qmi8658_read_all(qmi8658_dev_t *dev, qmi8658_data_t *data)
{
    return qmi8658_read_data(dev, data);
}

bool qmi8658_is_stationary(qmi8658_dev_t *dev, float threshold, int duration_ms)
{
    qmi8658_data_t data;
    if (qmi8658_read_data(dev, &data) != ESP_OK) return false;

    float total_g = sqrtf(data.accel.x * data.accel.x +
                          data.accel.y * data.accel.y +
                          data.accel.z * data.accel.z);

    float total_dps = sqrtf(data.gyro.x * data.gyro.x +
                            data.gyro.y * data.gyro.y +
                            data.gyro.z * data.gyro.z);

    // Check if acceleration is close to 1g (gravity only) and gyro is near zero
    float accel_diff = fabsf(total_g - 1.0f);
    bool stationary = (accel_diff < threshold) && (total_dps < 1.0f);

    static int64_t stationary_start = 0;
    if (stationary) {
        if (stationary_start == 0) {
            stationary_start = esp_timer_get_time();
        }
        return ((esp_timer_get_time() - stationary_start) / 1000) >= duration_ms;
    } else {
        stationary_start = 0;
        return false;
    }
}

float qmi8658_get_total_accel(qmi8658_dev_t *dev)
{
    qmi8658_data_t data;
    if (qmi8658_read_data(dev, &data) != ESP_OK) return 0.0f;
    return sqrtf(data.accel.x * data.accel.x +
                 data.accel.y * data.accel.y +
                 data.accel.z * data.accel.z);
}
