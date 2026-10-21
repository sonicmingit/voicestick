#include "button_bindings.h"

#include <algorithm>
#include <charconv>
#include <cctype>

namespace voicestick {
const std::vector<ButtonActionInfo>& ButtonActions() {
    static const std::vector<ButtonActionInfo> actions{
        {"none", L"不执行操作"}, {"confirm_send", L"确认发送已填入的文字"},
        {"hold_record", L"按住录音，松开结束（仅长按）"}, {"toggle_record", L"开始 / 结束录音"},
        {"cancel", L"取消当前识别"}, {"restore_last", L"重新填入上次识别文字"},
        {"paste", L"粘贴  Ctrl+V"}, {"copy", L"复制  Ctrl+C"}, {"cut", L"剪切  Ctrl+X"},
        {"undo", L"撤销  Ctrl+Z"}, {"redo", L"重做  Ctrl+Y"}, {"select_all", L"全选  Ctrl+A"},
        {"enter", L"回车  Enter"}, {"newline", L"换行  Shift+Enter"}, {"escape", L"Esc"},
        {"custom", L"自定义快捷键…"}, {"script", L"快捷键脚本…"}};
    return actions;
}

std::optional<Shortcut> ParseShortcut(std::string_view text) {
    Shortcut result;
    while (!text.empty()) {
        auto end = text.find('+');
        auto token = std::string(text.substr(0, end));
        std::transform(token.begin(), token.end(), token.begin(), [](unsigned char c) { return char(std::toupper(c)); });
        if (token == "CTRL" && !result.ctrl && !result.key) result.ctrl = true;
        else if (token == "SHIFT" && !result.shift && !result.key) result.shift = true;
        else if (token == "ALT" && !result.alt && !result.key) result.alt = true;
        else if (token == "WIN" && !result.win && !result.key) result.win = true;
        else if (!result.key && end == std::string_view::npos) {
            if (token.size() == 1 && ((token[0] >= 'A' && token[0] <= 'Z') || (token[0] >= '0' && token[0] <= '9'))) result.key = token[0];
            else if (token == "ENTER") result.key = 13;
            else if (token == "ESC") result.key = 27;
            else if (token == "TAB") result.key = 9;
            else if (token == "SPACE") result.key = 32;
            else if (token == "BACKSPACE") result.key = 8;
            else if (token == "DELETE") result.key = 46;
            else if (token == "LEFT") result.key = 37;
            else if (token == "UP") result.key = 38;
            else if (token == "RIGHT") result.key = 39;
            else if (token == "DOWN") result.key = 40;
            else if (token == "HOME") result.key = 36;
            else if (token == "END") result.key = 35;
            else if (token == "PAGEUP") result.key = 33;
            else if (token == "PAGEDOWN") result.key = 34;
            else if (token.size() >= 2 && token[0] == 'F') {
                int n = 0;
                auto [p, ec] = std::from_chars(token.data() + 1, token.data() + token.size(), n);
                if (ec == std::errc{} && p == token.data() + token.size() && n >= 1 && n <= 24) result.key = 111 + n;
            }
            if (!result.key) return std::nullopt;
        } else return std::nullopt;
        if (end == std::string_view::npos) break;
        text.remove_prefix(end + 1);
        if (text.empty()) return std::nullopt;
    }
    return result.key ? std::optional(result) : std::nullopt;
}

std::string FormatShortcut(const Shortcut& s) {
    std::string key;
    if ((s.key >= 'A' && s.key <= 'Z') || (s.key >= '0' && s.key <= '9')) key = char(s.key);
    else if (s.key >= 112 && s.key <= 135) key = "F" + std::to_string(s.key - 111);
    else {
        static const std::pair<int, const char*> names[] = {{13,"Enter"},{27,"Esc"},{9,"Tab"},{32,"Space"},{8,"Backspace"},{46,"Delete"},{37,"Left"},{38,"Up"},{39,"Right"},{40,"Down"},{36,"Home"},{35,"End"},{33,"PageUp"},{34,"PageDown"}};
        for (auto [vk, name] : names) if (s.key == vk) key = name;
    }
    if (key.empty()) return {};
    return std::string(s.ctrl ? "Ctrl+" : "") + (s.shift ? "Shift+" : "") + (s.alt ? "Alt+" : "") + (s.win ? "Win+" : "") + key;
}

std::optional<Shortcut> ShortcutForAction(std::string_view action) {
    if (action.starts_with("shortcut:")) return ParseShortcut(action.substr(9));
    static const std::pair<const char*, const char*> shortcuts[] = {{"paste","Ctrl+V"},{"copy","Ctrl+C"},{"cut","Ctrl+X"},{"undo","Ctrl+Z"},{"redo","Ctrl+Y"},{"select_all","Ctrl+A"},{"enter","Enter"},{"newline","Shift+Enter"},{"escape","Esc"}};
    for (auto [id, keys] : shortcuts) if (action == id) return ParseShortcut(keys);
    return std::nullopt;
}

std::optional<ButtonScript> ParseButtonScript(std::string_view text) {
    if (text.size() > 4096) return std::nullopt;
    ButtonScript steps;
    int total_wait = 0;
    bool has_shortcut = false;
    while (!text.empty()) {
        auto end = text.find_first_of(";\r\n");
        auto token = text.substr(0, end);
        auto first = token.find_first_not_of(" \t");
        if (first != std::string_view::npos) {
            token = token.substr(first, token.find_last_not_of(" \t") - first + 1);
            std::string lower(token);
            std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return char(std::tolower(c)); });
            if (lower.starts_with("wait:")) {
                auto number = std::string_view(lower).substr(5);
                auto start = number.find_first_not_of(" \t");
                if (start == std::string_view::npos) return std::nullopt;
                number.remove_prefix(start);
                int ms = 0;
                auto [p, ec] = std::from_chars(number.data(), number.data() + number.size(), ms);
                if (ec != std::errc{} || p != number.data() + number.size() || ms < 1 || ms > 5000) return std::nullopt;
                total_wait += ms;
                if (total_wait > 10000) return std::nullopt;
                steps.push_back({std::nullopt, ms});
            } else {
                auto shortcut = ParseShortcut(token);
                if (!shortcut) return std::nullopt;
                steps.push_back({shortcut, 0});
                has_shortcut = true;
            }
            if (steps.size() > 32) return std::nullopt;
        }
        if (end == std::string_view::npos) break;
        text.remove_prefix(end + 1);
    }
    return has_shortcut ? std::optional(steps) : std::nullopt;
}

