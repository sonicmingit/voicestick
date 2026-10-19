#pragma once
#include <stdbool.h>
#include <stdint.h>

// A late UP must never release the key held by a later recording. A host must
// renew DOWN at least once every two seconds; its crash cannot leave keys held.
typedef struct { uint32_t session; uint64_t deadline_ms; bool held; } voice_key_lease_t;
static inline uint8_t voice_key_modifiers(uint8_t shortcut) {
    return shortcut == 1 ? 0x40 : shortcut == 2 ? 0x09 : 0;
}
static inline bool voice_key_lease_request(voice_key_lease_t *lease, uint32_t session,
                                          bool down, uint64_t now_ms) {
    if (!session || (lease->held && lease->session != session)) return false;
    if (!down && lease->session && lease->session != session) return false;
    lease->session = session;
    lease->held = down;
    lease->deadline_ms = down ? now_ms + 2000 : 0;
    return true;
}
static inline bool voice_key_lease_expired(const voice_key_lease_t *lease, uint64_t now_ms) {
    return lease->held && now_ms >= lease->deadline_ms;
}
