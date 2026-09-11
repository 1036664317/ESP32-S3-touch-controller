#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BLE_COMBO_DEVICE_GAMEPAD = 0x01,
    BLE_COMBO_DEVICE_MOUSE = 0x02,
    BLE_COMBO_DEVICE_MEDIA = 0x04,
    BLE_COMBO_DEVICE_ALL = 0x07,
} ble_combo_device_type_t;

typedef struct {
    bool connected;
    bool advertising;
    ble_combo_device_type_t device_type;
} ble_combo_state_t;

// Gamepad state
typedef struct {
    int16_t left_x;      // -32767 to +32767
    int16_t left_y;
    int16_t right_x;
    int16_t right_y;
    uint8_t hat;         // D-pad (0-7, 8=neutral)
    uint16_t buttons;    // Button bitmask
} ble_combo_gamepad_t;

// Mouse state
typedef struct {
    int16_t x;           // Mouse X relative
    int16_t y;           // Mouse Y relative
    int8_t wheel;        // Wheel delta
    uint8_t buttons;     // Mouse buttons
} ble_combo_mouse_t;

// Media key state
typedef struct {
    uint16_t keys;       // Media key bitmask
} ble_combo_media_t;

typedef void (*ble_combo_connected_cb_t)(void *ctx);
typedef void (*ble_combo_disconnected_cb_t)(void *ctx);

esp_err_t ble_combo_init(ble_combo_device_type_t type);
esp_err_t ble_combo_start(void);
esp_err_t ble_combo_stop(void);
esp_err_t ble_combo_send_gamepad(ble_combo_gamepad_t *pad);
esp_err_t ble_combo_send_mouse(ble_combo_mouse_t *mouse);
esp_err_t ble_combo_send_media(ble_combo_media_t *media);
esp_err_t ble_combo_send_mouse_button(uint8_t buttons);
bool ble_combo_is_connected(void);
void ble_combo_register_callbacks(ble_combo_connected_cb_t on_connect,
                                   ble_combo_disconnected_cb_t on_disconnect,
                                   void *ctx);

#define MEDIA_KEY_NEXT          0x0001
#define MEDIA_KEY_PREV          0x0002
#define MEDIA_KEY_PLAY          0x0004
#define MEDIA_KEY_PAUSE         0x0008
#define MEDIA_KEY_MUTE          0x0010
#define MEDIA_KEY_VOL_UP        0x0020
#define MEDIA_KEY_VOL_DOWN      0x0040
#define MEDIA_KEY_PLAY_PAUSE    0x0080

#ifdef __cplusplus
}
#endif
