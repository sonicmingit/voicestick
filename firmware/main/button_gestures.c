#include "button_gestures.h"

void bg_tick(bg_state_t *s, int button, uint64_t now, int long_ms, int double_ms,
             bool has_long, bg_emit_t emit, void *context)
{
    if (s->pending && !s->down && now - s->up_ms >= (uint64_t)double_ms) {
        s->pending = false;
        emit(button, BG_SINGLE, 0, context);
    }
    if (s->down && has_long && !s->long_sent && now - s->down_ms >= (uint64_t)long_ms) {
        // A first click followed by a hold is a single click plus a long press.
        if (s->pending) { s->pending = false; emit(button, BG_SINGLE, 0, context); }
        s->second = false;
        s->long_sent = true;
        emit(button, BG_LONG_START, (uint32_t)(now - s->down_ms), context);
    }
}

void bg_edge(bg_state_t *s, int button, bool down, uint64_t now,
             int long_ms, int double_ms, bool has_long, bool has_double,
             bg_emit_t emit, void *context)
{
    bg_tick(s, button, now, long_ms, double_ms, has_long, emit, context);
    if (down == s->down) return;
    if (down) {
        s->second = s->pending && now - s->up_ms < (uint64_t)double_ms;
        s->down = true;
        s->long_sent = false;
        s->down_ms = now;
    } else {
        s->down = false;
        if (s->long_sent) emit(button, BG_LONG_END, (uint32_t)(now - s->down_ms), context);
        else if (s->second) {
            s->pending = s->second = false;
            emit(button, BG_DOUBLE, (uint32_t)(now - s->down_ms), context);
        } else if (has_double) { s->pending = true; s->up_ms = now; }
        else emit(button, BG_SINGLE, (uint32_t)(now - s->down_ms), context);
    }
}
