#pragma once
#include "app_config.h"
#include <Windows.h>

namespace voicestick {
// Edits a draft; the containing Settings dialog persists it on Save.
bool ShowButtonSettings(HWND parent, HINSTANCE instance, AppConfig& config);
}
