#pragma once

#include "voice_stick_coordinator.h"

#include <Windows.h>

#include <string>

namespace voicestick {

class InputInjectorWin : public InputInjector {
public:
    ~InputInjectorWin() override { CancelScript(); }
    void SetClipboardOwner(HWND owner) { clipboard_owner_ = owner; }
    bool Paste(const std::string& text, bool press_enter) override;
    bool SendEnter() override;
    bool SendShortcut(const Shortcut& shortcut) override;
    bool StartScript(const ButtonScript& script, std::function<void(bool)> completion) override;
    bool IsScriptRunning() const override { return !script_.empty(); }
    void CancelScript() override;
    void HandleScriptTimer();
    static constexpr UINT_PTR kScriptTimer = 0x5653;

protected:
    virtual HWND ScriptForegroundWindow() const { return GetForegroundWindow(); }
    virtual bool ScriptModifiersReleased() const;

private:
    static std::wstring Utf16FromUtf8(const std::string& text);
    bool SendCtrlV();
    HWND last_paste_window_ = nullptr;
    HWND clipboard_owner_ = nullptr;
    ButtonScript script_;
    size_t script_index_ = 0;
    HWND script_target_ = nullptr;
    ULONGLONG script_due_ = 0;
    std::function<void(bool)> script_completion_;
};

} // namespace voicestick
