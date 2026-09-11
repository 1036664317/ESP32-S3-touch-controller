#include "ble_combo.h"
#include "esp_log.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include <string.h>

void ble_store_config_init(void);

static const char *TAG = "BLE_COMBO";

static ble_combo_connected_cb_t on_connected_cb = NULL;
static ble_combo_disconnected_cb_t on_disconnected_cb = NULL;
static void *cb_ctx = NULL;

static ble_combo_state_t state = {
    .connected = false,
    .advertising = false,
    .device_type = BLE_COMBO_DEVICE_ALL,
};

static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t gamepad_char_handle = 0;
static uint16_t mouse_char_handle = 0;
static uint16_t media_char_handle = 0;
static uint16_t battery_char_handle = 0;

static void advertise(void);
static int gap_event_cb(struct ble_gap_event *event, void *arg);

// ==========================================
// HID Report Map (Gamepad + Mouse + Media)
// ==========================================
static const uint8_t hid_report_map[] = {
    // Gamepad (Report ID 1)
    0x05, 0x01,        // USAGE_PAGE (Generic Desktop)
    0x09, 0x05,        // USAGE (Gamepad)
    0xA1, 0x01,        // COLLECTION (Application)
    0x85, 0x01,        //   REPORT_ID (1)
    // Left stick (X, Y) - Thumbstick Move
    0x09, 0x30,        //   USAGE (X)
    0x09, 0x31,        //   USAGE (Y)
    0x16, 0x01, 0x80,  //   LOGICAL_MINIMUM (-32767)
    0x26, 0xFF, 0x7F,  //   LOGICAL_MAXIMUM (32767)
    0x75, 0x10,        //   REPORT_SIZE (16)
    0x95, 0x02,        //   REPORT_COUNT (2)
    0x81, 0x02,        //   INPUT (Data,Var,Abs)
    // Right stick / Touchpad (Z, Rz) - Look / View
    0x09, 0x32,        //   USAGE (Z)
    0x09, 0x35,        //   USAGE (Rz)
    0x16, 0x01, 0x80,  //   LOGICAL_MINIMUM (-32767)
    0x26, 0xFF, 0x7F,  //   LOGICAL_MAXIMUM (32767)
    0x75, 0x10,        //   REPORT_SIZE (16)
    0x95, 0x02,        //   REPORT_COUNT (2)
    0x81, 0x02,        //   INPUT (Data,Var,Abs)
    // Hat switch (D-pad)
    0x05, 0x01,        //   USAGE_PAGE (Generic Desktop)
    0x09, 0x39,        //   USAGE (Hat switch)
    0x15, 0x00,        //   LOGICAL_MINIMUM (0)
    0x25, 0x07,        //   LOGICAL_MAXIMUM (7)
    0x35, 0x00,        //   PHYSICAL_MINIMUM (0)
    0x46, 0x3B, 0x01,  //   PHYSICAL_MAXIMUM (315)
    0x65, 0x14,        //   UNIT (Eng Rot:Angular Pos)
    0x75, 0x04,        //   REPORT_SIZE (4)
    0x95, 0x01,        //   REPORT_COUNT (1)
    0x81, 0x42,        //   INPUT (Data,Var,Abs,Null)
    0x65, 0x00,        //   UNIT (None)
    0x75, 0x04,        //   REPORT_SIZE (4) - padding
    0x95, 0x01,        //   REPORT_COUNT (1)
    0x81, 0x03,        //   INPUT (Cnst,Var,Abs)
    // 16 Buttons (Trigger, Grip, A, B, Menu, etc.)
    0x05, 0x09,        //   USAGE_PAGE (Button)
    0x19, 0x01,        //   USAGE_MINIMUM (Button 1)
    0x29, 0x10,        //   USAGE_MAXIMUM (Button 16)
    0x15, 0x00,        //   LOGICAL_MINIMUM (0)
    0x25, 0x01,        //   LOGICAL_MAXIMUM (1)
    0x75, 0x01,        //   REPORT_SIZE (1)
    0x95, 0x10,        //   REPORT_COUNT (16)
    0x81, 0x02,        //   INPUT (Data,Var,Abs)
    0xC0,              // END_COLLECTION

    // Mouse (Report ID 2)
    0x05, 0x01,        // USAGE_PAGE (Generic Desktop)
    0x09, 0x02,        // USAGE (Mouse)
    0xA1, 0x01,        // COLLECTION (Application)
    0x85, 0x02,        //   REPORT_ID (2)
    0x09, 0x01,        //   USAGE (Pointer)
    0xA1, 0x00,        //   COLLECTION (Physical)
    0x05, 0x09,        //     USAGE_PAGE (Button)
    0x19, 0x01,        //     USAGE_MINIMUM (Button 1)
    0x29, 0x03,        //     USAGE_MAXIMUM (Button 3)
    0x15, 0x00,        //     LOGICAL_MINIMUM (0)
    0x25, 0x01,        //     LOGICAL_MAXIMUM (1)
    0x75, 0x01,        //     REPORT_SIZE (1)
    0x95, 0x03,        //     REPORT_COUNT (3)
    0x81, 0x02,        //     INPUT (Data,Var,Abs)
    0x75, 0x05,        //     REPORT_SIZE (5)
    0x95, 0x01,        //     REPORT_COUNT (1)
    0x81, 0x03,        //     INPUT (Cnst,Var,Abs)
    0x05, 0x01,        //     USAGE_PAGE (Generic Desktop)
    0x09, 0x30,        //     USAGE (X)
    0x09, 0x31,        //     USAGE (Y)
    0x16, 0x01, 0x80,  //     LOGICAL_MINIMUM (-32767)
    0x26, 0xFF, 0x7F,  //     LOGICAL_MAXIMUM (32767)
    0x75, 0x10,        //     REPORT_SIZE (16)
    0x95, 0x02,        //     REPORT_COUNT (2)
    0x81, 0x06,        //     INPUT (Data,Var,Rel)
    0x09, 0x38,        //     USAGE (Wheel)
    0x15, 0x81,        //     LOGICAL_MINIMUM (-127)
    0x25, 0x7F,        //     LOGICAL_MAXIMUM (127)
    0x75, 0x08,        //     REPORT_SIZE (8)
    0x95, 0x01,        //     REPORT_COUNT (1)
    0x81, 0x06,        //     INPUT (Data,Var,Rel)
    0xC0,              //   END_COLLECTION
    0xC0,              // END_COLLECTION

    // Media Keys (Report ID 3)
    0x05, 0x0C,        // USAGE_PAGE (Consumer Devices)
    0x09, 0x01,        // USAGE (Consumer Control)
    0xA1, 0x01,        // COLLECTION (Application)
    0x85, 0x03,        //   REPORT_ID (3)
    0x19, 0x00,        //   USAGE_MINIMUM (0)
    0x2A, 0xFF, 0x03,  //   USAGE_MAXIMUM (0x3FF)
    0x15, 0x00,        //   LOGICAL_MINIMUM (0)
    0x26, 0xFF, 0x03,  //   LOGICAL_MAXIMUM (0x3FF)
    0x75, 0x10,        //   REPORT_SIZE (16)
    0x95, 0x01,        //   REPORT_COUNT (1)
    0x81, 0x00,        //   INPUT (Data,Ary,Abs)
    0xC0               // END_COLLECTION
};

