#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace voicestick {

struct ButtonActionInfo { const char* id; const wchar_t* label; };
const std::vector<ButtonActionInfo>& ButtonActions();
// Portable Windows virtual-key values; parsing never executes a shortcut.
struct Shortcut {
    bool ctrl = false, shift = false, alt = false, win = false;
    std::uint16_t key = 0;
    bool operator==(const Shortcut&) const = default;
};
std::optional<Shortcut> ParseShortcut(std::string_view text);
std::string FormatShortcut(const Shortcut& shortcut);
std::optional<Shortcut> ShortcutForAction(std::string_view action);

// A script contains shortcuts and explicit waits, separated by lines or semicolons.
struct ButtonScriptStep {
    std::optional<Shortcut> shortcut;
    int wait_ms = 0;
    bool operator==(const ButtonScriptStep&) const = default;
};
using ButtonScript = std::vector<ButtonScriptStep>;
std::optional<ButtonScript> ParseButtonScript(std::string_view text);
std::string FormatButtonScript(const ButtonScript& script);

struct ButtonBindings {
    bool enabled = true;
    int long_press_ms = 500;
    int double_click_ms = 300;
    // primary single/double/long, secondary single/double/long
    std::array<std::string, 6> actions{"confirm_send", "none", "hold_record", "undo", "redo", "paste"};
    bool operator==(const ButtonBindings&) const = default;
    std::string Validate() const;
    std::string FirmwarePayload(std::uint32_t revision) const;
};

} // namespace voicestick
