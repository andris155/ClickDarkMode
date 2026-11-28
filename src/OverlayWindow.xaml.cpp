#include "pch.h"
#include "OverlayWindow.xaml.h"
#if __has_include("OverlayWindow.g.cpp")
#include "OverlayWindow.g.cpp"
#endif

#include "OverlayWindow.h"
#include "Settings.h"
#include "DpiHelper.h"
#include "ThemeHelper.h"
#include "ThemeManager.h"
#include "LocalizationManager.h"
#include "resource.h"

#include <microsoft.ui.xaml.window.h>
#include <winrt/Microsoft.UI.Interop.h>
#include <winrt/Microsoft.UI.Composition.SystemBackdrops.h>
#include <winrt/Microsoft.UI.Composition.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <dwmapi.h>
#include <commctrl.h>
#include <algorithm>
#include <cmath>

namespace winrt::ThemeSwitcherApp::implementation
{
    static constexpr int kOsdWidthDip = 160;
    static constexpr int kOsdHeightDip = 48;

    static bool DetermineOsdIsDark(int themeState)
    {
        if (g_Settings.osdTheme == 1) return true;  // Dark
        if (g_Settings.osdTheme == 2) return false; // Light
        return (themeState == 0);                   // Follow the theme (0 = dark, 1 = light)
    }

