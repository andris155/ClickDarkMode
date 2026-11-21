#include "pch.h"
#include "HotkeyManager.h"
#include "Settings.h"
#include "resource.h"
#include <cwctype>

namespace HotkeyManager {

static std::wstring s_activeHotkeyStr = L"";
static bool s_isRegistered = false;
static HWND s_hBoundWnd = NULL;

std::wstring GetKeyDisplayName(UINT vk) {
    if (vk >= 'A' && vk <= 'Z') {
        return std::wstring(1, (wchar_t)vk);
    }
    if (vk >= '0' && vk <= '9') {
        return std::wstring(1, (wchar_t)vk);
    }
    if (vk >= VK_F1 && vk <= VK_F24) {
        return L"F" + std::to_wstring(vk - VK_F1 + 1);
    }

    switch (vk) {
        case VK_SPACE:   return L"Space";
        case VK_TAB:     return L"Tab";
        case VK_RETURN:  return L"Enter";
        case VK_ESCAPE:  return L"Escape";
        case VK_BACK:    return L"Backspace";
        case VK_DELETE:  return L"Delete";
        case VK_INSERT:  return L"Insert";
        case VK_HOME:    return L"Home";
        case VK_END:     return L"End";
        case VK_PRIOR:   return L"PageUp";
        case VK_NEXT:    return L"PageDown";
        case VK_UP:      return L"Up";
        case VK_DOWN:    return L"Down";
        case VK_LEFT:    return L"Left";
        case VK_RIGHT:   return L"Right";
        case VK_OEM_3:   return L"`";
        case VK_OEM_MINUS: return L"-";
        case VK_OEM_PLUS:  return L"=";
        case VK_OEM_4:   return L"[";
        case VK_OEM_6:   return L"]";
        case VK_OEM_5:   return L"\\";
        case VK_OEM_1:   return L";";
        case VK_OEM_7:   return L"'";
        case VK_OEM_COMMA: return L",";
        case VK_OEM_PERIOD: return L".";
        case VK_OEM_2:   return L"/";
    }

    UINT scanCode = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    wchar_t name[64] = { 0 };
    LONG lParam = (scanCode << 16);
    if (GetKeyNameTextW(lParam, name, 64) > 0) {
        return name;
    }

    return L"Key" + std::to_wstring(vk);
}

static UINT ParseKeyName(const std::wstring& keyName) {
    if (keyName.empty()) return 0;

    if (keyName.length() == 1) {
        wchar_t ch = ::towupper(keyName[0]);
        if ((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')) {
            return (UINT)ch;
        }
        if (ch == '`' || ch == '~') return VK_OEM_3;
        if (ch == '-' || ch == '_') return VK_OEM_MINUS;
        if (ch == '=' || ch == '+') return VK_OEM_PLUS;
        if (ch == '[' || ch == '{') return VK_OEM_4;
        if (ch == ']' || ch == '}') return VK_OEM_6;
        if (ch == '\\' || ch == '|') return VK_OEM_5;
        if (ch == ';' || ch == ':') return VK_OEM_1;
        if (ch == '\'' || ch == '"') return VK_OEM_7;
        if (ch == ',' || ch == '<') return VK_OEM_COMMA;
        if (ch == '.' || ch == '>') return VK_OEM_PERIOD;
        if (ch == '/' || ch == '?') return VK_OEM_2;
    }

    std::wstring lower = keyName;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);

    if (lower[0] == L'f') {
        try {
            int fNum = std::stoi(lower.substr(1));
            if (fNum >= 1 && fNum <= 24) {
                return VK_F1 + (fNum - 1);
            }
        } catch (...) {}
    }

    if (lower == L"space") return VK_SPACE;
    if (lower == L"tab") return VK_TAB;
    if (lower == L"enter" || lower == L"return") return VK_RETURN;
    if (lower == L"esc" || lower == L"escape") return VK_ESCAPE;
    if (lower == L"backspace" || lower == L"bs") return VK_BACK;
    if (lower == L"delete" || lower == L"del") return VK_DELETE;
    if (lower == L"insert" || lower == L"ins") return VK_INSERT;
    if (lower == L"home") return VK_HOME;
    if (lower == L"end") return VK_END;
    if (lower == L"pgup" || lower == L"pageup") return VK_PRIOR;
    if (lower == L"pgdn" || lower == L"pagedown") return VK_NEXT;
    if (lower == L"up") return VK_UP;
    if (lower == L"down") return VK_DOWN;
    if (lower == L"left") return VK_LEFT;
    if (lower == L"right") return VK_RIGHT;

    return 0;
}

ParsedHotkey ParseAhkHotkey(const std::wstring& ahkKey) {
    ParsedHotkey result;
    size_t i = 0;
    while (i < ahkKey.length()) {
        wchar_t ch = ahkKey[i];
        if (ch == L'^') {
            result.fsModifiers |= MOD_CONTROL;
            i++;
        } else if (ch == L'!') {
            result.fsModifiers |= MOD_ALT;
            i++;
        } else if (ch == L'+') {
            result.fsModifiers |= MOD_SHIFT;
            i++;
        } else if (ch == L'#') {
            result.fsModifiers |= MOD_WIN;
            i++;
        } else {
            break;
        }
    }

    std::wstring keyPart = ahkKey.substr(i);
    result.vk = ParseKeyName(keyPart);
    result.isValid = (result.vk != 0);
    return result;
}

std::wstring FormatAhkHotkey(UINT vk, UINT fsModifiers) {
    std::wstring str;
    if (fsModifiers & MOD_WIN)     str += L"#";
    if (fsModifiers & MOD_CONTROL) str += L"^";
    if (fsModifiers & MOD_ALT)     str += L"!";
    if (fsModifiers & MOD_SHIFT)   str += L"+";

    if (vk >= 'A' && vk <= 'Z') {
        str += (wchar_t)::towlower((wchar_t)vk);
    } else if (vk >= '0' && vk <= '9') {
        str += (wchar_t)vk;
    } else if (vk >= VK_F1 && vk <= VK_F24) {
        str += L"F" + std::to_wstring(vk - VK_F1 + 1);
    } else {
        str += GetKeyDisplayName(vk);
    }
    return str;
}

std::wstring FormatPreview(const ParsedHotkey& hk) {
    if (!hk.isValid || hk.vk == 0) {
        return L"(None)";
    }

    std::wstring preview;
    if (hk.fsModifiers & MOD_WIN)     preview += L"Win + ";
    if (hk.fsModifiers & MOD_CONTROL) preview += L"Ctrl + ";
    if (hk.fsModifiers & MOD_ALT)     preview += L"Alt + ";
    if (hk.fsModifiers & MOD_SHIFT)   preview += L"Shift + ";

    preview += GetKeyDisplayName(hk.vk);
    return preview;
}

std::wstring FormatPreview(bool useWin, const std::wstring& baseHk) {
    std::wstring full = (useWin ? L"#" : L"") + baseHk;
    ParsedHotkey parsed = ParseAhkHotkey(full);
    return FormatPreview(parsed);
}

bool Register(HWND hWnd) {
    if (hWnd) s_hBoundWnd = hWnd;
    if (!s_hBoundWnd) {
        s_hBoundWnd = FindWindowW(L"ClickDarkMode_MessageWndClass", NULL);
    }
    HWND target = s_hBoundWnd;
    if (!target) return false;

    Unregister(target);

    if (!g_Settings.hotkeyEnabled || g_Settings.hotkey.empty()) {
        return false;
    }

    ParsedHotkey hk = ParseAhkHotkey(g_Settings.hotkey);
    if (!hk.isValid) {
        return false;
    }

    BOOL res = RegisterHotKey(target, ID_HOTKEY_TOGGLE, hk.fsModifiers | MOD_NOREPEAT, hk.vk);
    if (!res) {
        res = RegisterHotKey(target, ID_HOTKEY_TOGGLE, hk.fsModifiers, hk.vk);
    }

    if (res) {
        s_isRegistered = true;
        s_activeHotkeyStr = g_Settings.hotkey;
        return true;
    }
    return false;
}

void Unregister(HWND hWnd) {
    HWND target = hWnd ? hWnd : s_hBoundWnd;
    if (!target) {
        target = FindWindowW(L"ClickDarkMode_MessageWndClass", NULL);
    }
    if (s_isRegistered && target) {
        UnregisterHotKey(target, ID_HOTKEY_TOGGLE);
        s_isRegistered = false;
        s_activeHotkeyStr = L"";
    }
}

bool IsRegistered() {
    return s_isRegistered;
}

} // namespace HotkeyManager
