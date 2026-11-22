#include "pch.h"
#include "SettingsWindow.xaml.h"
#if __has_include("SettingsWindow.g.cpp")
#include "SettingsWindow.g.cpp"
#endif
#if __has_include("SettingsWindow.xaml.g.hpp")
#include "SettingsWindow.xaml.g.hpp"
#endif
#if __has_include("SettingsWindow.xaml.g.cpp")
#include "SettingsWindow.xaml.g.cpp"
#endif

#include "Settings.h"
#include "HotkeyManager.h"
#include "ThemeManager.h"
#include "TrayIcon.h"
#include "DpiHelper.h"
#include "ThemeHelper.h"
#include "LocalizationManager.h"
#include "ScheduleManager.h"
#include "OverlayWindow.h"
#include "resource.h"

#include <microsoft.ui.xaml.window.h>
#include <winrt/Microsoft.UI.Composition.SystemBackdrops.h>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Interop.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>
#include <winrt/Windows.Storage.Streams.h>
#include <robuffer.h>
#include <dwmapi.h>
#include <commctrl.h>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "dwmapi.lib")

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;

static HHOOK s_hKeyboardHook = NULL;
static winrt::ThemeSwitcherApp::implementation::SettingsWindow* s_currentCaptureInstance = nullptr;
static UINT s_captureMods = 0;
static winrt::ThemeSwitcherApp::SettingsWindow s_settingsWindow{ nullptr };

struct CityPreset {
    std::wstring name;
    double lat;
    double lon;
};

static const std::vector<CityPreset> s_cityPresets = {
    { L"Amsterdam (Netherlands)", 52.3676, 4.9041 },
    { L"Athens (Greece)", 37.9838, 23.7275 },
    { L"Auckland (New Zealand)", -36.8485, 174.7633 },
    { L"Berlin (Germany)", 52.5200, 13.4050 },
    { L"Budapest (Hungary)", 47.4979, 19.0402 },
    { L"Chicago (USA)", 41.8781, -87.6298 },
    { L"Dubai (UAE)", 25.2048, 55.2708 },
    { L"Dublin (Ireland)", 53.3498, -6.2603 },
    { L"Helsinki (Finland)", 60.1699, 24.9384 },
    { L"Hong Kong", 22.3193, 114.1694 },
    { L"London (UK)", 51.5074, -0.1278 },
    { L"Los Angeles (USA)", 34.0522, -118.2437 },
    { L"Madrid (Spain)", 40.4168, -3.7038 },
    { L"Melbourne (Australia)", -37.8136, 144.9631 },
    { L"New York (USA)", 40.7128, -74.0060 },
    { L"Oslo (Norway)", 59.9139, 10.7522 },
    { L"Paris (France)", 48.8566, 2.3522 },
    { L"Prague (Czech Republic)", 50.0755, 14.4378 },
    { L"Rome (Italy)", 41.9028, 12.4964 },
    { L"San Francisco (USA)", 37.7749, -122.4194 },
    { L"São Paulo (Brazil)", -23.5505, -46.6333 },
    { L"Seoul (South Korea)", 37.5665, 126.9780 },
    { L"Singapore", 1.3521, 103.8198 },
    { L"Stockholm (Sweden)", 59.3293, 18.0686 },
    { L"Sydney (Australia)", -33.8688, 151.2093 },
    { L"Tokyo (Japan)", 35.6762, 139.6503 },
    { L"Toronto (Canada)", 43.6532, -79.3832 },
    { L"Vienna (Austria)", 48.2082, 16.3738 },
    { L"Warsaw (Poland)", 52.2297, 21.0122 },
    { L"Zurich (Switzerland)", 47.3769, 8.5417 }
};

static winrt::Microsoft::UI::Xaml::Media::Imaging::WriteableBitmap HIconToWriteableBitmap(HICON hIcon, int width, int height)
{
    if (!hIcon) return nullptr;

    HDC hdcScreen = GetDC(NULL);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height; // Top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pBits = nullptr;
    HBITMAP hbm = CreateDIBSection(hdcMem, &bmi, DIB_RGB_COLORS, &pBits, NULL, 0);
    if (!hbm || !pBits)
    {
        if (hbm) DeleteObject(hbm);
        DeleteDC(hdcMem);
        ReleaseDC(NULL, hdcScreen);
        return nullptr;
    }

    HBITMAP hOldBm = (HBITMAP)SelectObject(hdcMem, hbm);
    memset(pBits, 0, width * height * 4);

    DrawIconEx(hdcMem, 0, 0, hIcon, width, height, 0, NULL, DI_NORMAL);

    uint32_t* pPixels = reinterpret_cast<uint32_t*>(pBits);
    int totalPixels = width * height;
    bool hasAlpha = false;
    for (int i = 0; i < totalPixels; ++i)
    {
        if ((pPixels[i] & 0xFF000000) != 0)
        {
            hasAlpha = true;
            break;
        }
    }

    if (!hasAlpha)
    {
        for (int i = 0; i < totalPixels; ++i)
        {
            if ((pPixels[i] & 0x00FFFFFF) != 0)
            {
                pPixels[i] |= 0xFF000000;
            }
        }
    }

    winrt::Microsoft::UI::Xaml::Media::Imaging::WriteableBitmap wb(width, height);
    auto buffer = wb.PixelBuffer();
    auto byteAccess = buffer.as<::Windows::Storage::Streams::IBufferByteAccess>();
    byte* destPixels = nullptr;
    if (SUCCEEDED(byteAccess->Buffer(&destPixels)) && destPixels)
    {
        memcpy(destPixels, pBits, width * height * 4);
    }

    SelectObject(hdcMem, hOldBm);
    DeleteObject(hbm);
    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdcScreen);

    wb.Invalidate();
    return wb;
}

namespace winrt::ThemeSwitcherApp::implementation
{
    SettingsWindow::SettingsWindow()
    {
        InitializeComponent();
        bool isDark = ThemeHelper::IsAppInDarkMode(g_Settings.appTheme);
        RootGrid().RequestedTheme(isDark ? ElementTheme::Dark : ElementTheme::Light);
        SetupWindow();
        SetupBackdrop();
        UpdateLocalizedStrings();
        InitializeControls();
        PopulateLanguageList();
        LoadSettingsToUI();
        ApplyTheme();
        m_isInitialized = true;
    }

    SettingsWindow::~SettingsWindow()
    {
        StopCapture(false);
        if (m_hwnd)
        {
            RemoveWindowSubclass(m_hwnd, SubclassProc, 1);
        }
        if (m_hIconBig)
        {
            DestroyIcon(m_hIconBig);
            m_hIconBig = nullptr;
        }
        if (m_hIconSmall)
        {
            DestroyIcon(m_hIconSmall);
            m_hIconSmall = nullptr;
        }
    }

    void SettingsWindow::SetupWindow()
    {
        auto windowNative = this->try_as<::IWindowNative>();
        if (windowNative)
        {
            windowNative->get_WindowHandle(&m_hwnd);
        }

        if (m_hwnd)
        {
            SetWindowSubclass(m_hwnd, SubclassProc, 1, reinterpret_cast<DWORD_PTR>(this));

            HINSTANCE hInst = GetModuleHandleW(NULL);
            UINT winDpi = GetDpiForWindow(m_hwnd);
            if (winDpi == 0) winDpi = 96;
            int cxIcon = GetSystemMetricsForDpi(SM_CXICON, winDpi);
            int cyIcon = GetSystemMetricsForDpi(SM_CYICON, winDpi);
            int cxSmIcon = GetSystemMetricsForDpi(SM_CXSMICON, winDpi);
            int cySmIcon = GetSystemMetricsForDpi(SM_CYSMICON, winDpi);
            m_hIconBig = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, cxIcon, cyIcon, LR_DEFAULTCOLOR);
            m_hIconSmall = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, cxSmIcon, cySmIcon, LR_DEFAULTCOLOR);

            if (m_hIconBig)
            {
                SendMessageW(m_hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(m_hIconBig));
                SetClassLongPtrW(m_hwnd, GCLP_HICON, reinterpret_cast<LONG_PTR>(m_hIconBig));
            }
            if (m_hIconSmall)
            {
                SendMessageW(m_hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(m_hIconSmall));
                SetClassLongPtrW(m_hwnd, GCLP_HICONSM, reinterpret_cast<LONG_PTR>(m_hIconSmall));
            }

            bool isDark = ThemeHelper::IsAppInDarkMode(g_Settings.appTheme);
            BOOL darkMode = isDark ? TRUE : FALSE;
            DwmSetWindowAttribute(m_hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkMode, sizeof(darkMode));

            COLORREF captionColor = isDark ? RGB(32, 32, 32) : RGB(243, 243, 243);
            DwmSetWindowAttribute(m_hwnd, DWMWA_CAPTION_COLOR, &captionColor, sizeof(captionColor));

            BOOL forceDisableTransitions = FALSE;
            DwmSetWindowAttribute(m_hwnd, DWMWA_TRANSITIONS_FORCEDISABLED, &forceDisableTransitions, sizeof(forceDisableTransitions));

            auto windowId = Microsoft::UI::GetWindowIdFromWindow(m_hwnd);
            m_appWindow = Microsoft::UI::Windowing::AppWindow::GetFromWindowId(windowId);

            if (m_appWindow)
            {
                m_appWindow.Title(L"ClickDarkMode settings");
                SetWindowTextW(m_hwnd, L"ClickDarkMode settings");

                wchar_t exePath[MAX_PATH] = {};
                GetModuleFileNameW(NULL, exePath, MAX_PATH);
                wchar_t* lastSlash = wcsrchr(exePath, L'\\');
                std::wstring exeDir = lastSlash ? std::wstring(exePath, lastSlash + 1) : L"";

                std::wstring iconPath = exeDir + L"program.ico";
                if (GetFileAttributesW(iconPath.c_str()) == INVALID_FILE_ATTRIBUTES)
                {
                    iconPath = exeDir + L"resources\\program.ico";
                }

                bool iconSet = false;
                if (GetFileAttributesW(iconPath.c_str()) != INVALID_FILE_ATTRIBUTES)
                {
                    try
                    {
                        m_appWindow.SetIcon(iconPath);
                        iconSet = true;
                    }
                    catch (...)
                    {
                    }
                }

                if (!iconSet && (m_hIconBig || m_hIconSmall))
                {
                    try
                    {
                        HICON hBest = m_hIconBig ? m_hIconBig : m_hIconSmall;
                        m_appWindow.SetIcon(winrt::Microsoft::UI::GetIconIdFromIcon(hBest));
                    }
                    catch (...)
                    {
                    }
                }

                UINT dpi = GetDpiForWindow(m_hwnd);
                if (dpi == 0) dpi = 96;

                HMONITOR hMon = MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST);
                MONITORINFO mi = { sizeof(mi) };
                GetMonitorInfoW(hMon, &mi);
                int screenW = mi.rcWork.right - mi.rcWork.left;
                int screenH = mi.rcWork.bottom - mi.rcWork.top;

                int width = DpiHelper::Scale(860, dpi);
                int height = DpiHelper::Scale(760, dpi);
                if (height > screenH - DpiHelper::Scale(50, dpi))
                {
                    height = screenH - DpiHelper::Scale(50, dpi);
                }

                int x = mi.rcWork.left + (screenW - width) / 2;
                int y = mi.rcWork.top + (screenH - height) / 2;

                m_appWindow.MoveAndResize({ x, y, width, height });

