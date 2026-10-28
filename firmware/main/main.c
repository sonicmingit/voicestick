#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "button_gpio.h"
#include "cJSON.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_pm.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "driver/rtc_io.h"
#include "esp_timer.h"
#include "hal/gpio_types.h"
#include "iot_button.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "audio_pipeline.h"
#include "button_gestures.h"
#include "stick_s3_board.h"
#include "ui_status.h"
#include "voice_ble.h"

static const char *TAG = "voice_stick";

#define BATTERY_REFRESH_FALLBACK_MS (10 * 1000)
#define DISPLAY_DIM_TIMEOUT_MS (30 * 1000)
#define DISPLAY_ACTIVE_BRIGHTNESS 128
#define DISPLAY_DIM_BRIGHTNESS 32
#define DISPLAY_DIM_TIMEOUT_US (DISPLAY_DIM_TIMEOUT_MS * 1000ULL)
#define BATTERY_REFRESH_FALLBACK_US (BATTERY_REFRESH_FALLBACK_MS * 1000ULL)
#define DEFAULT_SLEEP_TIMEOUT_MINUTES 5
#define MAX_SLEEP_TIMEOUT_MINUTES 1440

static uint32_t s_sleep_timeout_minutes = DEFAULT_SLEEP_TIMEOUT_MINUTES;
static int64_t s_sleep_deadline_us;

static bool s_recording;
static bool s_ota_updating;
static bool s_display_dimmed;
static bool s_recording_pm_locked;
static bool s_ota_pm_locked;
static bool s_battery_charging;
static bool s_usb_powered;
static esp_pm_lock_handle_t s_cpu_freq_lock;
static esp_timer_handle_t s_display_dim_timer;
static esp_timer_handle_t s_deep_sleep_timer;
static esp_timer_handle_t s_battery_refresh_timer;
static esp_timer_handle_t s_host_response_timer;
static uint32_t s_session_id = 1;
static QueueHandle_t s_app_event_queue;
static button_handle_t s_front_button;
static button_handle_t s_side_button;
static int64_t s_primary_down_us;
static int64_t s_secondary_down_us;
static uint32_t s_primary_session_id;
static bool s_custom_buttons;

typedef enum {
    APP_UI_STATE_READY,
    APP_UI_STATE_RECORDING,
    APP_UI_STATE_THINKING,
    APP_UI_STATE_PENDING_CONFIRMATION,
    APP_UI_STATE_ERROR,
} app_ui_state_t;

static app_ui_state_t s_app_ui_state = APP_UI_STATE_READY;

typedef enum {
    INTERACTION_MODE_HOLD_TO_TALK,
    INTERACTION_MODE_CLICK_TO_TALK,
} interaction_mode_t;

static interaction_mode_t s_interaction_mode = INTERACTION_MODE_HOLD_TO_TALK;

static const char *app_ui_state_name(app_ui_state_t state)
{
    switch (state) {
    case APP_UI_STATE_READY:
        return "ready";
    case APP_UI_STATE_RECORDING:
        return "recording";
    case APP_UI_STATE_THINKING:
        return "thinking";
    case APP_UI_STATE_PENDING_CONFIRMATION:
        return "pending_confirmation";
    case APP_UI_STATE_ERROR:
        return "error";
    }
    return "unknown";
}

typedef enum {
    APP_EVENT_BUTTON_PROFILE,
    APP_EVENT_RECORD_CANCEL,
    APP_EVENT_FRONT_DOWN,
    APP_EVENT_FRONT_UP,
    APP_EVENT_SIDE_DOWN,
    APP_EVENT_SIDE_UP,
    APP_EVENT_UI_STATE,
    APP_EVENT_BLE_CONNECTED,
    APP_EVENT_BLE_DISCONNECTED,
    APP_EVENT_POWER_IRQ,
    APP_EVENT_BATTERY_REFRESH,
    APP_EVENT_ENTER_DEEP_SLEEP,
    APP_EVENT_SLEEP_TIMEOUT,
    APP_EVENT_OTA_BEGIN,
    APP_EVENT_OTA_PROGRESS,
    APP_EVENT_OTA_DONE,
    APP_EVENT_OTA_END,
    APP_EVENT_HOST_RESPONSE_TIMEOUT,
} app_event_type_t;

typedef struct {
    app_event_type_t type;
    uint32_t written;
    uint32_t size;
    uint64_t timestamp_ms;
    char state[32];
    char text[192];
} app_event_t;

static void update_battery_status(void);
static void queue_app_event(app_event_type_t type);
static void queue_app_event_with_ota(app_event_type_t type, uint32_t written, uint32_t size);
static void queue_ui_state_event(const char *state, const char *text);
static void apply_interaction_mode(interaction_mode_t mode);

static bool is_external_powered(void)
{
    return s_battery_charging || s_usb_powered;
}

static esp_err_t init_power_management(void)
{
    const esp_pm_config_t pm_config = {
        .max_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
        .min_freq_mhz = CONFIG_XTAL_FREQ,
        .light_sleep_enable = false,
    };
    esp_err_t err = esp_pm_configure(&pm_config);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX, 0, "recording_cpu", &s_cpu_freq_lock);
    if (err != ESP_OK) {
        return err;
    }
    return ESP_OK;
}

static esp_err_t acquire_recording_pm_locks(void)
{
    if (s_recording_pm_locked) {
        return ESP_OK;
    }

    esp_err_t err = esp_pm_lock_acquire(s_cpu_freq_lock);
    if (err != ESP_OK) {
        return err;
    }

    s_recording_pm_locked = true;
    return ESP_OK;
}

static void release_recording_pm_locks(void)
{
    if (!s_recording_pm_locked) {
        return;
    }

    (void)esp_pm_lock_release(s_cpu_freq_lock);
    s_recording_pm_locked = false;
}

