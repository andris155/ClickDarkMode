#include "pch.h"
#include "LocalizationManager.h"
#include <windows.h>
#include <shlobj.h>
#include <filesystem>
#include <fstream>
#include <algorithm>

namespace fs = std::filesystem;

LocalizationManager& LocalizationManager::GetInstance() {
    static LocalizationManager s_instance;
    return s_instance;
}

LocalizationManager::LocalizationManager()
    : m_configuredLang(L"default"), m_effectiveLang(L"en-US") {
    LoadBuiltInDefaults();
}

std::wstring LocalizationManager::GetWindowsDisplayLanguage() const {
    ULONG numLangs = 0;
    ULONG cchBuffer = 0;
    if (GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &numLangs, NULL, &cchBuffer) && cchBuffer > 0) {
        std::vector<wchar_t> buffer(cchBuffer);
        if (GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &numLangs, buffer.data(), &cchBuffer) && numLangs > 0) {
            return std::wstring(buffer.data());
        }
    }

    LANGID langId = GetUserDefaultUILanguage();
    wchar_t localeName[LOCALE_NAME_MAX_LENGTH] = { 0 };
    if (LCIDToLocaleName(MAKELCID(langId, SORT_DEFAULT), localeName, LOCALE_NAME_MAX_LENGTH, 0) > 0) {
        return localeName;
    }

    return L"en-US";
}

