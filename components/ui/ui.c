#include "ui.h"
#include "lcd_driver.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "UI";

ui_state_t g_ui = {
    .mode = MODE_VR_GAMEPAD,
    .air_mouse_active = false,
    .sleeping = false,
    .last_activity_time = 0,
    .joy_x = 0,
    .joy_y = 0,
    .touchpad_x = 0,
    .touchpad_y = 0,
    .mouse_x = 0,
    .mouse_y = 0,
    .mouse_wheel = 0,
    .mouse_buttons = 0,
    .button_trigger = false,
    .button_grip = false,
    .button_a = false,
    .button_b = false,
    .button_menu = false,
    .button_l3 = false,
};

// LVGL objects: Screen & Top Bar
static lv_obj_t *scr_main;
static lv_obj_t *top_bar;
static lv_obj_t *lbl_bt;
static lv_obj_t *lbl_bat;
static lv_obj_t *lbl_mode_top;

// Containers for 3 Dynamic Layouts
static lv_obj_t *cont_gamepad;
static lv_obj_t *cont_mouse;
static lv_obj_t *cont_media;

// === Layout 1: Gamepad Objects ===
static lv_obj_t *gp_joystick_zone;
static lv_obj_t *gp_joystick_cursor;
static lv_obj_t *gp_touchpad_zone;
static lv_obj_t *gp_touchpad_cursor;
static lv_obj_t *btn_gp_trigger;
static lv_obj_t *btn_gp_grip;
static lv_obj_t *btn_gp_a;
static lv_obj_t *btn_gp_b;
static lv_obj_t *btn_gp_menu;
static lv_obj_t *btn_gp_mode;
static lv_obj_t *lbl_gp_mode;

// === Layout 2: Mouse Objects ===
static lv_obj_t *mouse_touchpad_zone;
static lv_obj_t *mouse_touchpad_cursor;
static lv_obj_t *btn_ms_left;
static lv_obj_t *btn_ms_right;
static lv_obj_t *btn_ms_back;
static lv_obj_t *btn_ms_mode;
static lv_obj_t *lbl_ms_mode;

// === Layout 3: Media Objects ===
static lv_obj_t *btn_md_prev;
static lv_obj_t *btn_md_play;
static lv_obj_t *btn_md_next;
static lv_obj_t *btn_md_mute;
static lv_obj_t *btn_md_voldown;
static lv_obj_t *btn_md_volup;
static lv_obj_t *btn_md_mode;
static lv_obj_t *lbl_md_mode;

// Joystick tracking
static bool joy_touching = false;
static int joy_origin_x = 0, joy_origin_y = 0;
static const int joy_max_radius = 45;

// Touchpad tracking (Shared between Gamepad & Mouse)
static bool pad_touching = false;
static int pad_origin_x = 0, pad_origin_y = 0;
static int pad_last_y = 0;
static int64_t pad_press_time = 0;
static bool pad_is_scrolling = false;
static bool pad_long_pressed = false;

// Media command queue flag
static uint16_t s_pending_media_key = 0;

// LCD rotation & DMA transfer buffers
static uint16_t *s_rot_buf = NULL;
static uint16_t *s_dma_buf = NULL;

static void disp_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p);
static void touch_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data);

// ==========================================
// Mode Switch Event Handler
// ==========================================
static void event_cycle_mode(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        ui_mode_t next_mode = (ui_mode_t)((g_ui.mode + 1) % 3);
        ui_set_mode(next_mode);
        ESP_LOGI(TAG, "Mode switched to: %d", next_mode);
    }
}

// ==========================================
// Layout 1: Gamepad Buttons & Sticks
// ==========================================
static void event_btn_trigger(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        g_ui.button_trigger = true;
        lv_obj_set_style_bg_color(btn_gp_trigger, lv_color_make(255, 140, 0), 0);
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_CANCEL) {
        g_ui.button_trigger = false;
        lv_obj_set_style_bg_color(btn_gp_trigger, lv_color_make(200, 80, 20), 0);
    }
}

static void event_btn_grip(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        g_ui.button_grip = true;
        lv_obj_set_style_bg_color(btn_gp_grip, lv_color_make(0, 180, 255), 0);
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_CANCEL) {
        g_ui.button_grip = false;
        lv_obj_set_style_bg_color(btn_gp_grip, lv_color_make(20, 110, 180), 0);
    }
}

static void event_btn_a(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        g_ui.button_a = true;
        lv_obj_set_style_bg_color(btn_gp_a, lv_color_make(60, 220, 80), 0);
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_CANCEL) {
        g_ui.button_a = false;
        lv_obj_set_style_bg_color(btn_gp_a, lv_color_make(35, 140, 55), 0);
    }
}

static void event_btn_b(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        g_ui.button_b = true;
        lv_obj_set_style_bg_color(btn_gp_b, lv_color_make(240, 60, 60), 0);
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_CANCEL) {
        g_ui.button_b = false;
        lv_obj_set_style_bg_color(btn_gp_b, lv_color_make(160, 35, 35), 0);
    }
}