static esp_err_t acquire_ota_pm_locks(void)
{
    if (s_ota_pm_locked) {
        return ESP_OK;
    }

    esp_err_t err = esp_pm_lock_acquire(s_cpu_freq_lock);
    if (err != ESP_OK) {
        return err;
    }

    s_ota_pm_locked = true;
    return ESP_OK;
}

static void release_ota_pm_locks(void)
{
    if (!s_ota_pm_locked) {
        return;
    }

    (void)esp_pm_lock_release(s_cpu_freq_lock);
    s_ota_pm_locked = false;
}

static void restart_display_dim_timer(void)
{
    if (!s_display_dim_timer) {
        return;
    }

    (void)esp_timer_stop(s_display_dim_timer);
    if (!s_recording && !s_ota_updating) {
        esp_err_t err = esp_timer_start_once(s_display_dim_timer, DISPLAY_DIM_TIMEOUT_US);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "start dim timer failed: %s", esp_err_to_name(err));
        }
    }
}

static void restart_deep_sleep_timer(void)
{
    if (!s_deep_sleep_timer) {
        return;
    }

    (void)esp_timer_stop(s_deep_sleep_timer);
    s_sleep_deadline_us = 0;
    if (!s_recording && !s_ota_updating && !is_external_powered()) {
        const uint64_t timeout_us = (uint64_t)s_sleep_timeout_minutes * 60 * 1000000;
        s_sleep_deadline_us = esp_timer_get_time() + timeout_us;
        esp_err_t err = esp_timer_start_once(s_deep_sleep_timer, timeout_us);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "start deep sleep timer failed: %s", esp_err_to_name(err));
        }
    } else if (is_external_powered()) {
        ESP_LOGD(TAG, "deep sleep timer paused while external power is present");
    }
}

static void load_sleep_timeout(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    nvs_handle_t handle;
    err = nvs_open("power", NVS_READONLY, &handle);
    if (err == ESP_OK) {
        uint32_t minutes;
        err = nvs_get_u32(handle, "sleep_minutes", &minutes);
        if (err == ESP_OK && minutes >= 1 && minutes <= MAX_SLEEP_TIMEOUT_MINUTES) {
            s_sleep_timeout_minutes = minutes;
        }
        nvs_close(handle);
    }
    ESP_LOGI(TAG, "auto sleep timeout: %lu minutes", (unsigned long)s_sleep_timeout_minutes);
}

static void apply_sleep_timeout(uint32_t minutes)
{
    if (minutes < 1 || minutes > MAX_SLEEP_TIMEOUT_MINUTES || minutes == s_sleep_timeout_minutes) return;
    nvs_handle_t handle;
    esp_err_t err = nvs_open("power", NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        err = nvs_set_u32(handle, "sleep_minutes", minutes);
        if (err == ESP_OK) err = nvs_commit(handle);
        nvs_close(handle);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "save sleep timeout failed: %s", esp_err_to_name(err));
        return;
    }
    s_sleep_timeout_minutes = minutes;
    restart_deep_sleep_timer();
    ESP_LOGI(TAG, "auto sleep timeout set to %lu minutes", (unsigned long)minutes);
}

static void note_activity(void)
{
    if (s_display_dimmed) {
        esp_err_t err = ui_status_set_brightness(DISPLAY_ACTIVE_BRIGHTNESS);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "restore brightness failed: %s", esp_err_to_name(err));
        } else {
            s_display_dimmed = false;
            ui_status_set_idle_dimmed(false);
        }
    }
    restart_display_dim_timer();
    restart_deep_sleep_timer();
}

static void stop_host_response_timer(void)
{
    if (s_host_response_timer) {
        (void)esp_timer_stop(s_host_response_timer);
    }
}

static void enter_deep_sleep(void)
{
    // An old timer event may still be queued after activity or a settings change.
    if (s_sleep_deadline_us == 0 || esp_timer_get_time() < s_sleep_deadline_us) return;
    if (s_recording || s_ota_updating || voice_ble_ota_is_active()) {
        restart_deep_sleep_timer();
        return;
    }

    if (is_external_powered()) {
        ESP_LOGI(TAG, "skip deep sleep while charging or USB powered");
        restart_deep_sleep_timer();
        return;
    }

    bool charging = false;
    bool usb_powered = false;
    esp_err_t power_err = stick_s3_board_battery_charging(&charging);
    if (power_err == ESP_OK) {
        power_err = stick_s3_board_usb_powered(&usb_powered);
    }
    if (power_err == ESP_OK && (charging || usb_powered)) {
        s_battery_charging = charging;
        s_usb_powered = usb_powered;
        ESP_LOGI(TAG, "skip deep sleep after fresh power check charging=%d usb=%d",
                 charging, usb_powered);
        restart_deep_sleep_timer();
        return;
    }

    const gpio_num_t wake_gpio = STICK_S3_PIN_BUTTON_FRONT;
    if (!esp_sleep_is_valid_wakeup_gpio(wake_gpio)) {
        ESP_LOGE(TAG, "GPIO%d cannot wake from deep sleep", wake_gpio);
        restart_deep_sleep_timer();
        return;
    }

    if (gpio_get_level(wake_gpio) == 0) {
        ESP_LOGI(TAG, "skip deep sleep: front button is pressed");
        restart_deep_sleep_timer();
        return;
    }

    ESP_LOGI(TAG, "entering deep sleep, wake on front button GPIO%d low (level=%d)",
             wake_gpio, gpio_get_level(wake_gpio));
    release_recording_pm_locks();
    ESP_ERROR_CHECK_WITHOUT_ABORT(ui_status_set_brightness(0));
    ui_status_prepare_deep_sleep();
    stick_s3_board_prepare_deep_sleep();

    /* Clear any stale wakeup source bits left over from light sleep / esp_pm
       configuration (e.g. gpio_wakeup_enable on the PMIC IRQ line). Without
       this the chip can wake immediately from an unrelated trigger. */
    (void)esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);

    /* Keep RTC peripherals powered so the internal pull-up on the wake pin
       remains effective in deep sleep; combined with the explicit RTC pull-up
       below this prevents GPIO%d from floating low and self-waking. */
    (void)esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
    (void)rtc_gpio_pulldown_dis(wake_gpio);
    (void)rtc_gpio_pullup_en(wake_gpio);

    esp_err_t err = esp_sleep_enable_ext1_wakeup_io(1ULL << wake_gpio,
                                                    ESP_EXT1_WAKEUP_ANY_LOW);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "enable deep sleep wake failed: %s", esp_err_to_name(err));
        restart_deep_sleep_timer();
        return;
    }

    /* Wait for the wake pin to settle high (button release bounce, parasitic
       capacitance, etc.). If it stays low we would just wake up immediately
       after esp_deep_sleep_start(), so abort and retry later. */
    int wait_ms = 0;
    while (gpio_get_level(wake_gpio) == 0 && wait_ms < 200) {
        vTaskDelay(pdMS_TO_TICKS(10));
        wait_ms += 10;
    }
    if (gpio_get_level(wake_gpio) == 0) {
        ESP_LOGW(TAG, "front button still low after %d ms, abort deep sleep", wait_ms);
        restart_deep_sleep_timer();
        return;
    }

    ESP_LOGI(TAG, "deep sleep go (wait_ms=%d level=%d)", wait_ms,
             gpio_get_level(wake_gpio));
    esp_deep_sleep_start();
}