void LocalizationManager::LoadBuiltInDefaults() {
    m_fallbackStrings = {
        { L"App_Title", L"ClickDarkMode settings" },
        { L"Lang_FollowWindows", L"Follow Windows" },
        { L"Search_Placeholder", L"Search settings" },
        { L"Search_NoResults", L"No results found" },

        { L"Tab_General", L"General" },
        { L"Tab_TrayIcon", L"Tray Icon" },
        { L"Tab_Hotkey", L"Shortcuts" },
        { L"Tab_About", L"About" },

        { L"Gen_HeaderTitle", L"General Settings" },
        { L"Gen_HeaderSubtitle", L"Windows 11 Apps & System Theme Switcher" },
        { L"Gen_StatusTitle", L"Current Theme" },
        { L"Gen_StatusDark", L"Windows and applications are currently configured to use Dark Theme." },
        { L"Gen_StatusLight", L"Windows and applications are currently configured to use Light Theme." },
        { L"Gen_StatusMixed", L"Mixed mode: Apps (%s), Windows (%s)" },

        { L"Gen_AppearanceSection", L"Appearance" },
        { L"Gen_Language", L"Language" },
        { L"Gen_LanguageDesc", L"Select application display language. If unsupported, English is used." },
        { L"Gen_AppTheme", L"Application Theme" },
        { L"Gen_AppThemeDesc", L"Select the color theme for settings and flyout surfaces" },
        { L"Gen_AppTheme_Follow", L"Follow Windows (app)" },
        { L"Gen_AppTheme_Dark", L"Dark" },
        { L"Gen_AppTheme_Light", L"Light" },
        { L"Gen_TrayMenuTheme", L"Tray Menu Theme" },
        { L"Gen_TrayMenuThemeDesc", L"Select the color theme for the notification area tray right-click menu" },
        { L"Gen_TrayMenuTheme_Follow", L"Follow Windows (system)" },
        { L"Gen_Transparency", L"Mica Transparency" },
        { L"Gen_TransparencyDesc", L"Enable modern Windows 11 translucent Mica backdrop" },

        { L"Gen_TargetSection", L"Target Theme" },
        { L"Gen_TargetTheme", L"Theme Switch Mode" },
        { L"Gen_TargetDesc", L"Choose whether switching applies to Apps, Windows System UI (Taskbar/Start), or Both" },
        { L"Gen_TargetBoth", L"Both (Apps & Windows)" },
        { L"Gen_TargetApps", L"Apps Only" },
        { L"Gen_TargetSystem", L"Windows Only" },

        { L"Gen_BehaviorSection", L"Toggle Behavior" },
        { L"Gen_CooldownLabel", L"Accidental Click Cooldown (ms)" },
        { L"Gen_CooldownDesc", L"Ignores clicks occurring within this delay to prevent rapid double-toggling" },

        { L"Gen_OsdSection", L"On-Screen Display (OSD)" },
        { L"Gen_OsdTheme", L"OSD Theme" },
        { L"Gen_OsdThemeDesc", L"Select the color theme for the on-screen display banner" },
        { L"Gen_OsdTheme_Follow", L"Follow the theme" },
        { L"Gen_OsdTheme_Dark", L"Dark" },
        { L"Gen_OsdTheme_Light", L"Light" },
        { L"Gen_OsdTimeoutTitle", L"Hide Time (ms)" },
        { L"Gen_OsdTimeoutDesc", L"Auto-hide timeout for on-screen banner overlay" },
        { L"Gen_OsdDnd", L"Suppress in Do Not Disturb" },
        { L"Gen_OsdDndDesc", L"Do not show on-screen display when Windows Do Not Disturb or Focus Assist is active" },

        { L"Gen_StartupSection", L"Startup & Maintenance" },
        { L"Gen_StartWithWindows", L"Start with Windows" },
        { L"Gen_StartWithWindowsDesc", L"Launch ClickDarkMode automatically when Windows starts" },
        { L"Gen_RestoreDefaults", L"Restore Defaults" },
        { L"Gen_RestoreDefaultsDesc", L"Reset all settings to initial factory defaults" },
        { L"Gen_RestoreConfirmText", L"All settings will be reset. Do you want to continue?" },
        { L"Gen_RestoreConfirmBtn", L"Yes, reset settings" },

        { L"Icon_HeaderTitle", L"Tray Icon Settings" },
        { L"Icon_HeaderSubtitle", L"Customize system notification area tray icons" },
        { L"Icon_BehaviorSection", L"Icon Behavior" },
        { L"Icon_TrayIconMode", L"Tray Icon" },
        { L"Icon_TrayIconModeDesc", L"Choose tray icon appearance and behavior" },
        { L"Icon_TrayIconMode_Follow", L"Follow Windows (system)" },
        { L"Icon_TrayIconMode_Custom", L"Custom" },
        { L"Icon_TrayIconMode_Disabled", L"Disabled" },
        { L"Icon_DualCheckbox", L"Use Separate Icons" },
        { L"Icon_DualDesc", L"Display distinct tray icons for Light and Dark modes to indicate current status" },
        { L"Icon_ShowOsd", L"Show On-Screen Display (OSD)" },
        { L"Icon_ShowOsdDesc", L"Display a floating notification banner when clicking the tray icon" },
        { L"Icon_LightGroup", L"Light / Primary Mode Icon" },
        { L"Icon_LightDesc", L"Primary icon representing Light theme mode" },
        { L"Icon_DarkGroup", L"Dark Mode Icon" },
        { L"Icon_DarkDesc", L"Icon representing Dark theme mode" },
        { L"Icon_Browse", L"Browse..." },
        { L"Icon_LightPresetsTitle", L"Light Mode Presets" },
        { L"Icon_LightPresetsDesc", L"Quickly choose from built-in or standard Windows light mode icons" },
        { L"Icon_DarkPresetsTitle", L"Dark Mode Presets" },
        { L"Icon_DarkPresetsDesc", L"Quickly choose from built-in or standard Windows dark mode icons" },
        { L"Icon_Sun", L"Sun" },
        { L"Icon_SunBundled", L"Sun" },
        { L"Icon_SunWhite", L"Sun (White)" },
        { L"Icon_SunDark", L"Sun (Dark)" },
        { L"Icon_Moon", L"Moon" },
        { L"Icon_MoonBundled", L"Moon" },
        { L"Icon_MoonWhite", L"Moon (White)" },
        { L"Icon_MoonDark", L"Moon (Dark)" },
        { L"Icon_Star", L"Star" },
        { L"Icon_Monitor", L"Monitor" },
        { L"Icon_MonitorDark", L"Monitor (Dark)" },
        { L"Icon_Crescent", L"Monitor (Dark)" },
        { L"Icon_SelectPreset", L"Select preset..." },
        { L"Icon_BundledTag", L"(Bundled)" },

        { L"Hk_HeaderTitle", L"Shortcut Settings" },
        { L"Hk_HeaderSubtitle", L"Configure global keyboard shortcuts to toggle theme anywhere" },
        { L"Hk_GlobalSection", L"Global Shortcut" },
        { L"Hk_EnableCheckbox", L"Enable Global Shortcut" },
        { L"Hk_EnableDesc", L"Press a keyboard shortcut combination anytime to toggle theme immediately" },
        { L"Hk_ShowOsd", L"Show On-Screen Display (OSD)" },
        { L"Hk_ShowOsdDesc", L"Display a floating notification banner when toggling theme with shortcut" },
        { L"Hk_CombinationLabel", L"Shortcut Combination" },
        { L"Hk_CombinationDesc", L"Click the button and press your desired key combination" },
        { L"Hk_PressKey", L"Press key..." },
        { L"Hk_PressAnyKey", L"Press any key..." },
        { L"Hk_ResetShortcutsTitle", L"Reset Shortcuts" },
        { L"Hk_ResetShortcutsDesc", L"Restore shortcut to default (Ctrl + Alt + T)" },
        { L"Hk_ResetShortcutsBtn", L"Reset Hotkeys" },

        { L"Schedule_Mode_Disabled", L"Disabled (Manual only)" },
        { L"Schedule_Mode_Custom", L"Custom Time Schedule" },
        { L"Schedule_Mode_Sun", L"Sunrise / Sunset Sync" },
        { L"Schedule_ModeDisabled", L"Disabled (Manual only)" },
        { L"Schedule_ModeCustom", L"Custom Time Schedule" },
        { L"Schedule_ModeSun", L"Sunrise / Sunset Sync" },

        { L"Schedule_SunSourceTitle", L"Solar Location Source" },
        { L"Schedule_SunSourceDesc", L"Select whether to use Windows Location service or manual geographic coordinates" },
        { L"Schedule_SunSourceLocationService", L"Location service" },
        { L"Schedule_SunSourceCoordinates", L"Geographic coordinates" },
        { L"Schedule_SunPresetTitle", L"Quick Location Presets" },
        { L"Schedule_SunPresetDesc", L"Quickly fill coordinates for major regions" },
        { L"Schedule_PresetSelectPlaceholder", L"Select a city preset..." },
        { L"Schedule_DetectTitle", L"Auto-Detect Location" },
        { L"Schedule_DetectDesc", L"Detect current geographic coordinates using Windows Location" },
        { L"Schedule_DetectLocation", L"Detect" },
        { L"Schedule_DetectTooltip", L"Detect current coordinates using Windows Location" },
        { L"Schedule_LocDetecting", L"Detecting..." },
        { L"Schedule_LocNotDetected", L"No coordinates detected yet. Click Detect to acquire." },
        { L"Schedule_SunriseOffsetTitle", L"Sunrise Offset (minutes)" },
        { L"Schedule_SunriseOffsetDesc", L"Shift Light Mode switch earlier (negative) or later (positive) relative to sunrise" },
        { L"Schedule_SunsetOffsetTitle", L"Sunset Offset (minutes)" },
        { L"Schedule_SunsetOffsetDesc", L"Shift Dark Mode switch earlier (negative) or later (positive) relative to sunset" },
        { L"Schedule_LocDeniedTitle", L"Location Access Disabled" },
        { L"Schedule_LocDeniedMsg", L"Location access is turned off. Please enable Location in Windows Settings > Privacy & security > Location." },
        { L"Schedule_LocErrorTitle", L"Location Error" },
        { L"Schedule_LocErrorMsg", L"Could not determine your location. Please check your network connection or enter coordinates manually." },
        { L"Schedule_LatitudeTitle", L"Latitude" },
        { L"Schedule_LatitudeDesc", L"Geographic coordinate north or south of the equator (-90.0\u00B0 to 90.0\u00B0)" },
        { L"Schedule_LongitudeTitle", L"Longitude" },
        { L"Schedule_LongitudeDesc", L"Geographic coordinate east or west of the prime meridian (-180.0\u00B0 to 180.0\u00B0)" },
        { L"Schedule_Refresh", L"Refresh" },

        { L"About_HeaderTitle", L"About ClickDarkMode" },
        { L"About_HeaderSubtitle", L"Lightweight, modern Windows 11 theme switching utility" },
        { L"About_AppType", L"Native WinUI 3 / Windows App SDK 2.6 Desktop Application" },
        { L"About_DescTitle", L"Description" },
        { L"About_DescContent", L"A modern, lightweight Windows 11 utility for instant switching between Light and Dark themes for applications and system surfaces. Supports custom tray icons, accidental-click cooldown, global keyboard shortcuts, and ultra-low background memory footprint." },
        { L"About_LicenseTitle", L"License" },
        { L"About_LicenseContent", L"Free and Open Source under the MIT License." },

        { L"Common_On", L"On" },
        { L"Common_Off", L"Off" },

        { L"Tray_Toggle", L"Toggle Mode" },
        { L"Tray_Settings", L"Settings..." },
        { L"Tray_Exit", L"Exit" },

        { L"Tip_Mixed", L"Theme: Mixed (Apps %s, Win %s)" },
        { L"Tip_Windows", L"Windows Theme: %s" },
        { L"Tip_Both", L"Theme: %s (All)" },
        { L"Tip_App", L"App Theme: %s" },
        { L"Theme_Light", L"Light" },
        { L"Theme_Dark", L"Dark" },
        { L"Theme_Mixed", L"Mixed" },

        { L"OSD_LightMode", L"Light Mode" },
        { L"OSD_DarkMode", L"Dark Mode" },
        { L"OSD_TargetBoth", L"Apps & Windows" },
        { L"OSD_TargetApps", L"Apps Only" },
        { L"OSD_TargetSystem", L"Windows Only" }
    };
}

