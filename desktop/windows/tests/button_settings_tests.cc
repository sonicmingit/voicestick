// Exercise the real edit subclass without injecting keys into the user's desktop.
#include "../src/button_settings_dialog.cc"
#include <cassert>
#include <cstdio>

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
    // GetKeyState reads this thread's keyboard state. Isolate the test from
    // modifiers held on the real desktop without injecting or releasing keys.
    BYTE saved_keys[256]{};
    BYTE test_keys[256]{};
    assert(GetKeyboardState(saved_keys));
    assert(SetKeyboardState(test_keys));
    d.recording_row = 0;
    SendMessageW(d.keys[0], WM_KEYDOWN, 'V', 0);
    SendMessageW(d.keys[0], WM_CHAR, 22, 0); // Ctrl+V character, queued by TranslateMessage.
    SendMessageW(d.keys[0], WM_CHAR, 'v', 0);
    if (voicestick::Read(d.keys[0]) != "V")
        std::fprintf(stderr, "captured=%s row=%d accepted=%d ctrl=%d shift=%d alt=%d win=%d\n",
                     voicestick::Read(d.keys[0]).c_str(), d.recording_row, d.shortcut_captured,
                     GetKeyState(VK_CONTROL), GetKeyState(VK_SHIFT), GetKeyState(VK_MENU), GetKeyState(VK_LWIN));
    assert(voicestick::Read(d.keys[0]) == "V");
    SendMessageW(d.keys[0], WM_KEYUP, 'V', 0);
    assert(d.recording_row == -1);
    assert(SetKeyboardState(saved_keys));
    auto b = voicestick::ButtonBindings{};
    b.actions[1] = "shortcut:Ctrl+Shift+V";
    d.Load(b);
    voicestick::ButtonBindings result;
    assert(d.Collect(result) && result == b);
    DestroyWindow(d.hwnd);
    DeleteObject(d.font);
}
