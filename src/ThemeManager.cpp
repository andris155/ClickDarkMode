#include "pch.h"
#include "ThemeManager.h"
#include "TrayIcon.h"
#include "Settings.h"
#include "LocalizationManager.h"
#include "App.xaml.h"
#include "OverlayWindow.h"
#include <thread>
#include <atomic>

namespace ThemeManager {

static const wchar_t* REG_PERSONALIZE = L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize";
static std::atomic<bool> s_isBusy{ false };
static std::atomic<ULONGLONG> s_lastToggleTick{ 0 };

int ReadRegKey(const wchar_t* valueName, int defaultValue) {
    HKEY hKey = NULL;
    LONG lRes = RegOpenKeyExW(HKEY_CURRENT_USER, REG_PERSONALIZE, 0, KEY_READ, &hKey);
    if (lRes != ERROR_SUCCESS) {
        return defaultValue;
    }

    DWORD dwValue = 0;
    DWORD dwType = REG_DWORD;
    DWORD dwSize = sizeof(dwValue);
    lRes = RegQueryValueExW(hKey, valueName, NULL, &dwType, (LPBYTE)&dwValue, &dwSize);
    RegCloseKey(hKey);

    if (lRes == ERROR_SUCCESS && dwType == REG_DWORD) {
        return (int)(dwValue & 1);
    }
    return defaultValue;
}

bool WriteRegKey(const wchar_t* valueName, DWORD value) {
    HKEY hKey = NULL;
    LONG lRes = RegCreateKeyExW(HKEY_CURRENT_USER, REG_PERSONALIZE, 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL);
    if (lRes != ERROR_SUCCESS) {
        return false;
    }

    lRes = RegSetValueExW(hKey, valueName, 0, REG_DWORD, (const BYTE*)&value, sizeof(DWORD));
    RegCloseKey(hKey);
    return (lRes == ERROR_SUCCESS);
}

int GetState() {
    if (g_Settings.target == L"System") {
        return ReadRegKey(L"SystemUsesLightTheme", 1);
    } else if (g_Settings.target == L"Both") {
        int appVal = ReadRegKey(L"AppsUseLightTheme", 1);
        int sysVal = ReadRegKey(L"SystemUsesLightTheme", 1);
        return (appVal != sysVal) ? -1 : appVal;
    } else {
        // "Apps"
        return ReadRegKey(L"AppsUseLightTheme", 1);
    }
}

int GetNextToggleState() {
    int currentState = GetState();
    if (currentState == -1) {
        // Mixed: unify to Dark if apps are currently Light, else Light
        return (ReadRegKey(L"AppsUseLightTheme", 1) == 1) ? 0 : 1;
    }
    return currentState ^ 1;
}

void SetState(int targetState, bool showOsd) {
    if (s_isBusy) return;
    s_isBusy = true;

    if (showOsd) {
        g_OverlayWindow.Show(targetState, g_Settings.target);
    } else {
        g_OverlayWindow.PreArm(targetState ^ 1, g_Settings.target);
    }

    if (g_Settings.target == L"Apps" || g_Settings.target == L"Both") {
        WriteRegKey(L"AppsUseLightTheme", (DWORD)targetState);
    }

    if (g_Settings.target == L"System" || g_Settings.target == L"Both") {
        WriteRegKey(L"SystemUsesLightTheme", (DWORD)targetState);
    }

    TrayIcon::SyncUI();
    OnSettingsThemeChanged();

    // Broadcast theme change notification to all top-level windows asynchronously
    // in a worker thread so the main UI thread never stalls and OSD displays instantly!
    std::thread([targetState]()
    {
        DWORD_PTR dwResult = 0;

        // 1. Notify immersive / modern apps (XAML / WinUI 3)
        SendMessageTimeoutW(
            HWND_BROADCAST,
            WM_SETTINGCHANGE,
            0,
            (LPARAM)L"ImmersiveColorSet",
            SMTO_ABORTIFHUNG | SMTO_NOTIMEOUTIFNOTHUNG,
            1500,
            &dwResult
        );

        // 2. Notify classic theme elements and visual styles
        SendMessageTimeoutW(
            HWND_BROADCAST,
            WM_THEMECHANGED,
            0,
            0,
            SMTO_ABORTIFHUNG | SMTO_NOTIMEOUTIFNOTHUNG,
            1500,
            &dwResult
        );

        // 2b. Notify system color changes (classic Win32 / MFC / Qt palette updates)
        SendMessageTimeoutW(
            HWND_BROADCAST,
            WM_SYSCOLORCHANGE,
            0,
            0,
            SMTO_ABORTIFHUNG | SMTO_NOTIMEOUTIFNOTHUNG,
            1000,
            &dwResult
        );

        // 3. Notify Windows theme elements
        SendMessageTimeoutW(
            HWND_BROADCAST,
            WM_SETTINGCHANGE,
            0,
            (LPARAM)L"WindowsThemeElement",
            SMTO_ABORTIFHUNG | SMTO_NOTIMEOUTIFNOTHUNG,
            1000,
            &dwResult
        );

        // 4. Asynchronous broadcast backup to guarantee delivery across all desktop windows
        SendNotifyMessageW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, (LPARAM)L"ImmersiveColorSet");
        SendNotifyMessageW(HWND_BROADCAST, WM_THEMECHANGED, 0, 0);
        SendNotifyMessageW(HWND_BROADCAST, WM_SYSCOLORCHANGE, 0, 0);

        // 5. Directly target Task Manager (which uses both Win32 command bar and WinUI island)
        EnumWindows([](HWND hWnd, LPARAM) -> BOOL {
            wchar_t className[64] = { 0 };
            if (GetClassNameW(hWnd, className, 64) && _wcsicmp(className, L"TaskManagerWindow") == 0) {
                PostMessageW(hWnd, WM_SETTINGCHANGE, 0, (LPARAM)L"ImmersiveColorSet");
                PostMessageW(hWnd, WM_THEMECHANGED, 0, 0);
                EnumChildWindows(hWnd, [](HWND hChild, LPARAM) -> BOOL {
                    PostMessageW(hChild, WM_SETTINGCHANGE, 0, (LPARAM)L"ImmersiveColorSet");
                    PostMessageW(hChild, WM_THEMECHANGED, 0, 0);
                    InvalidateRect(hChild, NULL, TRUE);
                    return TRUE;
                }, 0);
                RedrawWindow(hWnd, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW | RDW_FRAME);
            }
            return TRUE;
        }, 0);

        s_lastToggleTick = GetTickCount64();
        s_isBusy = false;
    }).detach();
}

