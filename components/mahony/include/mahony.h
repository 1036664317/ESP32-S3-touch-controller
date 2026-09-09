#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float q0, q1, q2, q3;
    float pitch, yaw, roll;
    float twoKp;
    float twoKi;
    float integralFBx, integralFBy, integralFBz;
    float sample_period;
} mahony_state_t;

void mahony_init(mahony_state_t *state, float sample_period_s);
void mahony_update(mahony_state_t *state,
                   float ax, float ay, float az,
                   float gx, float gy, float gz);
void mahony_get_angles(mahony_state_t *state, float *pitch, float *yaw, float *roll);

#ifdef __cplusplus
}
#endif
