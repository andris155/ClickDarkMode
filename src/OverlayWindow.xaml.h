#pragma once

#include "OverlayWindow.g.h"
#include "OverlayWindow.xaml.g.h"
#include <winrt/Microsoft.UI.Windowing.h>
#include <winrt/Microsoft.UI.Composition.SystemBackdrops.h>
#include <string>

namespace winrt::ThemeSwitcherApp::implementation
{
    struct OverlayWindow : OverlayWindowT<OverlayWindow>
    {
        OverlayWindow();
        ~OverlayWindow();

        void Show(int themeState, const std::wstring& target);
        bool UpdateContent(int themeState, const std::wstring& target);
        bool UpdateTheme(bool isDark);
        void ResetGraphics();
        void RefreshOnDisplayChange();
        void PositionWindow();
        void PreArm(int nextExpectedState, const std::wstring& target);
        void SyncWithSystem();
        void HideWindow();
        void DestroyWindow();

    private:
        enum class AnimState { None, Showing, Hiding };

        void SetupWindow();
        void SetupBackdrop(bool isDark);
        void DetachBackdrop();
        void StartShowAnimation();
        void StartHideAnimation();
        void StopAnimation();
        void UpdateAnimation();
        void FinishHide();
        static LRESULT CALLBACK SubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);

        HWND m_hwnd{ nullptr };
        Microsoft::UI::Windowing::AppWindow m_appWindow{ nullptr };

        Microsoft::UI::Composition::SystemBackdrops::DesktopAcrylicController m_acrylicController{ nullptr };
        Microsoft::UI::Composition::SystemBackdrops::SystemBackdropConfiguration m_backdropConfig{ nullptr };

        int m_displayedState{ 1 };
        std::wstring m_displayedTarget{ L"Both" };
        bool m_isVisible{ false };

        int m_lastTheme{ -1 };
        int m_lastTransparency{ -1 };
        int m_contentState{ -1 };
        std::wstring m_contentTarget;
        std::wstring m_contentLang;
        int m_lastX{ -999999 };
        int m_lastY{ -999999 };
        int m_lastW{ 0 };
        int m_lastH{ 0 };
        ULONGLONG m_hideAfterTick{ 0 };

        AnimState m_animState{ AnimState::None };
        LARGE_INTEGER m_animStartQpc{};
        double m_animDurationMs{ 180.0 };
        int m_animStartY{ 0 };
        int m_animEndY{ 0 };
        int m_currentY{ 0 };
        int m_slideOffset{ 64 };
        winrt::event_token m_renderingToken{};
    };
}

namespace winrt::ThemeSwitcherApp::factory_implementation
{
    struct OverlayWindow : OverlayWindowT<OverlayWindow, implementation::OverlayWindow>
    {
    };
}
