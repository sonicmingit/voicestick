#include "input_injector_win.h"
#include <cassert>

using namespace voicestick;

// Exercise the native timer while substituting all keyboard and foreground access.
// These tests never inject keys or change the real foreground window.
class TestInjector : public InputInjectorWin {
public:
    HWND foreground{};
    bool modifiers_released = true, send_ok = true;
    std::vector<std::string> sent;
    bool SendShortcut(const Shortcut& shortcut) override {
        sent.push_back(FormatShortcut(shortcut));
        return send_ok;
    }
protected:
    HWND ScriptForegroundWindow() const override { return foreground; }
    bool ScriptModifiersReleased() const override { return modifiers_released; }
};

LRESULT CALLBACK Proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE)
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, LONG_PTR(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams));
    auto* injector = reinterpret_cast<TestInjector*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_TIMER && wp == InputInjectorWin::kScriptTimer && injector) {
        injector->HandleScriptTimer();
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void PumpUntil(const std::function<bool()>& done) {
    auto deadline = GetTickCount64() + 2000;
    while (!done() && GetTickCount64() < deadline) {
        MSG msg{};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&msg);
        MsgWaitForMultipleObjectsEx(0, nullptr, 5, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    }
    assert(done());
}

int main() {
    TestInjector injector;
    WNDCLASSW wc{};
    wc.lpfnWndProc = Proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"VoiceStickScriptTest";
    assert(RegisterClassW(&wc));
    auto owner = CreateWindowW(wc.lpszClassName, L"", 0, 0, 0, 1, 1, nullptr, nullptr, wc.hInstance, &injector);
    auto target = CreateWindowW(L"STATIC", L"", 0, 0, 0, 1, 1, nullptr, nullptr, wc.hInstance, nullptr);
    assert(owner && target);
    injector.SetClipboardOwner(owner);
    injector.foreground = target;
    auto script = *ParseButtonScript("Ctrl+A; Wait:100; Ctrl+C");
    int completed = 0;
    bool success = false;
    auto completion = [&](bool ok) { ++completed; success = ok; };
    assert(injector.StartScript(script, completion));
    assert(!injector.StartScript(script, completion));
    PumpUntil([&] { return injector.sent.size() == 1; });
    auto first_at = GetTickCount64();
    injector.HandleScriptTimer(); // The second shortcut must still wait.
    assert(injector.sent.size() == 1);
    PumpUntil([&] { return completed == 1; });
    assert(GetTickCount64() - first_at >= 100);
    assert(success && !injector.IsScriptRunning());
    assert((injector.sent == std::vector<std::string>{"Ctrl+A", "Ctrl+C"}));

    // Foreground changes stop the remaining steps, even during a wait.
    injector.sent.clear();
    assert(injector.StartScript(script, completion));
    PumpUntil([&] { return injector.sent.size() == 1; });
    injector.foreground = owner;
    PumpUntil([&] { return completed == 2; });
    assert(!success && injector.sent.size() == 1 && !injector.IsScriptRunning());
    injector.foreground = target;

    // Explicit cancellation drops completion and queued timer work.
    assert(injector.StartScript(script, completion));
    injector.CancelScript();
    injector.HandleScriptTimer();
    assert(completed == 2 && !injector.IsScriptRunning());

    injector.send_ok = false;
    assert(injector.StartScript(script, completion));
    PumpUntil([&] { return completed == 3; });
    assert(!success && !injector.IsScriptRunning());
    injector.send_ok = true;
    injector.modifiers_released = false;
    assert(!injector.StartScript(script, completion));
    injector.modifiers_released = true;
    assert(injector.StartScript(script, completion));
    injector.modifiers_released = false;
    PumpUntil([&] { return completed == 4; });
    assert(!success);
    injector.modifiers_released = true;
    assert(!injector.StartScript({}, completion));
    assert(injector.StartScript(script, completion));
    DestroyWindow(target);
    PumpUntil([&] { return completed == 5; });
    assert(!success && !injector.IsScriptRunning());
    DestroyWindow(owner);
    injector.SetClipboardOwner(nullptr);
}