static bool app_ui_allows_recording_start(void)
{
    return s_app_ui_state != APP_UI_STATE_PENDING_CONFIRMATION;
}

static uint32_t begin_recording(bool prepare)
{
    const bool ble_ready = voice_ble_is_ready();
    const bool ota_active = voice_ble_ota_is_active();
    const bool ui_allows_start = app_ui_allows_recording_start();
    if (s_recording || s_ota_updating || ota_active || !ble_ready || !ui_allows_start) {
        ESP_LOGW(TAG,
                 "start recording denied: recording=%d ota=%d ble_ota=%d ble_ready=%d ui_state=%d",
                 s_recording, s_ota_updating, ota_active, ble_ready, s_app_ui_state);
        return 0;
    }

    const uint32_t session_id = s_session_id++;
    esp_err_t err = acquire_recording_pm_locks();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "acquire recording pm locks failed: %s", esp_err_to_name(err));
        ui_status_set_error("Power lock failed");
        return 0;
    }

    err = prepare ? audio_pipeline_prepare(session_id) : audio_pipeline_start(session_id);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "audio start failed: %s", esp_err_to_name(err));
        release_recording_pm_locks();
        ui_status_set_error("Audio start failed");
        return 0;
    }

    s_recording = true;
    if (!prepare) s_app_ui_state = APP_UI_STATE_RECORDING;
    restart_display_dim_timer();
    restart_deep_sleep_timer();
    if (!prepare) ui_status_set_recording(session_id);
    return session_id;
}

static uint32_t start_recording(void) { return begin_recording(false); }

static uint32_t stop_recording(void)
{
    if (!s_recording) {
        return 0;
    }

    const uint32_t session_id = audio_pipeline_session_id();
    s_recording = false;
    audio_pipeline_stop();
    release_recording_pm_locks();
    restart_display_dim_timer();
    restart_deep_sleep_timer();
    return session_id;
}

static void queue_app_event(app_event_type_t type)
{
    queue_app_event_with_ota(type, 0, 0);
}

static void queue_app_event_with_ota(app_event_type_t type, uint32_t written, uint32_t size)
{
    if (s_app_event_queue) {
        app_event_t event = {
            .type = type,
            .timestamp_ms = (uint64_t)esp_timer_get_time() / 1000,
            .written = written,
            .size = size,
        };
        (void)xQueueSend(s_app_event_queue, &event, 0);
    }
}

static void queue_app_event_from_isr(app_event_type_t type, BaseType_t *high_task_woken)
{
    if (s_app_event_queue) {
        app_event_t event = {
            .type = type,
            .written = 0,
            .size = 0,
        };
        (void)xQueueSendFromISR(s_app_event_queue, &event, high_task_woken);
    }
}

static void queue_ui_state_event(const char *state, const char *text)
{
    if (!s_app_event_queue) {
        ESP_LOGW(TAG, "drop ui_state state=%s text_len=%u: app queue unavailable",
                 state ? state : "nil",
                 (unsigned)(text ? strlen(text) : 0));
        return;
    }

    app_event_t event = {
        .type = APP_EVENT_UI_STATE,
    };
    if (state) {
        strlcpy(event.state, state, sizeof(event.state));
    }
    if (text) {
        strlcpy(event.text, text, sizeof(event.text));
    }
    ESP_LOGI(TAG, "queue ui_state state=%s text_len=%u current=%s recording=%d",
             event.state[0] ? event.state : "nil",
             (unsigned)strlen(event.text),
             app_ui_state_name(s_app_ui_state),
             s_recording);
    if (xQueueSend(s_app_event_queue, &event, 0) != pdTRUE) {
        ESP_LOGW(TAG, "drop ui_state state=%s: app queue full",
                 event.state[0] ? event.state : "nil");
    }
}

static void front_button_down_cb(void *button_handle, void *usr_data)
{
    (void)button_handle;
    (void)usr_data;
    queue_app_event(APP_EVENT_FRONT_DOWN);
}

static void front_button_up_cb(void *button_handle, void *usr_data)
{
    (void)button_handle;
    (void)usr_data;
    queue_app_event(APP_EVENT_FRONT_UP);
}

static void side_button_down_cb(void *button_handle, void *usr_data)
{
    (void)button_handle;
    (void)usr_data;
    queue_app_event(APP_EVENT_SIDE_DOWN);
}

static void side_button_up_cb(void *button_handle, void *usr_data)
{
    (void)button_handle;
    (void)usr_data;
    queue_app_event(APP_EVENT_SIDE_UP);
}

