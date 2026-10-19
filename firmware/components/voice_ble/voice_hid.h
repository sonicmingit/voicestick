#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "host/ble_gap.h"

typedef void (*voice_hid_ack_t)(uint32_t session, bool down, bool ok);
esp_err_t voice_hid_init(voice_hid_ack_t ack);
void voice_hid_gap_event(const struct ble_gap_event *event);
esp_err_t voice_hid_key(uint32_t session, bool down, bool cancel, uint8_t shortcut);
