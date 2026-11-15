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

namespace winrt::ThemeSwitcherApp::implementation
{
    static bool DetermineOsdIsDark(int themeState)
    {
        if (g_Settings.osdTheme == 1) return true;  // Dark
        if (g_Settings.osdTheme == 2) return false; // Light
        return (themeState == 0);                   // Follow the theme (0 = dark, 1 = light)
    }

    OverlayWindow::OverlayWindow()
    {
        InitializeComponent();
        int curSys = ThemeManager::GetState();
        int nextTarget = (curSys == 0) ? 1 : 0;
        m_displayedState = nextTarget;
        m_displayedTarget = g_Settings.target;
        bool isDark = DetermineOsdIsDark(m_displayedState);
        RootGrid().RequestedTheme(isDark ? winrt::Microsoft::UI::Xaml::ElementTheme::Dark : winrt::Microsoft::UI::Xaml::ElementTheme::Light);
        SetupWindow();
        SetupBackdrop(isDark);
        UpdateContent(m_displayedState, m_displayedTarget);
        UpdateTheme(isDark);

        // Pre-warm layout and DirectX composition offscreen at startup
        // Pre-scaled to primary monitor DPI to prevent first-hit WM_DPICHANGED / swapchain resize
        if (m_hwnd)
        {
            POINT ptCursor = { 0, 0 };
            GetCursorPos(&ptCursor);
            HMONITOR hMon = MonitorFromPoint(ptCursor, MONITOR_DEFAULTTOPRIMARY);
            UINT dpi = DpiHelper::GetDpiForMonitor(hMon);
            if (dpi == 0) dpi = GetDpiForSystem();
            if (dpi == 0) dpi = 96;

            int width = DpiHelper::Scale(160, dpi);
            int height = DpiHelper::Scale(48, dpi);
            m_lastW = width;
            m_lastH = height;

            BOOL cloak = TRUE;
            DwmSetWindowAttribute(m_hwnd, DWMWA_CLOAK, &cloak, sizeof(cloak));
            HWND hPrevForeground = GetForegroundWindow();
            SetWindowPos(m_hwnd, NULL, -32000, -32000, width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
            this->Activate();
            RootGrid().UpdateLayout();
            ShowWindow(m_hwnd, SW_HIDE);
            if (hPrevForeground && hPrevForeground != m_hwnd)
            {
                SetForegroundWindow(hPrevForeground);
            }
        }
    }

    OverlayWindow::~OverlayWindow()
    {
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

    void OverlayWindow::UpdateTheme(bool isDark)
    {
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

            int backdropType = g_Settings.transparencyEnabled ? DWMSBT_TRANSIENTWINDOW : DWMSBT_NONE;
            DwmSetWindowAttribute(m_hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &backdropType, sizeof(backdropType));

            // Native FlyoutWindow DWM border (matches ClickBrightness)
            COLORREF borderColor = DWMWA_COLOR_DEFAULT;
            DwmSetWindowAttribute(m_hwnd, DWMWA_BORDER_COLOR, &borderColor, sizeof(borderColor));

            int ncPolicy = 0;
            DwmSetWindowAttribute(m_hwnd, DWMWA_NCRENDERING_POLICY, &ncPolicy, sizeof(ncPolicy));

            SetWindowPos(m_hwnd, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED | SWP_NOACTIVATE);
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
    }

    void OverlayWindow::ResetGraphics()
    {
        if (m_isVisible) return; // Do NOT reset or flash backdrop while OSD is visible on screen!
        bool isDark = DetermineOsdIsDark(m_displayedState);
        DetachBackdrop();
        SetupBackdrop(isDark);
        UpdateTheme(isDark);
    }

    void OverlayWindow::UpdateContent(int themeState, const std::wstring& target)
    {
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
    }

    void OverlayWindow::PositionWindow(bool waitExtra)
    {
        if (!m_hwnd || !m_appWindow) return;

        POINT ptCursor;
        GetCursorPos(&ptCursor);
        HMONITOR hMon = MonitorFromPoint(ptCursor, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi = { sizeof(mi) };
        GetMonitorInfoW(hMon, &mi);

        UINT dpi = DpiHelper::GetDpiForMonitor(hMon);
        if (dpi == 0) dpi = GetDpiForWindow(m_hwnd);
        if (dpi == 0) dpi = 96;
        int width = DpiHelper::Scale(160, dpi);
        int height = DpiHelper::Scale(48, dpi);

        int workW = mi.rcWork.right - mi.rcWork.left;
        int targetX = mi.rcWork.left + (workW - width) / 2;
        int targetY = mi.rcWork.bottom - height - DpiHelper::Scale(60, dpi);

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
            SetWindowPos(m_hwnd, HWND_TOPMOST, targetX, targetY, width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
            RootGrid().UpdateLayout();
            UINT uncloakDelay = waitExtra ? 120 : 40;
            SetTimer(m_hwnd, IDT_OVERLAY_UNCLOAK, uncloakDelay, NULL);
            m_isVisible = true;
        }
        else if (posChanged)
        {
            SetWindowPos(m_hwnd, HWND_TOPMOST, targetX, targetY, width, height, swpFlags | SWP_NOZORDER);
        }
    }

    void OverlayWindow::Show(int themeState, const std::wstring& target)
    {
        bool targetIsDark = DetermineOsdIsDark(themeState);
        bool currentIsDark = DetermineOsdIsDark(m_displayedState);
        bool themeMismatch = (targetIsDark != currentIsDark);

        m_displayedState = themeState;
        m_displayedTarget = target;

        if (themeMismatch)
        {
            DetachBackdrop();
            SetupBackdrop(targetIsDark);
        }

        UpdateTheme(targetIsDark);
        UpdateContent(themeState, target);
        PositionWindow(themeMismatch);

        if (m_backdropConfig)
        {
            m_backdropConfig.IsInputActive(true);
        }

        int timeout = g_Settings.osdTimeoutMs;
        if (timeout < 200) timeout = 1500;

        ULONGLONG now = GetTickCount64();
        m_hideAfterTick = now + timeout;

        if (m_hwnd)
        {
            KillTimer(m_hwnd, IDT_OVERLAY_HIDE);
            SetTimer(m_hwnd, IDT_OVERLAY_HIDE, timeout, NULL);
        }
    }

    void OverlayWindow::PreArm(int nextExpectedState, const std::wstring& target)
    {
        if (m_isVisible) return; // Never touch overlay if currently showing on screen
        m_displayedState = nextExpectedState;
        m_displayedTarget = target;
        bool isDark = DetermineOsdIsDark(nextExpectedState);
        UpdateTheme(isDark);
        UpdateContent(nextExpectedState, target);
        if (m_hwnd)
        {
            RootGrid().UpdateLayout();
        }
    }

    void OverlayWindow::SyncWithSystem()
    {
        if (m_isVisible) return;
        int curSys = ThemeManager::GetState();
        int nextExpected = (curSys == 0) ? 1 : 0;
        PreArm(nextExpected, g_Settings.target);
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
            m_isVisible = false;
            m_hideAfterTick = 0;
            m_lastX = -999999;
            m_lastY = -999999;
            if (m_hwnd)
            {
                BOOL cloak = TRUE;
                DwmSetWindowAttribute(m_hwnd, DWMWA_CLOAK, &cloak, sizeof(cloak));
                ::ShowWindow(m_hwnd, SW_HIDE);
            }

            // Pre-arm the hidden overlay with the NEXT expected theme
            // so its swapchain cache is already primed before the next toggle!
            int nextExpected = m_displayedState ^ 1;
            PreArm(nextExpected, m_displayedTarget);
        }
    }

    void OverlayWindow::DestroyWindow()
    {
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
                }
                return 0;
            }
        }

        if (uMsg == WM_NCDESTROY)
        {
            RemoveWindowSubclass(hWnd, SubclassProc, uIdSubclass);
        }

        return DefSubclassProc(hWnd, uMsg, wParam, lParam);
    }
}
