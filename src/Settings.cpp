#include "pch.h"
#include "Settings.h"
#include "LocalizationManager.h"

AppSettings g_Settings;

namespace Config {

std::wstring GetIniFilePath() {
    // If running in portable mode (settings.ini or portable.txt exists next to exe), keep settings app-local
    wchar_t exePath[MAX_PATH] = { 0 };
    DWORD len = GetModuleFileNameW(NULL, exePath, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
        std::wstring exeDir(exePath);
        size_t slash = exeDir.find_last_of(L"\\/");
        if (slash != std::wstring::npos) {
            exeDir = exeDir.substr(0, slash);
        }
        std::wstring localIni = exeDir + L"\\settings.ini";
        std::wstring portableMarker = exeDir + L"\\portable.txt";

        if (GetFileAttributesW(localIni.c_str()) != INVALID_FILE_ATTRIBUTES ||
            GetFileAttributesW(portableMarker.c_str()) != INVALID_FILE_ATTRIBUTES) {
            return localIni;
        }
    }

    PWSTR pKnownPath = NULL;
    std::wstring appDataDir;

    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, NULL, &pKnownPath))) {
        appDataDir = std::wstring(pKnownPath) + L"\\ClickDarkMode";
        CoTaskMemFree(pKnownPath);
    } else {
        wchar_t envBuf[MAX_PATH] = { 0 };
        if (GetEnvironmentVariableW(L"LOCALAPPDATA", envBuf, MAX_PATH) > 0) {
            appDataDir = std::wstring(envBuf) + L"\\ClickDarkMode";
        } else {
            appDataDir = L".";
        }
    }

    CreateDirectoryW(appDataDir.c_str(), NULL);
    return appDataDir + L"\\settings.ini";
}

bool IsInstalledInProgramFiles() {
    wchar_t exePath[MAX_PATH] = { 0 };
    DWORD len = GetModuleFileNameW(NULL, exePath, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return false;

    auto checkFolder = [&](REFKNOWNFOLDERID rfid) -> bool {
        PWSTR pPath = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(rfid, 0, NULL, &pPath)) && pPath) {
            std::wstring folder = pPath;
            CoTaskMemFree(pPath);
            if (!folder.empty() && folder.back() != L'\\') folder += L'\\';
            if (folder.length() <= wcslen(exePath) && _wcsnicmp(exePath, folder.c_str(), folder.length()) == 0) {
                return true;
            }
        }
        return false;
    };

    if (checkFolder(FOLDERID_ProgramFiles)) return true;
    if (checkFolder(FOLDERID_ProgramFilesX86)) return true;
    if (checkFolder(FOLDERID_ProgramFilesX64)) return true;

    // Fallback: check environment variables ProgramFiles, ProgramFiles(x86), ProgramW6432
    wchar_t envBuf[MAX_PATH] = { 0 };
    if (GetEnvironmentVariableW(L"ProgramFiles", envBuf, MAX_PATH) > 0) {
        std::wstring pf = envBuf;
        if (!pf.empty() && pf.back() != L'\\') pf += L'\\';
        if (pf.length() <= wcslen(exePath) && _wcsnicmp(exePath, pf.c_str(), pf.length()) == 0) return true;
    }
    if (GetEnvironmentVariableW(L"ProgramFiles(x86)", envBuf, MAX_PATH) > 0) {
        std::wstring pf = envBuf;
        if (!pf.empty() && pf.back() != L'\\') pf += L'\\';
        if (pf.length() <= wcslen(exePath) && _wcsnicmp(exePath, pf.c_str(), pf.length()) == 0) return true;
    }
    if (GetEnvironmentVariableW(L"ProgramW6432", envBuf, MAX_PATH) > 0) {
        std::wstring pf = envBuf;
        if (!pf.empty() && pf.back() != L'\\') pf += L'\\';
        if (pf.length() <= wcslen(exePath) && _wcsnicmp(exePath, pf.c_str(), pf.length()) == 0) return true;
    }

    return false;
}

bool IsStartWithWindowsEnabled() {
    HKEY hKey = NULL;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        wchar_t path[MAX_PATH] = { 0 };
        DWORD size = sizeof(path);
        DWORD type = REG_SZ;
        LONG res = RegQueryValueExW(hKey, L"ClickDarkMode", NULL, &type, (LPBYTE)path, &size);
        RegCloseKey(hKey);
        return (res == ERROR_SUCCESS && wcslen(path) > 0);
    }
    return false;
}