static std::wstring UnescapeJsonString(const std::wstring& s) {
    std::wstring out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == L'\\' && i + 1 < s.size()) {
            wchar_t next = s[++i];
            if (next == L'"') out += L'"';
            else if (next == L'\\') out += L'\\';
            else if (next == L'/') out += L'/';
            else if (next == L'n') out += L'\n';
            else if (next == L'r') out += L'\r';
            else if (next == L't') out += L'\t';
            else if (next == L'u' && i + 4 < s.size()) {
                wchar_t hex[5] = { s[i+1], s[i+2], s[i+3], s[i+4], 0 };
                wchar_t* end = nullptr;
                long val = wcstol(hex, &end, 16);
                out += (wchar_t)val;
                i += 4;
            } else {
                out += next;
            }
        } else {
            out += s[i];
        }
    }
    return out;
}

static bool ParseJsonStrings(const std::wstring& content,
                            std::unordered_map<std::wstring, std::wstring>& targetMap,
                            std::wstring& outCode,
                            std::wstring& outName) {
    targetMap.clear();
    outCode.clear();
    outName.clear();

    size_t i = 0;
    size_t len = content.size();

    auto skipWs = [&]() {
        while (i < len) {
            wchar_t c = content[i];
            if (c == L' ' || c == L'\t' || c == L'\r' || c == L'\n') {
                i++;
            } else if (c == L'/' && i + 1 < len && content[i + 1] == L'/') {
                i += 2;
                while (i < len && content[i] != L'\n') i++;
            } else {
                break;
            }
        }
    };

    skipWs();
    if (i >= len || content[i] != L'{') return false;
    i++; // Skip '{'

    auto readString = [&](std::wstring& str) -> bool {
        skipWs();
        if (i >= len || content[i] != L'"') return false;
        i++; // skip open quote
        size_t start = i;
        bool escape = false;
        while (i < len) {
            if (escape) {
                escape = false;
            } else if (content[i] == L'\\') {
                escape = true;
            } else if (content[i] == L'"') {
                break;
            }
            i++;
        }
        if (i >= len) return false;
        str = UnescapeJsonString(content.substr(start, i - start));
        i++; // skip close quote
        return true;
    };

    while (i < len) {
        skipWs();
        if (i < len && content[i] == L'}') {
            i++;
            break;
        }

        std::wstring key;
        if (!readString(key)) break;

        skipWs();
        if (i >= len || content[i] != L':') break;
        i++; // skip ':'

        std::wstring val;
        if (!readString(val)) break;

        if (key == L"__language_code") {
            outCode = val;
        } else if (key == L"__language_name") {
            outName = val;
        } else {
            targetMap[key] = val;
        }

        skipWs();
        if (i < len && content[i] == L',') {
            i++;
        }
    }

    return !targetMap.empty();
}

