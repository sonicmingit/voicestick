#include "button_settings_dialog.h"
#include <CommCtrl.h>
#include <array>
#include <vector>

namespace voicestick {
namespace {
std::wstring Wide(const std::string& text) {
    int n = MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0);
    std::wstring s(n, 0); MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), s.data(), n); return s;
}
std::string Read(HWND hwnd) {
    std::wstring s(GetWindowTextLengthW(hwnd) + 1, 0);
    s.resize(GetWindowTextW(hwnd, s.data(), int(s.size())));
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0, nullptr, nullptr);
    std::string r(n, 0); WideCharToMultiByte(CP_UTF8, 0, s.data(), int(s.size()), r.data(), n, nullptr, nullptr); return r;
}
struct ScriptEditor {
    std::string text;
    HWND edit{};
    static INT_PTR CALLBACK Proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        auto* d = reinterpret_cast<ScriptEditor*>(GetWindowLongPtrW(hwnd, DWLP_USER));
        if (msg == WM_INITDIALOG) {
            d = reinterpret_cast<ScriptEditor*>(lp);
            SetWindowLongPtrW(hwnd, DWLP_USER, lp);
            int dpi = GetDpiForWindow(hwnd);
            auto scale = [dpi](int n) { return MulDiv(n, dpi, 96); };
            RECT rect{0, 0, scale(580), scale(380)};
            AdjustWindowRectExForDpi(&rect, WS_CAPTION | WS_SYSMENU | DS_MODALFRAME, FALSE, 0, dpi);
            SetWindowPos(hwnd, nullptr, 0, 0, rect.right - rect.left, rect.bottom - rect.top, SWP_NOMOVE | SWP_NOZORDER);
            auto control = [&](const wchar_t* type, const wchar_t* title, DWORD style, int id, int x, int y, int w, int h) {
                auto c = CreateWindowExW(0, type, title, WS_CHILD | WS_VISIBLE | style, scale(x), scale(y), scale(w), scale(h), hwnd, reinterpret_cast<HMENU>(INT_PTR(id)), nullptr, nullptr);
                SendMessageW(c, WM_SETFONT, SendMessageW(GetParent(hwnd), WM_GETFONT, 0, 0), TRUE);
                return c;
            };
            control(L"STATIC", L"每行一个快捷键或等待，也可用英文分号分隔。\n例：Ctrl+A → 等待 → Ctrl+C，实现全选后复制。", 0, 0, 20, 16, 540, 48);
            d->edit = control(L"EDIT", Wide(d->text).c_str(), WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL | WS_TABSTOP, 10, 20, 74, 540, 190);
            SendMessageW(d->edit, EM_SETLIMITTEXT, 4096, 0);
            control(L"STATIC", L"等待写法：Wait:100（毫秒）。最多 32 步，单次等待 1–5000 毫秒，\n累计等待最多 10000 毫秒。切换窗口、开始录音或取消会停止脚本。", 0, 0, 20, 276, 540, 45);
            control(L"BUTTON", L"全选后复制示例", WS_TABSTOP, 11, 20, 334, 160, 30);
            control(L"BUTTON", L"确定", BS_DEFPUSHBUTTON | WS_TABSTOP, IDOK, 370, 334, 90, 30);
            control(L"BUTTON", L"取消", WS_TABSTOP, IDCANCEL, 470, 334, 90, 30);
            SetFocus(d->edit);
            return FALSE;
        }
        if (!d) return FALSE;
        if (msg == WM_CLOSE) { EndDialog(hwnd, IDCANCEL); return TRUE; }
        if (msg != WM_COMMAND) return FALSE;
        if (LOWORD(wp) == 11) { SetWindowTextW(d->edit, L"Ctrl+A\r\nWait:100\r\nCtrl+C"); return TRUE; }
        if (LOWORD(wp) == IDCANCEL) { EndDialog(hwnd, IDCANCEL); return TRUE; }
        if (LOWORD(wp) == IDOK) {
            auto script = ParseButtonScript(Read(d->edit));
            if (!script) {
                MessageBoxW(hwnd, L"请检查脚本。每行填写一个快捷键或 Wait:毫秒，至少包含一个快捷键；最多 32 步，单次等待 1–5000 毫秒，累计等待不超过 10000 毫秒。", L"脚本格式无效", MB_OK | MB_ICONINFORMATION);
                return TRUE;
            }
            d->text = FormatButtonScript(*script);
            EndDialog(hwnd, IDOK);
            return TRUE;
        }
        return FALSE;
    }
};
bool EditScript(HWND parent, std::string& text) {
    struct Template { DLGTEMPLATE dialog; WORD menu, window_class; wchar_t title[32]; } t{};
    t.dialog.style = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | DS_CENTER;
    t.dialog.cx = 360; t.dialog.cy = 240;
    wcscpy_s(t.title, L"快捷键脚本");
    ScriptEditor d{text};
    // Win32 multiline edits use CRLF line breaks.
    if (auto script = ParseButtonScript(text)) {
        d.text.clear();
        for (const auto& step : *script) {
            if (!d.text.empty()) d.text += "\r\n";
            d.text += step.shortcut ? FormatShortcut(*step.shortcut) : "Wait:" + std::to_string(step.wait_ms);
        }
    }
    auto result = DialogBoxIndirectParamW(GetModuleHandleW(nullptr), &t.dialog, parent, ScriptEditor::Proc, LPARAM(&d));
    if (result != IDOK) return false;
    text = std::move(d.text);
    return true;
}
struct Dialog {
    AppConfig draft;
    HWND hwnd{}, enabled{}, hold{}, dbl{}, preset{};
    std::array<HWND, 6> combos{}, keys{};
    HFONT font{};
    int dpi = 96, recording_row = -1;
    bool shortcut_captured = false;
    int D(int n) const { return MulDiv(n, dpi, 96); }
    HWND Control(const wchar_t* type, const wchar_t* title, DWORD style, int id, int x, int y, int w, int h) {
        auto c = CreateWindowExW(0, type, title, WS_CHILD | WS_VISIBLE | style, D(x), D(y), D(w), D(h), hwnd, reinterpret_cast<HMENU>(INT_PTR(id)), nullptr, nullptr);
        SendMessageW(c, WM_SETFONT, WPARAM(font), TRUE); return c;
    }
    void Load(const ButtonBindings& b) {
        StopCapture();
        SendMessageW(enabled, BM_SETCHECK, b.enabled ? BST_CHECKED : BST_UNCHECKED, 0);
        SetWindowTextW(hold, std::to_wstring(b.long_press_ms).c_str());
        SetWindowTextW(dbl, std::to_wstring(b.double_click_ms).c_str());
        for (size_t i = 0; i < 6; ++i) {
            size_t index = 0;
            for (size_t j = 0; j < ButtonActions().size(); ++j)
                if (b.actions[i] == ButtonActions()[j].id || (b.actions[i].starts_with("shortcut:") && std::string_view(ButtonActions()[j].id) == "custom") ||
                    (b.actions[i].starts_with("script:") && std::string_view(ButtonActions()[j].id) == "script")) index = j;
            SendMessageW(combos[i], CB_SETCURSEL, index, 0);
            SetWindowTextW(keys[i], b.actions[i].starts_with("shortcut:") ? Wide(b.actions[i].substr(9)).c_str() :
                b.actions[i].starts_with("script:") ? Wide(b.actions[i].substr(7)).c_str() : L"");
            Refresh(i);
        }
    }
    void StopCapture() {
        if (recording_row >= 0) SetWindowTextW(GetDlgItem(hwnd, 200 + recording_row), L"录制");
        recording_row = -1;
        shortcut_captured = false;
    }
    void Refresh(size_t i) {
        auto index = SendMessageW(combos[i], CB_GETCURSEL, 0, 0);
        bool custom = index >= 0 && size_t(index) < ButtonActions().size() && std::string_view(ButtonActions()[index].id) == "custom";
        bool script = index >= 0 && size_t(index) < ButtonActions().size() && std::string_view(ButtonActions()[index].id) == "script";
        EnableWindow(keys[i], custom || script);
        SendMessageW(keys[i], EM_SETREADONLY, script, 0);
        EnableWindow(GetDlgItem(hwnd, 200 + int(i)), custom || script);
        SetWindowTextW(GetDlgItem(hwnd, 200 + int(i)), script ? L"编辑…" : L"录制");
    }
    bool Collect(ButtonBindings& b) {
        b.enabled = SendMessageW(enabled, BM_GETCHECK, 0, 0) == BST_CHECKED;
        b.long_press_ms = GetDlgItemInt(hwnd, 11, nullptr, FALSE);
        b.double_click_ms = GetDlgItemInt(hwnd, 12, nullptr, FALSE);
        for (size_t i = 0; i < 6; ++i) {
            auto index = SendMessageW(combos[i], CB_GETCURSEL, 0, 0);
            if (index < 0 || size_t(index) >= ButtonActions().size()) return false;
            b.actions[i] = ButtonActions()[index].id;
            if (b.actions[i] == "custom") {
                auto parsed = ParseShortcut(Read(keys[i]));
                b.actions[i] = "shortcut:" + (parsed ? FormatShortcut(*parsed) : Read(keys[i]));
            } else if (b.actions[i] == "script") {
                auto parsed = ParseButtonScript(Read(keys[i]));
                b.actions[i] = "script:" + (parsed ? FormatButtonScript(*parsed) : Read(keys[i]));
            }
        }
        auto error = b.Validate();
        if (!error.empty()) { MessageBoxW(hwnd, Wide(error).c_str(), L"请检查按键设置", MB_OK | MB_ICONINFORMATION); return false; }
        return true;
    }
    static LRESULT CALLBACK Capture(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR data) {
        auto* d = reinterpret_cast<Dialog*>(data);
        if (msg == WM_GETDLGCODE && d->recording_row >= 0) return DLGC_WANTALLKEYS;
        if (d->recording_row >= 0 && hwnd == d->keys[d->recording_row] &&
            (msg == WM_CHAR || msg == WM_SYSCHAR || msg == WM_PASTE)) return 0;
        if (d->recording_row >= 0 && hwnd == d->keys[d->recording_row] &&
            (msg == WM_KEYUP || msg == WM_SYSKEYUP) && d->shortcut_captured &&
            wp != VK_CONTROL && wp != VK_SHIFT && wp != VK_MENU && wp != VK_LWIN && wp != VK_RWIN) {
            d->recording_row = -1; d->shortcut_captured = false;
            return 0;
        }
        if (d->recording_row >= 0 && hwnd == d->keys[d->recording_row] && (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN)) {
            if (d->shortcut_captured) return 0;
            if (wp == VK_CONTROL || wp == VK_SHIFT || wp == VK_MENU || wp == VK_LWIN || wp == VK_RWIN) return 0;
            Shortcut s{bool(GetKeyState(VK_CONTROL) & 0x8000), bool(GetKeyState(VK_SHIFT) & 0x8000), bool(GetKeyState(VK_MENU) & 0x8000), bool((GetKeyState(VK_LWIN) | GetKeyState(VK_RWIN)) & 0x8000), std::uint16_t(wp)};
            auto text = FormatShortcut(s);
            if (!text.empty()) {
                SetWindowTextW(hwnd, Wide(text).c_str());
                SetWindowTextW(GetDlgItem(d->hwnd, 200 + d->recording_row), L"录制");
                d->shortcut_captured = true;
            }
            return 0;
        }
        return DefSubclassProc(hwnd, msg, wp, lp);
    }
    void Build() {
        dpi = GetDpiForWindow(hwnd);
        font = CreateFontW(-D(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
        SendMessageW(hwnd, WM_SETFONT, WPARAM(font), FALSE);
        RECT rect{0,0,D(760),D(560)}; AdjustWindowRectExForDpi(&rect, WS_CAPTION | WS_SYSMENU | DS_MODALFRAME, FALSE, 0, dpi);
        SetWindowPos(hwnd, nullptr, 0, 0, rect.right - rect.left, rect.bottom - rect.top, SWP_NOMOVE | SWP_NOZORDER);
        enabled = Control(L"BUTTON", L"启用自定义按键（需支持手势的新固件）", BS_AUTOCHECKBOX | WS_TABSTOP, 10, 20, 16, 710, 26);
        Control(L"STATIC", L"主键 / 侧键各支持单击、双击和长按。旧固件继续使用原有操作，请先升级固件。", 0, 0, 20, 50, 720, 24);
        const wchar_t* names[] = {L"主键 · 单击", L"主键 · 双击", L"主键 · 长按", L"侧键 · 单击", L"侧键 · 双击", L"侧键 · 长按"};
        for (int i = 0; i < 6; ++i) {
            int y = 86 + i * 43;
            Control(L"STATIC", names[i], 0, 0, 20, y + 5, 110, 25);
            combos[i] = Control(WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 100+i, 132, y, 302, 360);
            for (const auto& a : ButtonActions()) SendMessageW(combos[i], CB_ADDSTRING, 0, LPARAM(a.label));
            keys[i] = Control(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, 300+i, 447, y, 190, 27);
            SendMessageW(keys[i], EM_SETLIMITTEXT, 4096, 0);
            SetWindowSubclass(keys[i], Capture, 1, DWORD_PTR(this));
            Control(L"BUTTON", L"录制", WS_TABSTOP, 200+i, 648, y, 80, 28);
        }
        Control(L"STATIC", L"长按阈值（毫秒）", 0, 0, 20, 357, 140, 24);
        hold = Control(L"EDIT", L"", WS_BORDER | ES_NUMBER | WS_TABSTOP, 11, 163, 352, 75, 28);
        Control(L"STATIC", L"双击间隔（毫秒）", 0, 0, 270, 357, 145, 24);
        dbl = Control(L"EDIT", L"", WS_BORDER | ES_NUMBER | WS_TABSTOP, 12, 420, 352, 75, 28);
        Control(L"STATIC", L"500–1500", 0, 0, 163, 385, 100, 20);
        Control(L"STATIC", L"150–500", 0, 0, 420, 385, 100, 20);
        preset = Control(WC_COMBOBOXW, L"默认方案", CBS_DROPDOWN | WS_VSCROLL | WS_TABSTOP, 13, 20, 418, 250, 200);
        SendMessageW(preset, CB_LIMITTEXT, 64, 0);
        for (const auto& [name, b] : draft.button_presets) SendMessageW(preset, CB_ADDSTRING, 0, LPARAM(Wide(name).c_str()));
        Control(L"BUTTON", L"载入预设", WS_TABSTOP, 14, 280, 418, 100, 28);
        Control(L"BUTTON", L"保存为预设", WS_TABSTOP, 15, 390, 418, 110, 28);
        Control(L"BUTTON", L"恢复默认", WS_TABSTOP, 16, 510, 418, 100, 28);
        Control(L"STATIC", L"录音或识别期间仅响应录音控制与取消。撤销作用于目标软件最近一次编辑。\n设置了双击时，单击会等待双击间隔；未设置双击则松开立即触发。", 0, 0, 20, 462, 710, 46);
        Control(L"BUTTON", L"确定", BS_DEFPUSHBUTTON | WS_TABSTOP, IDOK, 540, 518, 90, 30);
        Control(L"BUTTON", L"取消", WS_TABSTOP, IDCANCEL, 640, 518, 90, 30);
        Load(draft.buttons);
    }
    static INT_PTR CALLBACK Proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        auto* d = reinterpret_cast<Dialog*>(GetWindowLongPtrW(hwnd, DWLP_USER));
        if (msg == WM_INITDIALOG) { d = reinterpret_cast<Dialog*>(lp); d->hwnd = hwnd; SetWindowLongPtrW(hwnd, DWLP_USER, lp); d->Build(); return TRUE; }
        if (!d) return FALSE;
        if (msg == WM_CLOSE) { EndDialog(hwnd, IDCANCEL); return TRUE; }
        if (msg != WM_COMMAND) return FALSE;
        int id = LOWORD(wp);
        if (id >= 100 && id < 106 && HIWORD(wp) == CBN_SELCHANGE) { d->StopCapture(); d->Refresh(id - 100); return TRUE; }
        if (id >= 200 && id < 206) {
            d->StopCapture();
            auto index = SendMessageW(d->combos[id - 200], CB_GETCURSEL, 0, 0);
            if (index >= 0 && size_t(index) < ButtonActions().size() && std::string_view(ButtonActions()[index].id) == "script") {
                auto text = Read(d->keys[id - 200]);
                if (EditScript(hwnd, text)) SetWindowTextW(d->keys[id - 200], Wide(text).c_str());
                return TRUE;
            }
            d->recording_row = id - 200; d->shortcut_captured = false; SetWindowTextW(GetDlgItem(hwnd, id), L"请按键…"); SetFocus(d->keys[id - 200]); return TRUE;
        }
        if (id == IDCANCEL) { EndDialog(hwnd, IDCANCEL); return TRUE; }
        if (id == IDOK) { if (d->Collect(d->draft.buttons)) EndDialog(hwnd, IDOK); return TRUE; }
        if (id == 16) { d->Load(ButtonBindings{}); return TRUE; }
        if (id == 14) {
            auto it = d->draft.button_presets.find(Read(d->preset));
            if (it != d->draft.button_presets.end()) d->Load(it->second);
            else MessageBoxW(hwnd, L"请先选择已保存的预设。", L"按键预设", MB_OK);
            return TRUE;
        }
        if (id == 15) {
            auto name = Read(d->preset);
            ButtonBindings b;
            if (name.empty()) MessageBoxW(hwnd, L"请在预设框输入名称。", L"按键预设", MB_OK);
            else if (d->Collect(b)) {
                if (!d->draft.button_presets.contains(name)) SendMessageW(d->preset, CB_ADDSTRING, 0, LPARAM(Wide(name).c_str()));
                d->draft.button_presets[name] = b;
            }
            return TRUE;
        }
        return FALSE;
    }
};
}
bool ShowButtonSettings(HWND parent, HINSTANCE instance, AppConfig& config) {
    struct Template { DLGTEMPLATE dialog; WORD menu, window_class; wchar_t title[32]; } t{};
    t.dialog.style = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | DS_CENTER;
    t.dialog.cx = 460; t.dialog.cy = 350;
    wcscpy_s(t.title, L"按键与快捷键");
    Dialog d; d.draft = config;
    auto result = DialogBoxIndirectParamW(instance, &t.dialog, parent, Dialog::Proc, LPARAM(&d));
    if (d.font) DeleteObject(d.font);
    if (result == IDOK) { config.buttons = d.draft.buttons; config.button_presets = std::move(d.draft.button_presets); return true; }
    return false;
}
}
