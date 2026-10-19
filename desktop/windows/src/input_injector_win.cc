#include "input_injector_win.h"

namespace voicestick {

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
    INPUT inputs[4]{};
    for (auto& input : inputs) input.type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = inputs[3].ki.wVk = VK_CONTROL;
    inputs[1].ki.wVk = inputs[2].ki.wVk = 'V';
    inputs[2].ki.dwFlags = inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;
    return SendInput(4, inputs, sizeof(INPUT)) == 4;
}

bool InputInjectorWin::SendEnter() {
    if (!last_paste_window_ || !IsWindow(last_paste_window_) ||
        GetForegroundWindow() != last_paste_window_) return false;
    INPUT inputs[2]{};
    for (auto& input : inputs) {
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = VK_RETURN;
    }
    inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
    if (SendInput(2, inputs, sizeof(INPUT)) != 2) return false;
    last_paste_window_ = nullptr;
    return true;
}

} // namespace voicestick