                m_appWindow.Closing([this](auto&&, Microsoft::UI::Windowing::AppWindowClosingEventArgs const&)
                {
                    m_isClosing = true;
                    StopCapture(false);

                    bool isDark = ThemeHelper::IsAppInDarkMode(g_Settings.appTheme);
                    if (m_hwnd)
                    {
                        BOOL darkMode = isDark ? TRUE : FALSE;
                        DwmSetWindowAttribute(m_hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkMode, sizeof(darkMode));

                        COLORREF captionColor = isDark ? RGB(32, 32, 32) : RGB(243, 243, 243);
                        DwmSetWindowAttribute(m_hwnd, DWMWA_CAPTION_COLOR, &captionColor, sizeof(captionColor));
                    }

                    winrt::Windows::UI::Color solidBg = isDark
                        ? winrt::Microsoft::UI::ColorHelper::FromArgb(255, 32, 32, 32)
                        : winrt::Microsoft::UI::ColorHelper::FromArgb(255, 243, 243, 243);
                    RootGrid().Background(winrt::Microsoft::UI::Xaml::Media::SolidColorBrush(solidBg));

                    s_settingsWindow = nullptr;
                });
            }
        }

        this->Closed([this](auto&&, auto&&)
        {
            StopCapture(false);
            s_settingsWindow = nullptr;
            TrimProcessWorkingSetAsync();
        });

        ExtendsContentIntoTitleBar(true);
        SetTitleBar(AppTitleBar());

        AppTitleBar().SizeChanged([this](auto&&, auto&&)
        {
            UpdateTitleBarDragRegions();
        });

        SettingsSearchBox().SizeChanged([this](auto&&, auto&&)
        {
            UpdateTitleBarDragRegions();
        });

        SettingsSearchBox().Loaded([this](auto&&, auto&&)
        {
            UpdateTitleBarDragRegions();
        });
    }

    void SettingsWindow::UpdateTitleBarDragRegions()
    {
        try
        {
            if (!m_appWindow) return;
            auto windowId = m_appWindow.Id();
            auto nonClientSource = winrt::Microsoft::UI::Input::InputNonClientPointerSource::GetForWindowId(windowId);
            if (!nonClientSource) return;

            auto searchBox = SettingsSearchBox();
            if (!searchBox) return;

            auto xamlRoot = searchBox.XamlRoot();
            if (!xamlRoot) return;

            double scale = xamlRoot.RasterizationScale();
            auto transform = searchBox.TransformToVisual(nullptr);
            auto bounds = transform.TransformBounds(winrt::Windows::Foundation::Rect{
                0.0f,
                0.0f,
                static_cast<float>(searchBox.ActualWidth()),
                static_cast<float>(searchBox.ActualHeight())
            });

            if (bounds.Width <= 0.0f || bounds.Height <= 0.0f)
            {
                return;
            }

            winrt::Windows::Graphics::RectInt32 passthroughRect{
                static_cast<int32_t>(std::round(bounds.X * scale)),
                static_cast<int32_t>(std::round(bounds.Y * scale)),
                static_cast<int32_t>(std::round(bounds.Width * scale)),
                static_cast<int32_t>(std::round(bounds.Height * scale))
            };

            nonClientSource.SetRegionRects(
                winrt::Microsoft::UI::Input::NonClientRegionKind::Passthrough,
                { passthroughRect }
            );
        }
        catch (...)
        {
        }
    }

    void SettingsWindow::SetupBackdrop()
    {
        try
        {
            if (g_Settings.transparencyEnabled)
            {
                if (!SystemBackdrop())
                {
                    SystemBackdrop(winrt::Microsoft::UI::Xaml::Media::MicaBackdrop());
                }
            }
            else
            {
                if (SystemBackdrop())
                {
                    SystemBackdrop(nullptr);
                }
            }
        }
        catch (...)
        {
        }
    }

    std::wstring SettingsWindow::FormatIconLabel(const std::wstring& path, int number)
    {
        std::wstring pLower = path;
        std::transform(pLower.begin(), pLower.end(), pLower.begin(), ::towlower);
        size_t slash = pLower.find_last_of(L"\\/");
        std::wstring fname = (slash != std::wstring::npos) ? pLower.substr(slash + 1) : pLower;
        std::wstring bundledTag = g_Loc.Get(L"Icon_BundledTag", L"(Bundled)");

        if (fname == L"sun.ico") return L"sun.ico " + bundledTag;
        if (fname == L"sun-white.ico" || fname == L"sun-icon-white.ico") return L"sun-white.ico " + bundledTag;
        if (fname == L"sun-dark.ico" || fname == L"sun-icon-black.ico" || fname == L"sun-black.ico") return L"sun-dark.ico " + bundledTag;
        if (fname == L"moon.ico") return L"moon.ico " + bundledTag;
        if (fname == L"moon-white.ico" || fname == L"moon-icon-white.ico") return L"moon-white.ico " + bundledTag;
        if (fname == L"moon-dark.ico" || fname == L"moon-icon-black.ico" || fname == L"moon-black.ico") return L"moon-dark.ico " + bundledTag;
        if (fname == L"program.ico") return L"program.ico " + bundledTag;

        // If path is a system DLL, show clean basename
        if (slash != std::wstring::npos)
        {
            return path.substr(slash + 1) + L", " + std::to_wstring(number);
        }
        return path + L", " + std::to_wstring(number);
    }

    void SettingsWindow::UpdateIconPreviews()
    {
        // Light icon preview
        HICON hLight = TrayIcon::LoadIconResource(g_Settings.iconFile, g_Settings.iconNumber, 32, 32);
        if (hLight)
        {
            auto wbLight = HIconToWriteableBitmap(hLight, 32, 32);
            if (wbLight) LightIconImage().Source(wbLight);
            DestroyIcon(hLight);
        }
        LightIconNameText().Text(FormatIconLabel(g_Settings.iconFile, g_Settings.iconNumber).c_str());

        // Dark icon preview
        HICON hDark = TrayIcon::LoadIconResource(g_Settings.darkIconFile, g_Settings.darkIconNumber, 32, 32);
        if (hDark)
        {
            auto wbDark = HIconToWriteableBitmap(hDark, 32, 32);
            if (wbDark) DarkIconImage().Source(wbDark);
            DestroyIcon(hDark);
        }
        DarkIconNameText().Text(FormatIconLabel(g_Settings.darkIconFile, g_Settings.darkIconNumber).c_str());

        UpdatePresetComboBoxSelections();
    }

    void SettingsWindow::UpdatePresetComboBoxSelections()
    {
        m_isUpdatingUI = true;

        std::wstring pLower = g_Settings.iconFile;
        std::transform(pLower.begin(), pLower.end(), pLower.begin(), ::towlower);
        size_t slash = pLower.find_last_of(L"\\/");
        std::wstring fname = (slash != std::wstring::npos) ? pLower.substr(slash + 1) : pLower;

        int lightIdx = -1;
        if (fname == L"sun.ico") lightIdx = 0;
        else if (fname == L"sun-white.ico" || fname == L"sun-icon-white.ico") lightIdx = 1;
        else if (fname == L"sun-dark.ico" || fname == L"sun-icon-black.ico" || fname == L"sun-black.ico") lightIdx = 2;
        else if (fname == L"shell32.dll" && g_Settings.iconNumber == 44) lightIdx = 3;
        else if (fname == L"shell32.dll" && g_Settings.iconNumber == 16) lightIdx = 4;

        LightPresetComboBox().SelectedIndex(lightIdx);

        std::wstring dpLower = g_Settings.darkIconFile;
        std::transform(dpLower.begin(), dpLower.end(), dpLower.begin(), ::towlower);
        size_t dslash = dpLower.find_last_of(L"\\/");
        std::wstring dfname = (dslash != std::wstring::npos) ? dpLower.substr(dslash + 1) : dpLower;

        int darkIdx = -1;
        if (dfname == L"moon.ico") darkIdx = 0;
        else if (dfname == L"moon-white.ico" || dfname == L"moon-icon-white.ico") darkIdx = 1;
        else if (dfname == L"moon-dark.ico" || dfname == L"moon-icon-black.ico" || dfname == L"moon-black.ico") darkIdx = 2;
        else if (dfname == L"shell32.dll" && g_Settings.darkIconNumber == 26) darkIdx = 3;

        DarkPresetComboBox().SelectedIndex(darkIdx);

        m_isUpdatingUI = false;
    }

    void SettingsWindow::UpdateIconVisibility()
    {
        bool isCustom = (g_Settings.trayIconMode == 1);
        bool isDisabled = (g_Settings.trayIconMode == 2);

        DualIconsCard().Visibility(isDisabled ? Visibility::Collapsed : Visibility::Visible);

        auto customVis = isCustom ? Visibility::Visible : Visibility::Collapsed;
        LightIconSectionTitle().Visibility(customVis);
        LightIconCard().Visibility(customVis);
        LightPresetsCard().Visibility(customVis);

        auto darkVis = (isCustom && g_Settings.dualIcons) ? Visibility::Visible : Visibility::Collapsed;
        DarkIconSectionTitle().Visibility(darkVis);
        DarkIconCard().Visibility(darkVis);
        DarkPresetsCard().Visibility(darkVis);
    }

    void SettingsWindow::UpdateDarkIconVisibility()
    {
        UpdateIconVisibility();
    }

    void SettingsWindow::UpdateHotkeyVisibility()
    {
        auto vis = g_Settings.hotkeyEnabled ? Visibility::Visible : Visibility::Collapsed;
        HotkeyCombinationCard().Visibility(vis);
        HotkeyOsdCard().Visibility(vis);
        ResetHotkeysCard().Visibility(vis);
    }

    void SettingsWindow::EnsureTrayTabLoaded()
    {
        if (m_trayTabLoaded) return;
        m_trayTabLoaded = true;
        UpdateIconPreviews();
        UpdatePresetComboBoxSelections();
    }

    void SettingsWindow::EnsureScheduleTabLoaded()
    {
        if (m_scheduleTabLoaded) return;
        m_scheduleTabLoaded = true;
        PopulateLocationPresets();
        UpdateDetectedLocationText();
        UpdateSunPreview();
    }

    void SettingsWindow::EnsureAboutTabLoaded()
    {
        if (m_aboutTabLoaded) return;
        m_aboutTabLoaded = true;
        try
        {
            HICON hApp = (HICON)::LoadImageW(::GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, 48, 48, LR_DEFAULTCOLOR);
            if (!hApp)
            {
                hApp = (HICON)::LoadImageW(::GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE);
            }
            if (hApp)
            {
                auto wbApp = HIconToWriteableBitmap(hApp, 48, 48);
                if (wbApp)
                {
                    AboutAppIconImage().Source(wbApp);
                }
                DestroyIcon(hApp);
            }
        }
        catch (...) {}
    }

    void SettingsWindow::InitializeControls()
    {
        // Load actual high-resolution program icon for TitleBar (supersampled to prevent pixelation)
        try
        {
            HICON hTitleIcon = (HICON)::LoadImageW(::GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, 48, 48, LR_DEFAULTCOLOR);
            bool destroyTitleIcon = (hTitleIcon != NULL);
            if (!hTitleIcon)
            {
                hTitleIcon = (HICON)::LoadImageW(::GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR);
                destroyTitleIcon = (hTitleIcon != NULL);
            }
            if (!hTitleIcon && m_hIconBig)
            {
                hTitleIcon = m_hIconBig;
                destroyTitleIcon = false;
            }
            if (!hTitleIcon && m_hIconSmall)
            {
                hTitleIcon = m_hIconSmall;
                destroyTitleIcon = false;
            }
            if (hTitleIcon)
            {
                auto wbTitle = HIconToWriteableBitmap(hTitleIcon, 48, 48);
                if (wbTitle)
                {
                    AppTitleBarIcon().Source(wbTitle);
                }
                if (destroyTitleIcon)
                {
                    ::DestroyIcon(hTitleIcon);
                }
            }
        }
        catch (...) {}

        m_generalPanel = GeneralPanel();
        m_trayPanel = TrayPanel();
        m_shortcutsPanel = ShortcutsPanel();
        m_schedulePanel = SchedulePanel();
        m_aboutPanel = AboutPanel();

        auto initialChildren = SubmenuContainer().Children();
        initialChildren.Clear();
        m_generalPanel.Visibility(Visibility::Visible);
        initialChildren.Append(m_generalPanel);

        // Navigation Menu
        NavView().SelectionChanged([this](NavigationView const&, NavigationViewSelectionChangedEventArgs const& args)
        {
            auto selectedItem = args.SelectedItem().try_as<NavigationViewItem>();
            if (!selectedItem) return;
            auto tag = winrt::unbox_value_or<winrt::hstring>(selectedItem.Tag(), L"");

            auto children = SubmenuContainer().Children();
            children.Clear();

            if (tag == L"General")
            {
                m_generalPanel.Visibility(Visibility::Visible);
                children.Append(m_generalPanel);
            }
            else if (tag == L"Tray")
            {
                EnsureTrayTabLoaded();
                m_trayPanel.Visibility(Visibility::Visible);
                children.Append(m_trayPanel);
            }
            else if (tag == L"Shortcuts")
            {
                m_shortcutsPanel.Visibility(Visibility::Visible);
                children.Append(m_shortcutsPanel);
            }
            else if (tag == L"Schedule")
            {
                EnsureScheduleTabLoaded();
                m_schedulePanel.Visibility(Visibility::Visible);
                children.Append(m_schedulePanel);
                UpdateSunPreview();
            }
            else if (tag == L"About")
            {
                EnsureAboutTabLoaded();
                m_aboutPanel.Visibility(Visibility::Visible);
                children.Append(m_aboutPanel);
            }
        });

        // Settings Search AutoSuggestBox
        struct SettingSearchEntry
        {
            std::wstring title;
            std::wstring category;
            std::wstring keywords;
            std::wstring navTag;
            winrt::Microsoft::UI::Xaml::FrameworkElement targetControl{ nullptr };
        };

        auto toLower = [](std::wstring s) -> std::wstring
        {
            if (!s.empty())
            {
                CharLowerBuffW(&s[0], static_cast<DWORD>(s.size()));
            }
            return s;
        };

        auto buildSearchEntries = [this]() -> std::vector<SettingSearchEntry>
        {
            std::wstring catGen = g_Loc.Get(L"Tab_General", L"General");
            std::wstring catTray = g_Loc.Get(L"Tab_TrayIcon", L"Tray Icon");
            std::wstring catSc = g_Loc.Get(L"Tab_Hotkey", L"Shortcuts");
            std::wstring catSchedule = g_Loc.Get(L"Tab_Schedule", L"Schedule");
            std::wstring catAbout = g_Loc.Get(L"Tab_About", L"About");

            return {
                // General
                { g_Loc.Get(L"Gen_Language", L"Language"), catGen, L"language locale english magyar nyelv fordítás", L"General", LanguageComboBox() },
                { g_Loc.Get(L"Gen_AppTheme", L"Application Theme"), catGen, L"theme dark light appearance sötét világos téma külső alkalmazás", L"General", AppThemeComboBox() },
                { g_Loc.Get(L"Gen_TrayMenuTheme", L"Tray Menu Theme"), catGen, L"tray menu theme context right click tálca jobb gomb menü téma", L"General", TrayMenuThemeComboBox() },
                { g_Loc.Get(L"Gen_Transparency", L"Mica Transparency"), catGen, L"mica transparency acrylic translucent blur áttetsző háttér", L"General", TransparencyToggle() },
                { g_Loc.Get(L"Gen_TargetTheme", L"Theme Switch Mode"), catGen, L"target mode apps system windows mindkettő alkalmazás rendszer", L"General", TargetComboBox() },
                { g_Loc.Get(L"Gen_CooldownLabel", L"Accidental Click Cooldown (ms)"), catGen, L"cooldown delay accidental double click késleltetés véletlen kattintás", L"General", CooldownSlider() },
                { g_Loc.Get(L"Gen_OsdTheme", L"OSD Theme"), catGen, L"osd theme appearance dark light banner lebegő téma", L"General", OsdThemeComboBox() },
                { g_Loc.Get(L"Gen_OsdTimeoutTitle", L"Hide Time (ms)"), catGen, L"osd banner duration timeout hide time display lebegő eltűnési idő", L"General", OsdTimeoutSlider() },
                { g_Loc.Get(L"Gen_OsdDnd", L"Suppress in Do Not Disturb"), catGen, L"osd dnd do not disturb focus assist quiet hours ne zavarjanak fókuszsegéd elrejtés banner", L"General", OsdDndToggle() },
                { g_Loc.Get(L"Gen_StartWithWindows", L"Start with Windows"), catGen, L"startup boot autostart windows indítás indulás", L"General", StartupToggle() },
                { g_Loc.Get(L"Gen_RestoreDefaults", L"Restore Defaults"), catGen, L"restore reset factory defaults alapértelmezések visszaállítás", L"General", RestoreDefaultsButton() },

                // Tray Icon
                { g_Loc.Get(L"Icon_TrayIconMode", L"Tray Icon"), catTray, L"tray icon mode appearance follow windows custom disabled tálcaikon megjelenés követés egyéni letiltva", L"Tray", TrayIconModeComboBox() },
                { g_Loc.Get(L"Icon_DualCheckbox", L"Use Separate Icons"), catTray, L"dual separate distinct icons külön ikon tálcaikon", L"Tray", DualIconsToggle() },
                { g_Loc.Get(L"Icon_ShowOsd", L"Show On-Screen Display (OSD)"), catTray, L"tray osd overlay banner floating visual feedback lebegő visszajelzés kijelző", L"Tray", TrayOsdToggle() },
                { g_Loc.Get(L"Icon_LightGroup", L"Light / Primary Mode Icon"), catTray, L"light icon browse sun világos ikon nap ico választás", L"Tray", BrowseLightIconButton() },
                { g_Loc.Get(L"Icon_LightPresetsTitle", L"Light Mode Presets"), catTray, L"preset light sun white dark star monitor nap fehér sötét csillag monitor", L"Tray", LightPresetComboBox() },
                { g_Loc.Get(L"Icon_DarkGroup", L"Dark Mode Icon"), catTray, L"dark icon browse moon sötét ikon hold választás", L"Tray", BrowseDarkIconButton() },
                { g_Loc.Get(L"Icon_DarkPresetsTitle", L"Dark Mode Presets"), catTray, L"preset dark moon white dark crescent monitor hold fehér sötét félhold", L"Tray", DarkPresetComboBox() },

                // Shortcuts
                { g_Loc.Get(L"Hk_EnableCheckbox", L"Enable Global Shortcut"), catSc, L"hotkey shortcut global enable gyorsbillentyű engedélyezés billentyű", L"Shortcuts", HotkeyToggle() },
                { g_Loc.Get(L"Hk_CombinationLabel", L"Shortcut Combination"), catSc, L"hotkey combination key capture ctrl alt shift win billentyűkombináció", L"Shortcuts", CaptureHotkeyButton() },
                { g_Loc.Get(L"Hk_ShowOsd", L"Show On-Screen Display (OSD)"), catSc, L"shortcut hotkey osd overlay banner floating visual feedback gyorsbillentyű lebegő visszajelzés kijelző", L"Shortcuts", HotkeyOsdToggle() },
                { g_Loc.Get(L"Hk_ResetShortcutsTitle", L"Reset Shortcuts"), catSc, L"reset shortcuts hotkeys default alaphelyzet visszaállítás", L"Shortcuts", ResetHotkeysButton() },

                // Schedule
                { g_Loc.Get(L"Schedule_ModeTitle", L"Automatic Theme Switching"), catSchedule, L"schedule timer automatic sunset sunrise napkelte napnyugta időzítés automatikus", L"Schedule", ScheduleModeComboBox() },
                { g_Loc.Get(L"Schedule_ShowOsd", L"Show OSD On Scheduled Switch"), catSchedule, L"schedule osd banner feedback időzítés lebegő visszajelzés", L"Schedule", ScheduleShowOsdToggle() },
                { g_Loc.Get(L"Schedule_LightStart", L"Light Mode Start Time"), catSchedule, L"schedule light time start időzítés világos kezdés", L"Schedule", ScheduleLightTimePicker() },
                { g_Loc.Get(L"Schedule_DarkStart", L"Dark Mode Start Time"), catSchedule, L"schedule dark time start időzítés sötét kezdés", L"Schedule", ScheduleDarkTimePicker() },
                { g_Loc.Get(L"Schedule_SunSourceTitle", L"Solar Location Source"), catSchedule, L"solar location source method service gps coordinates napállás helymeghatározás forrás", L"Schedule", SunSourceComboBox() },
                { g_Loc.Get(L"Schedule_DetectTitle", L"Auto-Detect Location"), catSchedule, L"detect location gps coordinates helymeghatározás koordináták", L"Schedule", DetectLocationButton() },
                { g_Loc.Get(L"Schedule_LatitudeTitle", L"Latitude"), catSchedule, L"latitude szélesség coordinates koordináták", L"Schedule", LatitudeNumberBox() },
                { g_Loc.Get(L"Schedule_LongitudeTitle", L"Longitude"), catSchedule, L"longitude hosszúság coordinates koordináták", L"Schedule", LongitudeNumberBox() },
                { g_Loc.Get(L"Schedule_SunriseOffsetTitle", L"Sunrise Offset (minutes)"), catSchedule, L"sunrise napkelte offset eltolás perc minutes", L"Schedule", SunriseOffsetNumberBox() },
                { g_Loc.Get(L"Schedule_SunsetOffsetTitle", L"Sunset Offset (minutes)"), catSchedule, L"sunset napnyugta offset eltolás perc minutes", L"Schedule", SunsetOffsetNumberBox() },

                // About
                { g_Loc.Get(L"About_HeaderTitle", L"About ClickDarkMode"), catAbout, L"about version license mit info névjegy verzió licenc", L"About", AboutPanel() }
            };
        };

        auto navigateToEntry = [this](const SettingSearchEntry& entry)
        {
            if (entry.navTag == L"General") NavView().SelectedItem(NavItemGeneral());
            else if (entry.navTag == L"Tray") NavView().SelectedItem(NavItemTray());
            else if (entry.navTag == L"Shortcuts") NavView().SelectedItem(NavItemShortcuts());
            else if (entry.navTag == L"Schedule") NavView().SelectedItem(NavItemSchedule());
            else if (entry.navTag == L"About") NavView().SelectedItem(NavItemAbout());

            if (entry.targetControl)
            {
                this->DispatcherQueue().TryEnqueue([ctrl = entry.targetControl]()
                {
                    try
                    {
                        if (ctrl.Visibility() == winrt::Microsoft::UI::Xaml::Visibility::Visible)
                        {
                            ctrl.StartBringIntoView();
                            if (auto focusable = ctrl.try_as<winrt::Microsoft::UI::Xaml::Controls::Control>())
                            {
                                focusable.Focus(winrt::Microsoft::UI::Xaml::FocusState::Programmatic);
                            }
                        }
                    }
                    catch (...) {}
                });
            }
        };

        SettingsSearchBox().GettingFocus([](winrt::Microsoft::UI::Xaml::UIElement const&, winrt::Microsoft::UI::Xaml::Input::GettingFocusEventArgs const& args)
        {
            if (args.FocusState() == winrt::Microsoft::UI::Xaml::FocusState::Programmatic)
            {
                args.Cancel(true);
            }
        });

        SettingsSearchBox().TextChanged([this, buildSearchEntries, toLower](AutoSuggestBox const& sender, AutoSuggestBoxTextChangedEventArgs const& args)
        {
            if (args.Reason() != AutoSuggestionBoxTextChangeReason::UserInput) return;

            std::wstring query = toLower(sender.Text().c_str());
            if (query.empty())
            {
                sender.ItemsSource(nullptr);
                return;
            }

            auto entries = buildSearchEntries();
            auto suggestions = winrt::single_threaded_observable_vector<winrt::hstring>();
            for (const auto& entry : entries)
            {
                std::wstring haystack = toLower(entry.title + L" " + entry.category + L" " + entry.keywords);
                if (haystack.find(query) != std::wstring::npos)
                {
                    suggestions.Append(winrt::hstring(entry.title + L"  (" + entry.category + L")"));
                }
            }

            if (suggestions.Size() == 0)
            {
                suggestions.Append(winrt::hstring(g_Loc.Get(L"Search_NoResults", L"No results found")));
            }
            sender.ItemsSource(suggestions);
        });

        SettingsSearchBox().SuggestionChosen([buildSearchEntries](AutoSuggestBox const& sender, AutoSuggestBoxSuggestionChosenEventArgs const& args)
        {
            auto chosen = winrt::unbox_value_or<winrt::hstring>(args.SelectedItem(), L"");
            if (chosen == g_Loc.Get(L"Search_NoResults", L"No results found"))
            {
                sender.Text(L"");
                return;
            }
            auto entries = buildSearchEntries();
            for (const auto& entry : entries)
            {
                std::wstring label = entry.title + L"  (" + entry.category + L")";
                if (chosen == label)
                {
                    sender.Text(winrt::hstring(entry.title));
                    break;
                }
            }
        });

        SettingsSearchBox().QuerySubmitted([buildSearchEntries, toLower, navigateToEntry](AutoSuggestBox const& sender, AutoSuggestBoxQuerySubmittedEventArgs const& args)
        {
            auto entries = buildSearchEntries();
            std::wstring chosenStr = winrt::unbox_value_or<winrt::hstring>(args.ChosenSuggestion(), L"").c_str();
            if (!chosenStr.empty())
            {
                for (const auto& entry : entries)
                {
                    std::wstring label = entry.title + L"  (" + entry.category + L")";
                    if (chosenStr == label)
                    {
                        navigateToEntry(entry);
                        sender.Text(L"");
                        sender.ItemsSource(nullptr);
                        return;
                    }
                }
            }

            std::wstring query = toLower(args.QueryText().c_str());
            if (!query.empty())
            {
                for (const auto& entry : entries)
                {
                    std::wstring haystack = toLower(entry.title + L" " + entry.category + L" " + entry.keywords);
                    if (haystack.find(query) != std::wstring::npos)
                    {
                        navigateToEntry(entry);
                        sender.Text(L"");
                        sender.ItemsSource(nullptr);
                        return;
                    }
                }
            }
        });

        // Language Combo
        LanguageComboBox().SelectionChanged([this](auto&&, auto&&)
        {
            if (m_isUpdatingUI) return;
            int idx = LanguageComboBox().SelectedIndex();
            auto langs = g_Loc.GetAvailableLanguages();
            if (idx >= 0 && idx < (int)langs.size())
            {
                g_Settings.language = langs[idx].code;
                g_Loc.SetLanguage(g_Settings.language);
                g_Settings.Save();
                UpdateLocalizedStrings();
                PopulateLanguageList();
                LoadSettingsToUI();
                RefreshComboBoxSelections();
                TrayIcon::SyncUI();
            }
        });

        // App Theme Combo
        AppThemeComboBox().SelectionChanged([this](auto&&, auto&&)
        {
            if (m_isUpdatingUI) return;
            auto item = AppThemeComboBox().SelectedItem().try_as<ComboBoxItem>();
            if (item)
            {
                auto tag = winrt::unbox_value_or<winrt::hstring>(item.Tag(), L"0");
                try { g_Settings.appTheme = std::stoi(tag.c_str()); } catch (...) { g_Settings.appTheme = 0; }
                g_Settings.Save();
                ApplyTheme();
            }
        });

        // Tray Menu Theme Combo
        TrayMenuThemeComboBox().SelectionChanged([this](auto&&, auto&&)
        {
            if (m_isUpdatingUI) return;
            auto item = TrayMenuThemeComboBox().SelectedItem().try_as<ComboBoxItem>();
            if (item)
            {
                auto tag = winrt::unbox_value_or<winrt::hstring>(item.Tag(), L"0");
                try { g_Settings.trayMenuTheme = std::stoi(tag.c_str()); } catch (...) { g_Settings.trayMenuTheme = 0; }
                g_Settings.Save();
            }
        });

        // Transparency Toggle
        TransparencyToggle().Toggled([this](auto&&, auto&&)
        {
            if (m_isUpdatingUI) return;
            g_Settings.transparencyEnabled = TransparencyToggle().IsOn();
            TransparencyStatusText().Text(g_Loc.Get(g_Settings.transparencyEnabled ? L"Common_On" : L"Common_Off", g_Settings.transparencyEnabled ? L"On" : L"Off").c_str());
            g_Settings.Save();
            SetupBackdrop();
            ApplyTheme();
        });

        // Target Theme Combo
        TargetComboBox().SelectionChanged([this](auto&&, auto&&)
        {
            if (m_isUpdatingUI) return;
            auto item = TargetComboBox().SelectedItem().try_as<ComboBoxItem>();
            if (item)
            {
                auto tag = winrt::unbox_value_or<winrt::hstring>(item.Tag(), L"Both");
                g_Settings.target = tag.c_str();
                g_Settings.Save();
                UpdateThemeStatus();
                TrayIcon::SyncUI();
                g_OverlayWindow.SyncWithSystem();
            }
        });

        // Cooldown Slider & NumberBox sync
        CooldownSlider().ValueChanged([this](auto&&, auto&& e)
        {
            if (m_isUpdatingUI) return;
            m_isUpdatingUI = true;
            CooldownNumberBox().Value(e.NewValue());
            g_Settings.toggleDelayMs = static_cast<int>(e.NewValue());
            g_Settings.Save();
            m_isUpdatingUI = false;
        });

        CooldownNumberBox().ValueChanged([this](auto&&, auto&& e)
        {
            if (m_isUpdatingUI) return;
            m_isUpdatingUI = true;
            double val = e.NewValue();
            if (std::isnan(val)) val = 500;
            CooldownSlider().Value(val);
            g_Settings.toggleDelayMs = static_cast<int>(val);
            g_Settings.Save();
            m_isUpdatingUI = false;
        });

        // OSD Theme Combo
        OsdThemeComboBox().SelectionChanged([this](auto&&, auto&&)
        {
            if (m_isUpdatingUI) return;
            auto item = OsdThemeComboBox().SelectedItem().try_as<ComboBoxItem>();
            if (item)
            {
                auto tag = winrt::unbox_value_or<winrt::hstring>(item.Tag(), L"0");
                try { g_Settings.osdTheme = std::stoi(tag.c_str()); } catch (...) { g_Settings.osdTheme = 0; }
                g_Settings.Save();
                g_OverlayWindow.ResetGraphicsResources();
                g_OverlayWindow.SyncWithSystem();
            }
        });

        // OSD Timeout Slider & NumberBox sync
        OsdTimeoutSlider().ValueChanged([this](auto&&, auto&& e)
        {
            if (m_isUpdatingUI) return;
            m_isUpdatingUI = true;
            OsdTimeoutNumberBox().Value(e.NewValue());
            g_Settings.osdTimeoutMs = static_cast<int>(e.NewValue());
            g_Settings.Save();
            m_isUpdatingUI = false;
        });

        OsdTimeoutNumberBox().ValueChanged([this](auto&&, auto&& e)
        {
            if (m_isUpdatingUI) return;
            m_isUpdatingUI = true;
            double val = e.NewValue();
            if (std::isnan(val) || val < 500) val = 500;
            if (val > 5000) val = 5000;
            OsdTimeoutSlider().Value(val);
            g_Settings.osdTimeoutMs = static_cast<int>(val);
            g_Settings.Save();
            m_isUpdatingUI = false;
        });

        // OSD Do Not Disturb Toggle
        OsdDndToggle().Toggled([this](auto&&, auto&&)
        {
            if (m_isUpdatingUI) return;
            g_Settings.osdSuppressOnDnd = OsdDndToggle().IsOn();
            OsdDndStatusText().Text(g_Loc.Get(g_Settings.osdSuppressOnDnd ? L"Common_On" : L"Common_Off", g_Settings.osdSuppressOnDnd ? L"On" : L"Off").c_str());
            g_Settings.Save();
        });

        // Startup Toggle
        StartupToggle().Toggled([this](auto&&, auto&&)
        {
            if (m_isUpdatingUI) return;
            if (!Config::IsInstalledInProgramFiles())
            {
                StartupToggle().IsOn(g_Settings.startWithWindows);
                return;
            }
            g_Settings.startWithWindows = StartupToggle().IsOn();
            StartupStatusText().Text(g_Loc.Get(g_Settings.startWithWindows ? L"Common_On" : L"Common_Off", g_Settings.startWithWindows ? L"On" : L"Off").c_str());
            g_Settings.Save();
        });

        // Restore Defaults Flyout Confirm Button
        ConfirmRestoreDefaultsButton().Click([this](auto&&, auto&&)
        {
            if (RestoreDefaultsFlyout())
            {
                RestoreDefaultsFlyout().Hide();
            }

            g_Settings.language = L"default";
            g_Settings.appTheme = 0;
            g_Settings.trayMenuTheme = 0;
            g_Settings.transparencyEnabled = true;
            g_Settings.target = L"Both";
            g_Settings.toggleDelayMs = 500;
            g_Settings.startWithWindows = false;
            g_Settings.trayIconMode = 0;
            g_Settings.dualIcons = true;
            g_Settings.iconFile = L"sun.ico";
            g_Settings.iconNumber = 1;
            g_Settings.darkIconFile = L"moon.ico";
            g_Settings.darkIconNumber = 1;
            g_Settings.hotkeyEnabled = false;
            g_Settings.hotkey = L"^!t";
            g_Settings.osdHotkeyEnabled = true;
            g_Settings.osdIconClickEnabled = false;
            g_Settings.osdTimeoutMs = 2000;
            g_Settings.osdTheme = 0;
            g_Settings.osdSuppressOnDnd = false;
            g_Settings.scheduleMode = 0;
            g_Settings.scheduleSunSource = 0;
            g_Settings.scheduleLightHour = 7;
            g_Settings.scheduleLightMinute = 0;
            g_Settings.scheduleDarkHour = 20;
            g_Settings.scheduleDarkMinute = 0;
            g_Settings.scheduleLatitude = 47.4979;
            g_Settings.scheduleLongitude = 19.0402;
            g_Settings.scheduleSunriseOffset = 0;
            g_Settings.scheduleSunsetOffset = 0;
            g_Settings.scheduleShowOsd = true;

            g_Settings.Save();
            g_Loc.SetLanguage(g_Settings.language);

            ScheduleManager::Refresh();
            UpdateLocalizedStrings();
            PopulateLanguageList();
            LoadSettingsToUI();
            RefreshComboBoxSelections();
            ApplyTheme();
            SetupBackdrop();
            TrayIcon::SyncUI();
            g_OverlayWindow.ResetGraphicsResources();
            g_OverlayWindow.SyncWithSystem();
            HotkeyManager::Register();
            TrimProcessWorkingSetAsync();
        });

        // Tray Icon Mode ComboBox
        TrayIconModeComboBox().SelectionChanged([this](auto&&, auto&&)
        {
            if (m_isUpdatingUI) return;
            auto item = TrayIconModeComboBox().SelectedItem().try_as<ComboBoxItem>();
            if (item)
            {
                auto tag = winrt::unbox_value_or<winrt::hstring>(item.Tag(), L"0");
                try { g_Settings.trayIconMode = std::stoi(tag.c_str()); } catch (...) { g_Settings.trayIconMode = 0; }
                g_Settings.Save();
                UpdateIconVisibility();
                TrayIcon::SyncUI();
            }
        });

        // Dual Icons Toggle
        DualIconsToggle().Toggled([this](auto&&, auto&&)
        {
            if (m_isUpdatingUI) return;
            g_Settings.dualIcons = DualIconsToggle().IsOn();
            DualIconsStatusText().Text(g_Loc.Get(g_Settings.dualIcons ? L"Common_On" : L"Common_Off", g_Settings.dualIcons ? L"On" : L"Off").c_str());
            UpdateDarkIconVisibility();
            g_Settings.Save();
            TrayIcon::SyncUI();
        });

        // Tray Icon OSD Toggle
        TrayOsdToggle().Toggled([this](auto&&, auto&&)
        {
            if (m_isUpdatingUI) return;
            g_Settings.osdIconClickEnabled = TrayOsdToggle().IsOn();
            TrayOsdStatusText().Text(g_Loc.Get(g_Settings.osdIconClickEnabled ? L"Common_On" : L"Common_Off", g_Settings.osdIconClickEnabled ? L"On" : L"Off").c_str());
            g_Settings.Save();
            g_OverlayWindow.SyncWithSystem();
        });

        // Browse Light Icon (defaults to %SystemRoot%\System32\SHELL32.dll if bundled or empty)
        BrowseLightIconButton().Click([this](auto&&, auto&&)
        {
            std::wstring file = g_Settings.iconFile;
            int num = g_Settings.iconNumber;
            if (TrayIcon::IsBundledIcon(file) || file.empty())
            {
                wchar_t sysDir[MAX_PATH] = { 0 };
                GetSystemDirectoryW(sysDir, MAX_PATH);
                file = std::wstring(sysDir) + L"\\SHELL32.dll";
                num = 44;
            }
            if (TrayIcon::PickIcon(m_hwnd, file, num))
            {
                g_Settings.iconFile = file;
                g_Settings.iconNumber = num;
                g_Settings.Save();
                UpdateIconPreviews();
                UpdatePresetComboBoxSelections();
                TrayIcon::SyncUI();
            }
        });

        // Preset Light Icons ComboBox
        LightPresetComboBox().SelectionChanged([this](auto&&, auto&&)
        {
            if (m_isUpdatingUI) return;
            int idx = LightPresetComboBox().SelectedIndex();
            if (idx == 0) // Sun
            {
                g_Settings.iconFile = L"sun.ico";
                g_Settings.iconNumber = 1;
            }
            else if (idx == 1) // Sun (White)
            {
                g_Settings.iconFile = L"sun-white.ico";
                g_Settings.iconNumber = 1;
            }
            else if (idx == 2) // Sun (Dark)
            {
                g_Settings.iconFile = L"sun-dark.ico";
                g_Settings.iconNumber = 1;
            }
            else if (idx == 3) // Star
            {
                wchar_t sysDir[MAX_PATH] = { 0 };
                GetSystemDirectoryW(sysDir, MAX_PATH);
                g_Settings.iconFile = std::wstring(sysDir) + L"\\SHELL32.dll";
                g_Settings.iconNumber = 44;
            }
            else if (idx == 4) // Monitor
            {
                wchar_t sysDir[MAX_PATH] = { 0 };
                GetSystemDirectoryW(sysDir, MAX_PATH);
                g_Settings.iconFile = std::wstring(sysDir) + L"\\SHELL32.dll";
                g_Settings.iconNumber = 16;
            }
            else
            {
                return;
            }
            g_Settings.Save();
            UpdateIconPreviews();
            TrayIcon::SyncUI();
        });

        // Browse Dark Icon (defaults to %SystemRoot%\System32\SHELL32.dll if bundled or empty)
        BrowseDarkIconButton().Click([this](auto&&, auto&&)
        {
            std::wstring file = g_Settings.darkIconFile;
            int num = g_Settings.darkIconNumber;
            if (TrayIcon::IsBundledIcon(file) || file.empty())
            {
                wchar_t sysDir[MAX_PATH] = { 0 };
                GetSystemDirectoryW(sysDir, MAX_PATH);
                file = std::wstring(sysDir) + L"\\SHELL32.dll";
                num = 26;
            }
            if (TrayIcon::PickIcon(m_hwnd, file, num))
            {
                g_Settings.darkIconFile = file;
                g_Settings.darkIconNumber = num;
                g_Settings.Save();
                UpdateIconPreviews();
                UpdatePresetComboBoxSelections();
                TrayIcon::SyncUI();
            }
        });

        // Preset Dark Icons ComboBox
        DarkPresetComboBox().SelectionChanged([this](auto&&, auto&&)
        {
            if (m_isUpdatingUI) return;
            int idx = DarkPresetComboBox().SelectedIndex();
            if (idx == 0) // Moon
            {
                g_Settings.darkIconFile = L"moon.ico";
                g_Settings.darkIconNumber = 1;
            }
            else if (idx == 1) // Moon (White)
            {
                g_Settings.darkIconFile = L"moon-white.ico";
                g_Settings.darkIconNumber = 1;
            }
            else if (idx == 2) // Moon (Dark)
            {
                g_Settings.darkIconFile = L"moon-dark.ico";
                g_Settings.darkIconNumber = 1;
            }
            else if (idx == 3) // Monitor (Dark)
            {
                wchar_t sysDir[MAX_PATH] = { 0 };
                GetSystemDirectoryW(sysDir, MAX_PATH);
                g_Settings.darkIconFile = std::wstring(sysDir) + L"\\SHELL32.dll";
                g_Settings.darkIconNumber = 26;
            }
            else
            {
                return;
            }
            g_Settings.Save();
            UpdateIconPreviews();
            TrayIcon::SyncUI();
        });

        // Hotkey Toggle
        HotkeyToggle().Toggled([this](auto&&, auto&&)
        {
            if (m_isUpdatingUI) return;
            g_Settings.hotkeyEnabled = HotkeyToggle().IsOn();
            HotkeyStatusText().Text(g_Loc.Get(g_Settings.hotkeyEnabled ? L"Common_On" : L"Common_Off", g_Settings.hotkeyEnabled ? L"On" : L"Off").c_str());
            UpdateHotkeyVisibility();
            g_Settings.Save();
            if (g_Settings.hotkeyEnabled) HotkeyManager::Register();
            else HotkeyManager::Unregister();
        });

        // Hotkey OSD Toggle
        HotkeyOsdToggle().Toggled([this](auto&&, auto&&)
        {
            if (m_isUpdatingUI) return;
            g_Settings.osdHotkeyEnabled = HotkeyOsdToggle().IsOn();
            HotkeyOsdStatusText().Text(g_Loc.Get(g_Settings.osdHotkeyEnabled ? L"Common_On" : L"Common_Off", g_Settings.osdHotkeyEnabled ? L"On" : L"Off").c_str());
            g_Settings.Save();
            g_OverlayWindow.SyncWithSystem();
        });

        // Hotkey Capture Button
        CaptureHotkeyButton().Click([this](auto&&, auto&&)
        {
            if (m_isCapturing)
            {
                StopCapture(false);
            }
            else
            {
                StartCapture();
            }
        });

        // Reset Hotkeys Button
        ResetHotkeysButton().Click([this](auto&&, auto&&)
        {
            g_Settings.hotkey = L"^!t";
            g_Settings.Save();
            auto parsed = HotkeyManager::ParseAhkHotkey(g_Settings.hotkey);
            CaptureHotkeyButton().Content(winrt::box_value(HotkeyManager::FormatPreview(parsed)));
            if (g_Settings.hotkeyEnabled) HotkeyManager::Register();
        });

        // Schedule Mode ComboBox
        ScheduleModeComboBox().SelectionChanged([this](auto&&, auto&&)
        {
            if (m_isUpdatingUI) return;
            auto item = ScheduleModeComboBox().SelectedItem().try_as<ComboBoxItem>();
            if (item)
            {
                auto tag = winrt::unbox_value_or<winrt::hstring>(item.Tag(), L"0");
                try { g_Settings.scheduleMode = std::stoi(tag.c_str()); } catch (...) { g_Settings.scheduleMode = 0; }
                g_Settings.Save();
                UpdateScheduleVisibility();
                ScheduleManager::Refresh();
            }
        });

        // Schedule Show OSD Toggle
        ScheduleShowOsdToggle().Toggled([this](auto&&, auto&&)
        {
            if (m_isUpdatingUI) return;
            g_Settings.scheduleShowOsd = ScheduleShowOsdToggle().IsOn();
            ScheduleShowOsdStatusText().Text(g_Loc.Get(g_Settings.scheduleShowOsd ? L"Common_On" : L"Common_Off", g_Settings.scheduleShowOsd ? L"On" : L"Off").c_str());
            g_Settings.Save();
            g_OverlayWindow.SyncWithSystem();
        });

        // Custom Schedule TimePickers
        ScheduleLightTimePicker().TimeChanged([this](auto&&, auto&& e)
        {
            if (m_isUpdatingUI) return;
            auto ts = e.NewTime();
            int64_t totalSec = ts.count() / 10000000LL;
            g_Settings.scheduleLightHour = static_cast<int>((totalSec / 3600) % 24);
            g_Settings.scheduleLightMinute = static_cast<int>((totalSec / 60) % 60);
            g_Settings.Save();
            ScheduleManager::Refresh();
        });

        ScheduleDarkTimePicker().TimeChanged([this](auto&&, auto&& e)
        {
            if (m_isUpdatingUI) return;
            auto ts = e.NewTime();
            int64_t totalSec = ts.count() / 10000000LL;
            g_Settings.scheduleDarkHour = static_cast<int>((totalSec / 3600) % 24);
            g_Settings.scheduleDarkMinute = static_cast<int>((totalSec / 60) % 60);
            g_Settings.Save();
            ScheduleManager::Refresh();
        });

        // Solar Coordinates NumberBoxes
        LatitudeNumberBox().ValueChanged([this](auto&&, auto&& e)
        {
            if (m_isUpdatingUI) return;
            double val = e.NewValue();
            if (!std::isnan(val) && val >= -90.0 && val <= 90.0)
            {
                g_Settings.scheduleLatitude = val;
                g_Settings.Save();
                LocationPresetComboBox().SelectedIndex(-1);
                UpdateDetectedLocationText();
                UpdateSunPreview();
                ScheduleManager::Refresh();
            }
        });

        LongitudeNumberBox().ValueChanged([this](auto&&, auto&& e)
        {
            if (m_isUpdatingUI) return;
            double val = e.NewValue();
            if (!std::isnan(val) && val >= -180.0 && val <= 180.0)
            {
                g_Settings.scheduleLongitude = val;
                g_Settings.Save();
                LocationPresetComboBox().SelectedIndex(-1);
                UpdateDetectedLocationText();
                UpdateSunPreview();
                ScheduleManager::Refresh();
            }
        });

        // Sun Source ComboBox (0 = Location service, 1 = Geographic coordinates)
        SunSourceComboBox().SelectionChanged([this](auto&&, auto&&)
        {
            if (m_isUpdatingUI) return;
            auto item = SunSourceComboBox().SelectedItem().try_as<ComboBoxItem>();
            if (item && item.Tag())
            {
                int val = _wtoi(winrt::unbox_value<winrt::hstring>(item.Tag()).c_str());
                g_Settings.scheduleSunSource = val;
                g_Settings.Save();
                UpdateScheduleVisibility();
                if (val == 0)
                {
                    DetectLocationAsync();
                }
                else
                {
                    UpdateSunPreview();
                    ScheduleManager::Refresh();
                }
            }
        });

        // Detect Location Button
        DetectLocationButton().Click([this](auto&&, auto&&)
        {
            DetectLocationAsync();
        });

        // Refresh Sun Times Button
        RefreshSunTimesButton().Click([this](auto&&, auto&&)
        {
            UpdateSunPreview();
        });

        // Quick Location Presets ComboBox
        LocationPresetComboBox().SelectionChanged([this](auto&&, auto&&)
        {
            if (m_isUpdatingUI) return;
            int idx = LocationPresetComboBox().SelectedIndex();
            if (idx >= 0 && idx < static_cast<int>(s_cityPresets.size()))
            {
                m_isUpdatingUI = true;
                g_Settings.scheduleLatitude = s_cityPresets[idx].lat;
                g_Settings.scheduleLongitude = s_cityPresets[idx].lon;
                LatitudeNumberBox().Value(g_Settings.scheduleLatitude);
                LongitudeNumberBox().Value(g_Settings.scheduleLongitude);
                g_Settings.Save();
                m_isUpdatingUI = false;

                LocationStatusInfoBar().IsOpen(false);
                LocationStatusInfoBar().Visibility(Visibility::Collapsed);

                UpdateDetectedLocationText();
                UpdateSunPreview();
                ScheduleManager::Refresh();
            }
        });

        // Sunrise Offset NumberBox
        SunriseOffsetNumberBox().ValueChanged([this](auto&&, auto&& e)
        {
            if (m_isUpdatingUI) return;
            double val = e.NewValue();
            if (!std::isnan(val) && val >= -180.0 && val <= 180.0)
            {
                g_Settings.scheduleSunriseOffset = static_cast<int>(val);
                g_Settings.Save();
                UpdateSunPreview();
                ScheduleManager::Refresh();
            }
        });

        // Sunset Offset NumberBox
        SunsetOffsetNumberBox().ValueChanged([this](auto&&, auto&& e)
        {
            if (m_isUpdatingUI) return;
            double val = e.NewValue();
            if (!std::isnan(val) && val >= -180.0 && val <= 180.0)
            {
                g_Settings.scheduleSunsetOffset = static_cast<int>(val);
                g_Settings.Save();
                UpdateSunPreview();
                ScheduleManager::Refresh();
            }
        });
    }

    void SettingsWindow::PopulateLanguageList()
    {
        m_isUpdatingUI = true;

        auto langs = g_Loc.GetAvailableLanguages();
        auto items = winrt::single_threaded_observable_vector<winrt::hstring>();
        int selectedIndex = 0;
        for (size_t i = 0; i < langs.size(); ++i)
        {
            std::wstring display = langs[i].displayName;
            if (langs[i].code == L"default")
            {
                display = g_Loc.Get(L"Lang_FollowWindows", L"Follow Windows");
                std::wstring winLang = g_Loc.GetWindowsDisplayLanguage();
                if (!winLang.empty())
                {
                    display += L" (" + winLang + L")";
                }
            }
            items.Append(winrt::hstring(display));
            if (_wcsicmp(langs[i].code.c_str(), g_Settings.language.c_str()) == 0)
            {
                selectedIndex = static_cast<int>(i);
            }
        }

        LanguageComboBox().ItemsSource(items);
        LanguageComboBox().SelectedIndex(selectedIndex);
        m_isUpdatingUI = false;
    }

    void SettingsWindow::RefreshComboBoxSelections()
    {
        m_isUpdatingUI = true;
        auto refresh = [](winrt::Microsoft::UI::Xaml::Controls::ComboBox const& cb)
        {
            if (!cb) return;
            int sel = cb.SelectedIndex();
            if (sel >= 0)
            {
                cb.SelectedIndex(-1);
                cb.SelectedIndex(sel);
            }
        };
        refresh(AppThemeComboBox());
        refresh(TrayMenuThemeComboBox());
        refresh(OsdThemeComboBox());
        refresh(TargetComboBox());
        refresh(TrayIconModeComboBox());
        refresh(LightPresetComboBox());
        refresh(DarkPresetComboBox());
        refresh(ScheduleModeComboBox());
        refresh(SunSourceComboBox());
        m_isUpdatingUI = false;
    }

    void SettingsWindow::UpdateLocalizedStrings()
    {
        AppTitleBarText().Text(g_Loc.Get(L"App_Title", L"ClickDarkMode settings"));
        m_appWindow.Title(g_Loc.Get(L"App_Title", L"ClickDarkMode settings"));
        if (m_hwnd) SetWindowTextW(m_hwnd, g_Loc.Get(L"App_Title", L"ClickDarkMode settings").c_str());
        SettingsSearchBox().PlaceholderText(g_Loc.Get(L"Search_Placeholder", L"Search settings"));

        NavItemGeneral().Content(winrt::box_value(g_Loc.Get(L"Tab_General", L"General")));
        NavItemTray().Content(winrt::box_value(g_Loc.Get(L"Tab_TrayIcon", L"Tray Icon")));
        NavItemShortcuts().Content(winrt::box_value(g_Loc.Get(L"Tab_Hotkey", L"Shortcuts")));
        NavItemSchedule().Content(winrt::box_value(g_Loc.Get(L"Tab_Schedule", L"Schedule")));
        NavItemAbout().Content(winrt::box_value(g_Loc.Get(L"Tab_About", L"About")));

        GeneralHeaderTitle().Text(g_Loc.Get(L"Gen_HeaderTitle", L"General Settings"));
        GeneralHeaderSubtitle().Text(g_Loc.Get(L"Gen_HeaderSubtitle", L"Windows 11 Apps & System Theme Switcher"));
        AppearanceSectionTitle().Text(g_Loc.Get(L"Gen_AppearanceSection", L"Appearance"));
        LanguageTitleText().Text(g_Loc.Get(L"Gen_Language", L"Language"));
        LanguageDescText().Text(g_Loc.Get(L"Gen_LanguageDesc", L"Select application display language. If unsupported, English is used."));
        AppThemeTitleText().Text(g_Loc.Get(L"Gen_AppTheme", L"Application Theme"));
        AppThemeDescText().Text(g_Loc.Get(L"Gen_AppThemeDesc", L"Select the color theme for settings and flyout surfaces"));
        AppThemeItemFollow().Content(winrt::box_value(g_Loc.Get(L"Gen_AppTheme_Follow", L"Follow Windows (app)")));
        AppThemeItemDark().Content(winrt::box_value(g_Loc.Get(L"Gen_AppTheme_Dark", L"Dark")));
        AppThemeItemLight().Content(winrt::box_value(g_Loc.Get(L"Gen_AppTheme_Light", L"Light")));

        TrayMenuThemeTitleText().Text(g_Loc.Get(L"Gen_TrayMenuTheme", L"Tray Menu Theme"));
        TrayMenuThemeDescText().Text(g_Loc.Get(L"Gen_TrayMenuThemeDesc", L"Select the color theme for the notification area tray right-click menu"));
        TrayMenuThemeItemFollow().Content(winrt::box_value(g_Loc.Get(L"Gen_TrayMenuTheme_Follow", L"Follow Windows (system)")));
        TrayMenuThemeItemDark().Content(winrt::box_value(g_Loc.Get(L"Gen_AppTheme_Dark", L"Dark")));
        TrayMenuThemeItemLight().Content(winrt::box_value(g_Loc.Get(L"Gen_AppTheme_Light", L"Light")));
        TransparencyTitleText().Text(g_Loc.Get(L"Gen_Transparency", L"Mica Transparency"));
        TransparencyDescText().Text(g_Loc.Get(L"Gen_TransparencyDesc", L"Enable modern Windows 11 translucent Mica backdrop"));

        TargetSectionTitle().Text(g_Loc.Get(L"Gen_TargetSection", L"Target Theme"));
        TargetTitleText().Text(g_Loc.Get(L"Gen_TargetTheme", L"Theme Switch Mode"));
        TargetDescText().Text(g_Loc.Get(L"Gen_TargetDesc", L"Choose whether switching applies to Apps, Windows System UI (Taskbar/Start), or Both"));
        TargetItemBoth().Content(winrt::box_value(g_Loc.Get(L"Gen_TargetBoth", L"Both (Apps & Windows)")));
        TargetItemApps().Content(winrt::box_value(g_Loc.Get(L"Gen_TargetApps", L"Apps Only")));
        TargetItemSystem().Content(winrt::box_value(g_Loc.Get(L"Gen_TargetSystem", L"Windows Only")));

        BehaviorSectionTitle().Text(g_Loc.Get(L"Gen_BehaviorSection", L"Toggle Behavior"));
        CooldownTitleText().Text(g_Loc.Get(L"Gen_CooldownLabel", L"Accidental Click Cooldown (ms)"));
        CooldownDescText().Text(g_Loc.Get(L"Gen_CooldownDesc", L"Ignores clicks occurring within this delay to prevent rapid double-toggling"));

        OsdSectionTitle().Text(g_Loc.Get(L"Gen_OsdSection", L"On-Screen Display (OSD)"));
        OsdThemeTitleText().Text(g_Loc.Get(L"Gen_OsdTheme", L"OSD Theme"));
        OsdThemeDescText().Text(g_Loc.Get(L"Gen_OsdThemeDesc", L"Select the color theme for the on-screen display banner"));
        OsdThemeItemFollow().Content(winrt::box_value(g_Loc.Get(L"Gen_OsdTheme_Follow", L"Follow the theme")));
        OsdThemeItemDark().Content(winrt::box_value(g_Loc.Get(L"Gen_OsdTheme_Dark", L"Dark")));
        OsdThemeItemLight().Content(winrt::box_value(g_Loc.Get(L"Gen_OsdTheme_Light", L"Light")));
        OsdTimeoutTitleText().Text(g_Loc.Get(L"Gen_OsdTimeoutTitle", L"Hide Time (ms)"));
        OsdTimeoutDescText().Text(g_Loc.Get(L"Gen_OsdTimeoutDesc", L"Auto-hide timeout for on-screen banner overlay"));
        OsdDndTitleText().Text(g_Loc.Get(L"Gen_OsdDnd", L"Suppress in Do Not Disturb"));
        OsdDndDescText().Text(g_Loc.Get(L"Gen_OsdDndDesc", L"Do not show on-screen display when Windows Do Not Disturb or Focus Assist is active"));
        OsdDndStatusText().Text(g_Loc.Get(OsdDndToggle().IsOn() ? L"Common_On" : L"Common_Off", OsdDndToggle().IsOn() ? L"On" : L"Off").c_str());

        StartupSectionTitle().Text(g_Loc.Get(L"Gen_StartupSection", L"Startup & Maintenance"));
        StartupTitleText().Text(g_Loc.Get(L"Gen_StartWithWindows", L"Start with Windows"));
        bool isInstalled = Config::IsInstalledInProgramFiles();
        if (isInstalled)
        {
            StartupDescText().Text(g_Loc.Get(L"Gen_StartWithWindowsDesc", L"Launch ClickDarkMode automatically when Windows starts"));
            StartupToggle().IsEnabled(true);
        }
        else
        {
            StartupDescText().Text(g_Loc.Get(L"Gen_StartWithWindows_NotInstalled", L"Launch ClickDarkMode automatically when Windows starts (Available when installed in Program Files)"));
            StartupToggle().IsEnabled(false);
        }
        StartupStatusText().Text(g_Loc.Get(StartupToggle().IsOn() ? L"Common_On" : L"Common_Off", StartupToggle().IsOn() ? L"On" : L"Off").c_str());
        RestoreDefaultsTitleText().Text(g_Loc.Get(L"Gen_RestoreDefaults", L"Restore Defaults"));
        RestoreDefaultsDescText().Text(g_Loc.Get(L"Gen_RestoreDefaultsDesc", L"Reset all settings to initial factory defaults"));
        RestoreDefaultsButton().Content(winrt::box_value(g_Loc.Get(L"Gen_RestoreDefaults", L"Restore Defaults")));
        RestoreConfirmText().Text(g_Loc.Get(L"Gen_RestoreConfirmText", L"All settings will be reset. Do you want to continue?"));
        ConfirmRestoreDefaultsButton().Content(winrt::box_value(g_Loc.Get(L"Gen_RestoreConfirmBtn", L"Yes, reset settings")));

        TrayHeaderTitle().Text(g_Loc.Get(L"Icon_HeaderTitle", L"Tray Icon Settings"));
        TrayHeaderSubtitle().Text(g_Loc.Get(L"Icon_HeaderSubtitle", L"Customize system notification area tray icons"));
        IconModeSectionTitle().Text(g_Loc.Get(L"Icon_BehaviorSection", L"Icon Behavior"));
        TrayIconModeTitleText().Text(g_Loc.Get(L"Icon_TrayIconMode", L"Tray Icon"));
        TrayIconModeDescText().Text(g_Loc.Get(L"Icon_TrayIconModeDesc", L"Select tray icon appearance mode"));
        TrayIconModeItemFollow().Content(winrt::box_value(g_Loc.Get(L"Icon_TrayIconMode_Follow", L"Follow Windows (system)")));
        TrayIconModeItemCustom().Content(winrt::box_value(g_Loc.Get(L"Icon_TrayIconMode_Custom", L"Custom")));
        TrayIconModeItemDisabled().Content(winrt::box_value(g_Loc.Get(L"Icon_TrayIconMode_Disabled", L"Disabled")));
        DualIconsTitleText().Text(g_Loc.Get(L"Icon_DualCheckbox", L"Use Separate Icons"));
        DualIconsDescText().Text(g_Loc.Get(L"Icon_DualDesc", L"Display distinct tray icons for Light and Dark modes to indicate current status"));
        TrayOsdTitleText().Text(g_Loc.Get(L"Icon_ShowOsd", L"Show On-Screen Display (OSD)"));
        TrayOsdDescText().Text(g_Loc.Get(L"Icon_ShowOsdDesc", L"Display a floating notification banner when clicking the tray icon"));
        LightIconSectionTitle().Text(g_Loc.Get(L"Icon_LightGroup", L"Light / Primary Mode Icon"));
        LightIconPathText().Text(g_Loc.Get(L"Icon_LightDesc", L"Primary icon representing Light theme mode"));
        BrowseLightIconButton().Content(winrt::box_value(g_Loc.Get(L"Icon_Browse", L"Browse...")));
        LightPresetsTitleText().Text(g_Loc.Get(L"Icon_LightPresetsTitle", L"Light Mode Presets"));
        LightPresetsDescText().Text(g_Loc.Get(L"Icon_LightPresetsDesc", L"Quickly choose from built-in or standard Windows light mode icons"));
        LightPresetComboBox().PlaceholderText(g_Loc.Get(L"Icon_SelectPreset", L"Select preset..."));
        LightPresetItemSun().Content(winrt::box_value(g_Loc.Get(L"Icon_Sun", L"Sun")));
        LightPresetItemSunWhite().Content(winrt::box_value(g_Loc.Get(L"Icon_SunWhite", L"Sun (White)")));
        LightPresetItemSunDark().Content(winrt::box_value(g_Loc.Get(L"Icon_SunDark", L"Sun (Dark)")));
        LightPresetItemStar().Content(winrt::box_value(g_Loc.Get(L"Icon_Star", L"Star")));
        LightPresetItemMonitor().Content(winrt::box_value(g_Loc.Get(L"Icon_Monitor", L"Monitor")));

        DarkIconSectionTitle().Text(g_Loc.Get(L"Icon_DarkGroup", L"Dark Mode Icon"));
        DarkIconPathText().Text(g_Loc.Get(L"Icon_DarkDesc", L"Icon representing Dark theme mode"));
        BrowseDarkIconButton().Content(winrt::box_value(g_Loc.Get(L"Icon_Browse", L"Browse...")));
        DarkPresetsTitleText().Text(g_Loc.Get(L"Icon_DarkPresetsTitle", L"Dark Mode Presets"));
        DarkPresetsDescText().Text(g_Loc.Get(L"Icon_DarkPresetsDesc", L"Quickly choose from built-in or standard Windows dark mode icons"));
        DarkPresetComboBox().PlaceholderText(g_Loc.Get(L"Icon_SelectPreset", L"Select preset..."));
        DarkPresetItemMoon().Content(winrt::box_value(g_Loc.Get(L"Icon_Moon", L"Moon")));
        DarkPresetItemMoonWhite().Content(winrt::box_value(g_Loc.Get(L"Icon_MoonWhite", L"Moon (White)")));
        DarkPresetItemMoonDark().Content(winrt::box_value(g_Loc.Get(L"Icon_MoonDark", L"Moon (Dark)")));
        DarkPresetItemMonitorDark().Content(winrt::box_value(g_Loc.Get(L"Icon_MonitorDark", L"Monitor (Dark)")));

        ShortcutsHeaderTitle().Text(g_Loc.Get(L"Hk_HeaderTitle", L"Shortcut Settings"));
        ShortcutsHeaderSubtitle().Text(g_Loc.Get(L"Hk_HeaderSubtitle", L"Configure global keyboard shortcuts to toggle theme anywhere"));
        GlobalShortcutSectionTitle().Text(g_Loc.Get(L"Hk_GlobalSection", L"Global Shortcut"));
        HotkeyEnableTitleText().Text(g_Loc.Get(L"Hk_EnableCheckbox", L"Enable Global Shortcut"));
        HotkeyEnableDescText().Text(g_Loc.Get(L"Hk_EnableDesc", L"Press a keyboard shortcut combination anytime to toggle theme immediately"));
        HotkeyCombinationTitleText().Text(g_Loc.Get(L"Hk_CombinationLabel", L"Shortcut Combination"));
        HotkeyCombinationDescText().Text(g_Loc.Get(L"Hk_CombinationDesc", L"Click the button and press your desired key combination"));
        HotkeyOsdTitleText().Text(g_Loc.Get(L"Hk_ShowOsd", L"Show On-Screen Display (OSD)"));
        HotkeyOsdDescText().Text(g_Loc.Get(L"Hk_ShowOsdDesc", L"Display a floating notification banner when toggling theme with shortcut"));
        ResetShortcutsTitleText().Text(g_Loc.Get(L"Hk_ResetShortcutsTitle", L"Reset Shortcuts"));
        ResetShortcutsDescText().Text(g_Loc.Get(L"Hk_ResetShortcutsDesc", L"Restore shortcut to default (Ctrl + Alt + T)"));
        ResetHotkeysButton().Content(winrt::box_value(g_Loc.Get(L"Hk_ResetShortcutsBtn", L"Reset Hotkeys")));

        ScheduleHeaderTitle().Text(g_Loc.Get(L"Schedule_HeaderTitle", L"Schedule Settings"));
        ScheduleHeaderSubtitle().Text(g_Loc.Get(L"Schedule_HeaderSubtitle", L"Automatically transition between Light and Dark themes based on time or solar position"));
        ScheduleModeSectionTitle().Text(g_Loc.Get(L"Schedule_ModeSection", L"Schedule Mode"));
        ScheduleModeTitleText().Text(g_Loc.Get(L"Schedule_ModeTitle", L"Automatic Theme Switching"));
        ScheduleModeDescText().Text(g_Loc.Get(L"Schedule_ModeDesc", L"Choose whether themes change automatically by custom schedule or solar position"));
        ScheduleModeItemDisabled().Content(winrt::box_value(g_Loc.Get(L"Schedule_Mode_Disabled", L"Disabled (Manual only)")));
        ScheduleModeItemCustom().Content(winrt::box_value(g_Loc.Get(L"Schedule_Mode_Custom", L"Custom Time Schedule")));
        ScheduleModeItemSun().Content(winrt::box_value(g_Loc.Get(L"Schedule_Mode_Sun", L"Sunrise / Sunset Sync")));

        ScheduleShowOsdTitleText().Text(g_Loc.Get(L"Schedule_ShowOsd", L"Show OSD On Scheduled Switch"));
        ScheduleShowOsdDescText().Text(g_Loc.Get(L"Schedule_ShowOsdDesc", L"Display the on-screen visual feedback banner when an automatic transition occurs"));

        CustomScheduleSectionTitle().Text(g_Loc.Get(L"Schedule_CustomSection", L"Custom Time Schedule"));
        ScheduleLightStartTitleText().Text(g_Loc.Get(L"Schedule_LightStart", L"Light Mode Start Time"));
        ScheduleLightStartDescText().Text(g_Loc.Get(L"Schedule_LightStartDesc", L"Time of day when the system switches to Light Theme"));
        ScheduleDarkStartTitleText().Text(g_Loc.Get(L"Schedule_DarkStart", L"Dark Mode Start Time"));
        ScheduleDarkStartDescText().Text(g_Loc.Get(L"Schedule_DarkStartDesc", L"Time of day when the system switches to Dark Theme"));

        SunScheduleSectionTitle().Text(g_Loc.Get(L"Schedule_SunSection", L"Sunrise / Sunset Sync"));
        SunPreviewTitleText().Text(g_Loc.Get(L"Schedule_SunPreviewTitle", L"Today's Solar Times"));
        RefreshSunTimesButton().Content(winrt::box_value(g_Loc.Get(L"Schedule_Refresh", L"Refresh")));

        SunSourceTitleText().Text(g_Loc.Get(L"Schedule_SunSourceTitle", L"Solar Location Source"));
        SunSourceDescText().Text(g_Loc.Get(L"Schedule_SunSourceDesc", L"Select whether to use Windows Location service or manual geographic coordinates"));
        SunSourceItemLocationService().Content(winrt::box_value(g_Loc.Get(L"Schedule_SunSourceLocationService", L"Location service")));
        SunSourceItemCoordinates().Content(winrt::box_value(g_Loc.Get(L"Schedule_SunSourceCoordinates", L"Geographic coordinates")));

        DetectLocationTitleText().Text(g_Loc.Get(L"Schedule_DetectTitle", L"Auto-Detect Location"));
        DetectLocationDescText().Text(g_Loc.Get(L"Schedule_DetectDesc", L"Detect current geographic coordinates using Windows Location"));
        DetectLocationBtnText().Text(g_Loc.Get(L"Schedule_DetectLocation", L"Detect"));
        ToolTipService::SetToolTip(DetectLocationButton(), winrt::box_value(g_Loc.Get(L"Schedule_DetectTooltip", L"Detect current coordinates using Windows Location")));

        SunPresetTitleText().Text(g_Loc.Get(L"Schedule_SunPresetTitle", L"Quick Location Presets"));
        SunPresetDescText().Text(g_Loc.Get(L"Schedule_SunPresetDesc", L"Quickly fill coordinates for major regions"));
        LocationPresetComboBox().PlaceholderText(g_Loc.Get(L"Schedule_PresetSelectPlaceholder", L"Select a city preset..."));

        LatitudeTitleText().Text(g_Loc.Get(L"Schedule_LatitudeTitle", L"Latitude"));
        LatitudeDescText().Text(g_Loc.Get(L"Schedule_LatitudeDesc", L"Geographic coordinate north or south of the equator (-90.0\u00B0 to 90.0\u00B0)"));

        LongitudeTitleText().Text(g_Loc.Get(L"Schedule_LongitudeTitle", L"Longitude"));
        LongitudeDescText().Text(g_Loc.Get(L"Schedule_LongitudeDesc", L"Geographic coordinate east or west of the prime meridian (-180.0\u00B0 to 180.0\u00B0)"));

        SunriseOffsetTitleText().Text(g_Loc.Get(L"Schedule_SunriseOffsetTitle", L"Sunrise Offset (minutes)"));
        SunriseOffsetDescText().Text(g_Loc.Get(L"Schedule_SunriseOffsetDesc", L"Shift Light Mode switch earlier (negative) or later (positive) relative to sunrise"));
        SunsetOffsetTitleText().Text(g_Loc.Get(L"Schedule_SunsetOffsetTitle", L"Sunset Offset (minutes)"));
        SunsetOffsetDescText().Text(g_Loc.Get(L"Schedule_SunsetOffsetDesc", L"Shift Dark Mode switch earlier (negative) or later (positive) relative to sunset"));

        UpdateDetectedLocationText();
        UpdateSunPreview();

        AboutHeaderTitle().Text(g_Loc.Get(L"About_HeaderTitle", L"About ClickDarkMode"));
        AboutHeaderSubtitle().Text(g_Loc.Get(L"About_HeaderSubtitle", L"Lightweight, modern Windows 11 theme switching utility"));
#if defined(_M_ARM64)
        AboutVersionText().Text(L"Version 1.0.2 (ARM64 Release)");
#else
        AboutVersionText().Text(L"Version 1.0.2 (x64 Release)");
#endif
        AboutAppTypeText().Text(g_Loc.Get(L"About_AppType", L"Native WinUI 3 / Windows App SDK 2.6 Desktop Application"));
        AboutDescTitleText().Text(g_Loc.Get(L"About_DescTitle", L"Description"));
        AboutDescContentText().Text(g_Loc.Get(L"About_DescContent", L"A modern, lightweight Windows 11 utility for instant switching between Light and Dark themes for applications and system surfaces. Supports custom tray icons, accidental-click cooldown, global keyboard shortcuts, and ultra-low background memory footprint."));
        AboutLicenseTitleText().Text(g_Loc.Get(L"About_LicenseTitle", L"License"));
        AboutLicenseContentText().Text(g_Loc.Get(L"About_LicenseContent", L"Free and Open Source under the MIT License."));

        UpdateThemeStatus();
    }

    void SettingsWindow::UpdateThemeStatus()
    {
        int state = ThemeManager::GetState();
        std::wstring lightStr = g_Loc.Get(L"Theme_Light", L"Light");
        std::wstring darkStr = g_Loc.Get(L"Theme_Dark", L"Dark");

        if (state == -1)
        {
            int apps = ThemeManager::ReadRegKey(L"AppsUseLightTheme", 1);
            int sys = ThemeManager::ReadRegKey(L"SystemUsesLightTheme", 1);
            wchar_t buf[256];
            swprintf_s(buf, g_Loc.Get(L"Gen_StatusMixed", L"Mixed mode: Apps (%s), Windows (%s)").c_str(),
                       apps ? lightStr.c_str() : darkStr.c_str(),
                       sys ? lightStr.c_str() : darkStr.c_str());
            ThemeStatusInfoBar().Severity(InfoBarSeverity::Warning);
            ThemeStatusInfoBar().Title(g_Loc.Get(L"Gen_StatusTitle", L"Current Theme") + L": " + g_Loc.Get(L"Theme_Mixed", L"Mixed"));
            ThemeStatusInfoBar().Message(buf);
            ThemeStatusInfoBar().IsOpen(true);
            ThemeStatusInfoBar().Visibility(Visibility::Visible);
        }
        else
        {
            // Consistent theme (Dark or Light) is good: do not show green status banner
            ThemeStatusInfoBar().IsOpen(false);
            ThemeStatusInfoBar().Visibility(Visibility::Collapsed);
        }
    }

    void SettingsWindow::LoadSettingsToUI()
    {
        m_isUpdatingUI = true;

        // App Theme
        int themeIdx = g_Settings.appTheme;
        if (themeIdx >= 0 && themeIdx < (int)AppThemeComboBox().Items().Size())
        {
            AppThemeComboBox().SelectedIndex(themeIdx);
        }

        // Tray Menu Theme
        int trayThemeIdx = g_Settings.trayMenuTheme;
        if (trayThemeIdx >= 0 && trayThemeIdx < (int)TrayMenuThemeComboBox().Items().Size())
        {
            TrayMenuThemeComboBox().SelectedIndex(trayThemeIdx);
        }

        // Transparency
        TransparencyToggle().IsOn(g_Settings.transparencyEnabled);
        TransparencyStatusText().Text(g_Loc.Get(g_Settings.transparencyEnabled ? L"Common_On" : L"Common_Off", g_Settings.transparencyEnabled ? L"On" : L"Off").c_str());

        // Target Theme
        for (uint32_t i = 0; i < TargetComboBox().Items().Size(); ++i)
        {
            auto item = TargetComboBox().Items().GetAt(i).try_as<ComboBoxItem>();
            if (item)
            {
                auto tag = winrt::unbox_value_or<winrt::hstring>(item.Tag(), L"");
                if (_wcsicmp(tag.c_str(), g_Settings.target.c_str()) == 0)
                {
                    TargetComboBox().SelectedIndex(i);
                    break;
                }
            }
        }

        // Cooldown
        CooldownSlider().Value(g_Settings.toggleDelayMs);
        CooldownNumberBox().Value(g_Settings.toggleDelayMs);

        // OSD Theme
        int osdThemeIdx = g_Settings.osdTheme;
        if (osdThemeIdx >= 0 && osdThemeIdx < (int)OsdThemeComboBox().Items().Size())
        {
            OsdThemeComboBox().SelectedIndex(osdThemeIdx);
        }

        // OSD Timeout
        OsdTimeoutSlider().Value(g_Settings.osdTimeoutMs);
        OsdTimeoutNumberBox().Value(g_Settings.osdTimeoutMs);

        // OSD Do Not Disturb
        OsdDndToggle().IsOn(g_Settings.osdSuppressOnDnd);
        OsdDndStatusText().Text(g_Loc.Get(g_Settings.osdSuppressOnDnd ? L"Common_On" : L"Common_Off", g_Settings.osdSuppressOnDnd ? L"On" : L"Off").c_str());

        // Startup
        bool isInstalled = Config::IsInstalledInProgramFiles();
        StartupToggle().IsEnabled(isInstalled);
        StartupToggle().IsOn(g_Settings.startWithWindows);
        StartupStatusText().Text(g_Loc.Get(g_Settings.startWithWindows ? L"Common_On" : L"Common_Off", g_Settings.startWithWindows ? L"On" : L"Off").c_str());

        // Tray Icon Mode
        int trayIconModeIdx = g_Settings.trayIconMode;
        if (trayIconModeIdx >= 0 && trayIconModeIdx < (int)TrayIconModeComboBox().Items().Size())
        {
            TrayIconModeComboBox().SelectedIndex(trayIconModeIdx);
        }

        // Dual Icons
        DualIconsToggle().IsOn(g_Settings.dualIcons);
        DualIconsStatusText().Text(g_Loc.Get(g_Settings.dualIcons ? L"Common_On" : L"Common_Off", g_Settings.dualIcons ? L"On" : L"Off").c_str());
        UpdateIconVisibility();

        // Tray Icon OSD
        TrayOsdToggle().IsOn(g_Settings.osdIconClickEnabled);
        TrayOsdStatusText().Text(g_Loc.Get(g_Settings.osdIconClickEnabled ? L"Common_On" : L"Common_Off", g_Settings.osdIconClickEnabled ? L"On" : L"Off").c_str());

        // Icon Previews (lazy loaded when Tray tab is opened)
        if (m_trayTabLoaded)
        {
            UpdateIconPreviews();
        }

        // Hotkey
        HotkeyToggle().IsOn(g_Settings.hotkeyEnabled);
        HotkeyStatusText().Text(g_Loc.Get(g_Settings.hotkeyEnabled ? L"Common_On" : L"Common_Off", g_Settings.hotkeyEnabled ? L"On" : L"Off").c_str());
        UpdateHotkeyVisibility();

        // Hotkey OSD
        HotkeyOsdToggle().IsOn(g_Settings.osdHotkeyEnabled);
        HotkeyOsdStatusText().Text(g_Loc.Get(g_Settings.osdHotkeyEnabled ? L"Common_On" : L"Common_Off", g_Settings.osdHotkeyEnabled ? L"On" : L"Off").c_str());

        auto parsed = HotkeyManager::ParseAhkHotkey(g_Settings.hotkey);
        CaptureHotkeyButton().Content(winrt::box_value(HotkeyManager::FormatPreview(parsed)));

        // Schedule Mode
        int schedMode = g_Settings.scheduleMode;
        if (schedMode >= 0 && schedMode < (int)ScheduleModeComboBox().Items().Size())
        {
            ScheduleModeComboBox().SelectedIndex(schedMode);
        }

        // Schedule Show OSD
        ScheduleShowOsdToggle().IsOn(g_Settings.scheduleShowOsd);
        ScheduleShowOsdStatusText().Text(g_Loc.Get(g_Settings.scheduleShowOsd ? L"Common_On" : L"Common_Off", g_Settings.scheduleShowOsd ? L"On" : L"Off").c_str());

        // Custom Times
        winrt::Windows::Foundation::TimeSpan lightTs{
            std::chrono::duration_cast<winrt::Windows::Foundation::TimeSpan>(
                std::chrono::hours(g_Settings.scheduleLightHour) + std::chrono::minutes(g_Settings.scheduleLightMinute))
        };
        ScheduleLightTimePicker().Time(lightTs);

        winrt::Windows::Foundation::TimeSpan darkTs{
            std::chrono::duration_cast<winrt::Windows::Foundation::TimeSpan>(
                std::chrono::hours(g_Settings.scheduleDarkHour) + std::chrono::minutes(g_Settings.scheduleDarkMinute))
        };
        ScheduleDarkTimePicker().Time(darkTs);

        // Sun Settings
        SunSourceComboBox().SelectedIndex(g_Settings.scheduleSunSource == 1 ? 1 : 0);
        LatitudeNumberBox().Value(g_Settings.scheduleLatitude);
        LongitudeNumberBox().Value(g_Settings.scheduleLongitude);
        SunriseOffsetNumberBox().Value(g_Settings.scheduleSunriseOffset);
        SunsetOffsetNumberBox().Value(g_Settings.scheduleSunsetOffset);
        // Location presets and solar preview (lazy loaded when Schedule tab is opened)
        if (m_scheduleTabLoaded)
        {
            PopulateLocationPresets();
            UpdateDetectedLocationText();
            UpdateSunPreview();
        }

        UpdateScheduleVisibility();
        UpdateThemeStatus();
        m_isUpdatingUI = false;
    }

    void SettingsWindow::PopulateLocationPresets()
    {
        if (!LocationPresetComboBox()) return;
        LocationPresetComboBox().Items().Clear();
        for (const auto& city : s_cityPresets)
        {
            LocationPresetComboBox().Items().Append(winrt::box_value(winrt::hstring(city.name)));
        }

        int matchIdx = -1;
        for (size_t i = 0; i < s_cityPresets.size(); ++i)
        {
            if (std::abs(g_Settings.scheduleLatitude - s_cityPresets[i].lat) < 0.001 &&
                std::abs(g_Settings.scheduleLongitude - s_cityPresets[i].lon) < 0.001)
            {
                matchIdx = static_cast<int>(i);
                break;
            }
        }
        LocationPresetComboBox().SelectedIndex(matchIdx);
    }

    winrt::fire_and_forget SettingsWindow::DetectLocationAsync()
    {
        auto lifetime = get_strong();

        DetectLocationButton().IsEnabled(false);
        DetectLocationBtnText().Text(g_Loc.Get(L"Schedule_LocDetecting", L"Detecting..."));

        try
        {
            auto access = co_await winrt::Windows::Devices::Geolocation::Geolocator::RequestAccessAsync();
            if (access != winrt::Windows::Devices::Geolocation::GeolocationAccessStatus::Allowed)
            {
                this->DispatcherQueue().TryEnqueue([this, lifetime]()
                {
                    LocationStatusInfoBar().Severity(InfoBarSeverity::Warning);
                    LocationStatusInfoBar().Title(g_Loc.Get(L"Schedule_LocDeniedTitle", L"Location Access Disabled"));
                    LocationStatusInfoBar().Message(g_Loc.Get(L"Schedule_LocDeniedMsg", L"Location access is turned off. Please enable Location in Windows Settings > Privacy & security > Location."));
                    LocationStatusInfoBar().IsOpen(true);
                    LocationStatusInfoBar().Visibility(Visibility::Visible);

                    DetectLocationButton().IsEnabled(true);
                    DetectLocationBtnText().Text(g_Loc.Get(L"Schedule_DetectLocation", L"Detect"));
                });
                co_return;
            }

            winrt::Windows::Devices::Geolocation::Geolocator geolocator;
            geolocator.DesiredAccuracy(winrt::Windows::Devices::Geolocation::PositionAccuracy::Default);

            winrt::Windows::Foundation::TimeSpan maxAge{ std::chrono::minutes(15) };
            winrt::Windows::Foundation::TimeSpan timeout{ std::chrono::seconds(10) };
            auto pos = co_await geolocator.GetGeopositionAsync(maxAge, timeout);

            double lat = pos.Coordinate().Point().Position().Latitude;
            double lon = pos.Coordinate().Point().Position().Longitude;

            this->DispatcherQueue().TryEnqueue([this, lifetime, lat, lon]()
            {
                m_isUpdatingUI = true;
                g_Settings.scheduleLatitude = lat;
                g_Settings.scheduleLongitude = lon;
                LatitudeNumberBox().Value(lat);
                LongitudeNumberBox().Value(lon);
                LocationPresetComboBox().SelectedIndex(-1);
                g_Settings.Save();
                m_isUpdatingUI = false;

                LocationStatusInfoBar().IsOpen(false);
                LocationStatusInfoBar().Visibility(Visibility::Collapsed);

                UpdateDetectedLocationText();
                UpdateSunPreview();
                ScheduleManager::Refresh();

                DetectLocationButton().IsEnabled(true);
                DetectLocationBtnText().Text(g_Loc.Get(L"Schedule_DetectLocation", L"Detect"));
            });
        }
        catch (winrt::hresult_error const&)
        {
            this->DispatcherQueue().TryEnqueue([this, lifetime]()
            {
                LocationStatusInfoBar().Severity(InfoBarSeverity::Error);
                LocationStatusInfoBar().Title(g_Loc.Get(L"Schedule_LocErrorTitle", L"Location Error"));
                LocationStatusInfoBar().Message(g_Loc.Get(L"Schedule_LocErrorMsg", L"Could not determine your location. Please check your network connection or enter coordinates manually."));
                LocationStatusInfoBar().IsOpen(true);
                LocationStatusInfoBar().Visibility(Visibility::Visible);

                DetectLocationButton().IsEnabled(true);
                DetectLocationBtnText().Text(g_Loc.Get(L"Schedule_DetectLocation", L"Detect"));
            });
        }
        catch (...)
        {
            this->DispatcherQueue().TryEnqueue([this, lifetime]()
            {
                LocationStatusInfoBar().Severity(InfoBarSeverity::Error);
                LocationStatusInfoBar().Title(g_Loc.Get(L"Schedule_LocErrorTitle", L"Location Error"));
                LocationStatusInfoBar().Message(g_Loc.Get(L"Schedule_LocErrorMsg", L"Could not determine your location. Please check your network connection or enter coordinates manually."));
                LocationStatusInfoBar().IsOpen(true);
                LocationStatusInfoBar().Visibility(Visibility::Visible);

                DetectLocationButton().IsEnabled(true);
                DetectLocationBtnText().Text(g_Loc.Get(L"Schedule_DetectLocation", L"Detect"));
            });
        }
    }

    void SettingsWindow::UpdateScheduleVisibility()
    {
        bool isScheduled = (g_Settings.scheduleMode != 0);
        ScheduleShowOsdCard().Visibility(isScheduled ? Visibility::Visible : Visibility::Collapsed);
        CustomScheduleSection().Visibility(g_Settings.scheduleMode == 1 ? Visibility::Visible : Visibility::Collapsed);
        SunScheduleSection().Visibility(g_Settings.scheduleMode == 2 ? Visibility::Visible : Visibility::Collapsed);

        if (g_Settings.scheduleMode == 2)
        {
            bool isLocService = (g_Settings.scheduleSunSource == 0);
            SunLocationServicePanel().Visibility(isLocService ? Visibility::Visible : Visibility::Collapsed);
            SunCoordinatesPanel().Visibility(!isLocService ? Visibility::Visible : Visibility::Collapsed);
        }
    }

    void SettingsWindow::UpdateDetectedLocationText()
    {
        if (!DetectedCoordsText()) return;

        double lat = g_Settings.scheduleLatitude;
        double lon = g_Settings.scheduleLongitude;

        wchar_t latDir = (lat >= 0) ? L'N' : L'S';
        wchar_t lonDir = (lon >= 0) ? L'E' : L'W';
        double absLat = std::abs(lat);
        double absLon = std::abs(lon);

        std::wstring coordsLabel = g_Loc.Get(L"Schedule_DetectedCoordsLabel", L"Coordinates");
        wchar_t buf[128];
        swprintf_s(buf, L"%s: %.4f\u00B0 %c, %.4f\u00B0 %c", coordsLabel.c_str(), absLat, latDir, absLon, lonDir);
        DetectedCoordsText().Text(buf);
    }

    void SettingsWindow::UpdateSunPreview()
    {
        auto info = ScheduleManager::GetTodaySunInfo(g_Settings.scheduleLatitude, g_Settings.scheduleLongitude);
        if (info.valid)
        {
            std::wstring sunriseLabel = g_Loc.Get(L"Schedule_Sunrise", L"Sunrise");
            std::wstring sunsetLabel = g_Loc.Get(L"Schedule_Sunset", L"Sunset");

            wchar_t riseBuf[64];
            if (g_Settings.scheduleSunriseOffset != 0)
            {
                swprintf_s(riseBuf, L"%02d:%02d (\u2192 %02d:%02d)", info.riseHour, info.riseMin, info.effectiveRiseHour, info.effectiveRiseMin);
            }
            else
            {
                swprintf_s(riseBuf, L"%02d:%02d", info.riseHour, info.riseMin);
            }

            wchar_t setBuf[64];
            if (g_Settings.scheduleSunsetOffset != 0)
            {
                swprintf_s(setBuf, L"%02d:%02d (\u2192 %02d:%02d)", info.setHour, info.setMin, info.effectiveSetHour, info.effectiveSetMin);
            }
            else
            {
                swprintf_s(setBuf, L"%02d:%02d", info.setHour, info.setMin);
            }

            wchar_t buf[256];
            swprintf_s(buf, L"%s: %s   |   %s: %s",
                sunriseLabel.c_str(), riseBuf,
                sunsetLabel.c_str(), setBuf);
            SunPreviewStatusText().Text(buf);
        }
        else
        {
            SunPreviewStatusText().Text(g_Loc.Get(L"Schedule_Polar", L"Polar day / night (no transition)").c_str());
        }
    }

    void SettingsWindow::ApplyTheme()
    {
        bool isDark = ThemeHelper::IsAppInDarkMode(g_Settings.appTheme);
        auto theme = isDark ? ElementTheme::Dark : ElementTheme::Light;
        RootGrid().RequestedTheme(theme);

        if (g_Settings.transparencyEnabled)
        {
            RootGrid().Background(winrt::Microsoft::UI::Xaml::Media::SolidColorBrush(winrt::Microsoft::UI::Colors::Transparent()));
        }
        else
        {
            winrt::Windows::UI::Color solidBg = isDark
                ? winrt::Microsoft::UI::ColorHelper::FromArgb(255, 32, 32, 32)
                : winrt::Microsoft::UI::ColorHelper::FromArgb(255, 243, 243, 243);
            RootGrid().Background(winrt::Microsoft::UI::Xaml::Media::SolidColorBrush(solidBg));
        }

        if (m_hwnd)
        {
            BOOL darkMode = isDark ? TRUE : FALSE;
            DwmSetWindowAttribute(m_hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkMode, sizeof(darkMode));

            COLORREF captionColor = isDark ? RGB(32, 32, 32) : RGB(243, 243, 243);
            DwmSetWindowAttribute(m_hwnd, DWMWA_CAPTION_COLOR, &captionColor, sizeof(captionColor));

            SetWindowPos(m_hwnd, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED | SWP_NOACTIVATE);
        }

        UpdateTitleBarButtons(isDark);
        UpdateThemeStatus();
    }

    void SettingsWindow::UpdateTitleBarButtons(bool isDark)
    {
        if (m_appWindow && m_appWindow.TitleBar())
        {
            auto tb = m_appWindow.TitleBar();
            tb.ButtonBackgroundColor(Microsoft::UI::Colors::Transparent());
            tb.ButtonInactiveBackgroundColor(Microsoft::UI::Colors::Transparent());
            if (isDark)
            {
                tb.ButtonForegroundColor(Microsoft::UI::Colors::White());
                tb.ButtonHoverForegroundColor(Microsoft::UI::Colors::White());
                tb.ButtonPressedForegroundColor(Microsoft::UI::Colors::White());
                tb.ButtonInactiveForegroundColor(winrt::Microsoft::UI::ColorHelper::FromArgb(255, 160, 160, 160));
                tb.ButtonHoverBackgroundColor(winrt::Microsoft::UI::ColorHelper::FromArgb(40, 255, 255, 255));
                tb.ButtonPressedBackgroundColor(winrt::Microsoft::UI::ColorHelper::FromArgb(60, 255, 255, 255));
            }
            else
            {
                tb.ButtonForegroundColor(Microsoft::UI::Colors::Black());
                tb.ButtonHoverForegroundColor(Microsoft::UI::Colors::Black());
                tb.ButtonPressedForegroundColor(Microsoft::UI::Colors::Black());
                tb.ButtonInactiveForegroundColor(winrt::Microsoft::UI::ColorHelper::FromArgb(255, 120, 120, 120));
                tb.ButtonHoverBackgroundColor(winrt::Microsoft::UI::ColorHelper::FromArgb(25, 0, 0, 0));
                tb.ButtonPressedBackgroundColor(winrt::Microsoft::UI::ColorHelper::FromArgb(50, 0, 0, 0));
            }
        }
    }

    LRESULT CALLBACK SettingsWindow::SubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
    {
        auto* pThis = reinterpret_cast<SettingsWindow*>(dwRefData);
        if (!pThis) return DefSubclassProc(hWnd, uMsg, wParam, lParam);

        if (uMsg == WM_GETICON)
        {
            if ((wParam == ICON_SMALL || wParam == 2 /*ICON_SMALL2*/) && pThis->m_hIconSmall)
            {
                return reinterpret_cast<LRESULT>(pThis->m_hIconSmall);
            }
            if (pThis->m_hIconBig)
            {
                return reinterpret_cast<LRESULT>(pThis->m_hIconBig);
            }
        }
        else if (uMsg == WM_ERASEBKGND)
        {
            return 1; // Prevent GDI background erase from flashing over DirectComposition / Mica
        }
        else if (uMsg == WM_SETTINGCHANGE || uMsg == WM_THEMECHANGED || uMsg == WM_SYSCOLORCHANGE || uMsg == WM_DWMCOLORIZATIONCOLORCHANGED)
        {
            // If theme change was initiated by our own program, suppress our own broadcast echo
            if (ThemeManager::IsBusy())
            {
                return 0;
            }

            pThis->ApplyTheme();
            return 0; // Prevent WinUI 3 from resetting controls to Application.RequestedTheme
        }
        else if (uMsg == WM_NCDESTROY)
        {
            RemoveWindowSubclass(hWnd, SubclassProc, uIdSubclass);
        }

        return DefSubclassProc(hWnd, uMsg, wParam, lParam);
    }

    void SettingsWindow::UpdateCaptureHint(UINT mods)
    {
        if (!m_isCapturing) return;

        std::wstring hint = L"[ ";
        if (mods != 0)
        {
            if (mods & MOD_WIN)     hint += L"Win + ";
            if (mods & MOD_CONTROL) hint += L"Ctrl + ";
            if (mods & MOD_ALT)     hint += L"Alt + ";
            if (mods & MOD_SHIFT)   hint += L"Shift + ";
            hint += g_Loc.Get(L"Hk_PressKey", L"Press key...") + L" ]";
        }
        else
        {
            hint += g_Loc.Get(L"Hk_PressAnyKey", L"Press any key...") + L" ]";
        }

        CaptureHotkeyButton().Content(winrt::box_value(hint));
    }

    LRESULT CALLBACK SettingsWindow::LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
    {
        if (nCode == HC_ACTION && s_currentCaptureInstance)
        {
            bool isDown = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
            bool isUp = (wParam == WM_KEYUP || wParam == WM_SYSKEYUP);
            if (isDown || isUp)
            {
                KBDLLHOOKSTRUCT* pKey = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
                if (pKey)
                {
                    UINT vkCode = pKey->vkCode;
                    UINT modBit = 0;
                    if (vkCode == VK_CONTROL || vkCode == VK_LCONTROL || vkCode == VK_RCONTROL) modBit = MOD_CONTROL;
                    else if (vkCode == VK_MENU || vkCode == VK_LMENU || vkCode == VK_RMENU) modBit = MOD_ALT;
                    else if (vkCode == VK_SHIFT || vkCode == VK_LSHIFT || vkCode == VK_RSHIFT) modBit = MOD_SHIFT;
                    else if (vkCode == VK_LWIN || vkCode == VK_RWIN) modBit = MOD_WIN;

                    if (modBit != 0)
                    {
                        if (isDown) s_captureMods |= modBit;
                        else s_captureMods &= ~modBit;
                        s_currentCaptureInstance->UpdateCaptureHint(s_captureMods);
                        return 1;
                    }

                    if (isDown)
                    {
                        s_currentCaptureInstance->HandleCapturedKey(vkCode, s_captureMods);
                    }
                    return 1;
                }
            }
        }
        return CallNextHookEx(s_hKeyboardHook, nCode, wParam, lParam);
    }

    void SettingsWindow::StartCapture()
    {
        StopCapture(false);
        m_isCapturing = true;
        s_currentCaptureInstance = this;
        s_captureMods = 0;

        UpdateCaptureHint(0);

        s_hKeyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc, GetModuleHandleW(NULL), 0);
    }

    void SettingsWindow::StopCapture(bool)
    {
        if (s_hKeyboardHook)
        {
            UnhookWindowsHookEx(s_hKeyboardHook);
            s_hKeyboardHook = NULL;
        }
        s_currentCaptureInstance = nullptr;
        s_captureMods = 0;
        m_isCapturing = false;

        auto parsed = HotkeyManager::ParseAhkHotkey(g_Settings.hotkey);
        CaptureHotkeyButton().Content(winrt::box_value(HotkeyManager::FormatPreview(parsed)));
    }

    void SettingsWindow::HandleCapturedKey(UINT vkCode, UINT mods)
    {
        if (!m_isCapturing) return;

        if (vkCode == VK_ESCAPE)
        {
            StopCapture(false);
            return;
        }

        g_Settings.hotkey = HotkeyManager::FormatAhkHotkey(vkCode, mods);
        g_Settings.Save();

        StopCapture(true);

        if (g_Settings.hotkeyEnabled)
        {
            HotkeyManager::Register();
        }
    }

    void SettingsWindow::ShowWindow()
    {
        if (!m_isInitialized)
        {
            LoadSettingsToUI();
            ApplyTheme();
            m_isInitialized = true;
        }
        else
        {
            UpdateThemeStatus();
        }

        if (m_hwnd && IsIconic(m_hwnd))
        {
            ::ShowWindow(m_hwnd, SW_RESTORE);
        }
        this->Activate();
        if (m_hwnd)
        {
            if (m_hIconBig) SendMessageW(m_hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(m_hIconBig));
            if (m_hIconSmall) SendMessageW(m_hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(m_hIconSmall));
            SetForegroundWindow(m_hwnd);
        }
        UpdateTitleBarDragRegions();
    }

    void SettingsWindow::HideWindow()
    {
        ApplyTheme();
        StopCapture(false);
        if (m_hwnd)
        {
            ::ShowWindow(m_hwnd, SW_HIDE);
        }
    }
}

