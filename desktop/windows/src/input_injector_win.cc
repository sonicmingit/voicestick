#include "input_injector_win.h"
#include <algorithm>
#include <vector>

namespace voicestick {

bool InputInjectorWin::ScriptModifiersReleased() const {
    for (int key : {VK_CONTROL, VK_SHIFT, VK_MENU, VK_LWIN, VK_RWIN})
        if (GetAsyncKeyState(key) & 0x8000) return false;
    return true;
}

bool InputInjectorWin::StartScript(const ButtonScript& script, std::function<void(bool)> completion) {
    if (IsScriptRunning() || !IsWindow(clipboard_owner_)) return false;
    const auto parsed = ParseButtonScript(FormatButtonScript(script));
    if (!parsed || *parsed != script) return false;
    auto target = ScriptForegroundWindow();
    if (!target || target == clipboard_owner_) return false;
    if (!ScriptModifiersReleased()) return false;
    if (!SetTimer(clipboard_owner_, kScriptTimer, 25, nullptr)) return false;
    script_ = script;
    script_target_ = target;
    script_index_ = 0;
    script_due_ = GetTickCount64();
    script_completion_ = std::move(completion);
    return true;
}

void InputInjectorWin::CancelScript() {
    if (clipboard_owner_) KillTimer(clipboard_owner_, kScriptTimer);
    script_.clear();
    script_index_ = 0;
    script_target_ = nullptr;
    script_completion_ = {};
}

void InputInjectorWin::HandleScriptTimer() {
    if (!IsScriptRunning()) return;
    auto finish = [this](bool ok) {
        auto completion = std::move(script_completion_);
        CancelScript();
        if (completion) completion(ok);
    };
    if (!IsWindow(script_target_) || ScriptForegroundWindow() != script_target_ || !ScriptModifiersReleased()) { finish(false); return; }
    if (GetTickCount64() < script_due_) return;
    if (script_index_ == script_.size()) { finish(true); return; }
    const auto step = script_[script_index_++];
    if (step.shortcut) {
        if (!SendShortcut(*step.shortcut)) { finish(false); return; }
        // Yield to the target application's message loop between shortcuts.
        script_due_ = GetTickCount64() + 25;
    } else script_due_ = GetTickCount64() + step.wait_ms;
}

bool InputInjectorWin::Paste(const std::string& text, bool press_enter) {
    last_paste_window_ = nullptr;
    if (text.empty()) return false;
    const std::wstring wide = Utf16FromUtf8(text);
    if (wide.empty()) return false;
    const HWND target_window = GetForegroundWindow();
    if (!target_window || !IsWindow(clipboard_owner_)) return false;

    bool clipboard_ready = false;
    for (int attempt = 0; attempt < 5; ++attempt) {
        if (!OpenClipboard(clipboard_owner_)) {
            Sleep(10);
            continue;
        }
        const SIZE_T bytes = (wide.size() + 1) * sizeof(wchar_t);
        HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (memory != nullptr) {
            void* target = GlobalLock(memory);
            if (target != nullptr) {
                memcpy(target, wide.c_str(), bytes);
                GlobalUnlock(memory);
                if (EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, memory)) {
                    clipboard_ready = true;
                    memory = nullptr;
                }
            }
            if (memory != nullptr) GlobalFree(memory);
        }
        CloseClipboard();
        break;
    }
    if (!clipboard_ready) return false;

    Sleep(40);
    if (GetForegroundWindow() != target_window || !SendCtrlV()) return false;
    last_paste_window_ = target_window;
    if (press_enter) {
        Sleep(120);
        return SendEnter();
    }
    return true;
}

std::wstring InputInjectorWin::Utf16FromUtf8(const std::string& text) {
    const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (length <= 0) return {};
    std::wstring wide(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), length);
    return wide;
}

bool InputInjectorWin::SendCtrlV() {
    return SendShortcut(Shortcut{true, false, false, false, 'V'});
}

bool InputInjectorWin::SendEnter() {
    if (!last_paste_window_ || !IsWindow(last_paste_window_) ||
        GetForegroundWindow() != last_paste_window_) return false;
    if (!SendShortcut(Shortcut{false, false, false, false, VK_RETURN})) return false;
    last_paste_window_ = nullptr;
    return true;
}

bool InputInjectorWin::SendShortcut(const Shortcut& shortcut) {
    if (FormatShortcut(shortcut).empty() || !GetForegroundWindow()) return false;
    // Do not combine injected shortcuts with physically held modifiers.
    for (int key : {VK_CONTROL, VK_SHIFT, VK_MENU, VK_LWIN, VK_RWIN})
        if (GetAsyncKeyState(key) & 0x8000) return false;
    std::vector<WORD> keys;
    if (shortcut.ctrl) keys.push_back(VK_CONTROL);
    if (shortcut.shift) keys.push_back(VK_SHIFT);
    if (shortcut.alt) keys.push_back(VK_MENU);
    if (shortcut.win) keys.push_back(VK_LWIN);
    keys.push_back(shortcut.key);
    std::vector<INPUT> inputs;
    auto append = [&](WORD key, bool up) {
        INPUT input{}; input.type = INPUT_KEYBOARD; input.ki.wVk = key;
        input.ki.dwFlags = up ? KEYEVENTF_KEYUP : 0;
        if ((key >= VK_PRIOR && key <= VK_DOWN) || key == VK_INSERT || key == VK_DELETE || key == VK_LWIN)
            input.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
        inputs.push_back(input);
    };
    for (auto key : keys) append(key, false);
    for (auto it = keys.rbegin(); it != keys.rend(); ++it) append(*it, true);
    auto sent = SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
    if (sent != inputs.size()) {
        // Release only keys whose down event was accepted, even after a partial send.
        inputs.clear();
        for (size_t i = (std::min)(size_t(sent), keys.size()); i > 0; --i) append(keys[i - 1], true);
        if (!inputs.empty()) SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
        return false;
    }
    return true;
}

} // namespace voicestick