static void event_btn_menu(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        g_ui.button_menu = true;
        lv_obj_set_style_bg_color(btn_gp_menu, lv_color_make(90, 110, 140), 0);
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_CANCEL) {
        g_ui.button_menu = false;
        lv_obj_set_style_bg_color(btn_gp_menu, lv_color_make(50, 65, 85), 0);
    }
}

static void event_gp_joystick(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = lv_indev_get_act();
    lv_point_t p;
    lv_indev_get_point(indev, &p);

    if (code == LV_EVENT_PRESSED) {
        joy_touching = true;
        joy_origin_x = p.x;
        joy_origin_y = p.y;
        lv_obj_set_style_bg_opa(gp_joystick_cursor, LV_OPA_COVER, 0);
        lv_obj_set_pos(gp_joystick_cursor, p.x - 12, p.y - 12);
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_CANCEL) {
        joy_touching = false;
        g_ui.joy_x = 0;
        g_ui.joy_y = 0;
        lv_obj_set_style_bg_opa(gp_joystick_cursor, LV_OPA_TRANSP, 0);
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

        lv_obj_set_pos(gp_joystick_cursor, joy_origin_x + dx - 12, joy_origin_y + dy - 12);
    }
}

static void event_gp_touchpad(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = lv_indev_get_act();
    lv_point_t p;
    lv_indev_get_point(indev, &p);

    if (code == LV_EVENT_PRESSED) {
        pad_touching = true;
        pad_origin_x = p.x;
        pad_origin_y = p.y;
        lv_obj_set_style_bg_opa(gp_touchpad_cursor, LV_OPA_COVER, 0);
        lv_obj_set_pos(gp_touchpad_cursor, p.x - 10, p.y - 10);
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_CANCEL) {
        pad_touching = false;
        g_ui.touchpad_x = 0;
        g_ui.touchpad_y = 0;
        lv_obj_set_style_bg_opa(gp_touchpad_cursor, LV_OPA_TRANSP, 0);
    } else if (code == LV_EVENT_PRESSING && pad_touching) {
        int dx = p.x - pad_origin_x;
        int dy = p.y - pad_origin_y;
        float dist = sqrtf((float)(dx * dx + dy * dy));
        if (dist > joy_max_radius) {
            dx = (int)((float)dx * joy_max_radius / dist);
            dy = (int)((float)dy * joy_max_radius / dist);
        }
        g_ui.touchpad_x = (int16_t)(((float)dx / joy_max_radius) * 32767.0f);
        g_ui.touchpad_y = (int16_t)(((float)dy / joy_max_radius) * 32767.0f);

        lv_obj_set_pos(gp_touchpad_cursor, pad_origin_x + dx - 10, pad_origin_y + dy - 10);
    }
}

// ==========================================
// Layout 2: Super Touchpad Mouse Handlers
// ==========================================
static void event_super_touchpad(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = lv_indev_get_act();
    lv_point_t p;
    lv_indev_get_point(indev, &p);

    if (code == LV_EVENT_PRESSED) {
        pad_touching = true;
        pad_origin_x = p.x;
        pad_origin_y = p.y;
        pad_last_y = p.y;
        pad_press_time = esp_timer_get_time();
        pad_is_scrolling = false;
        pad_long_pressed = false;

        lv_obj_set_style_bg_color(mouse_touchpad_cursor, lv_color_make(255, 160, 30), 0);
        lv_obj_set_style_bg_opa(mouse_touchpad_cursor, LV_OPA_COVER, 0);
        lv_obj_set_pos(mouse_touchpad_cursor, p.x - 12, p.y - 12);
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_CANCEL) {
        pad_touching = false;
        g_ui.mouse_wheel = 0;
        lv_obj_set_style_bg_opa(mouse_touchpad_cursor, LV_OPA_TRANSP, 0);

        if (!pad_is_scrolling && !pad_long_pressed) {
            int64_t dur_ms = (esp_timer_get_time() - pad_press_time) / 1000;
            if (dur_ms < 450) {
                g_ui.mouse_buttons = 0x01; // Left Click
                ESP_LOGI(TAG, "Touchpad: Left Click");
            }
        }
    } else if (code == LV_EVENT_PRESSING && pad_touching) {
        int dx = p.x - pad_origin_x;
        int dy = p.y - pad_origin_y;
        int move_dist = abs(dx) + abs(dy);

        if (move_dist > 8) {
            pad_is_scrolling = true;
        }

        if (pad_is_scrolling) {
            int delta_y = p.y - pad_last_y;
            if (abs(delta_y) >= 4) {
                g_ui.mouse_wheel = (int8_t)(-delta_y / 3);
                pad_last_y = p.y;
            }
        } else if (!pad_long_pressed) {
            int64_t hold_ms = (esp_timer_get_time() - pad_press_time) / 1000;
            if (hold_ms >= 450) {
                pad_long_pressed = true;
                g_ui.mouse_buttons = 0x02; // Right Click
                ESP_LOGI(TAG, "Touchpad: Right Click");
                lv_obj_set_style_bg_color(mouse_touchpad_cursor, lv_color_make(60, 200, 255), 0);
            }
        }
        lv_obj_set_pos(mouse_touchpad_cursor, p.x - 12, p.y - 12);
    }
}

