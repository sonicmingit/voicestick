#include "voice_hid.h"
#include "voice_key_lease.h"
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "host/ble_hs.h"
#include "host/ble_gatt.h"
#include "host/ble_uuid.h"
#include "nimble/nimble_port.h"

static const char *TAG = "voice_hid";
static uint16_t s_conn = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_report_handle;
static bool s_subscribed, s_suspended;
static uint8_t s_report[8];
static uint8_t s_modifiers = 0x40;
static voice_key_lease_t s_lease;
static struct ble_npl_callout s_watchdog;
static voice_hid_ack_t s_ack;

// Standard HID keyboard: modifier byte, reserved byte, six key usages. Report
// ID is supplied by the GATT Report Reference, not prefixed to notifications.
static const uint8_t s_report_map[] = {
    0x05,0x01, 0x09,0x06, 0xA1,0x01, 0x85,0x01,
    0x05,0x07, 0x19,0xE0, 0x29,0xE7, 0x15,0x00, 0x25,0x01,
    0x75,0x01, 0x95,0x08, 0x81,0x02,
    0x95,0x01, 0x75,0x08, 0x81,0x03,
    0x95,0x06, 0x75,0x08, 0x15,0x00, 0x25,0x65,
    0x05,0x07, 0x19,0x00, 0x29,0x65, 0x81,0x00, 0xC0,
};

static uint64_t now_ms(void) { return (uint64_t)esp_timer_get_time() / 1000; }
static int send_report(bool down) {
    memset(s_report, 0, sizeof(s_report));
    s_report[0] = down ? s_modifiers : 0;
    if (s_conn == BLE_HS_CONN_HANDLE_NONE || !s_subscribed) return BLE_HS_ENOTCONN;
    struct os_mbuf *buffer = ble_hs_mbuf_from_flat(s_report, sizeof(s_report));
    return buffer ? ble_gatts_notify_custom(s_conn, s_report_handle, buffer) : BLE_HS_ENOMEM;
}
static void release_key(bool notify) {
    if (!s_lease.held) return;
    const uint32_t session = s_lease.session;
    const int rc = send_report(false);
    if (rc && s_conn != BLE_HS_CONN_HANDLE_NONE && s_subscribed) {
        s_lease.deadline_ms = now_ms() + 100;
        return; // Transient notification allocation failure: retry in the host queue.
    }
    s_lease.held = false;
    s_lease.deadline_ms = 0;
    if (notify && s_ack) s_ack(session, false, false);
}
static void watchdog(struct ble_npl_event *event) {
    (void)event;
    if (voice_key_lease_expired(&s_lease, now_ms())) {
        ESP_LOGW(TAG, "voice key lease expired; releasing");
        release_key(true);
    }
    ble_npl_callout_reset(&s_watchdog, pdMS_TO_TICKS(100));
}
static int access(uint16_t conn, uint16_t handle, struct ble_gatt_access_ctxt *ctxt, void *arg) {
    (void)conn; (void)handle;
    const uintptr_t kind = (uintptr_t)arg;
    static const uint8_t info[] = {0x11,0x01,0x00,0x02};
    static const uint8_t reference[] = {1,1};
    if (kind == 4 && ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        uint8_t value = 0;
        if (OS_MBUF_PKTLEN(ctxt->om) != 1 || os_mbuf_copydata(ctxt->om, 0, 1, &value) != 0 || value > 1)
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        s_suspended = value == 0;
        if (s_suspended) release_key(true);
        return 0;
    }
    const void *data = kind == 1 ? (const void *)s_report_map :
                       kind == 2 ? (const void *)info :
                       kind == 3 ? (const void *)s_report : (const void *)reference;
    size_t size = kind == 1 ? sizeof(s_report_map) : kind == 2 ? sizeof(info) : kind == 3 ? sizeof(s_report) : sizeof(reference);
    return os_mbuf_append(ctxt->om, data, size) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}
