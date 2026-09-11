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
    .joy_x = 0,
    .joy_y = 0,
    .touchpad_x = 0,
    .touchpad_y = 0,
    .button_trigger = false,
    .button_grip = false,
    .button_a = false,
    .button_b = false,
    .button_menu = false,
};

// LVGL objects
static lv_obj_t *scr_main;
static lv_obj_t *top_bar;
static lv_obj_t *lbl_bt;
static lv_obj_t *lbl_bat;
static lv_obj_t *lbl_mode;

// Interactive Zones
static lv_obj_t *joystick_zone;
static lv_obj_t *joystick_cursor;
static lv_obj_t *touchpad_zone;
static lv_obj_t *touchpad_cursor;

// Bottom Bar VR Buttons
static lv_obj_t *bottom_bar;
static lv_obj_t *btn_trigger;
static lv_obj_t *btn_grip;
static lv_obj_t *btn_a;
static lv_obj_t *btn_b;
static lv_obj_t *btn_menu;
static lv_obj_t *btn_mode;

// Joystick tracking
static bool joy_touching = false;
static int joy_origin_x = 0, joy_origin_y = 0;
static const int joy_max_radius = 45;

// Touchpad tracking
static bool pad_touching = false;
static int pad_origin_x = 0, pad_origin_y = 0;
static const int pad_max_radius = 45;

// LCD rotation & DMA transfer buffers
static uint16_t *s_rot_buf = NULL;
static uint16_t *s_dma_buf = NULL;

static void disp_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p);
static void touch_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data);

// ==========================================
// Button Event Handlers (Support continuous press)
// ==========================================
static void event_btn_trigger(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        g_ui.button_trigger = true;
        lv_obj_set_style_bg_color(btn_trigger, lv_color_make(255, 140, 0), 0);
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_CANCEL) {
        g_ui.button_trigger = false;
        lv_obj_set_style_bg_color(btn_trigger, lv_color_make(200, 80, 20), 0);
    }
}

static void event_btn_grip(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        g_ui.button_grip = true;
        lv_obj_set_style_bg_color(btn_grip, lv_color_make(0, 180, 255), 0);
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_CANCEL) {
        g_ui.button_grip = false;
        lv_obj_set_style_bg_color(btn_grip, lv_color_make(20, 110, 180), 0);
    }
}

static void event_btn_a(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        g_ui.button_a = true;
        lv_obj_set_style_bg_color(btn_a, lv_color_make(60, 220, 80), 0);
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_CANCEL) {
        g_ui.button_a = false;
        lv_obj_set_style_bg_color(btn_a, lv_color_make(35, 140, 55), 0);
    }
}

static void event_btn_b(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        g_ui.button_b = true;
        lv_obj_set_style_bg_color(btn_b, lv_color_make(240, 60, 60), 0);
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_CANCEL) {
        g_ui.button_b = false;
        lv_obj_set_style_bg_color(btn_b, lv_color_make(160, 35, 35), 0);
    }
}

static void event_btn_menu(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        g_ui.button_menu = true;
        lv_obj_set_style_bg_color(btn_menu, lv_color_make(90, 110, 140), 0);
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_CANCEL) {
        g_ui.button_menu = false;
        lv_obj_set_style_bg_color(btn_menu, lv_color_make(50, 65, 85), 0);
    }
}

static void event_btn_mode(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        ui_mode_t next_mode = (ui_mode_t)((g_ui.mode + 1) % 3);
        ui_set_mode(next_mode);
        if (next_mode == MODE_AIR_MOUSE) {
            g_ui.pitch_offset = g_ui.pitch;
            g_ui.yaw_offset = g_ui.yaw;
        }
    }
}

