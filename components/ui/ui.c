#include "ui.h"
#include "lcd_driver.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "UI";

ui_state_t g_ui = {
    .mode = MODE_JOYSTICK,
    .air_mouse_active = false,
    .sleeping = false,
    .last_activity_time = 0,
};

// LVGL objects
static lv_obj_t *scr_main;
static lv_obj_t *top_bar;
static lv_obj_t *lbl_bt;
static lv_obj_t *lbl_bat;
static lv_obj_t *lbl_mode;
static lv_obj_t *joystick_zone;
static lv_obj_t *scroll_zone;
static lv_obj_t *btn_a, *btn_b, *btn_menu;
static lv_obj_t *lbl_btn_a, *lbl_btn_b, *lbl_btn_menu;
static lv_obj_t *joystick_cursor;
static lv_obj_t *cursor_obj;

// Joystick tracking
static bool joy_touching = false;
static int joy_origin_x = 0, joy_origin_y = 0;
static int joy_max_radius = 50;

// Scroll tracking
static bool scroll_touching = false;
static int scroll_last_y = 0;
static int64_t scroll_last_time = 0;

// Gesture detection
static float gesture_accel_history[10];
static int gesture_idx = 0;
static int64_t gesture_last_time = 0;

static void disp_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p);
static void touch_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data);

static void event_btn_a(lv_event_t *e)
{
    g_ui.button_a = true;
    ESP_LOGI(TAG, "Button A pressed");
}

static void event_btn_b(lv_event_t *e)
{
    g_ui.button_b = true;
    ESP_LOGI(TAG, "Button B pressed");
}

static void event_btn_menu(lv_event_t *e)
{
    g_ui.button_menu = true;
    ESP_LOGI(TAG, "Menu button pressed");
}

static void event_joystick_zone(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        lv_indev_t *indev = lv_indev_get_act();
        lv_point_t p;
        lv_indev_get_point(indev, &p);
        joy_touching = true;
        joy_origin_x = p.x;
        joy_origin_y = p.y;
        ESP_LOGI(TAG, "Joystick touch at (%d,%d)", p.x, p.y);
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_CANCEL) {
        joy_touching = false;
        g_ui.joy_x = 0;
        g_ui.joy_y = 0;
        lv_obj_set_style_bg_opa(joystick_cursor, LV_OPA_TRANSP, 0);
    } else if (code == LV_EVENT_PRESSING) {
        lv_indev_t *indev = lv_indev_get_act();
        lv_point_t p;
        lv_indev_get_point(indev, &p);
        if (joy_touching) {
            int dx = p.x - joy_origin_x;
            int dy = p.y - joy_origin_y;
            float dist = sqrtf(dx * dx + dy * dy);
            if (dist > joy_max_radius) {
                dx = (int)(dx * joy_max_radius / dist);
                dy = (int)(dy * joy_max_radius / dist);
                dist = joy_max_radius;
            }
            g_ui.joy_x = (int16_t)((float)dx / joy_max_radius * 32767);
            g_ui.joy_y = (int16_t)((float)dy / joy_max_radius * 32767);

            lv_obj_set_style_bg_opa(joystick_cursor, LV_OPA_COVER, 0);
            lv_obj_set_style_bg_color(joystick_cursor, lv_color_make(0, 200, 0), 0);
            lv_obj_set_pos(joystick_cursor, joy_origin_x + dx - 10, joy_origin_y + dy - 10);
        }
    }
}

static void event_scroll_zone(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        lv_indev_t *indev = lv_indev_get_act();
        lv_point_t p;
        lv_indev_get_point(indev, &p);
        scroll_touching = true;
        scroll_last_y = p.y;
        scroll_last_time = esp_timer_get_time();
        g_ui.touch_x = p.x;
        g_ui.touch_y = p.y;
        g_ui.touch_pressed = true;
        ESP_LOGI(TAG, "Scroll zone touch at (%d,%d)", p.x, p.y);
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_CANCEL) {
        scroll_touching = false;
        g_ui.mouse_wheel = 0;
        g_ui.touch_pressed = false;
    } else if (code == LV_EVENT_PRESSING) {
        lv_indev_t *indev = lv_indev_get_act();
        lv_point_t p;
        lv_indev_get_point(indev, &p);

        int64_t now = esp_timer_get_time();
        int64_t dt_ms = (now - scroll_last_time) / 1000;
        int dy = p.y - scroll_last_y;

        if (abs(dy) > 2) {
            float speed_factor = 1.0f;
            if (dt_ms > 0 && dt_ms < 50) {
                speed_factor = 3.0f;
            } else if (dt_ms > 50 && dt_ms < 100) {
                speed_factor = 2.0f;
            }

            g_ui.mouse_wheel = (int8_t)(-dy * speed_factor * 0.5f);
            scroll_last_y = p.y;
            scroll_last_time = now;
        }
    }
}

