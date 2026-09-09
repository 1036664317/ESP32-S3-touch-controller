#include "ble_combo.h"
#include "esp_log.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include <string.h>

static const char *TAG = "BLE_COMBO";

static ble_combo_connected_cb_t on_connected_cb = NULL;
static ble_combo_disconnected_cb_t on_disconnected_cb = NULL;
static void *cb_ctx = NULL;

static ble_combo_state_t state = {
    .connected = false,
    .advertising = false,
    .device_type = BLE_COMBO_DEVICE_ALL,
};

static int gatt_svc_hrm_handle;
static uint16_t gamepad_char_handle;
static uint16_t mouse_char_handle;
static uint16_t media_char_handle;

// HID Report Descriptors
static const uint8_t hid_report_desc_gamepad[] = {
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x05,        // Usage (Gamepad)
    0xA1, 0x01,        // Collection (Application)
    0x85, 0x01,        //   Report ID (1)
    0x05, 0x01,        //   Usage Page (Generic Desktop)
    0x09, 0x30,        //   Usage (X)
    0x09, 0x31,        //   Usage (Y)
    0x09, 0x32,        //   Usage (Z)
    0x09, 0x35,        //   Usage (Rz)
    0x16, 0x01, 0x80,  //   Logical Minimum (-32767)
    0x26, 0xFF, 0x7F,  //   Logical Maximum (32767)
    0x75, 0x10,        //   Report Size (16)
    0x95, 0x04,        //   Report Count (4)
    0x81, 0x02,        //   Input (Data,Var,Abs)
    0x05, 0x01,        //   Usage Page (Generic Desktop)
    0x09, 0x39,        //   Usage (Hat switch)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x07,        //   Logical Maximum (7)
    0x75, 0x01,        //   Report Size (1)
    0x95, 0x08,        //   Report Count (8)
    0x81, 0x42,        //   Input (Data,Var,Abs,Null)
    0x05, 0x09,        //   Usage Page (Button)
    0x19, 0x01,        //   Usage Minimum (1)
    0x29, 0x10,        //   Usage Maximum (16)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x01,        //   Logical Maximum (1)
    0x75, 0x01,        //   Report Size (1)
    0x95, 0x10,        //   Report Count (16)
    0x81, 0x02,        //   Input (Data,Var,Abs)
    0xC0               // End Collection
};

static const uint8_t hid_report_desc_mouse[] = {
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x02,        // Usage (Mouse)
    0xA1, 0x01,        // Collection (Application)
    0x85, 0x02,        //   Report ID (2)
    0x09, 0x01,        //   Usage (Pointer)
    0xA1, 0x00,        //   Collection (Physical)
    0x05, 0x09,        //     Usage Page (Button)
    0x19, 0x01,        //     Usage Minimum (1)
    0x29, 0x03,        //     Usage Maximum (3)
    0x15, 0x00,        //     Logical Minimum (0)
    0x25, 0x01,        //     Logical Maximum (1)
    0x75, 0x01,        //     Report Size (1)
    0x95, 0x03,        //     Report Count (3)
    0x81, 0x02,        //     Input (Data,Var,Abs)
    0x75, 0x05,        //     Report Size (5)
    0x95, 0x01,        //     Report Count (1)
    0x81, 0x03,        //     Input (Const,Array,Abs)
    0x05, 0x01,        //     Usage Page (Generic Desktop)
    0x09, 0x30,        //     Usage (X)
    0x09, 0x31,        //     Usage (Y)
    0x16, 0x01, 0x80,  //     Logical Minimum (-32767)
    0x26, 0xFF, 0x7F,  //     Logical Maximum (32767)
    0x75, 0x10,        //     Report Size (16)
    0x95, 0x02,        //     Report Count (2)
    0x81, 0x06,        //     Input (Data,Var,Rel)
    0x09, 0x38,        //     Usage (Wheel)
    0x15, 0x81,        //     Logical Minimum (-127)
    0x25, 0x7F,        //     Logical Maximum (127)
    0x75, 0x08,        //     Report Size (8)
    0x95, 0x01,        //     Report Count (1)
    0x81, 0x06,        //     Input (Data,Var,Rel)
    0xC0,              //   End Collection
    0xC0               // End Collection
};