    OverlayWindow::OverlayWindow()
    {
        InitializeComponent();
        m_displayedState = ThemeManager::GetNextToggleState();
        m_displayedTarget = g_Settings.target;
        bool isDark = DetermineOsdIsDark(m_displayedState);
        RootGrid().RequestedTheme(isDark ? winrt::Microsoft::UI::Xaml::ElementTheme::Dark : winrt::Microsoft::UI::Xaml::ElementTheme::Light);
        SetupWindow();
        SetupBackdrop(isDark);
        UpdateContent(m_displayedState, m_displayedTarget);
        UpdateTheme(isDark);

        // Pre-warm layout and DirectX composition while cloaked on the primary monitor at startup
        // Keeping the window on the primary monitor ensures it always uses the primary display's DPI
        if (m_hwnd)
        {
            POINT ptOrigin = { 0, 0 };
            HMONITOR hMon = MonitorFromPoint(ptOrigin, MONITOR_DEFAULTTOPRIMARY);
            UINT dpi = DpiHelper::GetDpiForMonitor(hMon);
            if (dpi == 0) dpi = GetDpiForSystem();
            if (dpi == 0) dpi = 96;

            int width = DpiHelper::Scale(kOsdWidthDip, dpi);
            int height = DpiHelper::Scale(kOsdHeightDip, dpi);
            m_lastW = width;
            m_lastH = height;

            MONITORINFO mi = { sizeof(mi) };
            GetMonitorInfoW(hMon, &mi);
            int marginBottom = DpiHelper::Scale(16, dpi);
            int workW = mi.rcWork.right - mi.rcWork.left;
            int targetX = mi.rcWork.left + (workW - width) / 2;
            int targetY = mi.rcWork.bottom - height - marginBottom;

            BOOL cloak = TRUE;
            DwmSetWindowAttribute(m_hwnd, DWMWA_CLOAK, &cloak, sizeof(cloak));
            HWND hPrevForeground = GetForegroundWindow();
            SetWindowPos(m_hwnd, NULL, targetX, targetY, width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
            this->Activate();
            RootGrid().UpdateLayout();
            if (hPrevForeground && hPrevForeground != m_hwnd)
            {
                SetForegroundWindow(hPrevForeground);
            }
        }
    }

    OverlayWindow::~OverlayWindow()
    {
        StopAnimation();
        DestroyWindow();
    }

    void OverlayWindow::SetupWindow()
    {
        auto windowNative = this->try_as<::IWindowNative>();
        if (windowNative)
        {
            windowNative->get_WindowHandle(&m_hwnd);
        }

        if (m_hwnd)
        {
            auto windowId = winrt::Microsoft::UI::GetWindowIdFromWindow(m_hwnd);
            m_appWindow = winrt::Microsoft::UI::Windowing::AppWindow::GetFromWindowId(windowId);

            if (m_appWindow)
            {
                auto presenter = m_appWindow.Presenter().try_as<winrt::Microsoft::UI::Windowing::OverlappedPresenter>();
                if (presenter)
                {
                    presenter.SetBorderAndTitleBar(false, false);
                    presenter.IsAlwaysOnTop(true);
                    presenter.IsResizable(false);
                    presenter.IsMinimizable(false);
                    presenter.IsMaximizable(false);
                }
            }

            // 1. Strip standard window frame styles to remove white border lines
            LONG_PTR style = GetWindowLongPtrW(m_hwnd, GWL_STYLE);
            style &= ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU | WS_BORDER | WS_DLGFRAME);
            style |= WS_POPUP;
            SetWindowLongPtrW(m_hwnd, GWL_STYLE, style);

            // 2. Window styles: toolwindow, topmost, noactivate, no edges
            LONG_PTR exStyle = GetWindowLongPtrW(m_hwnd, GWL_EXSTYLE);
            exStyle &= ~(WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE | WS_EX_STATICEDGE | WS_EX_DLGMODALFRAME);
            exStyle |= WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE;
            SetWindowLongPtrW(m_hwnd, GWL_EXSTYLE, exStyle);

            // Set window class background brush to NULL_BRUSH to avoid GDI white rectangle flash
            SetClassLongPtrW(m_hwnd, GCLP_HBRBACKGROUND, (LONG_PTR)GetStockObject(NULL_BRUSH));

            // 3. Window corners: round window frame
            DWM_WINDOW_CORNER_PREFERENCE cornerPref = DWMWCP_ROUND;
            DwmSetWindowAttribute(m_hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &cornerPref, sizeof(cornerPref));

            // 4. Immersive dark mode attribute
            bool isDark = DetermineOsdIsDark(m_displayedState);
            BOOL darkMode = isDark ? TRUE : FALSE;
            DwmSetWindowAttribute(m_hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkMode, sizeof(darkMode));

            COLORREF captionColor = isDark ? RGB(32, 32, 32) : RGB(243, 243, 243);
            DwmSetWindowAttribute(m_hwnd, DWMWA_CAPTION_COLOR, &captionColor, sizeof(captionColor));

            // 5. Border: native FlyoutWindow DWM border (DWMWA_COLOR_DEFAULT)
            COLORREF borderColor = DWMWA_COLOR_DEFAULT;
            DwmSetWindowAttribute(m_hwnd, DWMWA_BORDER_COLOR, &borderColor, sizeof(borderColor));

            // 6. Native DWM Transient Acrylic (Windows 11 Volume / Shell OSD Backdrop)
            int backdropType = g_Settings.transparencyEnabled ? DWMSBT_TRANSIENTWINDOW : DWMSBT_NONE;
            DwmSetWindowAttribute(m_hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &backdropType, sizeof(backdropType));

            // 7. Instant snappy display (no DWM open/close animation lag)
            BOOL forceDisableTransitions = TRUE;
            DwmSetWindowAttribute(m_hwnd, DWMWA_TRANSITIONS_FORCEDISABLED, &forceDisableTransitions, sizeof(forceDisableTransitions));

            // 8. Keep shadow effect under overlay
            int ncPolicy = 0; // DWMNCRP_USEWINDOWSTYLE
            DwmSetWindowAttribute(m_hwnd, DWMWA_NCRENDERING_POLICY, &ncPolicy, sizeof(ncPolicy));

            SetWindowPos(m_hwnd, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED | SWP_NOACTIVATE);

            // Subclass window for click-through and timers
            SetWindowSubclass(m_hwnd, SubclassProc, 3, reinterpret_cast<DWORD_PTR>(this));
        }

        UpdateContent(m_displayedState, m_displayedTarget);
    }