// Report References: {Report ID, Report Type (1 = Input)}
static const uint8_t gamepad_report_ref[] = {0x01, 0x01};
static const uint8_t mouse_report_ref[]   = {0x02, 0x01};
static const uint8_t media_report_ref[]   = {0x03, 0x01};

// ==========================================
// GATT Services Callbacks
// ==========================================
static int hid_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    uint16_t uuid16 = ble_uuid_u16(ctxt->chr->uuid);

    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        switch (uuid16) {
        case 0x2A4A: { // HID Information
            static const uint8_t hid_info[] = {0x11, 0x01, 0x00, 0x02}; // bcdHID=1.11, bCountry=0, RemoteWake
            os_mbuf_append(ctxt->om, hid_info, sizeof(hid_info));
            return 0;
        }
        case 0x2A4B: { // Report Map
            os_mbuf_append(ctxt->om, hid_report_map, sizeof(hid_report_map));
            return 0;
        }
        case 0x2A4E: { // Protocol Mode
            static const uint8_t proto_mode = 0x01; // Report Mode
            os_mbuf_append(ctxt->om, &proto_mode, 1);
            return 0;
        }
        case 0x2A4D: { // Input Report Read
            static const uint8_t zero_report[12] = {0};
            os_mbuf_append(ctxt->om, zero_report, sizeof(zero_report));
            return 0;
        }
        default:
            return 0;
        }
    }
    return 0;
}

