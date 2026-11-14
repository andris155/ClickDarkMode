#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>

class LocalizationManager {
public:
    static LocalizationManager& GetInstance();

    void Initialize(const std::wstring& baseDir = L"");
    void SetLanguage(const std::wstring& langCode);

    std::wstring Get(const std::wstring& key, const std::wstring& fallback = L"") const;
    std::wstring GetCurrentLanguage() const;
    std::wstring GetEffectiveLanguage() const;
    std::wstring GetWindowsDisplayLanguage() const;

    struct LanguageItem {
        std::wstring code;        // "default", "en-US", "hu-HU", etc.
        std::wstring displayName; // "Follow Windows", "English (United States)", etc.
    };

    std::vector<LanguageItem> GetAvailableLanguages() const;

private:
    LocalizationManager();
    ~LocalizationManager() = default;

    void ScanLanguageFiles();
    bool LoadLanguageFile(const std::wstring& filePath,
                          std::unordered_map<std::wstring, std::wstring>& targetMap,
                          std::wstring& outCode,
                          std::wstring& outName);
    void LoadBuiltInDefaults();

    mutable std::recursive_mutex m_mutex;
    std::wstring m_langDir;
    std::wstring m_configuredLang; // "default", "en-US", etc.
    std::wstring m_effectiveLang;  // actual loaded lang, e.g. "en-US"

    std::unordered_map<std::wstring, std::wstring> m_strings;
    std::unordered_map<std::wstring, std::wstring> m_fallbackStrings; // en-US baseline
    std::vector<LanguageItem> m_availableLanguages;
};

#define g_Loc LocalizationManager::GetInstance()
