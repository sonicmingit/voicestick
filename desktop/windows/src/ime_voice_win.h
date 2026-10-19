#pragma once
#include "ime_voice_session.h"
#include <Windows.h>
#include <memory>

namespace voicestick {
struct ImeAudioEndpoint { std::string id; std::string name; };
std::vector<ImeAudioEndpoint> ListImeAudioEndpoints();
// Public TSF interfaces only. The probe and production path use the same code.
bool ActivateDoubaoIme(std::string* error);
bool IsDoubaoVoiceWindowVisible();
bool SendRightAlt(bool down);
bool SendVoiceShortcut(std::string_view shortcut, bool down);

class ImeVoiceWin final : public ImeVoiceSession {
public:
    using Control = std::function<void(const std::string&, const std::string&)>;
    using Dispatch = std::function<void(std::function<void()>)>;
    ImeVoiceWin(Control control, Dispatch dispatch);
    ~ImeVoiceWin() override;
    void Begin(const AppConfig&, const std::string&, std::uint32_t) override;
    void Push(const AudioFrame&) override;
    void HandleState(const std::string&, const StateEvent&) override;
    void Finish() override;
    void Cancel() override;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace voicestick