static int hid_dsc_access(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    const uint8_t *ref = (const uint8_t *)arg;
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_DSC && ref) {
        os_mbuf_append(ctxt->om, ref, 2);
    }
    return 0;
}

static int dev_info_access(uint16_t conn_handle, uint16_t attr_handle,
                           struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    uint16_t uuid16 = ble_uuid_u16(ctxt->chr->uuid);

    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        switch (uuid16) {
        case 0x2A29: { // Manufacturer Name
            const char *mfr = "Waveshare";
            os_mbuf_append(ctxt->om, mfr, strlen(mfr));
            return 0;
        }
        case 0x2A24: { // Model Number
            const char *model = "ESP32-S3 VR";
            os_mbuf_append(ctxt->om, model, strlen(model));
            return 0;
        }
        case 0x2A50: { // PnP ID (Required by Android/Windows to bind HID driver)
            // Vendor ID Source: 0x02 (USB Implementers Forum)
            // Vendor ID: 0x02E5 (Espressif)
            // Product ID: 0xABCD
            // Product Version: 0x0100
            static const uint8_t pnp_id[7] = {0x02, 0xe5, 0x02, 0xcd, 0xab, 0x00, 0x01};
            os_mbuf_append(ctxt->om, pnp_id, sizeof(pnp_id));
            return 0;
        }
        default:
            return 0;
        }
    }
    return 0;
}

static int bat_svr_access(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        uint8_t bat_lvl = 100;
        os_mbuf_append(ctxt->om, &bat_lvl, 1);
        return 0;
    }
    return 0;
}

static const struct ble_gatt_svc_def gatt_svcs[] = {
    // 1. Device Information Service (0x180A)
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0x180A),
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = BLE_UUID16_DECLARE(0x2A29), // Manufacturer
                .access_cb = dev_info_access,
                .flags = BLE_GATT_CHR_F_READ,
            },
            {
                .uuid = BLE_UUID16_DECLARE(0x2A24), // Model Number
                .access_cb = dev_info_access,
                .flags = BLE_GATT_CHR_F_READ,
            },
            {
                .uuid = BLE_UUID16_DECLARE(0x2A50), // PnP ID
                .access_cb = dev_info_access,
                .flags = BLE_GATT_CHR_F_READ,
            },
            { 0 }
        },
    },

    // 2. Battery Service (0x180F)
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0x180F),
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = BLE_UUID16_DECLARE(0x2A19), // Battery Level
                .access_cb = bat_svr_access,
                .val_handle = &battery_char_handle,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
            },
            { 0 }
        },
    },

    // 3. HID Service (0x1812)
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0x1812),
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = BLE_UUID16_DECLARE(0x2A4A), // HID Information
                .access_cb = hid_chr_access,
                .flags = BLE_GATT_CHR_F_READ,
            },
            {
                .uuid = BLE_UUID16_DECLARE(0x2A4B), // Report Map
                .access_cb = hid_chr_access,
                .flags = BLE_GATT_CHR_F_READ,
            },
            {
                .uuid = BLE_UUID16_DECLARE(0x2A4C), // Control Point
                .access_cb = hid_chr_access,
                .flags = BLE_GATT_CHR_F_WRITE_NO_RSP,
            },
            {
                .uuid = BLE_UUID16_DECLARE(0x2A4E), // Protocol Mode
                .access_cb = hid_chr_access,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE_NO_RSP,
            },
            {
                .uuid = BLE_UUID16_DECLARE(0x2A4D), // Gamepad Input Report
                .access_cb = hid_chr_access,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &gamepad_char_handle,
                .descriptors = (struct ble_gatt_dsc_def[]) {
                    {
                        .uuid = BLE_UUID16_DECLARE(0x2908), // Report Reference
                        .att_flags = BLE_ATT_F_READ,
                        .access_cb = hid_dsc_access,
                        .arg = (void *)gamepad_report_ref,
                    },
                    { 0 }
                }
            },
            {
                .uuid = BLE_UUID16_DECLARE(0x2A4D), // Mouse Input Report
                .access_cb = hid_chr_access,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &mouse_char_handle,
                .descriptors = (struct ble_gatt_dsc_def[]) {
                    {
                        .uuid = BLE_UUID16_DECLARE(0x2908), // Report Reference
                        .att_flags = BLE_ATT_F_READ,
                        .access_cb = hid_dsc_access,
                        .arg = (void *)mouse_report_ref,
                    },
                    { 0 }
                }
            },
            {
                .uuid = BLE_UUID16_DECLARE(0x2A4D), // Media Key Input Report
                .access_cb = hid_chr_access,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &media_char_handle,
                .descriptors = (struct ble_gatt_dsc_def[]) {
                    {
                        .uuid = BLE_UUID16_DECLARE(0x2908), // Report Reference
                        .att_flags = BLE_ATT_F_READ,
                        .access_cb = hid_dsc_access,
                        .arg = (void *)media_report_ref,
                    },
                    { 0 }
                }
            },
            { 0 }
        },
    },
    { 0 }
};