void Toggle(bool fromHotkey) {
    ULONGLONG now = GetTickCount64();
    if (s_isBusy || (s_lastToggleTick > 0 && (now - s_lastToggleTick) < (ULONGLONG)g_Settings.toggleDelayMs)) {
        return;
    }

    int nextState = GetNextToggleState();
    bool showOsd = fromHotkey ? g_Settings.osdHotkeyEnabled : g_Settings.osdIconClickEnabled;
    SetState(nextState, showOsd);
}

void OnSettingChange(LPARAM lParam) {
    bool shouldSync = false;
    if (lParam == 0) {
        shouldSync = true;
    } else {
        __try {
            const wchar_t* pStr = (const wchar_t*)lParam;
            if (wcscmp(pStr, L"ImmersiveColorSet") == 0 || wcscmp(pStr, L"WindowsThemeElement") == 0) {
                shouldSync = true;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            shouldSync = true;
        }
    }
    if (shouldSync) {
        TrayIcon::SyncUI();
        OnSettingsThemeChanged();
        g_OverlayWindow.SyncWithSystem();
    }
}

std::wstring GetTooltipText() {
    int state = GetState();
    std::wstring lightStr = g_Loc.Get(L"Theme_Light", L"Light");
    std::wstring darkStr = g_Loc.Get(L"Theme_Dark", L"Dark");

    if (state == -1) {
        std::wstring appStr = (ReadRegKey(L"AppsUseLightTheme", 1) == 0) ? darkStr : lightStr;
        std::wstring sysStr = (ReadRegKey(L"SystemUsesLightTheme", 1) == 0) ? darkStr : lightStr;
        wchar_t buf[256];
        swprintf_s(buf, g_Loc.Get(L"Tip_Mixed", L"Theme: Mixed (Apps %s, Win %s)").c_str(), appStr.c_str(), sysStr.c_str());
        return buf;
    }

    std::wstring modeStr = (state == 0) ? darkStr : lightStr;
    wchar_t buf[256];
    if (g_Settings.target == L"System") {
        swprintf_s(buf, g_Loc.Get(L"Tip_Windows", L"Windows Theme: %s").c_str(), modeStr.c_str());
    } else if (g_Settings.target == L"Both") {
        swprintf_s(buf, g_Loc.Get(L"Tip_Both", L"Theme: %s (All)").c_str(), modeStr.c_str());
    } else {
        swprintf_s(buf, g_Loc.Get(L"Tip_App", L"App Theme: %s").c_str(), modeStr.c_str());
    }
    return buf;
}

} // namespace ThemeManager