static const uint8_t hid_report_desc_media[] = {
    0x05, 0x0C,        // Usage Page (Consumer)
    0x09, 0x01,        // Usage (Consumer Control)
    0xA1, 0x01,        // Collection (Application)
    0x85, 0x03,        //   Report ID (3)
    0x19, 0x00,        //   Usage Minimum (0)
    0x2A, 0xFF, 0x03,  //   Usage Maximum (0x03FF)
    0x15, 0x00,        //   Logical Minimum (0)
    0x26, 0xFF, 0x03,  //   Logical Maximum (0x03FF)
    0x75, 0x10,        //   Report Size (16)
    0x95, 0x01,        //   Report Count (1)
    0x81, 0x00,        //   Input (Data,Array,Abs)
    0xC0               // End Collection
};

static int gap_event_cb(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            state.connected = true;
            ESP_LOGI(TAG, "Connected");
            if (on_connected_cb) on_connected_cb(cb_ctx);
        } else {
            ESP_LOGE(TAG, "Connection failed, restarting advertising");
            ble_gap_adv_start(0, 0, BLE_GAP_FOREVER,
                             BLE_GAP_GEN_DISC, NULL, gap_event_cb, NULL);
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        state.connected = false;
        state.advertising = false;
        ESP_LOGI(TAG, "Disconnected, restarting advertising");
        if (on_disconnected_cb) on_disconnected_cb(cb_ctx);
        ble_gap_adv_start(0, 0, BLE_GAP_FOREVER,
                         BLE_GAP_GEN_DISC, NULL, gap_event_cb, NULL);
        break;
    case BLE_GAP_EVENT_SUBSCRIBE:
        ESP_LOGI(TAG, "Subscribe event: conn_handle=%d attr_handle=%d",
                 event->subscribe.conn_handle, event->subscribe.attr_handle);
        break;
    }
    return 0;
}

static void advertise(void)
{
    struct ble_gap_adv_params adv_params = {
        .conn_mode = BLE_GAP_CONN_MODE_UND,
        .disc_mode = BLE_GAP_DISC_GEN,
        .itvl_min = 0x20,
        .itvl_max = 0x40,
        .channel_map = BLE_GAP_CHNL_MAP_ALL,
        .auth_flags = BLE_HS_ADV_F_NO_SSP,
    };
    ble_gap_adv_start(0, 0, BLE_GAP_FOREVER,
                     BLE_GAP_GEN_DISC, NULL, gap_event_cb, NULL);
    state.advertising = true;
    ESP_LOGI(TAG, "Advertising started");
}

static int gatt_svr_write_cb(uint16_t conn_handle, uint16_t attr_handle,
                               struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    // Handle HID report writes if needed
    return 0;
}

static const struct ble_gatt_svc_def gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0x1812),  // HID Service
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = BLE_UUID16_DECLARE(0x2A4A),  // HID Information
                .access_cb = gatt_svr_write_cb,
                .flags = BLE_GATT_CHR_F_READ,
            },
            {
                .uuid = BLE_UUID16_DECLARE(0x2A4B),  // Report Map
                .access_cb = gatt_svr_write_cb,
                .flags = BLE_GATT_CHR_F_READ,
            },
            {
                .uuid = BLE_UUID16_DECLARE(0x2A4C),  // HID Control Point
                .access_cb = gatt_svr_write_cb,
                .flags = BLE_GATT_CHR_F_WRITE_NO_RSP,
            },
            {
                .uuid = BLE_UUID16_DECLARE(0x2A4D),  // Report
                .access_cb = gatt_svr_write_cb,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &gamepad_char_handle,
            },
            {
                .uuid = BLE_UUID16_DECLARE(0x2A4D),  // Report (Mouse)
                .access_cb = gatt_svr_write_cb,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &mouse_char_handle,
            },
            {
                .uuid = BLE_UUID16_DECLARE(0x2A4D),  // Report (Media)
                .access_cb = gatt_svr_write_cb,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &media_char_handle,
            },
            {
                0,
            }
        },
    },
    {
        0,
    }
};