// ==========================================
// Left Zone: Thumbstick (Movement)
// ==========================================
static void event_joystick_zone(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = lv_indev_get_act();
    lv_point_t p;
    lv_indev_get_point(indev, &p);

    if (code == LV_EVENT_PRESSED) {
        joy_touching = true;
        joy_origin_x = p.x;
        joy_origin_y = p.y;
        lv_obj_set_style_bg_opa(joystick_cursor, LV_OPA_COVER, 0);
        lv_obj_set_pos(joystick_cursor, p.x - 12, p.y - 12);
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_CANCEL) {
        joy_touching = false;
        g_ui.joy_x = 0;
        g_ui.joy_y = 0;
        lv_obj_set_style_bg_opa(joystick_cursor, LV_OPA_TRANSP, 0);
    } else if (code == LV_EVENT_PRESSING && joy_touching) {
        int dx = p.x - joy_origin_x;
        int dy = p.y - joy_origin_y;
        float dist = sqrtf((float)(dx * dx + dy * dy));
        if (dist > joy_max_radius) {
            dx = (int)((float)dx * joy_max_radius / dist);
            dy = (int)((float)dy * joy_max_radius / dist);
        }
        g_ui.joy_x = (int16_t)(((float)dx / joy_max_radius) * 32767.0f);
        g_ui.joy_y = (int16_t)(((float)dy / joy_max_radius) * 32767.0f);

        lv_obj_set_pos(joystick_cursor, joy_origin_x + dx - 12, joy_origin_y + dy - 12);
    }
}

// ==========================================
// Right Zone: Touchpad (Look / View Stick)
// ==========================================
static void event_touchpad_zone(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = lv_indev_get_act();
    lv_point_t p;
    lv_indev_get_point(indev, &p);

    if (code == LV_EVENT_PRESSED) {
        pad_touching = true;
        pad_origin_x = p.x;
        pad_origin_y = p.y;
        lv_obj_set_style_bg_opa(touchpad_cursor, LV_OPA_COVER, 0);
        lv_obj_set_pos(touchpad_cursor, p.x - 10, p.y - 10);
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_CANCEL) {
        pad_touching = false;
        g_ui.touchpad_x = 0;
        g_ui.touchpad_y = 0;
        g_ui.mouse_wheel = 0;
        lv_obj_set_style_bg_opa(touchpad_cursor, LV_OPA_TRANSP, 0);
    } else if (code == LV_EVENT_PRESSING && pad_touching) {
        int dx = p.x - pad_origin_x;
        int dy = p.y - pad_origin_y;

        if (g_ui.mode == MODE_SCROLL) {
            g_ui.mouse_wheel = (int8_t)(-dy / 4);
        } else {
            float dist = sqrtf((float)(dx * dx + dy * dy));
            if (dist > pad_max_radius) {
                dx = (int)((float)dx * pad_max_radius / dist);
                dy = (int)((float)dy * pad_max_radius / dist);
            }
            g_ui.touchpad_x = (int16_t)(((float)dx / pad_max_radius) * 32767.0f);
            g_ui.touchpad_y = (int16_t)(((float)dy / pad_max_radius) * 32767.0f);
        }

        lv_obj_set_pos(touchpad_cursor, pad_origin_x + dx - 10, pad_origin_y + dy - 10);
    }
}

static void lv_tick_timer_cb(void *arg)
{
    lv_tick_inc(5);
}

