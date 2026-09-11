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

static uint16_t gamepad_char_handle;
static uint16_t mouse_char_handle;
static uint16_t media_char_handle;
static void advertise(void);

// HID Report Descriptors (reserved for future GATT report map integration)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-const-variable"
static const uint8_t hid_report_desc_gamepad[] = {
    0x05, 0x01, 0x09, 0x05, 0xA1, 0x01, 0x85, 0x01,
    0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x09, 0x32,
    0x09, 0x35, 0x16, 0x01, 0x80, 0x26, 0xFF, 0x7F,
    0x75, 0x10, 0x95, 0x04, 0x81, 0x02, 0x05, 0x01,
    0x09, 0x39, 0x15, 0x00, 0x25, 0x07, 0x75, 0x01,
    0x95, 0x08, 0x81, 0x42, 0x05, 0x09, 0x19, 0x01,
    0x29, 0x10, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01,
    0x95, 0x10, 0x81, 0x02, 0xC0
};

static const uint8_t hid_report_desc_mouse[] = {
    0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x85, 0x02,
    0x09, 0x01, 0xA1, 0x00, 0x05, 0x09, 0x19, 0x01,
    0x29, 0x03, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01,
    0x95, 0x03, 0x81, 0x02, 0x75, 0x05, 0x95, 0x01,
    0x81, 0x03, 0x05, 0x01, 0x09, 0x30, 0x09, 0x31,
    0x16, 0x01, 0x80, 0x26, 0xFF, 0x7F, 0x75, 0x10,
    0x95, 0x02, 0x81, 0x06, 0x09, 0x38, 0x15, 0x81,
    0x25, 0x7F, 0x75, 0x08, 0x95, 0x01, 0x81, 0x06,
    0xC0, 0xC0
};

static const uint8_t hid_report_desc_media[] = {
    0x05, 0x0C, 0x09, 0x01, 0xA1, 0x01, 0x85, 0x03,
    0x19, 0x00, 0x2A, 0xFF, 0x03, 0x15, 0x00, 0x26,
    0xFF, 0x03, 0x75, 0x10, 0x95, 0x01, 0x81, 0x00,
    0xC0
};
#pragma GCC diagnostic pop

static int gap_event_cb(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            state.connected = true;
            state.advertising = false;
            ESP_LOGI(TAG, "Connected successfully");
            if (on_connected_cb) on_connected_cb(cb_ctx);
        } else {
            ESP_LOGE(TAG, "Connection failed, restarting advertising");
            advertise();
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        state.connected = false;
        state.advertising = false;
        ESP_LOGI(TAG, "Disconnected, restarting advertising");
        if (on_disconnected_cb) on_disconnected_cb(cb_ctx);
        advertise();
        break;
    case BLE_GAP_EVENT_SUBSCRIBE:
        ESP_LOGI(TAG, "Subscribe: conn=%d attr=%d",
                 event->subscribe.conn_handle, event->subscribe.attr_handle);
        break;
    }
    return 0;
}

static void advertise(void)
{
    struct ble_hs_adv_fields fields = {0};
    const char *device_name = "ESP32-S3 VR Controller";

    ble_svc_gap_device_name_set(device_name);

    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = (uint8_t *)device_name;
    fields.name_len = strlen(device_name);
    fields.name_is_complete = 1;

    // Appearance: 0x03C4 (Gamepad)
    fields.appearance = 0x03C4;
    fields.appearance_is_present = 1;

    // HID Service UUID 0x1812
    static const ble_uuid16_t hid_uuid = BLE_UUID16_INIT(0x1812);
    fields.uuids16 = (ble_uuid16_t *)&hid_uuid;
    fields.num_uuids16 = 1;
    fields.uuids16_is_complete = 1;

    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to set adv fields: %d", rc);
    }

    struct ble_gap_adv_params adv_params = {0};
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    adv_params.itvl_min = 0x20;
    adv_params.itvl_max = 0x40;
    adv_params.channel_map = 0x07;  // BLE_GAP_CHNL_MAP_ALL

    rc = ble_gap_adv_start(0, NULL, BLE_HS_FOREVER, &adv_params, gap_event_cb, NULL);
    if (rc == 0) {
        state.advertising = true;
        ESP_LOGI(TAG, "Advertising started as '%s'", device_name);
    } else {
        ESP_LOGE(TAG, "Failed to start adv: %d", rc);
    }
}

static int gatt_svr_write_cb(uint16_t conn_handle, uint16_t attr_handle,
                               struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    return 0;
}

static const struct ble_gatt_svc_def gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0x1812),
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = BLE_UUID16_DECLARE(0x2A4A),
                .access_cb = gatt_svr_write_cb,
                .flags = BLE_GATT_CHR_F_READ,
            },
            {
                .uuid = BLE_UUID16_DECLARE(0x2A4B),
                .access_cb = gatt_svr_write_cb,
                .flags = BLE_GATT_CHR_F_READ,
            },
            {
                .uuid = BLE_UUID16_DECLARE(0x2A4C),
                .access_cb = gatt_svr_write_cb,
                .flags = BLE_GATT_CHR_F_WRITE_NO_RSP,
            },
            {
                .uuid = BLE_UUID16_DECLARE(0x2A4D),
                .access_cb = gatt_svr_write_cb,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &gamepad_char_handle,
            },
            {
                .uuid = BLE_UUID16_DECLARE(0x2A4D),
                .access_cb = gatt_svr_write_cb,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &mouse_char_handle,
            },
            {
                .uuid = BLE_UUID16_DECLARE(0x2A4D),
                .access_cb = gatt_svr_write_cb,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &media_char_handle,
            },
            { 0 }
        },
    },
    { 0 }
};

static void gatt_svr_init(void)
{
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
    buf[0] = 0x01;
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
    buf[0] = 0x02;
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
    buf[0] = 0x03;
    memcpy(&buf[1], &media->keys, 2);

    struct os_mbuf *om = ble_hs_mbuf_from_flat(buf, sizeof(buf));
    if (!om) return ESP_ERR_NO_MEM;
    ble_gattc_notify_custom(0, media_char_handle, om);
    return ESP_OK;
}

esp_err_t ble_combo_send_mouse_button(uint8_t buttons)
{
    ble_combo_mouse_t mouse = { .x = 0, .y = 0, .wheel = 0, .buttons = buttons };
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
