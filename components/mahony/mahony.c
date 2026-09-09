#include "mahony.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

static void mahony_inverse_sqrt(float x[3], float y[3])
{
    float ax = fabsf(x[0]);
    float ay = fabsf(x[1]);
    float az = fabsf(x[2]);

    float mag = sqrtf(ax * ax + ay * ay + az * az);
    if (mag > 0.0f) {
        y[0] = x[0] / mag;
        y[1] = x[1] / mag;
        y[2] = x[2] / mag;
    } else {
        y[0] = y[1] = 0.0f;
        y[2] = 1.0f;
    }
}

void mahony_init(mahony_state_t *state, float sample_period_s)
{
    state->q0 = 1.0f;
    state->q1 = 0.0f;
    state->q2 = 0.0f;
    state->q3 = 0.0f;
    state->pitch = 0.0f;
    state->yaw = 0.0f;
    state->roll = 0.0f;
    state->twoKp = 2.0f * 0.5f;   // proportional gain
    state->twoKi = 2.0f * 0.0f;   // integral gain
    state->integralFBx = 0.0f;
    state->integralFBy = 0.0f;
    state->integralFBz = 0.0f;
    state->sample_period = sample_period_s;
}

void mahony_update(mahony_state_t *state,
                   float ax, float ay, float az,
                   float gx, float gy, float gz)
{
    float q0 = state->q0, q1 = state->q1, q2 = state->q2, q3 = state->q3;
    float recipNorm;
    float s0, s1, s2, s3;
    float qDot1, qDot2, qDot3, qDot4;
    float _2q0, _2q1, _2q2, _2q3;
    float _4q0, _4q1, _4q2;
    float _8q1, _8q2;
    float q0q0, q1q1, q2q2, q3q3;

    // Rate of change of quaternion from gyroscope
    qDot1 = 0.5f * (-q1 * gx - q2 * gy - q3 * gz);
    qDot2 = 0.5f * (q0 * gx + q2 * gz - q3 * gy);
    qDot3 = 0.5f * (q0 * gy - q1 * gz + q3 * gx);
    qDot4 = 0.5f * (q0 * gz + q1 * gy - q2 * gx);

    // Compute feedback only if accelerometer measurement valid
    if (!((ax == 0.0f) && (ay == 0.0f) && (az == 0.0f))) {
        // Normalise accelerometer measurement
        recipNorm = 1.0f / sqrtf(ax * ax + ay * ay + az * az);
        ax *= recipNorm;
        ay *= recipNorm;
        az *= recipNorm;

        // Auxiliary variables to avoid repeated arithmetic
        _2q0 = 2.0f * q0;
        _2q1 = 2.0f * q1;
        _2q2 = 2.0f * q2;
        _2q3 = 2.0f * q3;
        _4q0 = 4.0f * q0;
        _4q1 = 4.0f * q1;
        _4q2 = 4.0f * q2;
        _8q1 = 8.0f * q1;
        _8q2 = 8.0f * q2;
        q0q0 = q0 * q0;
        q1q1 = q1 * q1;
        q2q2 = q2 * q2;
        q3q3 = q3 * q3;

        // Gradient decent algorithm corrective step
        s0 = _4q0 * q2q2 + _2q2 * ax + _4q0 * q1q1 - _2q1 * ay;
        s1 = _4q1 * q3q3 - _2q3 * ax + 4.0f * q0q0 * q1 - _2q0 * ay - _4q1 + _8q1 * q1q1 + _8q1 * q2q2 + _4q1 * az;
        s2 = 4.0f * q0q0 * q2 + _2q0 * ax + _4q2 * q3q3 - _2q3 * ay - _4q2 + _8q2 * q1q1 + _8q2 * q2q2 + _4q2 * az;
        s3 = 4.0f * q1q1 * q3 - _2q1 * ax + 4.0f * q2q2 * q3 - _2q2 * ay;

        // Normalise step magnitude
        recipNorm = 1.0f / sqrtf(s0 * s0 + s1 * s1 + s2 * s2 + s3 * s3);
        s0 *= recipNorm;
        s1 *= recipNorm;
        s2 *= recipNorm;
        s3 *= recipNorm;

        // Apply feedback step
        qDot1 -= state->twoKp * s0;
        qDot2 -= state->twoKp * s1;
        qDot3 -= state->twoKp * s2;
        qDot4 -= state->twoKp * s3;
    }

    // Integrate rate of change of quaternion
    q0 += qDot1 * state->sample_period;
    q1 += qDot2 * state->sample_period;
    q2 += qDot3 * state->sample_period;
    q3 += qDot4 * state->sample_period;

    // Normalise quaternion
    recipNorm = 1.0f / sqrtf(q0 * q0 + q1 * q1 + q2 * q2 + q3 * q3);
    state->q0 = q0 * recipNorm;
    state->q1 = q1 * recipNorm;
    state->q2 = q2 * recipNorm;
    state->q3 = q3 * recipNorm;

    // Compute angles
    state->pitch = asinf(-2.0f * (state->q1 * state->q3 - state->q0 * state->q2)) * 180.0f / M_PI;
    state->yaw = atan2f(2.0f * (state->q1 * state->q2 + state->q0 * state->q3),
                        state->q0 * state->q0 + state->q1 * state->q1 - state->q2 * state->q2 - state->q3 * state->q3) * 180.0f / M_PI;
    state->roll = atan2f(2.0f * (state->q0 * state->q1 + state->q2 * state->q3),
                         state->q0 * state->q0 - state->q1 * state->q1 - state->q2 * state->q2 + state->q3 * state->q3) * 180.0f / M_PI;
}

void mahony_get_angles(mahony_state_t *state, float *pitch, float *yaw, float *roll)
{
    *pitch = state->pitch;
    *yaw = state->yaw;
    *roll = state->roll;
}
