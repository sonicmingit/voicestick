// Exercise the real edit subclass without injecting keys into the user's desktop.
#include "../src/button_settings_dialog.cc"
#include <cassert>

int main() {
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);
    voicestick::Dialog d;
    d.hwnd = CreateWindowExW(0, L"STATIC", L"VoiceStick settings test", WS_OVERLAPPEDWINDOW,
                            0, 0, 800, 600, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    assert(d.hwnd);
    d.Build();
    assert(voicestick::Read(d.hold) == "500");
    // A captured key's translated WM_CHAR must not edit/paste into the recorder.
    d.recording_row = 0;
    SendMessageW(d.keys[0], WM_KEYDOWN, 'V', 0);
    SendMessageW(d.keys[0], WM_CHAR, 22, 0); // Ctrl+V character, queued by TranslateMessage.
    SendMessageW(d.keys[0], WM_CHAR, 'v', 0);
    assert(voicestick::Read(d.keys[0]) == "V");
    SendMessageW(d.keys[0], WM_KEYUP, 'V', 0);
    assert(d.recording_row == -1);
    auto b = voicestick::ButtonBindings{};
    b.actions[1] = "shortcut:Ctrl+Shift+V";
    d.Load(b);
    voicestick::ButtonBindings result;
    assert(d.Collect(result) && result == b);
    DestroyWindow(d.hwnd);
    DeleteObject(d.font);
}