static void gatt_svr_init(void)
{
    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_gatts_count_cfg(gatt_svcs);
    ble_gatts_add_svcs(gatt_svcs);
}

// ==========================================
// GAP Event Handler & Advertising
// ==========================================
static int gap_event_cb(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        ESP_LOGI(TAG, "GAP connection event status=%d", event->connect.status);
        if (event->connect.status == 0) {
            state.connected = true;
            state.advertising = false;
            s_conn_handle = event->connect.conn_handle;
            ESP_LOGI(TAG, "Connected successfully (conn_handle=%d)", s_conn_handle);
            if (on_connected_cb) on_connected_cb(cb_ctx);

            // Initiate security/pairing negotiation with the central
            ble_gap_security_initiate(s_conn_handle);
        } else {
            s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
            state.connected = false;
            ESP_LOGE(TAG, "Connection failed, restarting advertising");
            advertise();
        }
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        state.connected = false;
        state.advertising = false;
        ESP_LOGI(TAG, "Disconnected, restarting advertising");
        if (on_disconnected_cb) on_disconnected_cb(cb_ctx);
        advertise();
        break;

    case BLE_GAP_EVENT_SUBSCRIBE:
        ESP_LOGI(TAG, "Subscribe: conn=%d attr=%d cur_notify=%d",
                 event->subscribe.conn_handle, event->subscribe.attr_handle,
                 event->subscribe.cur_notify);
        break;

    case BLE_GAP_EVENT_ENC_CHANGE:
        ESP_LOGI(TAG, "Encryption change: status=%d", event->enc_change.status);
        break;

    case BLE_GAP_EVENT_PASSKEY_ACTION: {
        ESP_LOGI(TAG, "Passkey action event: action=%d", event->passkey.params.action);
        struct ble_sm_io pkey = {0};
        if (event->passkey.params.action == BLE_SM_IOACT_NUMCMP) {
            pkey.action = event->passkey.params.action;
            pkey.numcmp_accept = 1;
            int rc = ble_sm_inject_io(event->passkey.conn_handle, &pkey);
            ESP_LOGI(TAG, "ble_sm_inject_io (numcmp) result: %d", rc);
        } else if (event->passkey.params.action == BLE_SM_IOACT_DISP) {
            pkey.action = event->passkey.params.action;
            pkey.passkey = 123456;
            ESP_LOGI(TAG, "Passkey display: %06ld", (long)pkey.passkey);
            int rc = ble_sm_inject_io(event->passkey.conn_handle, &pkey);
            ESP_LOGI(TAG, "ble_sm_inject_io (disp) result: %d", rc);
        } else if (event->passkey.params.action == BLE_SM_IOACT_INPUT) {
            pkey.action = event->passkey.params.action;
            pkey.passkey = 123456;
            int rc = ble_sm_inject_io(event->passkey.conn_handle, &pkey);
            ESP_LOGI(TAG, "ble_sm_inject_io (input) result: %d", rc);
        } else if (event->passkey.params.action == BLE_SM_IOACT_OOB) {
            static uint8_t oob[16] = {0};
            pkey.action = event->passkey.params.action;
            memcpy(pkey.oob, oob, 16);
            ble_sm_inject_io(event->passkey.conn_handle, &pkey);
        }
        return 0;
    }

    case BLE_GAP_EVENT_REPEAT_PAIRING: {
        struct ble_gap_conn_desc desc;
        ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc);
        ble_store_util_delete_peer(&desc.peer_id_addr);
        return BLE_GAP_REPEAT_PAIRING_RETRY;
    }

    case BLE_GAP_EVENT_ADV_COMPLETE:
        state.advertising = false;
        advertise();
        break;
    }
    return 0;
}

