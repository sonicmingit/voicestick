#include "ime_voice_win.h"
#include <opus.h>
#include <Windows.h>
#include <objbase.h>
#include <atomic>
#include <cmath>
#include <fstream>
#include <iostream>
#include <thread>

using namespace voicestick;
namespace {
HWND edit, status, shortcut_combo;
std::thread probe;
std::atomic_bool stopping{false};
std::wstring result = L"尚未测试";
std::wstring result_path;
bool SelfTest() {
    int error = 0;
    auto* encoder = opus_encoder_create(16000, 1, OPUS_APPLICATION_VOIP, &error);
    auto* decoder = opus_decoder_create(16000, 1, &error);
    if (!encoder || !decoder) return false;
    opus_encoder_ctl(encoder, OPUS_SET_BITRATE(20000));
    opus_encoder_ctl(encoder, OPUS_SET_VBR(0));
    opus_int16 input[960], output[1920]; unsigned char packet[220];
    long long energy = 0;
    for (int frame = 0; frame < 20; ++frame) {
        for (int i = 0; i < 960; ++i)
            input[i] = (opus_int16)(6000 * std::sin(2 * 3.141592653589793 * 440 * (frame * 960 + i) / 16000));
        int size = opus_encode(encoder, input, 960, packet, sizeof(packet));
        int n = size > 0 ? opus_decode(decoder, packet, size, output, 1920, 0) : -1;
        if (n != 960) { opus_encoder_destroy(encoder); opus_decoder_destroy(decoder); return false; }
        for (int i = 0; i < n; ++i) energy += (long long)output[i] * output[i];
    }
    opus_encoder_destroy(encoder); opus_decoder_destroy(decoder);
    return energy > 1000000;
}
LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
    switch (message) {
    case WM_CREATE:
        CreateWindowW(L"STATIC", L"此窗口仅用于测试，不会发送聊天消息。\n请先聚焦下方输入框；测试会短暂尝试开启豆包语音。",
            WS_CHILD | WS_VISIBLE, 16, 12, 600, 46, hwnd, nullptr, nullptr, nullptr);
        edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_MULTILINE,
            16, 65, 600, 130, hwnd, nullptr, nullptr, nullptr);
        CreateWindowW(L"BUTTON", L"测试模拟语音键", WS_CHILD | WS_VISIBLE,
            16, 210, 180, 32, hwnd, (HMENU)1, nullptr, nullptr);
        shortcut_combo = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
            210, 210, 320, 140, hwnd, (HMENU)2, nullptr, nullptr);
        SendMessageW(shortcut_combo, CB_ADDSTRING, 0, (LPARAM)L"左 Ctrl + 左 Win（长按）");
        SendMessageW(shortcut_combo, CB_ADDSTRING, 0, (LPARAM)L"右 Alt（长按）");
        SendMessageW(shortcut_combo, CB_SETCURSEL, 0, 0);
        status = CreateWindowW(L"STATIC", L"等待测试", WS_CHILD | WS_VISIBLE,
            16, 260, 600, 65, hwnd, nullptr, nullptr, nullptr);
        break;
    case WM_COMMAND:
        if (LOWORD(wp) == 1) {
            if (probe.joinable()) probe.join();
            SetFocus(edit);
            SetWindowTextW(status, L"正在切换豆包并测试模拟按键…");
            EnableWindow(GetDlgItem(hwnd, 1), FALSE);
            const std::string shortcut = SendMessageW(shortcut_combo, CB_GETCURSEL, 0, 0) == 1 ? "right_alt" : "left_ctrl_win";
            probe = std::thread([hwnd, shortcut] {
                std::string error;
                bool activated = ActivateDoubaoIme(&error);
                bool visible_before = IsDoubaoVoiceWindowVisible();
                bool down = activated && !visible_before && SendVoiceShortcut(shortcut, true);
                bool opened = false;
                for (int i = 0; i < 20 && !stopping.load(); ++i) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    opened |= IsDoubaoVoiceWindowVisible();
                }
                bool released = !down || SendVoiceShortcut(shortcut, false);
                result = !activated ? L"豆包激活失败，请先启用豆包输入法后重试。" :
                    visible_before ? L"测试前豆包已在录音，请结束后重试。" :
                    !down || !released ? L"按键发送失败。" :
                    opened ? L"已观察到豆包语音窗口：模拟按键可能可用，仍需实际出字验证。" :
                    L"模拟按键已发送，但未观察到豆包语音窗口。请用物理键盘对照。";
                if (!result_path.empty()) {
                    std::ofstream out(result_path);
                    out << "{\"shortcut\":\"" << shortcut << "\",\"ime_activated\":" << activated << ",\"voice_visible_before\":" << visible_before
                        << ",\"key_down_sent\":" << down << ",\"key_up_sent\":" << released
                        << ",\"voice_window_observed\":" << opened << "}\n";
                }
                PostMessageW(hwnd, WM_APP + 1, 0, 0);
            });
        }
        break;
    case WM_APP + 1:
        if (probe.joinable()) probe.join();
        SetWindowTextW(status, result.c_str());
        EnableWindow(GetDlgItem(hwnd, 1), TRUE);
        break;
    case WM_DESTROY:
        stopping = true;
        if (probe.joinable()) probe.join();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, message, wp, lp);
}
}
int wmain(int argc, wchar_t** argv) {
    if (argc > 1 && wcscmp(argv[1], L"--self-test") == 0) {
        bool ok = SelfTest(); std::cout << "Opus 16kHz mono 60ms encode/decode: " << (ok ? "passed" : "failed") << '\n';
        return ok ? 0 : 1;
    }
    if (argc > 1 && wcscmp(argv[1], L"--list-audio") == 0) {
        try { std::cout << "virtual_cable_render_endpoints=" << ListImeAudioEndpoints().size() << '\n'; return 0; }
        catch (const std::exception& e) { std::cout << e.what() << '\n'; return 1; }
    }
    if (argc > 2 && wcscmp(argv[1], L"--result") == 0) result_path = argv[2];
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    WNDCLASSW klass{}; klass.lpfnWndProc = WindowProc; klass.hInstance = GetModuleHandleW(nullptr);
    klass.lpszClassName = L"VoiceStickImeProbe"; klass.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassW(&klass);
    HWND window = CreateWindowW(klass.lpszClassName, L"VoiceStick 豆包接入测试", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 660, 390, nullptr, nullptr, klass.hInstance, nullptr);
    ShowWindow(window, SW_SHOW);
    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0) > 0) { TranslateMessage(&message); DispatchMessageW(&message); }
    CoUninitialize();
}
