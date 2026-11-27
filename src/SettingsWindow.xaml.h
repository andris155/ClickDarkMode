#pragma once

#include "SettingsWindow.g.h"
#include "SettingsWindow.xaml.g.h"
#include <winrt/Microsoft.UI.Windowing.h>

namespace winrt::ThemeSwitcherApp::implementation
{
    struct SettingsWindow : SettingsWindowT<SettingsWindow>
    {
        SettingsWindow();
        ~SettingsWindow();

        void ShowWindow();
        void HideWindow();
        void LoadSettingsToUI();
        void ApplyAndSave();
        void ApplyTheme();
        void SetupBackdrop();
        void UpdateThemeStatus();
        void UpdateIconPreviews();
        void UpdateIconVisibility();
        void UpdateDarkIconVisibility();
        void UpdateHotkeyVisibility();

        void StartCapture();
        void StopCapture(bool saveKey);
        void HandleCapturedKey(UINT vkCode, UINT mods);
        void UpdateCaptureHint(UINT mods);
        void SetExiting(bool exiting) { m_isAppExiting = exiting; }
        bool IsClosing() const { return m_isClosing; }
        void SetClosing(bool closing) { m_isClosing = closing; }

    private:
        void InitializeControls();
        void SetupWindow();
        void UpdateLocalizedStrings();
        void PopulateLanguageList();
        void RefreshComboBoxSelections();
        void UpdatePresetComboBoxSelections();
        void UpdateTitleBarDragRegions();
        void UpdateTitleBarButtons(bool isDark);

        void UpdateScheduleVisibility();
        void UpdateSunPreview();
        void UpdateDetectedLocationText();
        void PopulateLocationPresets();
        winrt::fire_and_forget DetectLocationAsync();

        void EnsureTrayTabLoaded();
        void EnsureScheduleTabLoaded();
        void EnsureAboutTabLoaded();

        std::wstring FormatIconLabel(const std::wstring& path, int number);

        static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);
        static LRESULT CALLBACK SubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);

        HWND m_hwnd{ nullptr };
        Microsoft::UI::Windowing::AppWindow m_appWindow{ nullptr };
        HICON m_hIconBig{ nullptr };
        HICON m_hIconSmall{ nullptr };

        bool m_isCapturing{ false };
        bool m_isUpdatingUI{ false };
        bool m_isAppExiting{ false };
        bool m_isClosing{ false };
        bool m_trayTabLoaded{ false };
        bool m_scheduleTabLoaded{ false };
        bool m_aboutTabLoaded{ false };

        winrt::Microsoft::UI::Xaml::Controls::StackPanel m_generalPanel{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::StackPanel m_trayPanel{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::StackPanel m_shortcutsPanel{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::StackPanel m_schedulePanel{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::StackPanel m_aboutPanel{ nullptr };
    };
}

namespace winrt::ThemeSwitcherApp::factory_implementation
{
    struct SettingsWindow : SettingsWindowT<SettingsWindow, implementation::SettingsWindow>
    {
    };
}

void InitSettingsWindow(bool show);
void ShowSettingsWindow();
void CloseSettingsWindow();
bool IsSettingsWindowOpen();
void OnSettingsThemeChanged();
void TrimProcessWorkingSetAsync();
