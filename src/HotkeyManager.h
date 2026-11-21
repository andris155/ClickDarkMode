#pragma once

#include <windows.h>
#include <string>

namespace HotkeyManager {

struct ParsedHotkey {
    UINT vk = 0;
    UINT fsModifiers = 0; // MOD_CONTROL, MOD_ALT, MOD_SHIFT, MOD_WIN
    bool isValid = false;
};

ParsedHotkey ParseAhkHotkey(const std::wstring& ahkKey);
std::wstring FormatAhkHotkey(UINT vk, UINT fsModifiers);
std::wstring FormatPreview(const ParsedHotkey& hk);
std::wstring FormatPreview(bool useWin, const std::wstring& baseHk);
std::wstring GetKeyDisplayName(UINT vk);

bool Register(HWND hWnd = NULL);
void Unregister(HWND hWnd = NULL);
bool IsRegistered();

} // namespace HotkeyManager
