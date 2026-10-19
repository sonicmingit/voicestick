#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef enum { BG_SINGLE, BG_DOUBLE, BG_LONG_START, BG_LONG_END } bg_event_t;
typedef struct {
    bool down, long_sent, pending, second;
    uint64_t down_ms, up_ms;
} bg_state_t;
typedef void (*bg_emit_t)(int button, bg_event_t event, uint32_t duration, void *context);
void bg_tick(bg_state_t *s, int button, uint64_t now, int long_ms, int double_ms,
             bool has_long, bg_emit_t emit, void *context);
void bg_edge(bg_state_t *s, int button, bool down, uint64_t now,
             int long_ms, int double_ms, bool has_long, bool has_double,
             bg_emit_t emit, void *context);
#ifdef __cplusplus
}
#endif