static void ble_connection_cb(bool connected)
{
    queue_app_event(connected ? APP_EVENT_BLE_CONNECTED : APP_EVENT_BLE_DISCONNECTED);
}

static void ble_control_cb(const char *json)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) {
        ESP_LOGW(TAG, "ignore invalid control json");
        return;
    }

    const cJSON *event = cJSON_GetObjectItemCaseSensitive(root, "event");
    const cJSON *state = cJSON_GetObjectItemCaseSensitive(root, "state");
    const cJSON *text = cJSON_GetObjectItemCaseSensitive(root, "text");
    const cJSON *mode = cJSON_GetObjectItemCaseSensitive(root, "mode");
    if (cJSON_IsString(event) && strcmp(event->valuestring, "voice_key") == 0) {
        const cJSON *session = cJSON_GetObjectItemCaseSensitive(root, "session_id");
        const cJSON *down = cJSON_GetObjectItemCaseSensitive(root, "down");
        const cJSON *cancel = cJSON_GetObjectItemCaseSensitive(root, "cancel");
        const cJSON *shortcut = cJSON_GetObjectItemCaseSensitive(root, "shortcut");
        if (cJSON_IsNumber(session) && session->valuedouble >= 1 && session->valuedouble <= UINT32_MAX &&
            session->valuedouble == (double)(uint32_t)session->valuedouble && cJSON_IsNumber(down) &&
            (down->valuedouble == 0 || down->valuedouble == 1))
            (void)voice_ble_set_voice_key((uint32_t)session->valuedouble, down->valueint == 1,
                cJSON_IsNumber(cancel) && cancel->valueint == 1,
                !shortcut ? 1 : cJSON_IsNumber(shortcut) && (shortcut->valuedouble == 1 || shortcut->valuedouble == 2)
                    ? (uint8_t)shortcut->valueint : 0);
    } else if (cJSON_IsString(event) && (strcmp(event->valuestring, "buttons") == 0 || strcmp(event->valuestring, "buttons_off") == 0)) {
        app_event_t profile = {.type = APP_EVENT_BUTTON_PROFILE};
        if (strlen(json) < sizeof(profile.text)) {
            strcpy(profile.text, json);
            (void)xQueueSend(s_app_event_queue, &profile, 0);
        }
    } else if (cJSON_IsString(event) && strcmp(event->valuestring, "sleep_timeout") == 0) {
        const cJSON *minutes = cJSON_GetObjectItemCaseSensitive(root, "minutes");
        if (cJSON_IsNumber(minutes) && minutes->valuedouble >= 1 &&
            minutes->valuedouble <= MAX_SLEEP_TIMEOUT_MINUTES &&
            minutes->valuedouble == (double)minutes->valueint) {
            app_event_t setting = {.type = APP_EVENT_SLEEP_TIMEOUT, .written = (uint32_t)minutes->valueint};
            if (xQueueSend(s_app_event_queue, &setting, 0) != pdTRUE)
                ESP_LOGW(TAG, "drop sleep timeout: app queue full");
        } else {
            ESP_LOGW(TAG, "ignore invalid sleep timeout (expected 1-1440 whole minutes)");
        }
    } else if (cJSON_IsString(event) && strcmp(event->valuestring, "battery_request") == 0) {
        queue_app_event(APP_EVENT_BATTERY_REFRESH);
    } else if (cJSON_IsString(event) && strcmp(event->valuestring, "record_cancel") == 0) {
        queue_app_event(APP_EVENT_RECORD_CANCEL);
    } else if (cJSON_IsString(event) && strcmp(event->valuestring, "ui_state") == 0 &&
        cJSON_IsString(state)) {
        queue_ui_state_event(state->valuestring, cJSON_IsString(text) ? text->valuestring : "");
    } else if (cJSON_IsString(event) && strcmp(event->valuestring, "interaction_mode") == 0 &&
               cJSON_IsString(mode)) {
        if (strcmp(mode->valuestring, "click_to_talk") == 0) {
            apply_interaction_mode(INTERACTION_MODE_CLICK_TO_TALK);
        } else if (strcmp(mode->valuestring, "hold_to_talk") == 0) {
            apply_interaction_mode(INTERACTION_MODE_HOLD_TO_TALK);
        } else {
            ESP_LOGW(TAG, "unknown interaction_mode %s", mode->valuestring);
        }
    }
    cJSON_Delete(root);
}

static uint32_t elapsed_button_ms(int64_t down_us)
{
    if (down_us <= 0) {
        return 0;
    }
    int64_t elapsed_us = esp_timer_get_time() - down_us;
    if (elapsed_us < 0) {
        elapsed_us = 0;
    }
    return (uint32_t)(elapsed_us / 1000);
}

static void apply_app_ui_state(const char *state, const char *text)
{
    ESP_LOGI(TAG, "apply ui_state state=%s text_len=%u current=%s recording=%d",
             state ? state : "nil",
             (unsigned)(text ? strlen(text) : 0),
             app_ui_state_name(s_app_ui_state),
             s_recording);
    stop_host_response_timer();
    if (strcmp(state, "ready") == 0) {
        if (s_recording) {
            ESP_LOGI(TAG, "ignore ready ui_state while recording");
            return;
        }
        s_app_ui_state = APP_UI_STATE_READY;
        ui_status_set_idle();
        note_activity();
        voice_ble_request_slow_interval();
    } else if (strcmp(state, "recording") == 0) {
        s_app_ui_state = APP_UI_STATE_RECORDING;
        if (!s_recording) {
            ui_status_set_recording(0);
        }
    } else if (strcmp(state, "thinking") == 0) {
        s_app_ui_state = APP_UI_STATE_THINKING;
        ui_status_set_partial_text("");
    } else if (strcmp(state, "pending_confirmation") == 0) {
        s_app_ui_state = APP_UI_STATE_PENDING_CONFIRMATION;
        ui_status_set_partial_text("Confirm or cancel");
    } else if (strcmp(state, "error") == 0) {
        s_app_ui_state = APP_UI_STATE_ERROR;
        ui_status_set_error(text && text[0] ? text : "Unknown error");
    } else {
        ESP_LOGW(TAG, "unknown ui_state %s", state);
    }
    ESP_LOGI(TAG, "applied ui_state current=%s recording=%d",
             app_ui_state_name(s_app_ui_state),
             s_recording);
}