    void OverlayWindow::SetupBackdrop(bool isDark)
    {
        if (!g_Settings.transparencyEnabled)
        {
            DetachBackdrop();
            UpdateTheme(isDark);
            return;
        }

        if (m_acrylicController)
        {
            if (m_backdropConfig)
            {
                m_backdropConfig.Theme(isDark ?
                    winrt::Microsoft::UI::Composition::SystemBackdrops::SystemBackdropTheme::Dark :
                    winrt::Microsoft::UI::Composition::SystemBackdrops::SystemBackdropTheme::Light);
            }
            return;
        }

        if (winrt::Microsoft::UI::Composition::SystemBackdrops::DesktopAcrylicController::IsSupported())
        {
            m_acrylicController = winrt::Microsoft::UI::Composition::SystemBackdrops::DesktopAcrylicController();
            if (!m_backdropConfig)
            {
                m_backdropConfig = winrt::Microsoft::UI::Composition::SystemBackdrops::SystemBackdropConfiguration();
            }

            // Force IsInputActive(true) permanently so compositor never reverts to opaque black
            m_backdropConfig.IsInputActive(true);

            m_backdropConfig.Theme(isDark ?
                winrt::Microsoft::UI::Composition::SystemBackdrops::SystemBackdropTheme::Dark :
                winrt::Microsoft::UI::Composition::SystemBackdrops::SystemBackdropTheme::Light);

            auto target = this->try_as<winrt::Microsoft::UI::Composition::ICompositionSupportsSystemBackdrop>();
            if (target)
            {
                m_acrylicController.AddSystemBackdropTarget(target);
                m_acrylicController.SetSystemBackdropConfiguration(m_backdropConfig);
            }
        }

        UpdateTheme(isDark);
    }

    void OverlayWindow::DetachBackdrop()
    {
        if (m_acrylicController)
        {
            try
            {
                m_acrylicController.RemoveAllSystemBackdropTargets();
                m_acrylicController.Close();
            }
            catch (...)
            {
            }
            m_acrylicController = nullptr;
        }
    }

    bool OverlayWindow::UpdateTheme(bool isDark)
    {
        int curTheme = isDark ? 1 : 0;
        int curTrans = g_Settings.transparencyEnabled ? 1 : 0;
        if (m_lastTheme == curTheme && m_lastTransparency == curTrans)
        {
            return false;
        }
        bool transChanged = (m_lastTransparency != curTrans);
        m_lastTheme = curTheme;
        m_lastTransparency = curTrans;

        RootGrid().RequestedTheme(isDark ? winrt::Microsoft::UI::Xaml::ElementTheme::Dark : winrt::Microsoft::UI::Xaml::ElementTheme::Light);

        if (g_Settings.transparencyEnabled)
        {
            if (!m_acrylicController)
            {
                SetupBackdrop(isDark);
            }
            RootGrid().Background(winrt::Microsoft::UI::Xaml::Media::SolidColorBrush(winrt::Microsoft::UI::Colors::Transparent()));
        }
        else
        {
            DetachBackdrop();
            winrt::Windows::UI::Color solidBg = isDark
                ? winrt::Microsoft::UI::ColorHelper::FromArgb(255, 44, 44, 44)
                : winrt::Microsoft::UI::ColorHelper::FromArgb(255, 243, 243, 243);
            RootGrid().Background(winrt::Microsoft::UI::Xaml::Media::SolidColorBrush(solidBg));
        }

        if (m_hwnd)
        {
            BOOL darkMode = isDark ? TRUE : FALSE;
            DwmSetWindowAttribute(m_hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkMode, sizeof(darkMode));

            COLORREF captionColor = isDark ? RGB(32, 32, 32) : RGB(243, 243, 243);
            DwmSetWindowAttribute(m_hwnd, DWMWA_CAPTION_COLOR, &captionColor, sizeof(captionColor));

            if (transChanged)
            {
                int backdropType = g_Settings.transparencyEnabled ? DWMSBT_TRANSIENTWINDOW : DWMSBT_NONE;
                DwmSetWindowAttribute(m_hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &backdropType, sizeof(backdropType));
            }
        }

        // Layout matching FlyoutWindow (no inner XAML border, native DWM border handles frame)
        PillBorder().Margin(winrt::Microsoft::UI::Xaml::Thickness{ 0, 0, 0, 0 });
        PillBorder().Padding(winrt::Microsoft::UI::Xaml::Thickness{ 0, 0, 0, 0 });
        PillBorder().BorderThickness(winrt::Microsoft::UI::Xaml::Thickness{ 0, 0, 0, 0 });
        PillBorder().BorderBrush(nullptr);
        PillBorder().Background(winrt::Microsoft::UI::Xaml::Media::SolidColorBrush(winrt::Microsoft::UI::Colors::Transparent()));
        PillBorder().CornerRadius(winrt::Microsoft::UI::Xaml::CornerRadius{ 8, 8, 8, 8 });

        if (m_backdropConfig)
        {
            m_backdropConfig.IsInputActive(true);
            m_backdropConfig.Theme(isDark ?
                winrt::Microsoft::UI::Composition::SystemBackdrops::SystemBackdropTheme::Dark :
                winrt::Microsoft::UI::Composition::SystemBackdrops::SystemBackdropTheme::Light);
        }

        winrt::Windows::UI::Color fgColor = isDark
            ? winrt::Microsoft::UI::Colors::White()
            : winrt::Microsoft::UI::ColorHelper::FromArgb(255, 28, 28, 28);

        ModeIcon().Foreground(winrt::Microsoft::UI::Xaml::Media::SolidColorBrush(fgColor));
        ModeTitleText().Foreground(winrt::Microsoft::UI::Xaml::Media::SolidColorBrush(fgColor));

        winrt::Windows::UI::Color subColor = isDark
            ? winrt::Microsoft::UI::ColorHelper::FromArgb(200, 200, 200, 200)
            : winrt::Microsoft::UI::ColorHelper::FromArgb(200, 96, 96, 96);
        ModeTargetText().Foreground(winrt::Microsoft::UI::Xaml::Media::SolidColorBrush(subColor));
        return true;
    }