static void event_btn_ms_left(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        g_ui.mouse_buttons = 0x01;
        ESP_LOGI(TAG, "Mouse Left Click Button");
    }
}

static void event_btn_ms_right(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        g_ui.mouse_buttons = 0x02;
        ESP_LOGI(TAG, "Mouse Right Click Button");
    }
}

static void event_btn_ms_back(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        g_ui.button_b = true;
        ESP_LOGI(TAG, "Mouse Back Button");
    }
}

// ==========================================
// Layout 3: Media Remote Handlers
// ==========================================
static void event_btn_md_prev(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        s_pending_media_key = MEDIA_KEY_PREV;
        ESP_LOGI(TAG, "Media: Prev");
    }
}

static void event_btn_md_play(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        s_pending_media_key = MEDIA_KEY_PLAY_PAUSE;
        ESP_LOGI(TAG, "Media: Play/Pause");
    }
}

static void event_btn_md_next(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        s_pending_media_key = MEDIA_KEY_NEXT;
        ESP_LOGI(TAG, "Media: Next");
    }
}

static void event_btn_md_mute(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        s_pending_media_key = MEDIA_KEY_MUTE;
        ESP_LOGI(TAG, "Media: Mute");
    }
}

static void event_btn_md_voldown(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        s_pending_media_key = MEDIA_KEY_VOL_DOWN;
        ESP_LOGI(TAG, "Media: Vol -");
    }
}