static void gatt_svr_init(void)
{
    ble_hs_cfg.gatts_svr_init_cb = NULL;
    ble_hs_cfg att_mtu = 247;

    ble_gatts_count_cfg(gatt_svcs);
    ble_gatts_add_svcs(gatt_svcs);
}

static void ble_host_task(void *param)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

esp_err_t ble_combo_init(ble_combo_device_type_t type)
{
    state.device_type = type;
    state.connected = false;

    nimble_port_init();
    ble_hs_cfg.reset_cb = NULL;
    ble_hs_cfg.sync_cb = NULL;

    gatt_svr_init();

    nimble_port_freertos_init(ble_host_task);

    ESP_LOGI(TAG, "BLE Combo initialized (type=0x%02X)", type);
    return ESP_OK;
}

esp_err_t ble_combo_start(void)
{
    advertise();
    return ESP_OK;
}

esp_err_t ble_combo_stop(void)
{
    ble_gap_adv_stop();
    state.advertising = false;
    return ESP_OK;
}

esp_err_t ble_combo_send_gamepad(ble_combo_gamepad_t *pad)
{
    if (!state.connected || !gamepad_char_handle) return ESP_ERR_INVALID_STATE;

    uint8_t buf[13];
    buf[0] = 0x01;  // Report ID
    memcpy(&buf[1], &pad->left_x, 2);
    memcpy(&buf[3], &pad->left_y, 2);
    memcpy(&buf[5], &pad->right_x, 2);
    memcpy(&buf[7], &pad->right_y, 2);
    buf[9] = pad->hat;
    buf[10] = pad->buttons & 0xFF;
    buf[11] = (pad->buttons >> 8) & 0xFF;
    buf[12] = 0;

    struct os_mbuf *om = ble_hs_mbuf_from_flat(buf, sizeof(buf));
    if (!om) return ESP_ERR_NO_MEM;

    ble_gattc_notify_custom(0, gamepad_char_handle, om);
    return ESP_OK;
}

esp_err_t ble_combo_send_mouse(ble_combo_mouse_t *mouse)
{
    if (!state.connected || !mouse_char_handle) return ESP_ERR_INVALID_STATE;

    uint8_t buf[7];
    buf[0] = 0x02;  // Report ID
    buf[1] = mouse->buttons;
    memcpy(&buf[2], &mouse->x, 2);
    memcpy(&buf[4], &mouse->y, 2);
    buf[6] = mouse->wheel;

    struct os_mbuf *om = ble_hs_mbuf_from_flat(buf, sizeof(buf));
    if (!om) return ESP_ERR_NO_MEM;

    ble_gattc_notify_custom(0, mouse_char_handle, om);
    return ESP_OK;
}

esp_err_t ble_combo_send_media(ble_combo_media_t *media)
{
    if (!state.connected || !media_char_handle) return ESP_ERR_INVALID_STATE;

    uint8_t buf[3];
    buf[0] = 0x03;  // Report ID
    memcpy(&buf[1], &media->keys, 2);

    struct os_mbuf *om = ble_hs_mbuf_from_flat(buf, sizeof(buf));
    if (!om) return ESP_ERR_NO_MEM;

    ble_gattc_notify_custom(0, media_char_handle, om);
    return ESP_OK;
}

esp_err_t ble_combo_send_mouse_button(uint8_t buttons)
{
    ble_combo_mouse_t mouse = {
        .x = 0, .y = 0, .wheel = 0,
        .buttons = buttons,
    };
    return ble_combo_send_mouse(&mouse);
}

bool ble_combo_is_connected(void)
{
    return state.connected;
}

void ble_combo_register_callbacks(ble_combo_connected_cb_t on_connect,
                                   ble_combo_disconnected_cb_t on_disconnect,
                                   void *ctx)
{
    on_connected_cb = on_connect;
    on_disconnected_cb = on_disconnect;
    cb_ctx = ctx;
}