std::string FormatButtonScript(const ButtonScript& script) {
    std::string text;
    for (const auto& step : script) {
        if (!text.empty()) text += "; ";
        text += step.shortcut ? FormatShortcut(*step.shortcut) : "Wait:" + std::to_string(step.wait_ms);
    }
    return text;
}

std::string ButtonBindings::Validate() const {
    if (long_press_ms < 500 || long_press_ms > 1500) return "长按时间须为 500–1500 毫秒。";
    if (double_click_ms < 150 || double_click_ms > 500) return "双击间隔须为 150–500 毫秒。";
    for (size_t i = 0; i < actions.size(); ++i) {
        const auto& action = actions[i];
        if (action == "hold_record" && i % 3 != 2) return "按住录音只能绑定到长按。";
        if (action.starts_with("shortcut:")) {
            if (!ShortcutForAction(action)) return "快捷键格式无效，例如 Ctrl+Shift+V。";
        } else if (action.starts_with("script:")) {
            if (!ParseButtonScript(std::string_view(action).substr(7))) return "脚本格式无效。示例：Ctrl+A; Wait:100; Ctrl+C。最多 32 步，单次等待 1–5000 毫秒，累计等待不超过 10000 毫秒。";
        } else if (action == "custom" || action == "script" || std::none_of(ButtonActions().begin(), ButtonActions().end(), [&](const auto& info) { return action == info.id; })) return "存在未知的按键动作。";
    }
    return {};
}

std::string ButtonBindings::FirmwarePayload(std::uint32_t revision) const {
    std::string map;
    for (const auto& action : actions) map += action == "hold_record" ? '1' : action == "toggle_record" ? '2' : action == "none" ? '0' : '3';
    return "{\"event\":\"buttons\",\"rev\":" + std::to_string(revision) + ",\"long_ms\":" + std::to_string(long_press_ms) + ",\"double_ms\":" + std::to_string(double_click_ms) + ",\"map\":\"" + map + "\"}";
}
} // namespace voicestick