void InitSettingsWindow(bool show)
{
    if (!s_settingsWindow)
    {
        s_settingsWindow = winrt::make<winrt::ThemeSwitcherApp::implementation::SettingsWindow>();
    }
    if (show)
    {
        ShowSettingsWindow();
    }
}

void ShowSettingsWindow()
{
    if (s_settingsWindow)
    {
        auto impl = winrt::get_self<winrt::ThemeSwitcherApp::implementation::SettingsWindow>(s_settingsWindow);
        if (impl && impl->IsClosing())
        {
            s_settingsWindow = nullptr;
        }
    }

    if (!s_settingsWindow)
    {
        s_settingsWindow = winrt::make<winrt::ThemeSwitcherApp::implementation::SettingsWindow>();
    }

    auto impl = winrt::get_self<winrt::ThemeSwitcherApp::implementation::SettingsWindow>(s_settingsWindow);
    if (impl)
    {
        impl->ShowWindow();
    }
}

void CloseSettingsWindow()
{
    if (s_settingsWindow)
    {
        try
        {
            auto impl = winrt::get_self<winrt::ThemeSwitcherApp::implementation::SettingsWindow>(s_settingsWindow);
            if (impl)
            {
                impl->SetExiting(true);
                impl->StopCapture(false);
                impl->SetClosing(true);
            }
            s_settingsWindow.Close();
            s_settingsWindow = nullptr;
        }
        catch (...) {}
    }
}

bool IsSettingsWindowOpen()
{
    if (!s_settingsWindow) return false;
    auto impl = winrt::get_self<winrt::ThemeSwitcherApp::implementation::SettingsWindow>(s_settingsWindow);
    return (impl && !impl->IsClosing());
}

void OnSettingsThemeChanged()
{
    if (s_settingsWindow)
    {
        auto impl = winrt::get_self<winrt::ThemeSwitcherApp::implementation::SettingsWindow>(s_settingsWindow);
        if (impl)
        {
            auto dq = impl->DispatcherQueue();
            if (dq && !dq.HasThreadAccess())
            {
                dq.TryEnqueue([impl]()
                {
                    impl->ApplyTheme();
                });
            }
            else
            {
                impl->ApplyTheme();
            }
        }
    }
}

void TrimProcessWorkingSetAsync()
{
    std::thread([]()
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(2000));
        SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1);
    }).detach();
}