static void advertise(void)
{
    const char *device_name = "ESP32-S3 VR Controller";
    ble_svc_gap_device_name_set(device_name);

    // 1. Primary Advertising Data (<= 31 bytes)
    struct ble_hs_adv_fields adv_fields = {0};
    adv_fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    adv_fields.appearance = 0x03C4;  // Gamepad
    adv_fields.appearance_is_present = 1;

    static const ble_uuid16_t hid_uuid = BLE_UUID16_INIT(0x1812);
    adv_fields.uuids16 = (ble_uuid16_t *)&hid_uuid;
    adv_fields.num_uuids16 = 1;
    adv_fields.uuids16_is_complete = 1;

    int rc = ble_gap_adv_set_fields(&adv_fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to set adv fields: %d", rc);
    }

    // 2. Scan Response Data for device name
    struct ble_hs_adv_fields rsp_fields = {0};
    rsp_fields.name = (uint8_t *)device_name;
    rsp_fields.name_len = strlen(device_name);
    rsp_fields.name_is_complete = 1;

    rc = ble_gap_adv_rsp_set_fields(&rsp_fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to set scan rsp fields: %d", rc);
    }

    // 3. Start advertising
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

static void ble_hs_on_sync(void)
{
    advertise();
}

static void ble_hs_on_reset(int reason)
{
    ESP_LOGE(TAG, "BLE Host reset: %d", reason);
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

    // Security Manager (SMP) Configuration: Just Works auto-bonding
    ble_hs_cfg.sync_cb = ble_hs_on_sync;
    ble_hs_cfg.reset_cb = ble_hs_on_reset;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;

    // Initialize NimBLE NVS storage
    ble_store_config_init();

    gatt_svr_init();

    nimble_port_freertos_init(ble_host_task);

    ESP_LOGI(TAG, "BLE Combo initialized (type=0x%02X)", type);
    return ESP_OK;
}

esp_err_t ble_combo_start(void)
{
    if (state.advertising || state.connected) {
        return ESP_OK;
    }
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
    if (!state.connected || s_conn_handle == BLE_HS_CONN_HANDLE_NONE || !gamepad_char_handle) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t buf[12];
    buf[0] = 0x01; // Report ID
    memcpy(&buf[1], &pad->left_x, 2);
    memcpy(&buf[3], &pad->left_y, 2);
    memcpy(&buf[5], &pad->right_x, 2);
    memcpy(&buf[7], &pad->right_y, 2);
    buf[9] = pad->hat;
    buf[10] = pad->buttons & 0xFF;
    buf[11] = (pad->buttons >> 8) & 0xFF;

    struct os_mbuf *om = ble_hs_mbuf_from_flat(buf, sizeof(buf));
    if (!om) return ESP_ERR_NO_MEM;
    int rc = ble_gatts_notify_custom(s_conn_handle, gamepad_char_handle, om);
    return (rc == 0) ? ESP_OK : ESP_FAIL;
}

esp_err_t ble_combo_send_mouse(ble_combo_mouse_t *mouse)
{
    if (!state.connected || s_conn_handle == BLE_HS_CONN_HANDLE_NONE || !mouse_char_handle) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t buf[7];
    buf[0] = 0x02; // Report ID
    buf[1] = mouse->buttons;
    memcpy(&buf[2], &mouse->x, 2);
    memcpy(&buf[4], &mouse->y, 2);
    buf[6] = mouse->wheel;

    struct os_mbuf *om = ble_hs_mbuf_from_flat(buf, sizeof(buf));
    if (!om) return ESP_ERR_NO_MEM;
    int rc = ble_gatts_notify_custom(s_conn_handle, mouse_char_handle, om);
    return (rc == 0) ? ESP_OK : ESP_FAIL;
}

esp_err_t ble_combo_send_media(ble_combo_media_t *media)
{
    if (!state.connected || s_conn_handle == BLE_HS_CONN_HANDLE_NONE || !media_char_handle) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t buf[3];
    buf[0] = 0x03; // Report ID
    memcpy(&buf[1], &media->keys, 2);

    struct os_mbuf *om = ble_hs_mbuf_from_flat(buf, sizeof(buf));
    if (!om) return ESP_ERR_NO_MEM;
    int rc = ble_gatts_notify_custom(s_conn_handle, media_char_handle, om);
    return (rc == 0) ? ESP_OK : ESP_FAIL;
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