// ==========================================
// UI Initialization
// ==========================================
esp_err_t ui_init(void)
{
    ESP_LOGI(TAG, "Initializing VR Controller UI");

    // 5ms LVGL tick timer
    const esp_timer_create_args_t tick_timer_args = {
        .callback = lv_tick_timer_cb,
        .name = "lv_tick",
    };
    esp_timer_handle_t tick_timer = NULL;
    esp_timer_create(&tick_timer_args, &tick_timer);
    esp_timer_start_periodic(tick_timer, 5000);

    lv_init();

    // Allocate draw buffer in PSRAM (full screen 640x172)
    g_ui.buf1 = heap_caps_malloc(LCD_WIDTH * LCD_HEIGHT * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    if (!g_ui.buf1) {
        ESP_LOGE(TAG, "Failed to allocate LVGL PSRAM buffer");
        return ESP_ERR_NO_MEM;
    }

    // Allocate 90-degree rotated buffer (172x640) in PSRAM
    s_rot_buf = heap_caps_malloc(LCD_PHYS_WIDTH * LCD_PHYS_HEIGHT * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
    if (!s_rot_buf) {
        ESP_LOGE(TAG, "Failed to allocate rotation buffer");
        return ESP_ERR_NO_MEM;
    }

    // Allocate DMA chunk buffer (64 lines = 22KB) in internal RAM
    s_dma_buf = heap_caps_malloc(LCD_PHYS_WIDTH * 64 * sizeof(uint16_t), MALLOC_CAP_DMA);
    if (!s_dma_buf) {
        ESP_LOGE(TAG, "Failed to allocate DMA chunk buffer");
        return ESP_ERR_NO_MEM;
    }

    lv_disp_draw_buf_init(&g_ui.draw_buf, g_ui.buf1, NULL, LCD_WIDTH * LCD_HEIGHT);

    lv_disp_drv_init(&g_ui.disp_drv);
    g_ui.disp_drv.hor_res = LCD_WIDTH;   // 640
    g_ui.disp_drv.ver_res = LCD_HEIGHT;  // 172
    g_ui.disp_drv.flush_cb = disp_flush_cb;
    g_ui.disp_drv.draw_buf = &g_ui.draw_buf;
    g_ui.disp_drv.full_refresh = 1;
    lv_disp_drv_register(&g_ui.disp_drv);

    lv_indev_drv_init(&g_ui.indev_drv);
    g_ui.indev_drv.type = LV_INDEV_TYPE_POINTER;
    g_ui.indev_drv.read_cb = touch_read_cb;
    lv_indev_drv_register(&g_ui.indev_drv);

    // Main screen
    scr_main = lv_scr_act();
    lv_obj_set_style_bg_color(scr_main, lv_color_make(18, 20, 32), 0);

    // ----------------------------------------------------
    // 1. TOP BAR (Y=0, H=20)
    // ----------------------------------------------------
    top_bar = lv_obj_create(scr_main);
    lv_obj_set_size(top_bar, LCD_WIDTH, TOP_BAR_H);
    lv_obj_set_pos(top_bar, 0, 0);
    lv_obj_set_style_bg_color(top_bar, lv_color_make(26, 30, 48), 0);
    lv_obj_set_style_bg_opa(top_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(top_bar, 0, 0);
    lv_obj_set_style_pad_all(top_bar, 2, 0);
    lv_obj_clear_flag(top_bar, LV_OBJ_FLAG_SCROLLABLE);

    lbl_bt = lv_label_create(top_bar);
    lv_label_set_text(lbl_bt, "BT: Ready (Scan)");
    lv_obj_set_style_text_color(lbl_bt, lv_color_make(255, 100, 100), 0);
    lv_obj_set_style_text_font(lbl_bt, &lv_font_montserrat_12, 0);
    lv_obj_align(lbl_bt, LV_ALIGN_LEFT_MID, 6, 0);

    lbl_bat = lv_label_create(top_bar);
    lv_label_set_text(lbl_bat, "ESP32-S3 VR | Bat 100%");
    lv_obj_set_style_text_color(lbl_bat, lv_color_make(180, 190, 210), 0);
    lv_obj_set_style_text_font(lbl_bat, &lv_font_montserrat_12, 0);
    lv_obj_align(lbl_bat, LV_ALIGN_CENTER, 0, 0);

    lbl_mode = lv_label_create(top_bar);
    lv_label_set_text(lbl_mode, "Mode: VR Joystick");
    lv_obj_set_style_text_color(lbl_mode, lv_color_make(80, 230, 150), 0);
    lv_obj_set_style_text_font(lbl_mode, &lv_font_montserrat_12, 0);
    lv_obj_align(lbl_mode, LV_ALIGN_RIGHT_MID, -6, 0);

    // ----------------------------------------------------
    // 2. LEFT ZONE: THUMBSTICK (X=2, Y=22, W=314, H=110)
    // ----------------------------------------------------
    joystick_zone = lv_obj_create(scr_main);
    lv_obj_set_size(joystick_zone, JOYSTICK_ZONE_W, CONTENT_H);
    lv_obj_set_pos(joystick_zone, JOYSTICK_ZONE_X, CONTENT_Y);
    lv_obj_set_style_bg_color(joystick_zone, lv_color_make(24, 28, 44), 0);
    lv_obj_set_style_bg_opa(joystick_zone, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(joystick_zone, lv_color_make(45, 52, 80), 0);
    lv_obj_set_style_border_width(joystick_zone, 1, 0);
    lv_obj_set_style_radius(joystick_zone, 6, 0);
    lv_obj_clear_flag(joystick_zone, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_joy = lv_label_create(joystick_zone);
    lv_label_set_text(lbl_joy, "LEFT: THUMBSTICK (MOVE)");
    lv_obj_set_style_text_color(lbl_joy, lv_color_make(100, 180, 255), 0);
    lv_obj_set_style_text_font(lbl_joy, &lv_font_montserrat_12, 0);
    lv_obj_align(lbl_joy, LV_ALIGN_TOP_LEFT, 6, 2);

    // Crosshairs
    lv_obj_t *joy_ch_h = lv_obj_create(joystick_zone);
    lv_obj_set_size(joy_ch_h, 70, 1);
    lv_obj_set_style_bg_color(joy_ch_h, lv_color_make(50, 60, 90), 0);
    lv_obj_align(joy_ch_h, LV_ALIGN_CENTER, 0, 4);

    lv_obj_t *joy_ch_v = lv_obj_create(joystick_zone);
    lv_obj_set_size(joy_ch_v, 1, 70);
    lv_obj_set_style_bg_color(joy_ch_v, lv_color_make(50, 60, 90), 0);
    lv_obj_align(joy_ch_v, LV_ALIGN_CENTER, 0, 4);

    joystick_cursor = lv_obj_create(joystick_zone);
    lv_obj_set_size(joystick_cursor, 24, 24);
    lv_obj_set_style_bg_color(joystick_cursor, lv_color_make(0, 230, 120), 0);
    lv_obj_set_style_radius(joystick_cursor, 12, 0);
    lv_obj_set_style_bg_opa(joystick_cursor, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(joystick_cursor, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_add_event_cb(joystick_zone, event_joystick_zone, LV_EVENT_ALL, NULL);

    // ----------------------------------------------------
    // 3. RIGHT ZONE: TOUCHPAD (X=324, Y=22, W=314, H=110)
    // ----------------------------------------------------
    touchpad_zone = lv_obj_create(scr_main);
    lv_obj_set_size(touchpad_zone, TOUCHPAD_ZONE_W, CONTENT_H);
    lv_obj_set_pos(touchpad_zone, TOUCHPAD_ZONE_X, CONTENT_Y);
    lv_obj_set_style_bg_color(touchpad_zone, lv_color_make(24, 28, 44), 0);
    lv_obj_set_style_bg_opa(touchpad_zone, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(touchpad_zone, lv_color_make(45, 52, 80), 0);
    lv_obj_set_style_border_width(touchpad_zone, 1, 0);
    lv_obj_set_style_radius(touchpad_zone, 6, 0);
    lv_obj_clear_flag(touchpad_zone, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_pad = lv_label_create(touchpad_zone);
    lv_label_set_text(lbl_pad, "RIGHT: TOUCHPAD (LOOK / VIEW)");
    lv_obj_set_style_text_color(lbl_pad, lv_color_make(255, 170, 70), 0);
    lv_obj_set_style_text_font(lbl_pad, &lv_font_montserrat_12, 0);
    lv_obj_align(lbl_pad, LV_ALIGN_TOP_LEFT, 6, 2);

    // Crosshairs
    lv_obj_t *pad_ch_h = lv_obj_create(touchpad_zone);
    lv_obj_set_size(pad_ch_h, 70, 1);
    lv_obj_set_style_bg_color(pad_ch_h, lv_color_make(50, 60, 90), 0);
    lv_obj_align(pad_ch_h, LV_ALIGN_CENTER, 0, 4);

    lv_obj_t *pad_ch_v = lv_obj_create(touchpad_zone);
    lv_obj_set_size(pad_ch_v, 1, 70);
    lv_obj_set_style_bg_color(pad_ch_v, lv_color_make(50, 60, 90), 0);
    lv_obj_align(pad_ch_v, LV_ALIGN_CENTER, 0, 4);

    touchpad_cursor = lv_obj_create(touchpad_zone);
    lv_obj_set_size(touchpad_cursor, 20, 20);
    lv_obj_set_style_bg_color(touchpad_cursor, lv_color_make(255, 160, 30), 0);
    lv_obj_set_style_radius(touchpad_cursor, 10, 0);
    lv_obj_set_style_bg_opa(touchpad_cursor, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(touchpad_cursor, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_add_event_cb(touchpad_zone, event_touchpad_zone, LV_EVENT_ALL, NULL);

    // ----------------------------------------------------
    // 4. BOTTOM BAR: VR BLIND-TOUCH BUTTONS (Y=134, H=38)
    // ----------------------------------------------------
    bottom_bar = lv_obj_create(scr_main);
    lv_obj_set_size(bottom_bar, LCD_WIDTH, BOTTOM_BAR_H);
    lv_obj_set_pos(bottom_bar, 0, LCD_HEIGHT - BOTTOM_BAR_H);
    lv_obj_set_style_bg_color(bottom_bar, lv_color_make(20, 23, 36), 0);
    lv_obj_set_style_bg_opa(bottom_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bottom_bar, 0, 0);
    lv_obj_set_style_pad_all(bottom_bar, 1, 0);
    lv_obj_clear_flag(bottom_bar, LV_OBJ_FLAG_SCROLLABLE);

    // 1. TRIGGER (Width: 116)
    btn_trigger = lv_btn_create(bottom_bar);
    lv_obj_set_size(btn_trigger, 116, 34);
    lv_obj_set_pos(btn_trigger, 2, 1);
    lv_obj_set_style_bg_color(btn_trigger, lv_color_make(200, 80, 20), 0);
    lv_obj_set_style_radius(btn_trigger, 5, 0);
    lv_obj_add_event_cb(btn_trigger, event_btn_trigger, LV_EVENT_ALL, NULL);
    lv_obj_t *lbl_trig = lv_label_create(btn_trigger);
    lv_label_set_text(lbl_trig, "TRIGGER");
    lv_obj_set_style_text_font(lbl_trig, &lv_font_montserrat_14, 0);
    lv_obj_center(lbl_trig);

    // 2. GRIP (Width: 116)
    btn_grip = lv_btn_create(bottom_bar);
    lv_obj_set_size(btn_grip, 116, 34);
    lv_obj_set_pos(btn_grip, 122, 1);
    lv_obj_set_style_bg_color(btn_grip, lv_color_make(20, 110, 180), 0);
    lv_obj_set_style_radius(btn_grip, 5, 0);
    lv_obj_add_event_cb(btn_grip, event_btn_grip, LV_EVENT_ALL, NULL);
    lv_obj_t *lbl_gp = lv_label_create(btn_grip);
    lv_label_set_text(lbl_gp, "GRIP");
    lv_obj_set_style_text_font(lbl_gp, &lv_font_montserrat_14, 0);
    lv_obj_center(lbl_gp);

    // 3. Button A (Width: 90)
    btn_a = lv_btn_create(bottom_bar);
    lv_obj_set_size(btn_a, 90, 34);
    lv_obj_set_pos(btn_a, 242, 1);
    lv_obj_set_style_bg_color(btn_a, lv_color_make(35, 140, 55), 0);
    lv_obj_set_style_radius(btn_a, 5, 0);
    lv_obj_add_event_cb(btn_a, event_btn_a, LV_EVENT_ALL, NULL);
    lv_obj_t *lbl_a = lv_label_create(btn_a);
    lv_label_set_text(lbl_a, "A");
    lv_obj_set_style_text_font(lbl_a, &lv_font_montserrat_16, 0);
    lv_obj_center(lbl_a);

    // 4. Button B (Width: 90)
    btn_b = lv_btn_create(bottom_bar);
    lv_obj_set_size(btn_b, 90, 34);
    lv_obj_set_pos(btn_b, 336, 1);
    lv_obj_set_style_bg_color(btn_b, lv_color_make(160, 35, 35), 0);
    lv_obj_set_style_radius(btn_b, 5, 0);
    lv_obj_add_event_cb(btn_b, event_btn_b, LV_EVENT_ALL, NULL);
    lv_obj_t *lbl_b = lv_label_create(btn_b);
    lv_label_set_text(lbl_b, "B");
    lv_obj_set_style_text_font(lbl_b, &lv_font_montserrat_16, 0);
    lv_obj_center(lbl_b);

    // 5. MENU (Width: 100)
    btn_menu = lv_btn_create(bottom_bar);
    lv_obj_set_size(btn_menu, 100, 34);
    lv_obj_set_pos(btn_menu, 430, 1);
    lv_obj_set_style_bg_color(btn_menu, lv_color_make(50, 65, 85), 0);
    lv_obj_set_style_radius(btn_menu, 5, 0);
    lv_obj_add_event_cb(btn_menu, event_btn_menu, LV_EVENT_ALL, NULL);
    lv_obj_t *lbl_m = lv_label_create(btn_menu);
    lv_label_set_text(lbl_m, "MENU");
    lv_obj_set_style_text_font(lbl_m, &lv_font_montserrat_14, 0);
    lv_obj_center(lbl_m);

    // 6. MODE (Width: 104)
    btn_mode = lv_btn_create(bottom_bar);
    lv_obj_set_size(btn_mode, 104, 34);
    lv_obj_set_pos(btn_mode, 534, 1);
    lv_obj_set_style_bg_color(btn_mode, lv_color_make(70, 45, 105), 0);
    lv_obj_set_style_radius(btn_mode, 5, 0);
    lv_obj_add_event_cb(btn_mode, event_btn_mode, LV_EVENT_ALL, NULL);
    lv_obj_t *lbl_md = lv_label_create(btn_mode);
    lv_label_set_text(lbl_md, "MODE");
    lv_obj_set_style_text_font(lbl_md, &lv_font_montserrat_14, 0);
    lv_obj_center(lbl_md);

    g_ui.last_activity_time = esp_timer_get_time();
    ESP_LOGI(TAG, "VR UI initialized successfully");
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

    const char *mode_str = "Joystick";
    lv_color_t color = lv_color_make(80, 230, 150);

    if (mode == MODE_AIR_MOUSE) {
        mode_str = "AirMouse";
        color = lv_color_make(255, 200, 50);
    } else if (mode == MODE_SCROLL) {
        mode_str = "Scroll";
        color = lv_color_make(100, 200, 255);
    }

    lv_label_set_text_fmt(lbl_mode, "Mode: %s", mode_str);
    lv_obj_set_style_text_color(lbl_mode, color, 0);
}

void ui_set_bt_status(bool connected)
{
    if (connected) {
        lv_label_set_text(lbl_bt, "BT: Connected");
        lv_obj_set_style_text_color(lbl_bt, lv_color_make(80, 255, 80), 0);
    } else {
        lv_label_set_text(lbl_bt, "BT: Ready (Scan)");
        lv_obj_set_style_text_color(lbl_bt, lv_color_make(255, 100, 100), 0);
    }
}

void ui_set_battery(uint8_t percent)
{
    lv_label_set_text_fmt(lbl_bat, "ESP32-S3 VR | Bat %d%%", percent);
}

void ui_update_imu(mahony_state_t *mahony, qmi8658_dev_t *imu)
{
    if (!g_ui.air_mouse_active) return;

    qmi8658_data_t data;
    if (qmi8658_read_data(imu, &data) != ESP_OK) return;

    const float dps_to_rad = 0.0174532925f; // M_PI / 180.0f
    mahony_update(mahony, data.accel.x, data.accel.y, data.accel.z,
                  data.gyro.x * dps_to_rad,
                  data.gyro.y * dps_to_rad,
                  data.gyro.z * dps_to_rad);

    float pitch, yaw, roll;
    mahony_get_angles(mahony, &pitch, &yaw, &roll);

    float dp = pitch - g_ui.pitch_offset;
    float dy = yaw - g_ui.yaw_offset;

    float sensitivity = 120.0f;
    g_ui.mouse_x = (int16_t)(dy * sensitivity);
    g_ui.mouse_y = (int16_t)(dp * sensitivity);

    g_ui.pitch = pitch;
    g_ui.yaw = yaw;
    g_ui.roll = roll;
}

void ui_process_touch(axs15231b_dev_t *touch_dev)
{
    // Touch handled by LVGL input driver callback
}

void ui_send_reports(ble_combo_state_t *ble_state)
{
    if (!ble_combo_is_connected()) return;

    // Send VR Gamepad Report
    ble_combo_gamepad_t pad = {
        .left_x = g_ui.joy_x,
        .left_y = g_ui.joy_y,
        .right_x = g_ui.touchpad_x,
        .right_y = g_ui.touchpad_y,
        .hat = 0x08, // Neutral
        .buttons = 0,
    };

    if (g_ui.button_trigger) pad.buttons |= (1 << 0); // Trigger
    if (g_ui.button_grip)    pad.buttons |= (1 << 1); // Grip
    if (g_ui.button_a)       pad.buttons |= (1 << 2); // Button A
    if (g_ui.button_b)       pad.buttons |= (1 << 3); // Button B
    if (g_ui.button_menu)    pad.buttons |= (1 << 4); // Menu

    ble_combo_send_gamepad(&pad);

    // AirMouse / Scroll
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
            .x = 0,
            .y = 0,
            .wheel = g_ui.mouse_wheel,
            .buttons = 0,
        };
        ble_combo_send_mouse(&mouse);
    }
}

void ui_detect_gesture(qmi8658_dev_t *imu)
{
    // Kept clean to avoid accidental multimedia triggering in VR mode
}

void ui_check_sleep(qmi8658_dev_t *imu)
{
    if (g_ui.sleeping) return;

    int64_t now = esp_timer_get_time();
    if ((now - g_ui.last_activity_time) > 60000000) {  // 60 seconds inactivity
        if (qmi8658_is_stationary(imu, 0.05f, 5000)) {
            g_ui.sleeping = true;
            ESP_LOGI(TAG, "Entering sleep mode");
            extern lcd_dev_t g_lcd;
            lcd_set_brightness(0);
        }
    }
}

void ui_wake_from_imu(qmi8658_dev_t *imu)
{
    if (!g_ui.sleeping) return;

    float total = qmi8658_get_total_accel(imu);
    if (fabsf(total - 1.0f) > 0.35f) {
        g_ui.sleeping = false;
        g_ui.last_activity_time = esp_timer_get_time();
        ESP_LOGI(TAG, "Waking up from IMU motion");
        extern lcd_dev_t g_lcd;
        lcd_set_brightness(255);
    }
}

// ==========================================
// LVGL Callbacks
// ==========================================
static void disp_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p)
{
    extern lcd_dev_t g_lcd;

    // Rotate 640x172 logical UI buffer 90 degrees clockwise into 172x640 physical buffer
    const uint16_t *src = (const uint16_t *)color_p;
    for (int y = 0; y < LCD_HEIGHT; y++) {
        int phys_x = LCD_HEIGHT - 1 - y;
        for (int x = 0; x < LCD_WIDTH; x++) {
            int phys_y = x;
            s_rot_buf[phys_y * LCD_PHYS_WIDTH + phys_x] = src[y * LCD_WIDTH + x];
        }
    }

    const int chunk_rows = 64;
    for (int y = 0; y < LCD_PHYS_HEIGHT; y += chunk_rows) {
        int lines = chunk_rows;
        if (y + lines > LCD_PHYS_HEIGHT) {
            lines = LCD_PHYS_HEIGHT - y;
        }
        memcpy(s_dma_buf, &s_rot_buf[y * LCD_PHYS_WIDTH], lines * LCD_PHYS_WIDTH * sizeof(uint16_t));
        lcd_draw_bitmap(&g_lcd, 0, y, LCD_PHYS_WIDTH, y + lines, s_dma_buf);
    }

    lv_disp_flush_ready(drv);
}

static void touch_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    data->state = LV_INDEV_STATE_RELEASED;

    extern axs15231b_dev_t g_touch;
    axs15231b_touch_data_t tdata;
    if (axs15231b_read_touch(&g_touch, &tdata) == ESP_OK && tdata.pressed) {
        data->state = LV_INDEV_STATE_PRESSED;

        int ui_x = LCD_WIDTH - (int)tdata.x;
        int ui_y = LCD_HEIGHT - (int)tdata.y;

        if (ui_x < 0) ui_x = 0;
        if (ui_x >= LCD_WIDTH) ui_x = LCD_WIDTH - 1;
        if (ui_y < 0) ui_y = 0;
        if (ui_y >= LCD_HEIGHT) ui_y = LCD_HEIGHT - 1;

        data->point.x = ui_x;
        data->point.y = ui_y;

        g_ui.last_activity_time = esp_timer_get_time();
        if (g_ui.sleeping) {
            g_ui.sleeping = false;
            extern lcd_dev_t g_lcd;
            lcd_set_brightness(255);
        }
    }
}