bool LocalizationManager::LoadLanguageFile(const std::wstring& filePath,
                                          std::unordered_map<std::wstring, std::wstring>& targetMap,
                                          std::wstring& outCode,
                                          std::wstring& outName) {
    std::ifstream file(fs::path(filePath), std::ios::binary);
    if (!file.is_open()) return false;

    std::string bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();

    if (bytes.size() >= 3 && (unsigned char)bytes[0] == 0xEF && (unsigned char)bytes[1] == 0xBB && (unsigned char)bytes[2] == 0xBF) {
        bytes.erase(0, 3);
    }

    if (bytes.empty()) return false;

    int wlen = MultiByteToWideChar(CP_UTF8, 0, bytes.data(), (int)bytes.size(), NULL, 0);
    if (wlen <= 0) return false;

    std::wstring content(wlen, 0);
    MultiByteToWideChar(CP_UTF8, 0, bytes.data(), (int)bytes.size(), &content[0], wlen);

    return ParseJsonStrings(content, targetMap, outCode, outName);
}

void LocalizationManager::Initialize(const std::wstring& baseDir) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);

    if (!baseDir.empty()) {
        m_langDir = baseDir;
    } else {
        wchar_t exePath[MAX_PATH] = { 0 };
        GetModuleFileNameW(NULL, exePath, MAX_PATH);
        fs::path p(exePath);
        m_langDir = (p.parent_path() / L"lang").wstring();
    }

    ScanLanguageFiles();

    std::wstring fallbackPath = (fs::path(m_langDir) / L"en-US.json").wstring();
    std::wstring c, n;
    LoadLanguageFile(fallbackPath, m_fallbackStrings, c, n);
}

