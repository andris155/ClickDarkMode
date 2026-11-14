#pragma once

#include <string>
#include <windows.h>

struct AppSettings {
    std::wstring language = L"default";
    int appTheme = 0; // 0 = Follow Windows (app), 1 = Dark, 2 = Light
    int trayMenuTheme = 0; // 0 = Follow Windows (system), 1 = Dark, 2 = Light
    bool transparencyEnabled = true;

    std::wstring target = L"Both"; // "Both", "Apps", "System"
    int toggleDelayMs = 500;
    bool startWithWindows = false;

    bool dualIcons = true;
    std::wstring iconFile = L"sun.ico";
    int iconNumber = 1;
    std::wstring darkIconFile = L"moon.ico";
    int darkIconNumber = 1;

    bool hotkeyEnabled = false;
    std::wstring hotkey = L"^!t";

    bool osdHotkeyEnabled = true;     // Show OSD banner on hotkey toggle (default: true)
    bool osdIconClickEnabled = false;  // Show OSD banner on tray icon click (default: false)
    int osdTimeoutMs = 2000;          // OSD banner duration in ms (default: 2000)
    int osdTheme = 0;                 // 0 = Follow the theme (default), 1 = Dark, 2 = Light

    // Schedule
    int scheduleMode = 0;             // 0 = Disabled, 1 = Custom Time, 2 = Sunrise / Sunset
    int scheduleSunSource = 0;        // 0 = Location service, 1 = Geographic coordinates
    int scheduleLightHour = 7;
    int scheduleLightMinute = 0;
    int scheduleDarkHour = 20;
    int scheduleDarkMinute = 0;
    double scheduleLatitude = 47.4979; // Default: Budapest
    double scheduleLongitude = 19.0402;
    int scheduleSunriseOffset = 0;    // Sunrise offset in minutes (-180 to +180, default: 0)
    int scheduleSunsetOffset = 0;     // Sunset offset in minutes (-180 to +180, default: 0)
    bool scheduleShowOsd = true;      // Show OSD banner on scheduled switch

    void Load();
    void Save();
};

extern AppSettings g_Settings;

namespace Config {
    std::wstring GetIniFilePath();
    bool IsInstalledInProgramFiles();
    bool IsStartWithWindowsEnabled();
    bool SetStartWithWindows(bool enable);
}
