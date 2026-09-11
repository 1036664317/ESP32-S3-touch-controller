#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "lvgl.h"
#include "qmi8658.h"
#include "axs15231b.h"
#include "ble_combo.h"
#include "mahony.h"

#ifdef __cplusplus
extern "C" {
#endif

// Physical & logical display resolutions
#define LCD_WIDTH           640
#define LCD_HEIGHT          172
#define LCD_PHYS_WIDTH      172
#define LCD_PHYS_HEIGHT     640

// UI Layout Coordinates (VR Controller Layout)
#define TOP_BAR_H           20
#define BOTTOM_BAR_H        38
#define CONTENT_Y           (TOP_BAR_H + 2)
#define CONTENT_H           (LCD_HEIGHT - TOP_BAR_H - BOTTOM_BAR_H - 4) // 110px

#define JOYSTICK_ZONE_X     2
#define JOYSTICK_ZONE_W     314

#define TOUCHPAD_ZONE_X     324
#define TOUCHPAD_ZONE_W     314

typedef enum {
    MODE_JOYSTICK = 0,
    MODE_AIR_MOUSE,
    MODE_SCROLL,
} ui_mode_t;

typedef struct {
    lv_disp_drv_t disp_drv;
    lv_indev_drv_t indev_drv;
    lv_disp_draw_buf_t draw_buf;
    lv_color_t *buf1;
    ui_mode_t mode;
    bool air_mouse_active;

    // IMU 6-axis & angles
    float pitch, yaw, roll;
    float pitch_offset, yaw_offset;

    // Controller input states
    int16_t joy_x, joy_y;           // Left Thumbstick (-32767 ~ +32767)
    int16_t touchpad_x, touchpad_y; // Right Touchpad / View Stick (-32767 ~ +32767)
    int16_t mouse_x, mouse_y;
    int8_t mouse_wheel;
    uint8_t mouse_buttons;

    // Button states (Continuous press supported)
    bool button_trigger; // VR Trigger
    bool button_grip;    // VR Grip
    bool button_a;       // Button A
    bool button_b;       // Button B
    bool button_menu;    // Button Menu

    // Gestures & Power
    bool gesture_left, gesture_right;
    int64_t last_activity_time;
    bool sleeping;
} ui_state_t;

extern ui_state_t g_ui;

esp_err_t ui_init(void);
void ui_update(void);
void ui_set_mode(ui_mode_t mode);
void ui_set_bt_status(bool connected);
void ui_set_battery(uint8_t percent);
void ui_update_imu(mahony_state_t *mahony, qmi8658_dev_t *imu);
void ui_process_touch(axs15231b_dev_t *touch_dev);
void ui_send_reports(ble_combo_state_t *ble_state);
void ui_detect_gesture(qmi8658_dev_t *imu);
void ui_check_sleep(qmi8658_dev_t *imu);
void ui_wake_from_imu(qmi8658_dev_t *imu);

#ifdef __cplusplus
}
#endif