void LocalizationManager::ScanLanguageFiles() {
    m_availableLanguages.clear();

    std::wstring winDisplayLang = GetWindowsDisplayLanguage();
    LanguageItem defaultItem;
    defaultItem.code = L"default";
    defaultItem.displayName = Get(L"Lang_FollowWindows", L"Follow Windows") + L" (" + winDisplayLang + L")";
    m_availableLanguages.push_back(defaultItem);

    if (!fs::exists(m_langDir) || !fs::is_directory(m_langDir)) {
        LanguageItem enItem;
        enItem.code = L"en-US";
        enItem.displayName = L"English (United States)";
        m_availableLanguages.push_back(enItem);
        return;
    }

    try {
        for (const auto& entry : fs::directory_iterator(m_langDir)) {
            if (entry.is_regular_file() && entry.path().extension() == L".json") {
                std::unordered_map<std::wstring, std::wstring> tempMap;
                std::wstring code, name;
                if (LoadLanguageFile(entry.path().wstring(), tempMap, code, name)) {
                    if (!code.empty()) {
                        LanguageItem item;
                        item.code = code;
                        item.displayName = name.empty() ? code : name;
                        m_availableLanguages.push_back(item);
                    }
                }
            }
        }
    } catch (...) {}

    if (m_availableLanguages.size() == 1) {
        LanguageItem enItem;
        enItem.code = L"en-US";
        enItem.displayName = L"English (United States)";
        m_availableLanguages.push_back(enItem);
    }
}

void LocalizationManager::SetLanguage(const std::wstring& langCode) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);

    m_configuredLang = langCode.empty() ? L"default" : langCode;

    if (m_configuredLang == L"default") {
        m_effectiveLang = GetWindowsDisplayLanguage();
    } else {
        m_effectiveLang = m_configuredLang;
    }

    m_strings.clear();
    std::wstring targetFile = (fs::path(m_langDir) / (m_effectiveLang + L".json")).wstring();
    std::wstring c, n;
    if (!LoadLanguageFile(targetFile, m_strings, c, n)) {
        size_t dash = m_effectiveLang.find(L'-');
        if (dash != std::wstring::npos) {
            std::wstring baseLang = m_effectiveLang.substr(0, dash);
            try {
                for (const auto& entry : fs::directory_iterator(m_langDir)) {
                    if (entry.is_regular_file() && entry.path().extension() == L".json") {
                        std::wstring fn = entry.path().stem().wstring();
                        if (fn.rfind(baseLang, 0) == 0) {
                            if (LoadLanguageFile(entry.path().wstring(), m_strings, c, n)) {
                                m_effectiveLang = c;
                                break;
                            }
                        }
                    }
                }
            } catch (...) {}
        }
    }

    if (m_strings.empty()) {
        m_effectiveLang = L"en-US";
        std::wstring enPath = (fs::path(m_langDir) / L"en-US.json").wstring();
        LoadLanguageFile(enPath, m_strings, c, n);
    }

    // Refresh "default" display name with current Windows display language
    if (!m_availableLanguages.empty() && m_availableLanguages[0].code == L"default") {
        std::wstring winDisplayLang = GetWindowsDisplayLanguage();
        m_availableLanguages[0].displayName = Get(L"Lang_FollowWindows", L"Follow Windows") + L" (" + winDisplayLang + L")";
    }
}

std::wstring LocalizationManager::Get(const std::wstring& key, const std::wstring& fallback) const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);

    auto it = m_strings.find(key);
    if (it != m_strings.end() && !it->second.empty()) {
        return it->second;
    }

    auto itFb = m_fallbackStrings.find(key);
    if (itFb != m_fallbackStrings.end() && !itFb->second.empty()) {
        return itFb->second;
    }

    return fallback.empty() ? key : fallback;
}

std::wstring LocalizationManager::GetCurrentLanguage() const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return m_configuredLang;
}

std::wstring LocalizationManager::GetEffectiveLanguage() const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return m_effectiveLang;
}

std::vector<LocalizationManager::LanguageItem> LocalizationManager::GetAvailableLanguages() const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return m_availableLanguages;
}
