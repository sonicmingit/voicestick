#pragma once

#include "voice_stick_coordinator.h"

#include <Windows.h>

#include <string>

namespace voicestick {

class InputInjectorWin : public InputInjector {
public:
    void SetClipboardOwner(HWND owner) { clipboard_owner_ = owner; }
    bool Paste(const std::string& text, bool press_enter) override;
    bool SendEnter() override;

private:
    static std::wstring Utf16FromUtf8(const std::string& text);
    static bool SendCtrlV();
    HWND last_paste_window_ = nullptr;
    HWND clipboard_owner_ = nullptr;
};

} // namespace voicestick
