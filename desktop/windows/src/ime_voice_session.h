#pragma once

#include "app_config.h"
#include "ble_protocol.h"
#include <functional>

namespace voicestick {

// Audio belongs to the external input method. Completion means that playback and
// the voice key have ended; it does not mean that text recognition has finished.
class ImeVoiceSession {
public:
    virtual ~ImeVoiceSession() = default;
    virtual void Begin(const AppConfig& config, const std::string& device_id,
                       std::uint32_t session_id) = 0;
    virtual void Push(const AudioFrame& frame) = 0;
    virtual void HandleState(const std::string& device_id, const StateEvent& event) = 0;
    virtual void Finish() = 0;
    virtual void Cancel() = 0;
    std::function<void(std::string, std::uint32_t, std::string)> on_complete;
};

} // namespace voicestick