static void apply_interaction_mode(interaction_mode_t mode)
{
    s_interaction_mode = mode;
    ui_status_set_idle_hint(s_custom_buttons ? "Custom buttons" : mode == INTERACTION_MODE_CLICK_TO_TALK ? "Click to Talk" : "Hold to Talk");
    if (s_app_ui_state == APP_UI_STATE_READY && !s_recording) {
        ui_status_set_idle();
    }
    ESP_LOGI(TAG, "interaction mode %s",
             mode == INTERACTION_MODE_CLICK_TO_TALK ? "click_to_talk" : "hold_to_talk");
}

static void ble_ota_cb(voice_ble_ota_event_t event, uint32_t written, uint32_t size)
{
    switch (event) {
    case VOICE_BLE_OTA_EVENT_BEGIN:
        queue_app_event_with_ota(APP_EVENT_OTA_BEGIN, written, size);
        break;
    case VOICE_BLE_OTA_EVENT_PROGRESS:
        queue_app_event_with_ota(APP_EVENT_OTA_PROGRESS, written, size);
        break;
    case VOICE_BLE_OTA_EVENT_DONE:
        queue_app_event_with_ota(APP_EVENT_OTA_DONE, written, size);
        break;
    case VOICE_BLE_OTA_EVENT_ERROR:
    case VOICE_BLE_OTA_EVENT_ABORT:
        queue_app_event(APP_EVENT_OTA_END);
        break;
    }
}

static uint32_t s_button_revision;
static int s_long_ms = 500, s_double_ms = 300;
static char s_button_actions[7] = "301333";
static bg_state_t s_gestures[2];
static int s_recording_slot = -1;
static bool s_preparing;

static void cancel_button_recording(void)
{
    if (s_recording) {
        audio_pipeline_discard();
        s_recording = false;
        release_recording_pm_locks();
    }
    s_recording_slot = -1;
    s_preparing = false;
    memset(s_gestures, 0, sizeof(s_gestures));
    apply_app_ui_state("ready", "");
}

static void emit_gesture(int button, bg_event_t gesture, uint32_t duration, void *context)
{
    (void)context;
    int slot = button * 3 + (gesture == BG_SINGLE ? 0 : gesture == BG_DOUBLE ? 1 : 2);
    char action = s_button_actions[slot];
    if (s_preparing && s_recording_slot / 3 == button && gesture != BG_LONG_START && gesture != BG_LONG_END) {
        audio_pipeline_discard();
        s_recording = s_preparing = false;
        s_recording_slot = -1;
        release_recording_pm_locks();
        restart_display_dim_timer(); restart_deep_sleep_timer();
        voice_ble_request_slow_interval();
    }
    if (gesture == BG_LONG_END) {
        if (action == '1' && s_recording_slot == slot && s_recording && !s_preparing) {
            uint32_t session = audio_pipeline_session_id();
            // State goes first so the desktop is already waiting when END arrives.
            (void)voice_ble_send_gesture("record_stop", slot, session, duration, s_button_revision);
            stop_recording(); s_recording_slot = -1;
            apply_app_ui_state("thinking", "");
        }
        return;
    }
    if (action == '1' || action == '2') {
        if (action == '2' && s_recording && !s_preparing) {
            uint32_t session = audio_pipeline_session_id();
            // Report the slot that started this session, irrespective of stop binding.
            (void)voice_ble_send_gesture("record_stop", s_recording_slot, session, duration, s_button_revision);
            stop_recording(); s_recording_slot = -1;
            apply_app_ui_state("thinking", "");
            return;
        }
        if (s_preparing && s_recording_slot == slot) {
            if (voice_ble_send_gesture("record_start", slot, audio_pipeline_session_id(), duration, s_button_revision) != ESP_OK) {
                cancel_button_recording(); return;
            }
            s_preparing = false;
            s_app_ui_state = APP_UI_STATE_RECORDING;
            ui_status_set_recording(audio_pipeline_session_id());
            audio_pipeline_commit();
        } else if (!s_recording && s_app_ui_state == APP_UI_STATE_READY) {
            uint32_t session = begin_recording(true);
            if (session) {
                s_recording_slot = slot;
                if (voice_ble_send_gesture("record_start", slot, session, 0, s_button_revision) != ESP_OK) {
                    cancel_button_recording(); return;
                }
                s_app_ui_state = APP_UI_STATE_RECORDING;
                ui_status_set_recording(session);
                audio_pipeline_commit();
            }
        }
    } else if (action == '3') {
        (void)voice_ble_send_gesture("button_gesture", slot, 0, duration, s_button_revision);
    }
}

static void handle_custom_edge(int button, bool down, uint64_t now)
{
    bg_edge(&s_gestures[button], button, down, now, s_long_ms, s_double_ms,
            true, s_button_actions[button * 3 + 1] != '0', emit_gesture, NULL);
    if (down && !s_recording && s_app_ui_state == APP_UI_STATE_READY && s_button_actions[button * 3 + 2] == '1') {
        uint32_t session = begin_recording(true);
        if (session) { s_preparing = true; s_recording_slot = button * 3 + 2; }
    }
    if (!down && s_preparing && s_recording_slot / 3 == button) {
        // A short click never transmits the speculative audio or opens ASR.
        audio_pipeline_discard(); s_recording = s_preparing = false;
        s_recording_slot = -1; release_recording_pm_locks();
        restart_display_dim_timer(); restart_deep_sleep_timer();
        voice_ble_request_slow_interval();
    }
}