bool SetStartWithWindows(bool enable) {
    if (!IsInstalledInProgramFiles()) {
        return false;
    }

    HKEY hKey = NULL;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
        if (enable) {
            wchar_t exePath[MAX_PATH] = { 0 };
            GetModuleFileNameW(NULL, exePath, MAX_PATH);
            std::wstring quoted = L"\"" + std::wstring(exePath) + L"\"";
            LONG res = RegSetValueExW(hKey, L"ClickDarkMode", 0, REG_SZ,
                (const BYTE*)quoted.c_str(), (DWORD)((quoted.length() + 1) * sizeof(wchar_t)));
            RegCloseKey(hKey);
            return (res == ERROR_SUCCESS);
        } else {
            RegDeleteValueW(hKey, L"ClickDarkMode");
            RegCloseKey(hKey);
            return true;
        }
    }
    return false;
}

} // namespace Config

void AppSettings::Load() {
    std::wstring ini = Config::GetIniFilePath();
    wchar_t buf[MAX_PATH] = { 0 };

    // Language
    GetPrivateProfileStringW(L"Settings", L"Language", L"default", buf, MAX_PATH, ini.c_str());
    language = (wcslen(buf) > 0) ? buf : L"default";
    g_Loc.SetLanguage(language);

    // AppTheme (0 = Follow Windows (app), 1 = Dark, 2 = Light)
    appTheme = (int)GetPrivateProfileIntW(L"Settings", L"AppTheme", 0, ini.c_str());
    if (appTheme < 0 || appTheme > 2) appTheme = 0;

    // TrayMenuTheme (0 = Follow Windows (system), 1 = Dark, 2 = Light)
    trayMenuTheme = (int)GetPrivateProfileIntW(L"Settings", L"TrayMenuTheme", 0, ini.c_str());
    if (trayMenuTheme < 0 || trayMenuTheme > 2) trayMenuTheme = 0;

    // TransparencyEnabled (default: 1)
    transparencyEnabled = (GetPrivateProfileIntW(L"Settings", L"TransparencyEnabled", 1, ini.c_str()) != 0);

    // Target ("Both", "Apps", "System")
    GetPrivateProfileStringW(L"Settings", L"Target", L"Both", buf, MAX_PATH, ini.c_str());
    if (_wcsicmp(buf, L"Apps") == 0 || _wcsicmp(buf, L"System") == 0 || _wcsicmp(buf, L"Both") == 0) {
        target = buf;
    } else {
        target = L"Both";
    }

    // ToggleDelayMs
    int delay = (int)GetPrivateProfileIntW(L"Settings", L"ToggleDelayMs", 500, ini.c_str());
    toggleDelayMs = (std::max)(0, delay);

    // StartWithWindows
    bool regStartup = Config::IsStartWithWindowsEnabled();
    int iniStartup = (int)GetPrivateProfileIntW(L"Settings", L"StartWithWindows", -1, ini.c_str());
    if (iniStartup != -1) {
        startWithWindows = (iniStartup == 1);
    } else {
        startWithWindows = regStartup;
    }

    // TrayIconMode
    trayIconMode = (int)GetPrivateProfileIntW(L"Settings", L"TrayIconMode", 0, ini.c_str());
    if (trayIconMode < 0 || trayIconMode > 2) trayIconMode = 0;

    // DualIcons
    dualIcons = (GetPrivateProfileIntW(L"Settings", L"DualIcons", 1, ini.c_str()) == 1);

    // IconFile & IconNumber
    GetPrivateProfileStringW(L"Settings", L"IconFile", L"sun.ico", buf, MAX_PATH, ini.c_str());
    iconFile = (wcslen(buf) > 0) ? buf : L"sun.ico";
    iconNumber = (int)GetPrivateProfileIntW(L"Settings", L"IconNumber", 1, ini.c_str());

    // DarkIconFile & DarkIconNumber
    GetPrivateProfileStringW(L"Settings", L"DarkIconFile", L"moon.ico", buf, MAX_PATH, ini.c_str());
    darkIconFile = (wcslen(buf) > 0) ? buf : L"moon.ico";
    darkIconNumber = (int)GetPrivateProfileIntW(L"Settings", L"DarkIconNumber", 1, ini.c_str());

    // Hotkey & HotkeyEnabled
    GetPrivateProfileStringW(L"Settings", L"Hotkey", L"^!t", buf, MAX_PATH, ini.c_str());
    hotkey = (wcslen(buf) > 0) ? buf : L"^!t";
    hotkeyEnabled = (GetPrivateProfileIntW(L"Settings", L"HotkeyEnabled", 0, ini.c_str()) == 1);

    // OSD Settings
    osdHotkeyEnabled = (GetPrivateProfileIntW(L"Settings", L"ShowOsdOnHotkey", 1, ini.c_str()) == 1);
    osdIconClickEnabled = (GetPrivateProfileIntW(L"Settings", L"ShowOsdOnTrayClick", 0, ini.c_str()) == 1);
    osdTimeoutMs = (int)GetPrivateProfileIntW(L"Settings", L"OsdTimeoutMs", 2000, ini.c_str());
    if (osdTimeoutMs < 200) osdTimeoutMs = 2000;
    osdTheme = (int)GetPrivateProfileIntW(L"Settings", L"OsdTheme", 0, ini.c_str());
    if (osdTheme < 0 || osdTheme > 2) osdTheme = 0;

    // Schedule Settings
    scheduleMode = (int)GetPrivateProfileIntW(L"Schedule", L"Mode", 0, ini.c_str());
    if (scheduleMode < 0 || scheduleMode > 2) scheduleMode = 0;
    scheduleSunSource = (int)GetPrivateProfileIntW(L"Schedule", L"SunSource", 0, ini.c_str());
    if (scheduleSunSource < 0 || scheduleSunSource > 1) scheduleSunSource = 0;
    scheduleLightHour = (int)GetPrivateProfileIntW(L"Schedule", L"LightHour", 7, ini.c_str());
    scheduleLightMinute = (int)GetPrivateProfileIntW(L"Schedule", L"LightMinute", 0, ini.c_str());
    scheduleDarkHour = (int)GetPrivateProfileIntW(L"Schedule", L"DarkHour", 20, ini.c_str());
    scheduleDarkMinute = (int)GetPrivateProfileIntW(L"Schedule", L"DarkMinute", 0, ini.c_str());

    wchar_t latBuf[64] = { 0 };
    GetPrivateProfileStringW(L"Schedule", L"Latitude", L"47.4979", latBuf, 64, ini.c_str());
    try { scheduleLatitude = std::stod(latBuf); } catch (...) { scheduleLatitude = 47.4979; }

    wchar_t lonBuf[64] = { 0 };
    GetPrivateProfileStringW(L"Schedule", L"Longitude", L"19.0402", lonBuf, 64, ini.c_str());
    try { scheduleLongitude = std::stod(lonBuf); } catch (...) { scheduleLongitude = 19.0402; }

    wchar_t offsetBuf[32] = { 0 };
    GetPrivateProfileStringW(L"Schedule", L"SunriseOffset", L"0", offsetBuf, 32, ini.c_str());
    try { scheduleSunriseOffset = std::stoi(offsetBuf); } catch (...) { scheduleSunriseOffset = 0; }
    if (scheduleSunriseOffset < -180 || scheduleSunriseOffset > 180) scheduleSunriseOffset = 0;

    GetPrivateProfileStringW(L"Schedule", L"SunsetOffset", L"0", offsetBuf, 32, ini.c_str());
    try { scheduleSunsetOffset = std::stoi(offsetBuf); } catch (...) { scheduleSunsetOffset = 0; }
    if (scheduleSunsetOffset < -180 || scheduleSunsetOffset > 180) scheduleSunsetOffset = 0;

    scheduleShowOsd = (GetPrivateProfileIntW(L"Schedule", L"ShowOsd", 1, ini.c_str()) != 0);
}

