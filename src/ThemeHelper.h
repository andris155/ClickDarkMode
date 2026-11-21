#pragma once

#include <windows.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <algorithm>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shell32.lib")

struct SystemAccentColors {
    COLORREF baseColor = RGB(0, 120, 212);    // SystemAccentColor (#0078D4)
    COLORREF light2Color = RGB(76, 194, 255);  // SystemAccentColorLight2 (#4CC2FF, for dark theme)
    COLORREF dark1Color = RGB(0, 103, 192);   // SystemAccentColorDark1 (#0067C0, for light theme)

    COLORREF GetForTheme(bool isLight) const {
        return isLight ? dark1Color : light2Color;
    }
};

class ThemeHelper {
public:
    static SystemAccentColors GetAccentColors(bool refresh = false) {
        static SystemAccentColors s_colors;
        static bool s_initialized = false;
        if (!s_initialized || refresh) {
            s_colors = QuerySystemAccentColors();
            s_initialized = true;
        }
        return s_colors;
    }

    static bool IsWindowsAppLightTheme() {
        DWORD val = 1;
        DWORD cb = sizeof(val);
        LONG res = RegGetValueW(
            HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
            L"AppsUseLightTheme",
            RRF_RT_REG_DWORD,
            NULL,
            &val,
            &cb
        );
        if (res == ERROR_SUCCESS) return (val != 0);
        return true;
    }

    static bool IsWindowsSystemLightTheme() {
        DWORD val = 0;
        DWORD cb = sizeof(val);
        LONG res = RegGetValueW(
            HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
            L"SystemUsesLightTheme",
            RRF_RT_REG_DWORD,
            NULL,
            &val,
            &cb
        );
        if (res == ERROR_SUCCESS) return (val != 0);
        return false;
    }

    static bool IsAppInDarkMode(int appTheme) {
        if (appTheme == 1) return true;  // Force Dark
        if (appTheme == 2) return false; // Force Light
        return !IsWindowsAppLightTheme(); // 0 = Follow Windows App
    }

    static bool IsOverlayInDarkMode(int overlayTheme) {
        if (overlayTheme == 1) return true;  // Force Dark
        if (overlayTheme == 2) return false; // Force Light
        return !IsWindowsSystemLightTheme(); // 0 = Follow Windows System
    }

    static bool IsTrayMenuInDarkMode(int trayMenuTheme) {
        if (trayMenuTheme == 1) return true;  // Force Dark
        if (trayMenuTheme == 2) return false; // Force Light
        return !IsWindowsSystemLightTheme(); // 0 = Follow Windows (system)
    }

    static bool IsDoNotDisturbActive() {
        // 1. Check Windows Notification Facility (WNF) for Quiet Hours / Do Not Disturb (Windows 11 / modern Windows 10)
        // WNF_SHEL_QUIETHOURS_ACTIVE_PROFILE_CHANGED = 0xd83063ea3bf1c75ULL
        // Profiles: 0 = Off, 1 = Priority Only (DND On), 2 = Alarms Only (DND On)
        HMODULE hNtDll = GetModuleHandleW(L"ntdll.dll");
        if (hNtDll) {
            typedef LONG(NTAPI* PFN_NtQueryWnfStateData)(const ULONG64*, const void*, const void*, ULONG*, void*, ULONG*);
            auto pfn = (PFN_NtQueryWnfStateData)GetProcAddress(hNtDll, "NtQueryWnfStateData");
            if (pfn) {
                ULONG64 wnfQuietHours = 0xd83063ea3bf1c75ULL;
                DWORD data = 0;
                ULONG size = sizeof(data);
                ULONG stamp = 0;
                if (pfn(&wnfQuietHours, NULL, NULL, &stamp, &data, &size) == 0 && size >= sizeof(DWORD)) {
                    if (data != 0) return true;
                }
            }
        }

        // 2. Fallback to SHQueryUserNotificationState (covers older Windows 10 Quiet Hours / Focus Assist)
        QUERY_USER_NOTIFICATION_STATE qstate = (QUERY_USER_NOTIFICATION_STATE)0;
        if (SUCCEEDED(SHQueryUserNotificationState(&qstate))) {
            if (qstate == QUNS_QUIET_TIME) return true;
        }

        return false;
    }

private:
    static SystemAccentColors QuerySystemAccentColors() {
        SystemAccentColors colors;

        // 1. Primary: Query AccentPalette from Explorer\Accent
        // AccentPalette is an 8-color binary array (32 bytes, each color is R, G, B, 0):
        // Index 1 (offset 4): AccentLight2 (used by Windows 11 in dark mode)
        // Index 3 (offset 12): AccentBase (user-chosen base accent color)
        // Index 4 (offset 16): AccentDark1 (used by Windows 11 in light mode)
        BYTE palette[32] = { 0 };
        DWORD cbData = sizeof(palette);
        LONG res = RegGetValueW(
            HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Accent",
            L"AccentPalette",
            RRF_RT_REG_BINARY,
            NULL,
            palette,
            &cbData
        );
        if (res == ERROR_SUCCESS && cbData >= 20) {
            colors.light2Color = RGB(palette[4], palette[5], palette[6]);
            colors.baseColor   = RGB(palette[12], palette[13], palette[14]);
            colors.dark1Color  = RGB(palette[16], palette[17], palette[18]);
            return colors;
        }

        // 2. Secondary: Query AccentColor from DWM (stored as 0xAABBGGRR)
        DWORD dwmAccent = 0;
        DWORD cbDwm = sizeof(dwmAccent);
        res = RegGetValueW(
            HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\DWM",
            L"AccentColor",
            RRF_RT_REG_DWORD,
            NULL,
            &dwmAccent,
            &cbDwm
        );
        if (res == ERROR_SUCCESS && dwmAccent != 0) {
            COLORREF baseCol = RGB(dwmAccent & 0xFF, (dwmAccent >> 8) & 0xFF, (dwmAccent >> 16) & 0xFF);
            colors.baseColor = baseCol;
            colors.light2Color = Lighten(baseCol, 0.35f);
            colors.dark1Color = Darken(baseCol, 0.15f);
            return colors;
        }

        // 3. Tertiary: DwmGetColorizationColor (stored as 0xAARRGGBB)
        DWORD dwmColor = 0;
        BOOL opaque = FALSE;
        if (SUCCEEDED(DwmGetColorizationColor(&dwmColor, &opaque)) && dwmColor != 0) {
            COLORREF baseCol = RGB((dwmColor >> 16) & 0xFF, (dwmColor >> 8) & 0xFF, dwmColor & 0xFF);
            colors.baseColor = baseCol;
            colors.light2Color = Lighten(baseCol, 0.35f);
            colors.dark1Color = Darken(baseCol, 0.15f);
            return colors;
        }

        return colors;
    }

    static COLORREF Lighten(COLORREF c, float factor) {
        BYTE r = (BYTE)(std::min)(255, (int)(GetRValue(c) + (255 - GetRValue(c)) * factor));
        BYTE g = (BYTE)(std::min)(255, (int)(GetGValue(c) + (255 - GetGValue(c)) * factor));
        BYTE b = (BYTE)(std::min)(255, (int)(GetBValue(c) + (255 - GetBValue(c)) * factor));
        return RGB(r, g, b);
    }

    static COLORREF Darken(COLORREF c, float factor) {
        BYTE r = (BYTE)(GetRValue(c) * (1.0f - factor));
        BYTE g = (BYTE)(GetGValue(c) * (1.0f - factor));
        BYTE b = (BYTE)(GetBValue(c) * (1.0f - factor));
        return RGB(r, g, b);
    }
};