static const struct ble_gatt_svc_def s_services[] = {
    {.type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = BLE_UUID16_DECLARE(0x1812),
     .characteristics = (struct ble_gatt_chr_def[]) {
        {.uuid = BLE_UUID16_DECLARE(0x2A4B), .access_cb = access, .arg = (void *)1,
         .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC},
        {.uuid = BLE_UUID16_DECLARE(0x2A4A), .access_cb = access, .arg = (void *)2,
         .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC},
        {.uuid = BLE_UUID16_DECLARE(0x2A4D), .access_cb = access, .arg = (void *)3,
         .val_handle = &s_report_handle,
         .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC | BLE_GATT_CHR_F_NOTIFY,
         .descriptors = (struct ble_gatt_dsc_def[]) {
            {.uuid = BLE_UUID16_DECLARE(0x2908), .access_cb = access, .arg = (void *)5,
             .att_flags = BLE_ATT_F_READ | BLE_ATT_F_READ_ENC}, {0}}},
        {.uuid = BLE_UUID16_DECLARE(0x2A4C), .access_cb = access, .arg = (void *)4,
         .flags = BLE_GATT_CHR_F_WRITE_NO_RSP | BLE_GATT_CHR_F_WRITE_ENC},
        {0}}}, {0}
};

esp_err_t voice_hid_init(voice_hid_ack_t ack) {
    s_ack = ack;
    int rc = ble_gatts_count_cfg(s_services);
    if (!rc) rc = ble_gatts_add_svcs(s_services);
    if (rc) return ESP_FAIL;
    ble_npl_callout_init(&s_watchdog, nimble_port_get_dflt_eventq(), watchdog, NULL);
    ble_npl_callout_reset(&s_watchdog, pdMS_TO_TICKS(100));
    return ESP_OK;
}

void voice_hid_gap_event(const struct ble_gap_event *event) {
    if (event->type == BLE_GAP_EVENT_CONNECT && event->connect.status == 0) {
        memset(&s_lease, 0, sizeof(s_lease)); memset(s_report, 0, sizeof(s_report));
        s_conn = event->connect.conn_handle; s_subscribed = false; s_suspended = false;
    } else if (event->type == BLE_GAP_EVENT_DISCONNECT) {
        release_key(false); s_conn = BLE_HS_CONN_HANDLE_NONE; s_subscribed = false;
        memset(&s_lease, 0, sizeof(s_lease));
    } else if (event->type == BLE_GAP_EVENT_SUBSCRIBE && event->subscribe.attr_handle == s_report_handle) {
        s_subscribed = event->subscribe.cur_notify;
        if (!s_subscribed) release_key(true);
        else (void)send_report(false);
        ESP_LOGI(TAG, "keyboard subscription=%d", s_subscribed);
    } else if (event->type == BLE_GAP_EVENT_ENC_CHANGE) {
        ESP_LOGI(TAG, "keyboard encryption status=%d", event->enc_change.status);
    }
}

esp_err_t voice_hid_key(uint32_t session, bool down, bool cancel, uint8_t shortcut) {
    struct ble_gap_conn_desc connection;
    const bool ready = s_conn != BLE_HS_CONN_HANDLE_NONE && s_subscribed && !s_suspended &&
        ble_gap_conn_find(s_conn, &connection) == 0 && connection.sec_state.encrypted;
    if (down && !ready) { if (s_ack) s_ack(session, down, false); return ESP_ERR_INVALID_STATE; }
    const bool already_held = s_lease.held;
    uint8_t modifiers = voice_key_modifiers(shortcut);
    if (down && (!modifiers || (already_held && s_modifiers != modifiers))) {
        if (s_ack) s_ack(session, down, false);
        return ESP_ERR_INVALID_ARG;
    }
    voice_key_lease_t previous = s_lease;
    if (!voice_key_lease_request(&s_lease, session, down, now_ms())) {
        if (s_ack) s_ack(session, down, false);
        return ESP_ERR_INVALID_STATE;
    }
    if (down && !already_held) s_modifiers = modifiers;
    if (!down && cancel && already_held) {
        // Escape while the voice shortcut is held cancels before its release.
        memset(s_report, 0, sizeof(s_report)); s_report[0] = s_modifiers; s_report[2] = 0x29;
        struct os_mbuf *buffer = ble_hs_mbuf_from_flat(s_report, sizeof(s_report));
        if (buffer) (void)ble_gatts_notify_custom(s_conn, s_report_handle, buffer);
    }
    // Renewals extend a lease without generating keyboard autorepeat.
    int rc = down && already_held ? 0 : send_report(down);
    if (rc) s_lease = previous; // Retain held state so the watchdog retries release.
    if (s_ack) s_ack(session, down, rc == 0);
    if (!already_held || !down) ESP_LOGI(TAG, "voice key down=%d ok=%d", down, rc == 0);
    return rc == 0 ? ESP_OK : ESP_FAIL;
}