static void apply_button_profile(const char *json)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) return;
    const cJSON *event = cJSON_GetObjectItemCaseSensitive(root, "event");
    if (cJSON_IsString(event) && strcmp(event->valuestring, "buttons_off") == 0) {
        cancel_button_recording(); s_custom_buttons = false;
        apply_interaction_mode(s_interaction_mode);
    } else {
        const cJSON *map = cJSON_GetObjectItemCaseSensitive(root, "map");
        const cJSON *long_ms = cJSON_GetObjectItemCaseSensitive(root, "long_ms");
        const cJSON *double_ms = cJSON_GetObjectItemCaseSensitive(root, "double_ms");
        const cJSON *rev = cJSON_GetObjectItemCaseSensitive(root, "rev");
        bool valid = cJSON_IsString(map) && strlen(map->valuestring) == 6 &&
                     cJSON_IsNumber(long_ms) && long_ms->valueint >= 500 && long_ms->valueint <= 1500 &&
                     cJSON_IsNumber(double_ms) && double_ms->valueint >= 150 && double_ms->valueint <= 500 &&
                     cJSON_IsNumber(rev) && rev->valuedouble > 0 && rev->valuedouble <= UINT32_MAX;
        if (valid) for (int i = 0; i < 6; ++i) if (map->valuestring[i] < '0' || map->valuestring[i] > '3' || (map->valuestring[i] == '1' && i % 3 != 2)) valid = false;
        if (valid) {
            cancel_button_recording();
            memcpy(s_button_actions, map->valuestring, 7);
            s_long_ms = long_ms->valueint; s_double_ms = double_ms->valueint;
            s_button_revision = (uint32_t)rev->valuedouble;
            s_custom_buttons = true;
            ui_status_set_idle_hint("Custom buttons");
            (void)voice_ble_send_buttons_ready(s_button_revision);
        }
    }
    cJSON_Delete(root);
}

static void app_event_task(void *arg)
{
    (void)arg;
    app_event_t event;
    while (true) {
        const TickType_t wait_ticks = s_custom_buttons &&
            (s_gestures[0].down || s_gestures[1].down || s_gestures[0].pending || s_gestures[1].pending)
            ? pdMS_TO_TICKS(10) : portMAX_DELAY;
        if (xQueueReceive(s_app_event_queue, &event, wait_ticks) != pdTRUE) {
            if (s_custom_buttons && !s_ota_updating) for (int i = 0; i < 2; ++i)
                bg_tick(&s_gestures[i], i, (uint64_t)esp_timer_get_time() / 1000, s_long_ms, s_double_ms,
                        true, emit_gesture, NULL);
            continue;
        }
        if (s_custom_buttons && event.type >= APP_EVENT_FRONT_DOWN && event.type <= APP_EVENT_SIDE_UP) {
            note_activity();
            if (!s_ota_updating) handle_custom_edge(event.type >= APP_EVENT_SIDE_DOWN ? 1 : 0,
                 event.type == APP_EVENT_FRONT_DOWN || event.type == APP_EVENT_SIDE_DOWN, event.timestamp_ms);
            continue;
        }

        switch (event.type) {
        case APP_EVENT_BUTTON_PROFILE:
            if (!s_ota_updating) apply_button_profile(event.text);
            break;
        case APP_EVENT_RECORD_CANCEL:
            cancel_button_recording();
            break;
        case APP_EVENT_FRONT_DOWN:
            ESP_LOGI(TAG, "button front down");
            note_activity();
            if (s_interaction_mode == INTERACTION_MODE_CLICK_TO_TALK && s_recording) {
                const uint32_t primary_duration_ms = elapsed_button_ms(s_primary_down_us);
                s_primary_session_id = stop_recording();
                esp_err_t primary_up_err = voice_ble_send_button_click("primary", primary_duration_ms,
                                                                       s_primary_session_id);
                if (s_primary_session_id != 0 && primary_up_err != ESP_OK) {
                    apply_app_ui_state("ready", "");
                }
                s_primary_down_us = 0;
                s_primary_session_id = 0;
            } else {
                s_primary_down_us = esp_timer_get_time();
                if (s_app_ui_state == APP_UI_STATE_PENDING_CONFIRMATION) {
                    ESP_LOGI(TAG, "button front down as pending confirmation control");
                    s_primary_session_id = 0;
                    (void)voice_ble_send_button_click("primary", 0, 0);
                    break;
                }
                s_primary_session_id = start_recording();
                if (s_primary_session_id == 0) {
                    s_primary_down_us = 0;
                    break;
                }
                esp_err_t primary_down_err = s_interaction_mode == INTERACTION_MODE_CLICK_TO_TALK
                    ? voice_ble_send_button_click("primary", 0, s_primary_session_id)
                    : voice_ble_send_button_down("primary", s_primary_session_id);
                if (s_primary_session_id != 0 && primary_down_err != ESP_OK) {
                    (void)stop_recording();
                    s_primary_session_id = 0;
                    apply_app_ui_state("ready", "");
                }
            }
            break;
        case APP_EVENT_FRONT_UP:
            ESP_LOGI(TAG, "button front up");
            note_activity();
            if (s_interaction_mode == INTERACTION_MODE_CLICK_TO_TALK) {
                break;
            }
            if (!s_recording && s_primary_session_id == 0 && s_primary_down_us == 0) {
                break;
            }
            const uint32_t primary_duration_ms = elapsed_button_ms(s_primary_down_us);
            if (s_recording) {
                s_primary_session_id = stop_recording();
            }
            esp_err_t primary_up_err = voice_ble_send_button_up("primary", primary_duration_ms,
                                                                s_primary_session_id);
            if (s_primary_session_id != 0 && primary_up_err != ESP_OK) {
                apply_app_ui_state("ready", "");
            }
            s_primary_down_us = 0;
            s_primary_session_id = 0;
            break;
        case APP_EVENT_SIDE_DOWN:
            ESP_LOGI(TAG, "button side down");
            note_activity();
            s_secondary_down_us = esp_timer_get_time();
            break;
        case APP_EVENT_SIDE_UP:
            ESP_LOGI(TAG, "button side up");
            note_activity();
            voice_ble_send_button_click("secondary", elapsed_button_ms(s_secondary_down_us), 0);
            s_secondary_down_us = 0;
            break;
        case APP_EVENT_UI_STATE:
            apply_app_ui_state(event.state, event.text);
            break;
        case APP_EVENT_BLE_CONNECTED:
            s_app_ui_state = APP_UI_STATE_READY;
            ui_status_set_idle();
            note_activity();
            break;
        case APP_EVENT_BLE_DISCONNECTED:
            s_custom_buttons = false;
            cancel_button_recording();
            s_recording = false;
            s_ota_updating = false;
            s_app_ui_state = APP_UI_STATE_READY;
            stop_host_response_timer();
            audio_pipeline_stop();
            release_recording_pm_locks();
            release_ota_pm_locks();
            ui_status_set_pairing(voice_ble_device_name());
            break;
        case APP_EVENT_POWER_IRQ:
            gpio_intr_enable(STICK_S3_PIN_PMIC_IRQ);
            /* fall through */
        case APP_EVENT_BATTERY_REFRESH:
            update_battery_status();
            break;
        case APP_EVENT_ENTER_DEEP_SLEEP:
            enter_deep_sleep();
            break;
        case APP_EVENT_SLEEP_TIMEOUT:
            apply_sleep_timeout(event.written);
            break;
        case APP_EVENT_OTA_BEGIN:
            if (s_custom_buttons) cancel_button_recording();
            s_ota_updating = true;
            if (s_recording) {
                const uint32_t session_id = stop_recording();
                voice_ble_send_button_up("primary", elapsed_button_ms(s_primary_down_us),
                                         session_id);
            }
            esp_err_t ota_pm_err = acquire_ota_pm_locks();
            if (ota_pm_err != ESP_OK) {
                ESP_LOGW(TAG, "acquire OTA pm lock failed: %s", esp_err_to_name(ota_pm_err));
            }
            note_activity();
            ui_status_set_ota_progress(event.written, event.size);
            break;
        case APP_EVENT_OTA_PROGRESS:
            s_ota_updating = true;
            ui_status_set_ota_progress(event.written, event.size);
            break;
        case APP_EVENT_OTA_DONE:
            ui_status_set_ota_rebooting();
            break;
        case APP_EVENT_OTA_END:
            s_ota_updating = false;
            release_ota_pm_locks();
            s_app_ui_state = APP_UI_STATE_READY;
            stop_host_response_timer();
            ui_status_set_idle();
            note_activity();
            break;
        case APP_EVENT_HOST_RESPONSE_TIMEOUT:
            if (!s_recording && (s_app_ui_state == APP_UI_STATE_RECORDING ||
                                 s_app_ui_state == APP_UI_STATE_THINKING)) {
                ESP_LOGW(TAG, "host response timeout, returning to ready");
                apply_app_ui_state("ready", "");
            }
            break;
        }
    }
}