    void OverlayWindow::ResetGraphics()
    {
        if (m_isVisible) return; // Do NOT reset or flash backdrop while OSD is visible on screen!
        m_lastTheme = -1;
        m_lastTransparency = -1;
        m_contentState = -1;
        bool isDark = DetermineOsdIsDark(m_displayedState);
        DetachBackdrop();
        SetupBackdrop(isDark);
        UpdateTheme(isDark);
        RefreshOnDisplayChange();
    }

    void OverlayWindow::RefreshOnDisplayChange()
    {
        if (m_isVisible || !m_hwnd) return;
        HMONITOR hMon = MonitorFromPoint({ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
        UINT dpi = DpiHelper::GetDpiForMonitor(hMon);
        if (dpi == 0) dpi = GetDpiForWindow(m_hwnd);
        if (dpi == 0) dpi = 96;
        int width = DpiHelper::Scale(kOsdWidthDip, dpi);
        int height = DpiHelper::Scale(kOsdHeightDip, dpi);
        m_lastW = width;
        m_lastH = height;
        SetWindowPos(m_hwnd, NULL, 0, 0, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
        RootGrid().UpdateLayout();
    }

    bool OverlayWindow::UpdateContent(int themeState, const std::wstring& target)
    {
        if (m_contentState == themeState && m_contentTarget == target && m_contentLang == g_Settings.language)
        {
            return false;
        }
        m_contentState = themeState;
        m_contentTarget = target;
        m_contentLang = g_Settings.language;

        if (themeState == 0)
        {
            ModeIcon().Glyph(L"\xE708"); // Moon
            ModeTitleText().Text(g_Loc.Get(L"OSD_DarkMode", L"Dark Mode"));
        }
        else
        {
            ModeIcon().Glyph(L"\xE706"); // Sun
            ModeTitleText().Text(g_Loc.Get(L"OSD_LightMode", L"Light Mode"));
        }

        if (target == L"Both")
        {
            ModeTargetText().Text(g_Loc.Get(L"OSD_TargetBoth", L"Apps & Windows"));
        }
        else if (target == L"Apps")
        {
            ModeTargetText().Text(g_Loc.Get(L"OSD_TargetApps", L"Apps Only"));
        }
        else if (target == L"System")
        {
            ModeTargetText().Text(g_Loc.Get(L"OSD_TargetSystem", L"Windows Only"));
        }
        else
        {
            ModeTargetText().Text(g_Loc.Get(L"OSD_TargetBoth", L"Apps & Windows"));
        }
        return true;
    }

    void OverlayWindow::PositionWindow()
    {
        if (!m_hwnd || !m_appWindow) return;

        POINT ptOrigin = { 0, 0 };
        HMONITOR hMon = MonitorFromPoint(ptOrigin, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO mi = { sizeof(mi) };
        GetMonitorInfoW(hMon, &mi);

        UINT dpi = DpiHelper::GetDpiForMonitor(hMon);
        if (dpi == 0) dpi = GetDpiForWindow(m_hwnd);
        if (dpi == 0) dpi = 96;
        int width = DpiHelper::Scale(160, dpi);
        int height = DpiHelper::Scale(48, dpi);
        int marginBottom = DpiHelper::Scale(16, dpi);
        m_slideOffset = height + marginBottom;

        int workW = mi.rcWork.right - mi.rcWork.left;
        int targetX = mi.rcWork.left + (workW - width) / 2;
        int targetY = mi.rcWork.bottom - height - marginBottom;

        // Place right behind the main display's taskbar in topmost Z-order so it emerges from under it
        HWND hInsertAfter = HWND_TOPMOST;
        HWND hTray = FindWindowW(L"Shell_TrayWnd", NULL);
        if (hTray && MonitorFromWindow(hTray, MONITOR_DEFAULTTONEAREST) == hMon &&
            (GetWindowLongPtrW(hTray, GWL_EXSTYLE) & WS_EX_TOPMOST))
        {
            hInsertAfter = hTray;
        }

        bool sizeChanged = (m_lastW != width || m_lastH != height);
        bool posChanged = (m_lastX != targetX || m_lastY != targetY || sizeChanged);
        m_lastX = targetX;
        m_lastY = targetY;
        m_lastW = width;
        m_lastH = height;

        UINT swpFlags = SWP_NOACTIVATE;
        if (!sizeChanged)
        {
            swpFlags |= SWP_NOSIZE;
        }

        if (!m_isVisible)
        {
            BOOL cloak = TRUE;
            DwmSetWindowAttribute(m_hwnd, DWMWA_CLOAK, &cloak, sizeof(cloak));
            m_currentY = g_Settings.osdAnimationEnabled ? (targetY + m_slideOffset) : targetY;
            SetWindowPos(m_hwnd, hInsertAfter, targetX, m_currentY, width, height, swpFlags | SWP_SHOWWINDOW | SWP_NOCOPYBITS);
            if (sizeChanged)
            {
                RootGrid().UpdateLayout();
            }
            SetTimer(m_hwnd, IDT_OVERLAY_UNCLOAK, 16, NULL);
            m_isVisible = true;
            m_animState = g_Settings.osdAnimationEnabled ? AnimState::Showing : AnimState::None;
        }
        else if (m_animState == AnimState::Hiding)
        {
            if (posChanged)
            {
                SetWindowPos(m_hwnd, hInsertAfter, targetX, m_currentY, width, height, swpFlags);
            }
            StartShowAnimation();
        }
        else if (m_animState == AnimState::Showing)
        {
            if (posChanged)
            {
                m_animEndY = targetY;
                SetWindowPos(m_hwnd, hInsertAfter, targetX, m_currentY, width, height, swpFlags);
            }
        }
        else if (posChanged)
        {
            m_currentY = targetY;
            SetWindowPos(m_hwnd, hInsertAfter, targetX, targetY, width, height, swpFlags);
        }
    }

    void OverlayWindow::Show(int themeState, const std::wstring& target)
    {
        if (g_Settings.osdSuppressOnDnd && ThemeHelper::IsDoNotDisturbActive())
        {
            if (m_isVisible)
            {
                HideWindow();
            }
            PreArm(themeState ^ 1, target);
            return;
        }

        bool targetIsDark = DetermineOsdIsDark(themeState);

        m_displayedState = themeState;
        m_displayedTarget = target;

        UpdateTheme(targetIsDark);
        UpdateContent(themeState, target);
        PositionWindow();

        int timeout = g_Settings.osdTimeoutMs;
        if (timeout < 200) timeout = 1500;

        ULONGLONG now = GetTickCount64();
        m_hideAfterTick = now + timeout;

        if (m_hwnd)
        {
            SetTimer(m_hwnd, IDT_OVERLAY_HIDE, timeout, NULL);
        }
    }

    void OverlayWindow::PreArm(int nextExpectedState, const std::wstring& target)
    {
        if (m_isVisible) return; // Never touch overlay if currently showing on screen
        m_displayedState = nextExpectedState;
        m_displayedTarget = target;
        bool isDark = DetermineOsdIsDark(nextExpectedState);
        bool changed = UpdateTheme(isDark);
        changed |= UpdateContent(nextExpectedState, target);
        if (changed && m_hwnd)
        {
            RootGrid().UpdateLayout();
        }
    }

    void OverlayWindow::SyncWithSystem()
    {
        if (m_isVisible) return;
        int nextExpected = ThemeManager::GetNextToggleState();
        PreArm(nextExpected, g_Settings.target);
    }

    void OverlayWindow::StopAnimation()
    {
        if (m_renderingToken.value != 0)
        {
            try
            {
                winrt::Microsoft::UI::Xaml::Media::CompositionTarget::Rendering(m_renderingToken);
            }
            catch (...) {}
            m_renderingToken = {};
        }
    }

    void OverlayWindow::StartShowAnimation()
    {
        if (!m_hwnd) return;
        m_animState = AnimState::Showing;
        m_animStartY = m_currentY;
        m_animEndY = m_lastY;
        m_animDurationMs = 180.0;
        QueryPerformanceCounter(&m_animStartQpc);
        if (m_renderingToken.value == 0)
        {
            m_renderingToken = winrt::Microsoft::UI::Xaml::Media::CompositionTarget::Rendering(
                [this](auto&&, auto&&) { UpdateAnimation(); });
        }
    }

    void OverlayWindow::StartHideAnimation()
    {
        if (!m_hwnd || !m_isVisible) return;
        if (m_animState == AnimState::Hiding) return;
        m_animState = AnimState::Hiding;
        m_animStartY = m_currentY;
        m_animEndY = m_lastY + m_slideOffset;
        m_animDurationMs = 150.0;
        QueryPerformanceCounter(&m_animStartQpc);
        if (m_renderingToken.value == 0)
        {
            m_renderingToken = winrt::Microsoft::UI::Xaml::Media::CompositionTarget::Rendering(
                [this](auto&&, auto&&) { UpdateAnimation(); });
        }
    }

    void OverlayWindow::UpdateAnimation()
    {
        if (!m_hwnd || m_animState == AnimState::None)
        {
            StopAnimation();
            return;
        }

        static LARGE_INTEGER s_freq = []() {
            LARGE_INTEGER f{};
            QueryPerformanceFrequency(&f);
            return f;
        }();

        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        double elapsedMs = (double)(now.QuadPart - m_animStartQpc.QuadPart) * 1000.0 / (double)s_freq.QuadPart;
        double t = (m_animDurationMs > 0.0) ? (elapsedMs / m_animDurationMs) : 1.0;
        if (t < 0.0) t = 0.0;
        if (t >= 1.0) t = 1.0;

        double eased = (m_animState == AnimState::Showing)
            ? (1.0 - std::pow(1.0 - t, 3.0))
            : (t * t);

        int newY = m_animStartY + static_cast<int>(std::round((m_animEndY - m_animStartY) * eased));
        if (newY != m_currentY)
        {
            m_currentY = newY;
            SetWindowPos(m_hwnd, NULL, m_lastX, m_currentY, 0, 0,
                SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOSENDCHANGING | SWP_NOCOPYBITS | SWP_DEFERERASE);
        }

        if (t >= 1.0)
        {
            StopAnimation();
            if (m_animState == AnimState::Hiding)
            {
                FinishHide();
            }
            else
            {
                m_animState = AnimState::None;
            }
        }
    }

    void OverlayWindow::FinishHide()
    {
        StopAnimation();
        if (m_hwnd)
        {
            KillTimer(m_hwnd, IDT_OVERLAY_UNCLOAK);
        }
        m_animState = AnimState::None;
        m_isVisible = false;
        m_hideAfterTick = 0;
        m_lastX = -999999;
        m_lastY = -999999;
        if (m_hwnd)
        {
            BOOL cloak = TRUE;
            DwmSetWindowAttribute(m_hwnd, DWMWA_CLOAK, &cloak, sizeof(cloak));
        }

        // Immediately pre-arm the hidden overlay with the NEXT expected theme,
        // so its swapchain cache is already primed before the next toggle!
        int nextExpected = m_displayedState ^ 1;
        PreArm(nextExpected, m_displayedTarget);
    }

    void OverlayWindow::HideWindow()
    {
        if (m_hwnd)
        {
            KillTimer(m_hwnd, IDT_OVERLAY_HIDE);
            KillTimer(m_hwnd, IDT_OVERLAY_UNCLOAK);
        }

        if (m_isVisible)
        {
            if (g_Settings.osdAnimationEnabled)
            {
                StartHideAnimation();
            }
            else
            {
                FinishHide();
            }
        }
    }

    void OverlayWindow::DestroyWindow()
    {
        StopAnimation();
        HWND h = m_hwnd;
        m_hwnd = NULL;
        if (h)
        {
            KillTimer(h, IDT_OVERLAY_HIDE);
            KillTimer(h, IDT_OVERLAY_UNCLOAK);
            RemoveWindowSubclass(h, SubclassProc, 3);
            ::ShowWindow(h, SW_HIDE);
        }
        DetachBackdrop();
        m_backdropConfig = nullptr;
        m_appWindow = nullptr;
        m_isVisible = false;
        m_hideAfterTick = 0;
        try
        {
            this->Close();
        }
        catch (...) {}
    }

    LRESULT CALLBACK OverlayWindow::SubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
    {
        auto pThis = reinterpret_cast<OverlayWindow*>(dwRefData);
        if (!pThis) return DefSubclassProc(hWnd, uMsg, wParam, lParam);

        if (uMsg == WM_ERASEBKGND)
        {
            return 1; // Prevent GDI white background flash before DWM
        }

        if (uMsg == WM_SETTINGCHANGE || uMsg == WM_THEMECHANGED || uMsg == WM_DWMCOLORIZATIONCOLORCHANGED || uMsg == WM_SYSCOLORCHANGE)
        {
            return 0; // Prevent WinUI 3 / DWM from mutating overlay theme while displayed
        }

        if (uMsg == WM_NCHITTEST)
        {
            return HTTRANSPARENT; // Click-through overlay
        }

        if (uMsg == WM_TIMER)
        {
            if (wParam == IDT_OVERLAY_HIDE)
            {
                ULONGLONG now = GetTickCount64();
                if (now < pThis->m_hideAfterTick)
                {
                    UINT remaining = (UINT)(pThis->m_hideAfterTick - now);
                    if (remaining < 30) remaining = 30;
                    SetTimer(hWnd, IDT_OVERLAY_HIDE, remaining, NULL);
                    return 0;
                }
                KillTimer(hWnd, IDT_OVERLAY_HIDE);
                pThis->HideWindow();
                return 0;
            }
            else if (wParam == IDT_OVERLAY_UNCLOAK)
            {
                KillTimer(hWnd, IDT_OVERLAY_UNCLOAK);
                if (pThis->m_isVisible)
                {
                    BOOL cloak = FALSE;
                    DwmSetWindowAttribute(hWnd, DWMWA_CLOAK, &cloak, sizeof(cloak));
                    if (g_Settings.osdAnimationEnabled)
                    {
                        pThis->StartShowAnimation();
                    }
                }
                return 0;
            }
        }

        if (uMsg == WM_DPICHANGED && !pThis->m_isVisible)
        {
            HMONITOR hMon = MonitorFromPoint({ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
            UINT dpi = DpiHelper::GetDpiForMonitor(hMon);
            if (dpi == 0) dpi = HIWORD(wParam);
            if (dpi == 0) dpi = 96;
            int width = DpiHelper::Scale(kOsdWidthDip, dpi);
            int height = DpiHelper::Scale(kOsdHeightDip, dpi);
            pThis->m_lastW = width;
            pThis->m_lastH = height;
            SetWindowPos(hWnd, NULL, 0, 0, width, height, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
            pThis->RootGrid().UpdateLayout();
            return 0;
        }

        if (uMsg == WM_NCDESTROY)
        {
            RemoveWindowSubclass(hWnd, SubclassProc, uIdSubclass);
        }

        return DefSubclassProc(hWnd, uMsg, wParam, lParam);
    }
}