static void event_btn_md_volup(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        s_pending_media_key = MEDIA_KEY_VOL_UP;
        ESP_LOGI(TAG, "Media: Vol +");
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
    ESP_LOGI(TAG, "Initializing Dynamic VR UI");

    const esp_timer_create_args_t tick_timer_args = {
        .callback = lv_tick_timer_cb,
        .name = "lv_tick",
    };
    esp_timer_handle_t tick_timer = NULL;
    esp_timer_create(&tick_timer_args, &tick_timer);
    esp_timer_start_periodic(tick_timer, 5000);

    lv_init();

    g_ui.buf1 = heap_caps_malloc(LCD_WIDTH * LCD_HEIGHT * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    if (!g_ui.buf1) return ESP_ERR_NO_MEM;

    s_rot_buf = heap_caps_malloc(LCD_PHYS_WIDTH * LCD_PHYS_HEIGHT * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
    if (!s_rot_buf) return ESP_ERR_NO_MEM;

    s_dma_buf = heap_caps_malloc(LCD_PHYS_WIDTH * 64 * sizeof(uint16_t), MALLOC_CAP_DMA);
    if (!s_dma_buf) return ESP_ERR_NO_MEM;

    lv_disp_draw_buf_init(&g_ui.draw_buf, g_ui.buf1, NULL, LCD_WIDTH * LCD_HEIGHT);

    lv_disp_drv_init(&g_ui.disp_drv);
    g_ui.disp_drv.hor_res = LCD_WIDTH;
    g_ui.disp_drv.ver_res = LCD_HEIGHT;
    g_ui.disp_drv.flush_cb = disp_flush_cb;
    g_ui.disp_drv.draw_buf = &g_ui.draw_buf;
    g_ui.disp_drv.full_refresh = 1;
    lv_disp_drv_register(&g_ui.disp_drv);

    lv_indev_drv_init(&g_ui.indev_drv);
    g_ui.indev_drv.type = LV_INDEV_TYPE_POINTER;
    g_ui.indev_drv.read_cb = touch_read_cb;
    lv_indev_drv_register(&g_ui.indev_drv);

    scr_main = lv_scr_act();
    lv_obj_set_style_bg_color(scr_main, lv_color_make(14, 16, 26), 0);

    // ----------------------------------------------------
    // 0. TOP BAR (Fixed Y=0, H=20 across all modes)
    // ----------------------------------------------------
    top_bar = lv_obj_create(scr_main);
    lv_obj_set_size(top_bar, LCD_WIDTH, TOP_BAR_H);
    lv_obj_set_pos(top_bar, 0, 0);
    lv_obj_set_style_bg_color(top_bar, lv_color_make(22, 25, 40), 0);
    lv_obj_set_style_bg_opa(top_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(top_bar, 0, 0);
    lv_obj_set_style_pad_all(top_bar, 2, 0);
    lv_obj_clear_flag(top_bar, LV_OBJ_FLAG_SCROLLABLE);

    lbl_bt = lv_label_create(top_bar);
    lv_label_set_text(lbl_bt, "BT: Ready");
    lv_obj_set_style_text_color(lbl_bt, lv_color_make(255, 100, 100), 0);
    lv_obj_set_style_text_font(lbl_bt, &lv_font_montserrat_12, 0);
    lv_obj_align(lbl_bt, LV_ALIGN_LEFT_MID, 6, 0);

    lbl_bat = lv_label_create(top_bar);
    lv_label_set_text(lbl_bat, "QUEST 2 CONTROLLER | Bat 100%");
    lv_obj_set_style_text_color(lbl_bat, lv_color_make(180, 190, 210), 0);
    lv_obj_set_style_text_font(lbl_bat, &lv_font_montserrat_12, 0);
    lv_obj_align(lbl_bat, LV_ALIGN_CENTER, 0, 0);

    lbl_mode_top = lv_label_create(top_bar);
    lv_label_set_text(lbl_mode_top, "MODE: VR PAD [Tap]");
    lv_obj_set_style_text_color(lbl_mode_top, lv_color_make(80, 230, 150), 0);
    lv_obj_set_style_text_font(lbl_mode_top, &lv_font_montserrat_12, 0);
    lv_obj_align(lbl_mode_top, LV_ALIGN_RIGHT_MID, -6, 0);
    lv_obj_add_flag(lbl_mode_top, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(lbl_mode_top, event_cycle_mode, LV_EVENT_CLICKED, NULL);

    // ====================================================
    // CONTAINER 1: VR GAMEPAD MODE (Y=20, H=152)
    // ====================================================
    cont_gamepad = lv_obj_create(scr_main);
    lv_obj_set_size(cont_gamepad, LCD_WIDTH, LCD_HEIGHT - TOP_BAR_H);
    lv_obj_set_pos(cont_gamepad, 0, TOP_BAR_H);
    lv_obj_set_style_bg_opa(cont_gamepad, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont_gamepad, 0, 0);
    lv_obj_set_style_pad_all(cont_gamepad, 0, 0);
    lv_obj_clear_flag(cont_gamepad, LV_OBJ_FLAG_SCROLLABLE);

    // Left Stick (Move)
    gp_joystick_zone = lv_obj_create(cont_gamepad);
    lv_obj_set_size(gp_joystick_zone, 314, 110);
    lv_obj_set_pos(gp_joystick_zone, 2, 2);
    lv_obj_set_style_bg_color(gp_joystick_zone, lv_color_make(22, 26, 40), 0);
    lv_obj_set_style_border_color(gp_joystick_zone, lv_color_make(45, 52, 78), 0);
    lv_obj_set_style_radius(gp_joystick_zone, 6, 0);
    lv_obj_clear_flag(gp_joystick_zone, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(gp_joystick_zone, event_gp_joystick, LV_EVENT_ALL, NULL);

    lv_obj_t *lbl_gp_joy = lv_label_create(gp_joystick_zone);
    lv_label_set_text(lbl_gp_joy, "THUMBSTICK (MOVE)");
    lv_obj_set_style_text_color(lbl_gp_joy, lv_color_make(100, 180, 255), 0);
    lv_obj_set_style_text_font(lbl_gp_joy, &lv_font_montserrat_12, 0);
    lv_obj_align(lbl_gp_joy, LV_ALIGN_TOP_LEFT, 6, 2);

    gp_joystick_cursor = lv_obj_create(gp_joystick_zone);
    lv_obj_set_size(gp_joystick_cursor, 24, 24);
    lv_obj_set_style_bg_color(gp_joystick_cursor, lv_color_make(0, 230, 120), 0);
    lv_obj_set_style_radius(gp_joystick_cursor, 12, 0);
    lv_obj_set_style_bg_opa(gp_joystick_cursor, LV_OPA_TRANSP, 0);

    // Right Touchpad (Look)
    gp_touchpad_zone = lv_obj_create(cont_gamepad);
    lv_obj_set_size(gp_touchpad_zone, 314, 110);
    lv_obj_set_pos(gp_touchpad_zone, 324, 2);
    lv_obj_set_style_bg_color(gp_touchpad_zone, lv_color_make(22, 26, 40), 0);
    lv_obj_set_style_border_color(gp_touchpad_zone, lv_color_make(45, 52, 78), 0);
    lv_obj_set_style_radius(gp_touchpad_zone, 6, 0);
    lv_obj_clear_flag(gp_touchpad_zone, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(gp_touchpad_zone, event_gp_touchpad, LV_EVENT_ALL, NULL);

    lv_obj_t *lbl_gp_pad = lv_label_create(gp_touchpad_zone);
    lv_label_set_text(lbl_gp_pad, "TOUCHPAD (LOOK / VIEW)");
    lv_obj_set_style_text_color(lbl_gp_pad, lv_color_make(255, 170, 70), 0);
    lv_obj_set_style_text_font(lbl_gp_pad, &lv_font_montserrat_12, 0);
    lv_obj_align(lbl_gp_pad, LV_ALIGN_TOP_LEFT, 6, 2);

    gp_touchpad_cursor = lv_obj_create(gp_touchpad_zone);
    lv_obj_set_size(gp_touchpad_cursor, 20, 20);
    lv_obj_set_style_bg_color(gp_touchpad_cursor, lv_color_make(255, 160, 30), 0);
    lv_obj_set_style_radius(gp_touchpad_cursor, 10, 0);
    lv_obj_set_style_bg_opa(gp_touchpad_cursor, LV_OPA_TRANSP, 0);

    // Gamepad Bottom Buttons (6 Big Buttons, Width: 120, 120, 95, 95, 100, 94)
    btn_gp_trigger = lv_btn_create(cont_gamepad);
    lv_obj_set_size(btn_gp_trigger, 120, 34);
    lv_obj_set_pos(btn_gp_trigger, 2, 115);
    lv_obj_set_style_bg_color(btn_gp_trigger, lv_color_make(200, 80, 20), 0);
    lv_obj_add_event_cb(btn_gp_trigger, event_btn_trigger, LV_EVENT_ALL, NULL);
    lv_obj_t *l_trig = lv_label_create(btn_gp_trigger);
    lv_label_set_text(l_trig, "TRIGGER");
    lv_obj_center(l_trig);

    btn_gp_grip = lv_btn_create(cont_gamepad);
    lv_obj_set_size(btn_gp_grip, 120, 34);
    lv_obj_set_pos(btn_gp_grip, 126, 115);
    lv_obj_set_style_bg_color(btn_gp_grip, lv_color_make(20, 110, 180), 0);
    lv_obj_add_event_cb(btn_gp_grip, event_btn_grip, LV_EVENT_ALL, NULL);
    lv_obj_t *l_grp = lv_label_create(btn_gp_grip);
    lv_label_set_text(l_grp, "GRIP");
    lv_obj_center(l_grp);

    btn_gp_a = lv_btn_create(cont_gamepad);
    lv_obj_set_size(btn_gp_a, 90, 34);
    lv_obj_set_pos(btn_gp_a, 250, 115);
    lv_obj_set_style_bg_color(btn_gp_a, lv_color_make(35, 140, 55), 0);
    lv_obj_add_event_cb(btn_gp_a, event_btn_a, LV_EVENT_ALL, NULL);
    lv_obj_t *l_a = lv_label_create(btn_gp_a);
    lv_label_set_text(l_a, "A");
    lv_obj_center(l_a);

    btn_gp_b = lv_btn_create(cont_gamepad);
    lv_obj_set_size(btn_gp_b, 90, 34);
    lv_obj_set_pos(btn_gp_b, 344, 115);
    lv_obj_set_style_bg_color(btn_gp_b, lv_color_make(160, 35, 35), 0);
    lv_obj_add_event_cb(btn_gp_b, event_btn_b, LV_EVENT_ALL, NULL);
    lv_obj_t *l_b = lv_label_create(btn_gp_b);
    lv_label_set_text(l_b, "B");
    lv_obj_center(l_b);

    btn_gp_menu = lv_btn_create(cont_gamepad);
    lv_obj_set_size(btn_gp_menu, 100, 34);
    lv_obj_set_pos(btn_gp_menu, 438, 115);
    lv_obj_set_style_bg_color(btn_gp_menu, lv_color_make(50, 65, 85), 0);
    lv_obj_add_event_cb(btn_gp_menu, event_btn_menu, LV_EVENT_ALL, NULL);
    lv_obj_t *l_menu = lv_label_create(btn_gp_menu);
    lv_label_set_text(l_menu, "MENU");
    lv_obj_center(l_menu);

    btn_gp_mode = lv_btn_create(cont_gamepad);
    lv_obj_set_size(btn_gp_mode, 96, 34);
    lv_obj_set_pos(btn_gp_mode, 542, 115);
    lv_obj_set_style_bg_color(btn_gp_mode, lv_color_make(160, 45, 100), 0);
    lv_obj_add_event_cb(btn_gp_mode, event_cycle_mode, LV_EVENT_CLICKED, NULL);
    lbl_gp_mode = lv_label_create(btn_gp_mode);
    lv_label_set_text(lbl_gp_mode, "MODE");
    lv_obj_center(lbl_gp_mode);

    // ====================================================
    // CONTAINER 2: TOUCHPAD MOUSE MODE (Full-Width Touchpad!)
    // ====================================================
    cont_mouse = lv_obj_create(scr_main);
    lv_obj_set_size(cont_mouse, LCD_WIDTH, LCD_HEIGHT - TOP_BAR_H);
    lv_obj_set_pos(cont_mouse, 0, TOP_BAR_H);
    lv_obj_set_style_bg_opa(cont_mouse, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont_mouse, 0, 0);
    lv_obj_set_style_pad_all(cont_mouse, 0, 0);
    lv_obj_clear_flag(cont_mouse, LV_OBJ_FLAG_SCROLLABLE);

    // Giant Super Touchpad (636px x 110px)
    mouse_touchpad_zone = lv_obj_create(cont_mouse);
    lv_obj_set_size(mouse_touchpad_zone, 636, 110);
    lv_obj_set_pos(mouse_touchpad_zone, 2, 2);
    lv_obj_set_style_bg_color(mouse_touchpad_zone, lv_color_make(20, 24, 38), 0);
    lv_obj_set_style_border_color(mouse_touchpad_zone, lv_color_make(60, 70, 110), 0);
    lv_obj_set_style_radius(mouse_touchpad_zone, 6, 0);
    lv_obj_clear_flag(mouse_touchpad_zone, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(mouse_touchpad_zone, event_super_touchpad, LV_EVENT_ALL, NULL);

    lv_obj_t *lbl_ms_hint = lv_label_create(mouse_touchpad_zone);
    lv_label_set_text(lbl_ms_hint, "TOUCHPAD + AIR MOUSE\nTouch & Wave: Air Mouse | Swipe: Wheel Scroll\nTap: Left Click | Long Press: Right Click");
    lv_obj_set_style_text_color(lbl_ms_hint, lv_color_make(130, 150, 190), 0);
    lv_obj_set_style_text_font(lbl_ms_hint, &lv_font_montserrat_12, 0);
    lv_obj_align(lbl_ms_hint, LV_ALIGN_CENTER, 0, 0);

    mouse_touchpad_cursor = lv_obj_create(mouse_touchpad_zone);
    lv_obj_set_size(mouse_touchpad_cursor, 24, 24);
    lv_obj_set_style_bg_color(mouse_touchpad_cursor, lv_color_make(255, 160, 30), 0);
    lv_obj_set_style_radius(mouse_touchpad_cursor, 12, 0);
    lv_obj_set_style_bg_opa(mouse_touchpad_cursor, LV_OPA_TRANSP, 0);

    // Mouse Bottom Buttons (Massive Left/Right Click Buttons)
    btn_ms_left = lv_btn_create(cont_mouse);
    lv_obj_set_size(btn_ms_left, 190, 34);
    lv_obj_set_pos(btn_ms_left, 2, 115);
    lv_obj_set_style_bg_color(btn_ms_left, lv_color_make(35, 140, 55), 0);
    lv_obj_add_event_cb(btn_ms_left, event_btn_ms_left, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l_ms_left = lv_label_create(btn_ms_left);
    lv_label_set_text(l_ms_left, "LEFT CLICK");
    lv_obj_center(l_ms_left);

    btn_ms_right = lv_btn_create(cont_mouse);
    lv_obj_set_size(btn_ms_right, 190, 34);
    lv_obj_set_pos(btn_ms_right, 196, 115);
    lv_obj_set_style_bg_color(btn_ms_right, lv_color_make(20, 110, 180), 0);
    lv_obj_add_event_cb(btn_ms_right, event_btn_ms_right, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l_ms_right = lv_label_create(btn_ms_right);
    lv_label_set_text(l_ms_right, "RIGHT CLICK");
    lv_obj_center(l_ms_right);

    btn_ms_back = lv_btn_create(cont_mouse);
    lv_obj_set_size(btn_ms_back, 120, 34);
    lv_obj_set_pos(btn_ms_back, 390, 115);
    lv_obj_set_style_bg_color(btn_ms_back, lv_color_make(160, 35, 35), 0);
    lv_obj_add_event_cb(btn_ms_back, event_btn_ms_back, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l_ms_back = lv_label_create(btn_ms_back);
    lv_label_set_text(l_ms_back, "BACK / ESC");
    lv_obj_center(l_ms_back);

    btn_ms_mode = lv_btn_create(cont_mouse);
    lv_obj_set_size(btn_ms_mode, 120, 34);
    lv_obj_set_pos(btn_ms_mode, 514, 115);
    lv_obj_set_style_bg_color(btn_ms_mode, lv_color_make(160, 45, 100), 0);
    lv_obj_add_event_cb(btn_ms_mode, event_cycle_mode, LV_EVENT_CLICKED, NULL);
    lbl_ms_mode = lv_label_create(btn_ms_mode);
    lv_label_set_text(lbl_ms_mode, "MODE");
    lv_obj_center(lbl_ms_mode);

    // ====================================================
    // CONTAINER 3: MEDIA REMOTE MODE (Big Movie Controller)
    // ====================================================
    cont_media = lv_obj_create(scr_main);
    lv_obj_set_size(cont_media, LCD_WIDTH, LCD_HEIGHT - TOP_BAR_H);
    lv_obj_set_pos(cont_media, 0, TOP_BAR_H);
    lv_obj_set_style_bg_opa(cont_media, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont_media, 0, 0);
    lv_obj_set_style_pad_all(cont_media, 0, 0);
    lv_obj_clear_flag(cont_media, LV_OBJ_FLAG_SCROLLABLE);

    // Upper Media Controls (4 Huge Buttons: Prev, Play/Pause, Next, Mute)
    btn_md_prev = lv_btn_create(cont_media);
    lv_obj_set_size(btn_md_prev, 154, 108);
    lv_obj_set_pos(btn_md_prev, 2, 2);
    lv_obj_set_style_bg_color(btn_md_prev, lv_color_make(35, 42, 65), 0);
    lv_obj_add_event_cb(btn_md_prev, event_btn_md_prev, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l_prev = lv_label_create(btn_md_prev);
    lv_label_set_text(l_prev, "|<< PREV");
    lv_obj_set_style_text_font(l_prev, &lv_font_montserrat_16, 0);
    lv_obj_center(l_prev);

    btn_md_play = lv_btn_create(cont_media);
    lv_obj_set_size(btn_md_play, 160, 108);
    lv_obj_set_pos(btn_md_play, 160, 2);
    lv_obj_set_style_bg_color(btn_md_play, lv_color_make(25, 140, 70), 0);
    lv_obj_add_event_cb(btn_md_play, event_btn_md_play, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l_play = lv_label_create(btn_md_play);
    lv_label_set_text(l_play, "> / ||\nPLAY");
    lv_obj_set_style_text_font(l_play, &lv_font_montserrat_16, 0);
    lv_obj_center(l_play);

    btn_md_next = lv_btn_create(cont_media);
    lv_obj_set_size(btn_md_next, 154, 108);
    lv_obj_set_pos(btn_md_next, 324, 2);
    lv_obj_set_style_bg_color(btn_md_next, lv_color_make(35, 42, 65), 0);
    lv_obj_add_event_cb(btn_md_next, event_btn_md_next, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l_next = lv_label_create(btn_md_next);
    lv_label_set_text(l_next, "NEXT >>|");
    lv_obj_set_style_text_font(l_next, &lv_font_montserrat_16, 0);
    lv_obj_center(l_next);

    btn_md_mute = lv_btn_create(cont_media);
    lv_obj_set_size(btn_md_mute, 154, 108);
    lv_obj_set_pos(btn_md_mute, 482, 2);
    lv_obj_set_style_bg_color(btn_md_mute, lv_color_make(50, 35, 65), 0);
    lv_obj_add_event_cb(btn_md_mute, event_btn_md_mute, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l_mute = lv_label_create(btn_md_mute);
    lv_label_set_text(l_mute, "MUTE");
    lv_obj_set_style_text_font(l_mute, &lv_font_montserrat_16, 0);
    lv_obj_center(l_mute);

    // Bottom Media Controls (Huge Vol - / Vol + Buttons)
    btn_md_voldown = lv_btn_create(cont_media);
    lv_obj_set_size(btn_md_voldown, 250, 34);
    lv_obj_set_pos(btn_md_voldown, 2, 115);
    lv_obj_set_style_bg_color(btn_md_voldown, lv_color_make(20, 80, 140), 0);
    lv_obj_add_event_cb(btn_md_voldown, event_btn_md_voldown, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l_voldown = lv_label_create(btn_md_voldown);
    lv_label_set_text(l_voldown, "VOLUME  -");
    lv_obj_center(l_voldown);

    btn_md_volup = lv_btn_create(cont_media);
    lv_obj_set_size(btn_md_volup, 250, 34);
    lv_obj_set_pos(btn_md_volup, 256, 115);
    lv_obj_set_style_bg_color(btn_md_volup, lv_color_make(18, 120, 150), 0);
    lv_obj_add_event_cb(btn_md_volup, event_btn_md_volup, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l_volup = lv_label_create(btn_md_volup);
    lv_label_set_text(l_volup, "VOLUME  +");
    lv_obj_center(l_volup);

    btn_md_mode = lv_btn_create(cont_media);
    lv_obj_set_size(btn_md_mode, 126, 34);
    lv_obj_set_pos(btn_md_mode, 510, 115);
    lv_obj_set_style_bg_color(btn_md_mode, lv_color_make(160, 45, 100), 0);
    lv_obj_add_event_cb(btn_md_mode, event_cycle_mode, LV_EVENT_CLICKED, NULL);
    lbl_md_mode = lv_label_create(btn_md_mode);
    lv_label_set_text(lbl_md_mode, "MODE");
    lv_obj_center(lbl_md_mode);

    g_ui.last_activity_time = esp_timer_get_time();
    ui_set_mode(MODE_VR_GAMEPAD);
    ESP_LOGI(TAG, "Dynamic multi-layout UI ready");
    return ESP_OK;
}

void ui_update(void)
{
    lv_timer_handler();
}

void ui_set_mode(ui_mode_t mode)
{
    g_ui.mode = mode;
    g_ui.air_mouse_active = (mode == MODE_TOUCHPAD_MOUSE);

    // Reset touch & button transient states
    g_ui.joy_x = 0;
    g_ui.joy_y = 0;
    g_ui.touchpad_x = 0;
    g_ui.touchpad_y = 0;
    g_ui.mouse_x = 0;
    g_ui.mouse_y = 0;
    g_ui.mouse_wheel = 0;
    g_ui.mouse_buttons = 0;
    g_ui.button_trigger = false;
    g_ui.button_grip = false;
    g_ui.button_a = false;
    g_ui.button_b = false;
    g_ui.button_menu = false;

    // Hide all containers first
    if (cont_gamepad) lv_obj_add_flag(cont_gamepad, LV_OBJ_FLAG_HIDDEN);
    if (cont_mouse)   lv_obj_add_flag(cont_mouse, LV_OBJ_FLAG_HIDDEN);
    if (cont_media)   lv_obj_add_flag(cont_media, LV_OBJ_FLAG_HIDDEN);

    const char *mode_name = "VR Gamepad";
    lv_color_t color = lv_color_make(80, 230, 150);

    if (mode == MODE_TOUCHPAD_MOUSE) {
        mode_name = "Touch + AirMouse";
        color = lv_color_make(255, 170, 70);
        if (cont_mouse) lv_obj_clear_flag(cont_mouse, LV_OBJ_FLAG_HIDDEN);
    } else if (mode == MODE_MEDIA_REMOTE) {
        mode_name = "Media Remote";
        color = lv_color_make(100, 200, 255);
        if (cont_media) lv_obj_clear_flag(cont_media, LV_OBJ_FLAG_HIDDEN);
    } else {
        mode_name = "VR Gamepad";
        color = lv_color_make(80, 230, 150);
        if (cont_gamepad) lv_obj_clear_flag(cont_gamepad, LV_OBJ_FLAG_HIDDEN);
    }

    if (lbl_mode_top) {
        lv_label_set_text_fmt(lbl_mode_top, "Mode: %s [Tap]", mode_name);
        lv_obj_set_style_text_color(lbl_mode_top, color, 0);
    }
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
    lv_label_set_text_fmt(lbl_bat, "QUEST 2 CONTROLLER | Bat %d%%", percent);
}

void ui_update_imu(mahony_state_t *mahony, qmi8658_dev_t *imu)
{
    qmi8658_data_t data;
    if (qmi8658_read_data(imu, &data) != ESP_OK) return;

    const float dps_to_rad = 0.0174532925f;
    mahony_update(mahony, data.accel.x, data.accel.y, data.accel.z,
                  data.gyro.x * dps_to_rad,
                  data.gyro.y * dps_to_rad,
                  data.gyro.z * dps_to_rad);

    mahony_get_angles(mahony, &g_ui.pitch, &g_ui.yaw, &g_ui.roll);

    // Touch-to-Aim Air Mouse:
    // Only active when finger is on touchpad AND not performing a dedicated wheel scroll
    if (g_ui.mode == MODE_TOUCHPAD_MOUSE && pad_touching && !pad_is_scrolling) {
        float vx = -data.gyro.z; // Horizontal yaw rotation
        float vy = data.gyro.x;  // Vertical pitch tilt (wrist down -> cursor down, wrist up -> cursor up)

        float deadzone = 2.2f;   // Filter slight tremor
        float sens = 0.6f;       // Smooth laser-pointer sensitivity

        if (fabsf(vx) > deadzone) {
            g_ui.mouse_x = (int16_t)(vx * sens);
        } else {
            g_ui.mouse_x = 0;
        }

        if (fabsf(vy) > deadzone) {
            g_ui.mouse_y = (int16_t)(vy * sens);
        } else {
            g_ui.mouse_y = 0;
        }
    } else {
        g_ui.mouse_x = 0;
        g_ui.mouse_y = 0;
    }
}

void ui_process_touch(axs15231b_dev_t *touch_dev)
{
}

void ui_send_reports(ble_combo_state_t *ble_state)
{
    if (!ble_combo_is_connected()) return;

    // 1. Send Gamepad Report (in VR Gamepad Mode)
    if (g_ui.mode == MODE_VR_GAMEPAD) {
        ble_combo_gamepad_t pad = {
            .left_x = g_ui.joy_x,
            .left_y = g_ui.joy_y,
            .right_x = g_ui.touchpad_x,
            .right_y = g_ui.touchpad_y,
            .hat = 0x08,
            .buttons = 0,
        };
        if (g_ui.button_trigger) pad.buttons |= (1 << 0);
        if (g_ui.button_grip)    pad.buttons |= (1 << 1);
        if (g_ui.button_a)       pad.buttons |= (1 << 2);
        if (g_ui.button_b)       pad.buttons |= (1 << 3);
        if (g_ui.button_menu)    pad.buttons |= (1 << 4);

        ble_combo_send_gamepad(&pad);
    }

    // 2. Send Mouse Report (Air Mouse move / Wheel scroll / Click / Right click)
    if (g_ui.mouse_x != 0 || g_ui.mouse_y != 0 || g_ui.mouse_wheel != 0 || g_ui.mouse_buttons != 0) {
        ble_combo_mouse_t mouse = {
            .x = g_ui.mouse_x,
            .y = g_ui.mouse_y,
            .wheel = g_ui.mouse_wheel,
            .buttons = g_ui.mouse_buttons,
        };
        ble_combo_send_mouse(&mouse);

        if (g_ui.mouse_buttons != 0) {
            vTaskDelay(pdMS_TO_TICKS(15));
            mouse.buttons = 0;
            mouse.wheel = 0;
            mouse.x = 0;
            mouse.y = 0;
            ble_combo_send_mouse(&mouse);
            g_ui.mouse_buttons = 0;
        }
        g_ui.mouse_wheel = 0;
        g_ui.mouse_x = 0;
        g_ui.mouse_y = 0;
    }

    // Back key in Mouse Mode mapped to B button
    if (g_ui.mode == MODE_TOUCHPAD_MOUSE && g_ui.button_b) {
        ble_combo_gamepad_t pad = {0};
        pad.hat = 0x08;
        pad.buttons = (1 << 3); // B button / Back
        ble_combo_send_gamepad(&pad);
        vTaskDelay(pdMS_TO_TICKS(15));
        pad.buttons = 0;
        ble_combo_send_gamepad(&pad);
        g_ui.button_b = false;
    }

    // 3. Send Media Keys (in Media Remote Mode)
    if (s_pending_media_key != 0) {
        ble_combo_media_t media = { .keys = s_pending_media_key };
        ble_combo_send_media(&media);
        vTaskDelay(pdMS_TO_TICKS(20));
        media.keys = 0;
        ble_combo_send_media(&media);
        s_pending_media_key = 0;
    }
}

void ui_detect_gesture(qmi8658_dev_t *imu)
{
}

void ui_check_sleep(qmi8658_dev_t *imu)
{
    if (g_ui.sleeping) return;

    int64_t now = esp_timer_get_time();
    if ((now - g_ui.last_activity_time) > 60000000) {
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