static esp_err_t init_gpio_button(gpio_num_t gpio_num, button_handle_t *button)
{
    const button_config_t button_config = {0};
    const button_gpio_config_t gpio_config = {
        .gpio_num = gpio_num,
        .active_level = 0,
        .enable_power_save = true
    };

    return iot_button_new_gpio_device(&button_config, &gpio_config, button);
}

static esp_err_t init_buttons(void)
{
    s_app_event_queue = xQueueCreate(12, sizeof(app_event_t));
    if (!s_app_event_queue) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = init_gpio_button(STICK_S3_PIN_BUTTON_FRONT, &s_front_button);
    if (err != ESP_OK) {
        return err;
    }
    err = init_gpio_button(STICK_S3_PIN_BUTTON_SIDE, &s_side_button);
    if (err != ESP_OK) {
        return err;
    }

    err = iot_button_register_cb(s_front_button, BUTTON_PRESS_DOWN, NULL,
                                 front_button_down_cb, NULL);
    if (err != ESP_OK) {
        return err;
    }
    err = iot_button_register_cb(s_front_button, BUTTON_PRESS_UP, NULL,
                                 front_button_up_cb, NULL);
    if (err != ESP_OK) {
        return err;
    }
    err = iot_button_register_cb(s_side_button, BUTTON_PRESS_DOWN, NULL,
                                 side_button_down_cb, NULL);
    if (err != ESP_OK) {
        return err;
    }
    err = iot_button_register_cb(s_side_button, BUTTON_PRESS_UP, NULL,
                                 side_button_up_cb, NULL);
    if (err != ESP_OK) {
        return err;
    }

    BaseType_t ok = xTaskCreate(app_event_task, "app_event_task", 4096,
                                NULL, 6, NULL);
    return ok == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

static void display_dim_timer_cb(void *arg)
{
    (void)arg;

    if (!s_display_dimmed && !s_recording && !s_ota_updating) {
        esp_err_t err = ui_status_set_brightness(DISPLAY_DIM_BRIGHTNESS);
        if (err == ESP_OK) {
            s_display_dimmed = true;
            ui_status_set_idle_dimmed(true);
            ESP_LOGI(TAG, "display dimmed after inactivity");
        } else {
            ESP_LOGW(TAG, "dim display failed: %s", esp_err_to_name(err));
        }
    }
}

static esp_err_t init_display_dim_timer(void)
{
    const esp_timer_create_args_t timer_args = {
        .callback = display_dim_timer_cb,
        .name = "display_dim",
    };
    return esp_timer_create(&timer_args, &s_display_dim_timer);
}

static void deep_sleep_timer_cb(void *arg)
{
    (void)arg;
    queue_app_event(APP_EVENT_ENTER_DEEP_SLEEP);
}

static void host_response_timer_cb(void *arg)
{
    (void)arg;
    queue_app_event(APP_EVENT_HOST_RESPONSE_TIMEOUT);
}

static esp_err_t init_deep_sleep_timer(void)
{
    const esp_timer_create_args_t timer_args = {
        .callback = deep_sleep_timer_cb,
        .name = "deep_sleep",
    };
    return esp_timer_create(&timer_args, &s_deep_sleep_timer);
}

static esp_err_t init_host_response_timer(void)
{
    const esp_timer_create_args_t timer_args = {
        .callback = host_response_timer_cb,
        .name = "host_response",
    };
    return esp_timer_create(&timer_args, &s_host_response_timer);
}

static void battery_refresh_timer_cb(void *arg)
{
    (void)arg;
    queue_app_event(APP_EVENT_BATTERY_REFRESH);
}

static esp_err_t init_battery_refresh_timer(void)
{
    const esp_timer_create_args_t timer_args = {
        .callback = battery_refresh_timer_cb,
        .name = "battery_refresh",
        .skip_unhandled_events = true,
    };
    esp_err_t err = esp_timer_create(&timer_args, &s_battery_refresh_timer);
    if (err != ESP_OK) {
        return err;
    }
    return esp_timer_start_periodic(s_battery_refresh_timer, BATTERY_REFRESH_FALLBACK_US);
}

static void IRAM_ATTR pmic_irq_isr(void *arg)
{
    (void)arg;
    gpio_intr_disable(STICK_S3_PIN_PMIC_IRQ);

    BaseType_t high_task_woken = pdFALSE;
    queue_app_event_from_isr(APP_EVENT_POWER_IRQ, &high_task_woken);
    if (high_task_woken) {
        portYIELD_FROM_ISR();
    }
}

static esp_err_t init_pmic_irq(void)
{
    gpio_config_t irq_config = {
        .pin_bit_mask = 1ULL << STICK_S3_PIN_PMIC_IRQ,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&irq_config);
    if (err != ESP_OK) {
        return err;
    }

    err = gpio_wakeup_enable(STICK_S3_PIN_PMIC_IRQ, GPIO_INTR_LOW_LEVEL);
    if (err != ESP_OK) {
        return err;
    }

    err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    err = gpio_isr_handler_add(STICK_S3_PIN_PMIC_IRQ, pmic_irq_isr, NULL);
    if (err != ESP_OK) {
        return err;
    }

    err = gpio_set_intr_type(STICK_S3_PIN_PMIC_IRQ, GPIO_INTR_LOW_LEVEL);
    if (err != ESP_OK) {
        return err;
    }
    err = gpio_intr_enable(STICK_S3_PIN_PMIC_IRQ);
    if (err != ESP_OK) {
        return err;
    }

    return ESP_OK;
}

static void update_battery_status(void)
{
    uint8_t sys_status = 0;
    esp_err_t irq_err = stick_s3_board_clear_power_irqs(&sys_status);
    if (irq_err == ESP_OK && sys_status) {
        ESP_LOGI(TAG, "PMIC sys irq=0x%02x", sys_status);
    }

    int level = 0;
    bool charging = false;
    bool usb_powered = false;
    esp_err_t err = stick_s3_board_battery_level(&level);
    if (err == ESP_OK) {
        err = stick_s3_board_battery_charging(&charging);
    }
    if (err == ESP_OK) {
        err = stick_s3_board_usb_powered(&usb_powered);
    }
    if (err == ESP_OK) {
        const bool external_power_changed = (charging != s_battery_charging) ||
                                            (usb_powered != s_usb_powered);
        s_battery_charging = charging;
        s_usb_powered = usb_powered;
        ui_status_set_battery(level, charging, usb_powered);
        if (voice_ble_is_ready() && !s_recording) {
            esp_err_t send_err = voice_ble_send_battery(level);
            if (send_err != ESP_OK) ESP_LOGW(TAG, "battery notify failed: %s", esp_err_to_name(send_err));
        }
        if (external_power_changed) {
            ESP_LOGI(TAG, "power source changed charging=%d usb=%d",
                     charging, usb_powered);
            restart_deep_sleep_timer();
        }
    } else {
        ESP_LOGW(TAG, "battery read failed: %s", esp_err_to_name(err));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "boot reset_reason=%d wakeup_cause=%d ext1_status=0x%llx",
             esp_reset_reason(), esp_sleep_get_wakeup_cause(),
             (unsigned long long)esp_sleep_get_ext1_wakeup_status());

    load_sleep_timeout();
    ESP_ERROR_CHECK(init_power_management());
    ESP_ERROR_CHECK(stick_s3_board_init());
    ESP_ERROR_CHECK(ui_status_init());
    ESP_ERROR_CHECK(init_display_dim_timer());
    ESP_ERROR_CHECK(init_deep_sleep_timer());
    ESP_ERROR_CHECK(init_host_response_timer());
    note_activity();
    voice_ble_set_connection_callback(ble_connection_cb);
    voice_ble_set_control_callback(ble_control_cb);
    voice_ble_set_ota_callback(ble_ota_cb);
    ESP_ERROR_CHECK(init_buttons());

    esp_err_t err = voice_ble_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "BLE init failed: %s", esp_err_to_name(err));
        ui_status_set_error("BLE init failed");
    } else {
        ui_status_set_device_name(voice_ble_device_name());
    }

    esp_err_t audio_err = audio_pipeline_init();
    if (audio_err != ESP_OK) {
        ESP_LOGE(TAG, "audio init failed: %s", esp_err_to_name(audio_err));
        ui_status_set_error("Audio init failed");
    }

    if (err == ESP_OK) {
        ui_status_set_pairing(voice_ble_device_name());
    }
    ESP_LOGI(TAG, "Voice Stick booted");

    update_battery_status();
    ESP_ERROR_CHECK(init_battery_refresh_timer());
    ESP_ERROR_CHECK(init_pmic_irq());

    ESP_LOGI(TAG, "configuring PMIC");
    esp_pm_config_t pm_config = {
        .max_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
        .min_freq_mhz = CONFIG_XTAL_FREQ,
        .light_sleep_enable = true,
    };
    esp_pm_configure(&pm_config);
}
