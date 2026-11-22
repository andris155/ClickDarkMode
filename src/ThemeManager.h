#pragma once

#include <string>
#include <windows.h>

namespace ThemeManager {
    bool IsBusy();
    int ReadRegKey(const wchar_t* valueName, int defaultValue);
    bool WriteRegKey(const wchar_t* valueName, DWORD value);
    int GetState(); // -1 = Mixed, 0 = Dark, 1 = Light
    int GetNextToggleState();
    void Toggle(bool fromHotkey = false);
    void SetState(int targetState, bool showOsd = false);
    void OnSettingChange(LPARAM lParam);
    std::wstring GetTooltipText();
}