esp_err_t ui_init(void)
{
    ESP_LOGI(TAG, "Initializing UI");

    // Init LVGL
    lv_init();

    // Allocate draw buffers
    g_ui.buf1 = heap_caps_malloc(LCD_WIDTH * 40 * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    g_ui.buf2 = heap_caps_malloc(LCD_WIDTH * 40 * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    if (!g_ui.buf1 || !g_ui.buf2) {
        ESP_LOGE(TAG, "Failed to allocate LVGL buffers");
        return ESP_ERR_NO_MEM;
    }
    lv_disp_draw_buf_init(&g_ui.draw_buf, g_ui.buf1, g_ui.buf2, LCD_WIDTH * 40);

    // Display driver
    lv_disp_drv_init(&g_ui.disp_drv);
    g_ui.disp_drv.hor_res = LCD_WIDTH;
    g_ui.disp_drv.ver_res = LCD_HEIGHT;
    g_ui.disp_drv.flush_cb = disp_flush_cb;
    g_ui.disp_drv.draw_buf = &g_ui.draw_buf;
    lv_disp_drv_register(&g_ui.disp_drv);

    // Input device driver
    lv_indev_drv_init(&g_ui.indev_drv);
    g_ui.indev_drv.type = LV_INDEV_TYPE_POINTER;
    g_ui.indev_drv.read_cb = touch_read_cb;
    lv_indev_drv_register(&g_ui.indev_drv);

    // Create main screen
    scr_main = lv_scr_act();
    lv_obj_set_style_bg_color(scr_main, lv_color_make(20, 20, 30), 0);

    // === TOP BAR ===
    top_bar = lv_obj_create(scr_main);
    lv_obj_set_size(top_bar, LCD_WIDTH, TOP_BAR_H);
    lv_obj_set_pos(top_bar, 0, 0);
    lv_obj_set_style_bg_color(top_bar, lv_color_make(30, 30, 50), 0);
    lv_obj_set_style_bg_opa(top_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(top_bar, 0, 0);
    lv_obj_set_style_pad_all(top_bar, 2, 0);
    lv_obj_clear_flag(top_bar, LV_OBJ_FLAG_SCROLLABLE);

    lbl_bt = lv_label_create(top_bar);
    lv_label_set_text(lbl_bt, "BT: Disconnected");
    lv_obj_set_style_text_color(lbl_bt, lv_color_make(255, 80, 80), 0);
    lv_obj_set_style_text_font(lbl_bt, &lv_font_montserrat_12, 0);
    lv_obj_align(lbl_bt, LV_ALIGN_LEFT_MID, 4, 0);

    lbl_bat = lv_label_create(top_bar);
    lv_label_set_text(lbl_bat, "Bat: 100%");
    lv_obj_set_style_text_color(lbl_bat, lv_color_make(200, 200, 200), 0);
    lv_obj_set_style_text_font(lbl_bat, &lv_font_montserrat_12, 0);
    lv_obj_align(lbl_bat, LV_ALIGN_CENTER, 0, 0);

    lbl_mode = lv_label_create(top_bar);
    lv_label_set_text(lbl_mode, "Mode: Joystick");
    lv_obj_set_style_text_color(lbl_mode, lv_color_make(100, 255, 100), 0);
    lv_obj_set_style_text_font(lbl_mode, &lv_font_montserrat_12, 0);
    lv_obj_align(lbl_mode, LV_ALIGN_RIGHT_MID, -4, 0);

    // === JOYSTICK ZONE (left 40%) ===
    joystick_zone = lv_obj_create(scr_main);
    lv_obj_set_size(joystick_zone, JOYSTICK_ZONE_W - 2, CONTENT_H);
    lv_obj_set_pos(joystick_zone, 1, CONTENT_Y);
    lv_obj_set_style_bg_color(joystick_zone, lv_color_make(25, 25, 45), 0);
    lv_obj_set_style_bg_opa(joystick_zone, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(joystick_zone, lv_color_make(60, 60, 100), 0);
    lv_obj_set_style_border_width(joystick_zone, 1, 0);
    lv_obj_set_style_radius(joystick_zone, 4, 0);
    lv_obj_clear_flag(joystick_zone, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *joy_label = lv_label_create(joystick_zone);
    lv_label_set_text(joy_label, "Joystick");
    lv_obj_set_style_text_color(joy_label, lv_color_make(120, 120, 180), 0);
    lv_obj_set_style_text_font(joy_label, &lv_font_montserrat_12, 0);
    lv_obj_align(joy_label, LV_ALIGN_TOP_MID, 0, 4);

    lv_obj_t *joy_cross_h = lv_obj_create(joystick_zone);
    lv_obj_set_size(joy_cross_h, 60, 1);
    lv_obj_set_style_bg_color(joy_cross_h, lv_color_make(60, 60, 100), 0);
    lv_obj_align(joy_cross_h, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *joy_cross_v = lv_obj_create(joystick_zone);
    lv_obj_set_size(joy_cross_v, 1, 60);
    lv_obj_set_style_bg_color(joy_cross_v, lv_color_make(60, 60, 100), 0);
    lv_obj_align(joy_cross_v, LV_ALIGN_CENTER, 0, 0);

    joystick_cursor = lv_obj_create(joystick_zone);
    lv_obj_set_size(joystick_cursor, 20, 20);
    lv_obj_set_style_bg_color(joystick_cursor, lv_color_make(0, 200, 0), 0);
    lv_obj_set_style_radius(joystick_cursor, 10, 0);
    lv_obj_set_style_bg_opa(joystick_cursor, LV_OPA_TRANSP, 0);
    lv_obj_align(joystick_cursor, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(joystick_cursor, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_add_event_cb(joystick_zone, event_joystick_zone, LV_EVENT_ALL, NULL);

    // === SCROLL ZONE (right 60%) ===
    scroll_zone = lv_obj_create(scr_main);
    lv_obj_set_size(scroll_zone, SCROLL_ZONE_W - 2, CONTENT_H);
    lv_obj_set_pos(scroll_zone, SCROLL_ZONE_X + 1, CONTENT_Y);
    lv_obj_set_style_bg_color(scroll_zone, lv_color_make(25, 25, 45), 0);
    lv_obj_set_style_bg_opa(scroll_zone, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(scroll_zone, lv_color_make(60, 60, 100), 0);
    lv_obj_set_style_border_width(scroll_zone, 1, 0);
    lv_obj_set_style_radius(scroll_zone, 4, 0);
    lv_obj_clear_flag(scroll_zone, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *scroll_label = lv_label_create(scroll_zone);
    lv_label_set_text(scroll_label, "Scroll & Cursor");
    lv_obj_set_style_text_color(scroll_label, lv_color_make(120, 120, 180), 0);
    lv_obj_set_style_text_font(scroll_label, &lv_font_montserrat_12, 0);
    lv_obj_align(scroll_label, LV_ALIGN_TOP_MID, 0, 4);

    lv_obj_t *scroll_hint = lv_label_create(scroll_zone);
    lv_label_set_text(scroll_hint, "Swipe: Scroll\nTap: Click\nLong: AirMouse");
    lv_obj_set_style_text_color(scroll_hint, lv_color_make(80, 80, 130), 0);
    lv_obj_set_style_text_font(scroll_hint, &lv_font_montserrat_12, 0);
    lv_obj_align(scroll_hint, LV_ALIGN_CENTER, 0, 0);

    cursor_obj = lv_obj_create(scroll_zone);
    lv_obj_set_size(cursor_obj, 12, 12);
    lv_obj_set_style_bg_color(cursor_obj, lv_color_make(255, 255, 0), 0);
    lv_obj_set_style_radius(cursor_obj, 6, 0);
    lv_obj_set_style_bg_opa(cursor_obj, LV_OPA_TRANSP, 0);
    lv_obj_align(cursor_obj, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(cursor_obj, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_add_event_cb(scroll_zone, event_scroll_zone, LV_EVENT_ALL, NULL);

    // === BOTTOM BAR ===
    lv_obj_t *bottom_bar = lv_obj_create(scr_main);
    lv_obj_set_size(bottom_bar, LCD_WIDTH, BOTTOM_BAR_H);
    lv_obj_set_pos(bottom_bar, 0, LCD_HEIGHT - BOTTOM_BAR_H);
    lv_obj_set_style_bg_color(bottom_bar, lv_color_make(30, 30, 50), 0);
    lv_obj_set_style_bg_opa(bottom_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bottom_bar, 0, 0);
    lv_obj_set_style_pad_all(bottom_bar, 2, 0);
    lv_obj_clear_flag(bottom_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(bottom_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bottom_bar, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Button A
    btn_a = lv_btn_create(bottom_bar);
    lv_obj_set_size(btn_a, 80, 26);
    lv_obj_set_style_bg_color(btn_a, lv_color_make(50, 150, 50), 0);
    lv_obj_set_style_radius(btn_a, 4, 0);
    lv_obj_add_event_cb(btn_a, event_btn_a, LV_EVENT_CLICKED, NULL);
    lbl_btn_a = lv_label_create(btn_a);
    lv_label_set_text(lbl_btn_a, "A");
    lv_obj_set_style_text_font(lbl_btn_a, &lv_font_montserrat_16, 0);
    lv_obj_center(lbl_btn_a);

    // Button B
    btn_b = lv_btn_create(bottom_bar);
    lv_obj_set_size(btn_b, 80, 26);
    lv_obj_set_style_bg_color(btn_b, lv_color_make(50, 100, 200), 0);
    lv_obj_set_style_radius(btn_b, 4, 0);
    lv_obj_add_event_cb(btn_b, event_btn_b, LV_EVENT_CLICKED, NULL);
    lbl_btn_b = lv_label_create(btn_b);
    lv_label_set_text(lbl_btn_b, "B");
    lv_obj_set_style_text_font(lbl_btn_b, &lv_font_montserrat_16, 0);
    lv_obj_center(lbl_btn_b);

    // Menu button
    btn_menu = lv_btn_create(bottom_bar);
    lv_obj_set_size(btn_menu, 80, 26);
    lv_obj_set_style_bg_color(btn_menu, lv_color_make(150, 50, 50), 0);
    lv_obj_set_style_radius(btn_menu, 4, 0);
    lv_obj_add_event_cb(btn_menu, event_btn_menu, LV_EVENT_CLICKED, NULL);
    lbl_btn_menu = lv_label_create(btn_menu);
    lv_label_set_text(lbl_btn_menu, "Menu");
    lv_obj_set_style_text_font(lbl_btn_menu, &lv_font_montserrat_14, 0);
    lv_obj_center(lbl_btn_menu);

    g_ui.last_activity_time = esp_timer_get_time();
    ESP_LOGI(TAG, "UI initialized");
    return ESP_OK;
}

void ui_update(void)
{
    lv_timer_handler();
}

void ui_set_mode(ui_mode_t mode)
{
    g_ui.mode = mode;
    g_ui.air_mouse_active = (mode == MODE_AIR_MOUSE);

    const char *mode_str;
    lv_color_t mode_color;
    switch (mode) {
    case MODE_JOYSTICK:
        mode_str = "Joystick";
        mode_color = lv_color_make(100, 255, 100);
        break;
    case MODE_AIR_MOUSE:
        mode_str = "AirMouse";
        mode_color = lv_color_make(255, 200, 50);
        break;
    case MODE_SCROLL:
        mode_str = "Scroll";
        mode_color = lv_color_make(100, 200, 255);
        break;
    default:
        mode_str = "Unknown";
        mode_color = lv_color_make(200, 200, 200);
        break;
    }

    lv_label_set_text_fmt(lbl_mode, "Mode: %s", mode_str);
    lv_obj_set_style_text_color(lbl_mode, mode_color, 0);
}

void ui_set_bt_status(bool connected)
{
    if (connected) {
        lv_label_set_text(lbl_bt, "BT: Connected");
        lv_obj_set_style_text_color(lbl_bt, lv_color_make(80, 255, 80), 0);
    } else {
        lv_label_set_text(lbl_bt, "BT: Disconnected");
        lv_obj_set_style_text_color(lbl_bt, lv_color_make(255, 80, 80), 0);
    }
}

void ui_set_battery(uint8_t percent)
{
    lv_label_set_text_fmt(lbl_bat, "Bat: %d%%", percent);
}

void ui_update_imu(mahony_state_t *mahony, qmi8658_dev_t *imu)
{
    if (!g_ui.air_mouse_active) return;

    qmi8658_data_t data;
    if (qmi8658_read_data(imu, &data) != ESP_OK) return;

    mahony_update(mahony, data.accel.x, data.accel.y, data.accel.z,
                  data.gyro.x, data.gyro.y, data.gyro.z);

    float pitch, yaw, roll;
    mahony_get_angles(mahony, &pitch, &yaw, &roll);

    float dp = pitch - g_ui.pitch_offset;
    float dy = yaw - g_ui.yaw_offset;

    // Map angle to mouse movement (sensitivity factor)
    float sensitivity = 150.0f;
    g_ui.mouse_x = (int16_t)(dy * sensitivity);
    g_ui.mouse_y = (int16_t)(dp * sensitivity);

    // Update cursor position on screen
    int cx = LCD_WIDTH / 2 + (int)(dy * 2.0f);
    int cy = LCD_HEIGHT / 2 + (int)(dp * 2.0f);
    lv_obj_set_pos(cursor_obj, cx - 6, cy - 6);
    lv_obj_set_style_bg_opa(cursor_obj, LV_OPA_COVER, 0);

    g_ui.pitch = pitch;
    g_ui.yaw = yaw;
}

void ui_process_touch(axs15231b_dev_t *touch_dev)
{
    axs15231b_touch_data_t data;
    if (axs15231b_read_touch(touch_dev, &data) != ESP_OK) return;

    if (data.pressed) {
        g_ui.last_activity_time = esp_timer_get_time();
        if (g_ui.sleeping) {
            g_ui.sleeping = false;
            ESP_LOGI(TAG, "Waking up from sleep");
        }
    }

    // Detect tap on scroll zone to trigger click
    static bool was_pressed = false;
    static int64_t press_time = 0;

    if (data.pressed && !was_pressed) {
        press_time = esp_timer_get_time();
    } else if (!data.pressed && was_pressed) {
        int64_t duration = (esp_timer_get_time() - press_time) / 1000;
        if (duration < 200 && data.x > SCROLL_ZONE_X) {
            // Short tap = mouse click
            g_ui.mouse_buttons = 0x01;
        } else if (duration > 500 && data.x > SCROLL_ZONE_X) {
            // Long press on scroll zone = toggle air mouse
            if (g_ui.mode == MODE_JOYSTICK) {
                ui_set_mode(MODE_AIR_MOUSE);
                g_ui.pitch_offset = g_ui.pitch;
                g_ui.yaw_offset = g_ui.yaw;
            } else {
                ui_set_mode(MODE_JOYSTICK);
            }
        }
    }
    was_pressed = data.pressed;
}

void ui_send_reports(ble_combo_state_t *ble_state)
{
    if (!ble_combo_is_connected()) return;

    // Send gamepad (joystick)
    ble_combo_gamepad_t pad = {
        .left_x = g_ui.joy_x,
        .left_y = g_ui.joy_y,
        .right_x = 0,
        .right_y = 0,
        .hat = 0x08,  // Neutral
        .buttons = 0,
    };

    if (g_ui.button_a) pad.buttons |= 0x01;
    if (g_ui.button_b) pad.buttons |= 0x02;
    if (g_ui.button_menu) pad.buttons |= 0x04;

    ble_combo_send_gamepad(&pad);

    // Send mouse
    if (g_ui.air_mouse_active && (g_ui.mouse_x != 0 || g_ui.mouse_y != 0)) {
        ble_combo_mouse_t mouse = {
            .x = g_ui.mouse_x,
            .y = g_ui.mouse_y,
            .wheel = g_ui.mouse_wheel,
            .buttons = g_ui.mouse_buttons,
        };
        ble_combo_send_mouse(&mouse);
    } else if (g_ui.mouse_wheel != 0) {
        ble_combo_mouse_t mouse = {
            .x = 0, .y = 0,
            .wheel = g_ui.mouse_wheel,
            .buttons = 0,
        };
        ble_combo_send_mouse(&mouse);
    }

    // Send media keys for gestures
    if (g_ui.gesture_left) {
        ble_combo_media_t media = { .keys = MEDIA_KEY_PREV };
        ble_combo_send_media(&media);
        ESP_LOGI(TAG, "Gesture: Previous");
    }
    if (g_ui.gesture_right) {
        ble_combo_media_t media = { .keys = MEDIA_KEY_NEXT };
        ble_combo_send_media(&media);
        ESP_LOGI(TAG, "Gesture: Next");
    }

    // Reset one-shot flags
    g_ui.button_a = false;
    g_ui.button_b = false;
    g_ui.button_menu = false;
    g_ui.mouse_buttons = 0;
    g_ui.gesture_left = false;
    g_ui.gesture_right = false;
    g_ui.mouse_x = 0;
    g_ui.mouse_y = 0;
}

// ===== Gesture Detection =====
void ui_detect_gesture(qmi8658_dev_t *imu)
{
    qmi8658_data_t data;
    if (qmi8658_read_data(imu, &data) != ESP_OK) return;

    float accel_total = fabsf(data.accel.x) + fabsf(data.accel.z);

    gesture_accel_history[gesture_idx] = accel_total;
    gesture_idx = (gesture_idx + 1) % 10;

    // Check for sharp acceleration peaks
    float max_val = 0;
    for (int i = 0; i < 10; i++) {
        if (gesture_accel_history[i] > max_val) max_val = gesture_accel_history[i];
    }

    int64_t now = esp_timer_get_time();
    if (max_val > 3.0f && (now - gesture_last_time) > 1000000) {
        if (data.accel.x > 0.5f) {
            g_ui.gesture_right = true;
        } else if (data.accel.x < -0.5f) {
            g_ui.gesture_left = true;
        }
        gesture_last_time = now;
    }
}

// ===== Sleep Logic =====
void ui_check_sleep(qmi8658_dev_t *imu)
{
    if (g_ui.sleeping) return;

    int64_t now = esp_timer_get_time();
    if ((now - g_ui.last_activity_time) > 30000000) {  // 30 seconds
        if (qmi8658_is_stationary(imu, 0.05f, 5000)) {
            g_ui.sleeping = true;
            ESP_LOGI(TAG, "Entering sleep mode");
            extern lcd_dev_t g_lcd;
            lcd_set_brightness(&g_lcd, 0);
        }
    }
}

void ui_wake_from_imu(qmi8658_dev_t *imu)
{
    if (!g_ui.sleeping) return;

    float total = qmi8658_get_total_accel(imu);
    if (fabsf(total - 1.0f) > 0.3f) {
        g_ui.sleeping = false;
        g_ui.last_activity_time = esp_timer_get_time();
        ESP_LOGI(TAG, "Waking up from IMU");
        extern lcd_dev_t g_lcd;
        lcd_set_brightness(&g_lcd, 128);
    }
}

// ===== LVGL Callbacks =====
static void disp_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p)
{
    extern lcd_dev_t g_lcd;
    esp_lcd_panel_draw_bitmap(g_lcd.panel_handle, area->x1, area->y1, area->x2 + 1, area->y2 + 1, color_p);
    lv_disp_flush_ready(drv);
}

static void touch_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    data->state = LV_INDEV_STATE_RELEASED;

    extern axs15231b_dev_t g_touch;
    axs15231b_touch_data_t tdata;
    if (axs15231b_read_touch(&g_touch, &tdata) == ESP_OK && tdata.pressed) {
        data->state = LV_INDEV_STATE_PRESSED;
        data->point.x = tdata.x;
        data->point.y = tdata.y;
    }
}
