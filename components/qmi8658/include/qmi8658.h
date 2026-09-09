#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "driver/i2c.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define QMI8658_I2C_ADDR         0x6B
#define QMI8658_I2C_ADDR_ALT     0x6A

typedef struct {
    i2c_port_t i2c_port;
    uint8_t addr;
    float accel_scale;   // LSB to g
    float gyro_scale;    // LSB to dps
    float mag_scale;
} qmi8658_dev_t;

typedef struct {
    float x, y, z;
} qmi8658_vec3_t;

typedef struct {
    qmi8658_vec3_t accel;   // g
    qmi8658_vec3_t gyro;    // dps
    int64_t timestamp_us;
} qmi8658_data_t;

esp_err_t qmi8658_init(qmi8658_dev_t *dev, i2c_port_t port, uint8_t addr);
esp_err_t qmi8658_read_data(qmi8658_dev_t *dev, qmi8658_data_t *data);
esp_err_t qmi8658_read_all(qmi8658_dev_t *dev, qmi8658_data_t *data);
bool qmi8658_is_stationary(qmi8658_dev_t *dev, float threshold, int duration_ms);
float qmi8658_get_total_accel(qmi8658_dev_t *dev);

#ifdef __cplusplus
}
#endif