void AppSettings::Save() {
    std::wstring ini = Config::GetIniFilePath();

    WritePrivateProfileStringW(L"Settings", L"Language", language.c_str(), ini.c_str());
    WritePrivateProfileStringW(L"Settings", L"AppTheme", std::to_wstring(appTheme).c_str(), ini.c_str());
    WritePrivateProfileStringW(L"Settings", L"TrayMenuTheme", std::to_wstring(trayMenuTheme).c_str(), ini.c_str());
    WritePrivateProfileStringW(L"Settings", L"TransparencyEnabled", transparencyEnabled ? L"1" : L"0", ini.c_str());
    WritePrivateProfileStringW(L"Settings", L"Target", target.c_str(), ini.c_str());
    WritePrivateProfileStringW(L"Settings", L"ToggleDelayMs", std::to_wstring(toggleDelayMs).c_str(), ini.c_str());

    WritePrivateProfileStringW(L"Settings", L"TrayIconMode", std::to_wstring(trayIconMode).c_str(), ini.c_str());
    WritePrivateProfileStringW(L"Settings", L"DualIcons", dualIcons ? L"1" : L"0", ini.c_str());
    WritePrivateProfileStringW(L"Settings", L"IconFile", iconFile.c_str(), ini.c_str());
    WritePrivateProfileStringW(L"Settings", L"IconNumber", std::to_wstring(iconNumber).c_str(), ini.c_str());
    WritePrivateProfileStringW(L"Settings", L"DarkIconFile", darkIconFile.c_str(), ini.c_str());
    WritePrivateProfileStringW(L"Settings", L"DarkIconNumber", std::to_wstring(darkIconNumber).c_str(), ini.c_str());

    WritePrivateProfileStringW(L"Settings", L"Hotkey", hotkey.c_str(), ini.c_str());
    WritePrivateProfileStringW(L"Settings", L"HotkeyEnabled", hotkeyEnabled ? L"1" : L"0", ini.c_str());

    WritePrivateProfileStringW(L"Settings", L"ShowOsdOnHotkey", osdHotkeyEnabled ? L"1" : L"0", ini.c_str());
    WritePrivateProfileStringW(L"Settings", L"ShowOsdOnTrayClick", osdIconClickEnabled ? L"1" : L"0", ini.c_str());
    WritePrivateProfileStringW(L"Settings", L"OsdTimeoutMs", std::to_wstring(osdTimeoutMs).c_str(), ini.c_str());
    WritePrivateProfileStringW(L"Settings", L"OsdTheme", std::to_wstring(osdTheme).c_str(), ini.c_str());

    WritePrivateProfileStringW(L"Schedule", L"Mode", std::to_wstring(scheduleMode).c_str(), ini.c_str());
    WritePrivateProfileStringW(L"Schedule", L"SunSource", std::to_wstring(scheduleSunSource).c_str(), ini.c_str());
    WritePrivateProfileStringW(L"Schedule", L"LightHour", std::to_wstring(scheduleLightHour).c_str(), ini.c_str());
    WritePrivateProfileStringW(L"Schedule", L"LightMinute", std::to_wstring(scheduleLightMinute).c_str(), ini.c_str());
    WritePrivateProfileStringW(L"Schedule", L"DarkHour", std::to_wstring(scheduleDarkHour).c_str(), ini.c_str());
    WritePrivateProfileStringW(L"Schedule", L"DarkMinute", std::to_wstring(scheduleDarkMinute).c_str(), ini.c_str());
    WritePrivateProfileStringW(L"Schedule", L"Latitude", std::to_wstring(scheduleLatitude).c_str(), ini.c_str());
    WritePrivateProfileStringW(L"Schedule", L"Longitude", std::to_wstring(scheduleLongitude).c_str(), ini.c_str());
    WritePrivateProfileStringW(L"Schedule", L"SunriseOffset", std::to_wstring(scheduleSunriseOffset).c_str(), ini.c_str());
    WritePrivateProfileStringW(L"Schedule", L"SunsetOffset", std::to_wstring(scheduleSunsetOffset).c_str(), ini.c_str());
    WritePrivateProfileStringW(L"Schedule", L"ShowOsd", scheduleShowOsd ? L"1" : L"0", ini.c_str());

    WritePrivateProfileStringW(L"Settings", L"StartWithWindows", startWithWindows ? L"1" : L"0", ini.c_str());
    if (Config::IsInstalledInProgramFiles()) {
        Config::SetStartWithWindows(startWithWindows);
    }
}
