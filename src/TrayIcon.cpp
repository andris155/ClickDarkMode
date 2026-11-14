#include "pch.h"
#include "TrayIcon.h"
#include "ThemeManager.h"
#include "Settings.h"
#include "LocalizationManager.h"
#include "ThemeHelper.h"
#include "DpiHelper.h"
#include "App.xaml.h"
#include "resource.h"

#include <microsoft.ui.xaml.window.h>
#include <winrt/Microsoft.UI.Interop.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Windowing.h>
#include <dwmapi.h>

namespace TrayIcon {

static HWND s_hOwnerWnd = NULL;
static NOTIFYICONDATAW s_nid = { sizeof(s_nid) };
static HICON s_hCurrentIcon = NULL;
static std::wstring s_activeIconFile = L"";
static int s_activeIconNumber = 0;
static bool s_isInstalled = false;

// WinUI 3 Tray Menu flyout & anchor window (ClickBrightness design)
static winrt::Microsoft::UI::Xaml::Window s_trayMenuWindow{ nullptr };
static winrt::Microsoft::UI::Xaml::Controls::Grid s_trayMenuRoot{ nullptr };
static winrt::Microsoft::UI::Xaml::Controls::MenuFlyout s_trayMenuFlyout{ nullptr };
static winrt::Microsoft::UI::Xaml::Controls::MenuFlyoutItem s_toggleItem{ nullptr };
static winrt::Microsoft::UI::Xaml::Controls::MenuFlyoutItem s_settingsItem{ nullptr };
static winrt::Microsoft::UI::Xaml::Controls::MenuFlyoutItem s_aboutItem{ nullptr };
static winrt::Microsoft::UI::Xaml::Controls::MenuFlyoutItem s_exitItem{ nullptr };

static HWND s_hTrayMenuWnd = NULL;
static std::atomic<bool> s_isMenuOpen{ false };
static std::atomic<ULONGLONG> s_lastMenuCloseTick{ 0 };
static ULONGLONG s_lastTrayRightClick = 0;

typedef int(WINAPI* PFN_PickIconDlg)(HWND, LPWSTR, UINT, int*);

static std::wstring ResolvePath(const std::wstring& path) {
    wchar_t expanded[MAX_PATH] = { 0 };
    ExpandEnvironmentStringsW(path.c_str(), expanded, MAX_PATH);

    if (GetFileAttributesW(expanded) != INVALID_FILE_ATTRIBUTES) {
        return std::wstring(expanded);
    }

    wchar_t moduleDir[MAX_PATH] = { 0 };
    GetModuleFileNameW(NULL, moduleDir, MAX_PATH);
    wchar_t* lastSlash = wcsrchr(moduleDir, L'\\');
    if (lastSlash) *(lastSlash + 1) = L'\0';
    std::wstring candidate = std::wstring(moduleDir) + expanded;
    if (GetFileAttributesW(candidate.c_str()) != INVALID_FILE_ATTRIBUTES) {
        return candidate;
    }

    candidate = std::wstring(moduleDir) + L"resources\\" + expanded;
    if (GetFileAttributesW(candidate.c_str()) != INVALID_FILE_ATTRIBUTES) {
        return candidate;
    }

    wchar_t sysDir[MAX_PATH] = { 0 };
    GetSystemDirectoryW(sysDir, MAX_PATH);
    candidate = std::wstring(sysDir) + L"\\" + expanded;
    if (GetFileAttributesW(candidate.c_str()) != INVALID_FILE_ATTRIBUTES) {
        return candidate;
    }

    wchar_t winDir[MAX_PATH] = { 0 };
    GetWindowsDirectoryW(winDir, MAX_PATH);
    candidate = std::wstring(winDir) + L"\\" + expanded;
    if (GetFileAttributesW(candidate.c_str()) != INVALID_FILE_ATTRIBUTES) {
        return candidate;
    }

    return std::wstring(expanded);
}

HICON LoadIconResource(const std::wstring& path, int iconNumber, int cx, int cy) {
    HINSTANCE hInst = GetModuleHandleW(NULL);

    std::wstring pLower = path;
    std::transform(pLower.begin(), pLower.end(), pLower.begin(), ::towlower);
    size_t slash = pLower.find_last_of(L"\\/");
    std::wstring fname = (slash != std::wstring::npos) ? pLower.substr(slash + 1) : pLower;

    if (fname == L"sun.ico" || (path == L"<bundle>" && iconNumber == IDI_ICON_SUN)) {
        HICON h = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_ICON_SUN), IMAGE_ICON, cx, cy, LR_DEFAULTCOLOR);
        if (h) return h;
    }
    if (fname == L"moon.ico" || (path == L"<bundle>" && iconNumber == IDI_ICON_MOON)) {
        HICON h = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_ICON_MOON), IMAGE_ICON, cx, cy, LR_DEFAULTCOLOR);
        if (h) return h;
    }
    if (fname == L"program.ico" || (path == L"<bundle>" && iconNumber == IDI_APP_ICON)) {
        HICON h = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, cx, cy, LR_DEFAULTCOLOR);
        if (h) return h;
    }

    std::wstring fullPath = ResolvePath(path.empty() ? L"shell32.dll" : path);

    size_t dotPos = fullPath.find_last_of(L'.');
    if (dotPos != std::wstring::npos) {
        std::wstring ext = fullPath.substr(dotPos);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
        if (ext == L".ico") {
            HICON hIco = (HICON)LoadImageW(NULL, fullPath.c_str(), IMAGE_ICON, cx, cy, LR_LOADFROMFILE | LR_DEFAULTSIZE);
            if (hIco) return hIco;
        }
    }

    HICON hIcon = NULL;
    UINT iconId = 0;
    int index0 = (iconNumber > 0) ? (iconNumber - 1) : 0;

    UINT extracted = PrivateExtractIconsW(fullPath.c_str(), index0, cx, cy, &hIcon, &iconId, 1, LR_LOADFROMFILE);
    if (extracted > 0 && hIcon) {
        return hIcon;
    }

    HICON hLarge = NULL, hSmall = NULL;
    UINT res = ExtractIconExW(fullPath.c_str(), index0, &hLarge, &hSmall, 1);
    if (res > 0) {
        if (cx <= 16 && hSmall) {
            if (hLarge) DestroyIcon(hLarge);
            return hSmall;
        }
        if (hLarge) {
            if (hSmall) DestroyIcon(hSmall);
            return hLarge;
        }
        if (hSmall) return hSmall;
    }

    // Fallback: shell32.dll default or embedded application icon
    wchar_t sysDir[MAX_PATH] = { 0 };
    GetSystemDirectoryW(sysDir, MAX_PATH);
    std::wstring shell32 = std::wstring(sysDir) + L"\\shell32.dll";
    int fallbackIndex = 43; // 44 (1-based) is index 43
    PrivateExtractIconsW(shell32.c_str(), fallbackIndex, cx, cy, &hIcon, &iconId, 1, LR_LOADFROMFILE);
    if (!hIcon) {
        hIcon = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, cx, cy, LR_DEFAULTCOLOR);
    }
    return hIcon;
}

bool PickIcon(HWND hWndParent, std::wstring& inOutFile, int& inOutNumber) {
    HMODULE hShell = LoadLibraryW(L"shell32.dll");
    if (!hShell) return false;

    PFN_PickIconDlg pfnPickIconDlg = (PFN_PickIconDlg)GetProcAddress(hShell, "PickIconDlg");
    if (!pfnPickIconDlg) {
        pfnPickIconDlg = (PFN_PickIconDlg)GetProcAddress(hShell, (LPCSTR)62);
    }

    if (!pfnPickIconDlg) {
        FreeLibrary(hShell);
        return false;
    }

    // Default to %SystemRoot%\System32\SHELL32.dll if bundled or empty
    std::wstring initialFile = inOutFile;
    if (initialFile == L"sun.ico" || initialFile == L"moon.ico" || initialFile.empty()) {
        wchar_t sysDir[MAX_PATH] = { 0 };
        GetSystemDirectoryW(sysDir, MAX_PATH);
        initialFile = std::wstring(sysDir) + L"\\SHELL32.dll";
        if (inOutNumber <= 0) inOutNumber = 1;
    } else {
        wchar_t expanded[MAX_PATH] = { 0 };
        ExpandEnvironmentStringsW(initialFile.c_str(), expanded, MAX_PATH);
        initialFile = expanded;
    }

    wchar_t szBuffer[MAX_PATH] = { 0 };
    wcsncpy_s(szBuffer, MAX_PATH, initialFile.c_str(), _TRUNCATE);
    int iconIdx0 = (inOutNumber > 0) ? (inOutNumber - 1) : 0;

    int res = pfnPickIconDlg(hWndParent, szBuffer, MAX_PATH, &iconIdx0);
    FreeLibrary(hShell);

    if (res == 1) {
        inOutFile = szBuffer;
        inOutNumber = iconIdx0 + 1;
        return true;
    }
    return false;
}

static void InitTrayMenu() {
    if (s_trayMenuWindow && s_trayMenuFlyout) return;

    try {
        s_trayMenuWindow = winrt::Microsoft::UI::Xaml::Window();
        auto windowNative = s_trayMenuWindow.try_as<::IWindowNative>();
        if (windowNative) {
            windowNative->get_WindowHandle(&s_hTrayMenuWnd);
        }

        if (s_hTrayMenuWnd) {
            auto windowId = winrt::Microsoft::UI::GetWindowIdFromWindow(s_hTrayMenuWnd);
            auto appWnd = winrt::Microsoft::UI::Windowing::AppWindow::GetFromWindowId(windowId);
            if (appWnd) {
                auto p = appWnd.Presenter().try_as<winrt::Microsoft::UI::Windowing::OverlappedPresenter>();
                if (p) {
                    p.SetBorderAndTitleBar(false, false);
                    p.IsAlwaysOnTop(true);
                    p.IsResizable(false);
                    p.IsMinimizable(false);
                    p.IsMaximizable(false);
                }
            }

            // Strip window decorations
            LONG_PTR style = GetWindowLongPtrW(s_hTrayMenuWnd, GWL_STYLE);
            style &= ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU | WS_BORDER | WS_DLGFRAME);
            style |= WS_POPUP;
            SetWindowLongPtrW(s_hTrayMenuWnd, GWL_STYLE, style);

            LONG_PTR exStyle = GetWindowLongPtrW(s_hTrayMenuWnd, GWL_EXSTYLE);
            exStyle &= ~(WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE | WS_EX_STATICEDGE | WS_EX_DLGMODALFRAME);
            exStyle |= WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE;
            SetWindowLongPtrW(s_hTrayMenuWnd, GWL_EXSTYLE, exStyle);

            COLORREF borderColor = DWMWA_COLOR_NONE;
            DwmSetWindowAttribute(s_hTrayMenuWnd, DWMWA_BORDER_COLOR, &borderColor, sizeof(borderColor));

            DWM_WINDOW_CORNER_PREFERENCE cornerPref = DWMWCP_DONOTROUND;
            DwmSetWindowAttribute(s_hTrayMenuWnd, DWMWA_WINDOW_CORNER_PREFERENCE, &cornerPref, sizeof(cornerPref));
        }

        s_trayMenuRoot = winrt::Microsoft::UI::Xaml::Controls::Grid();
        s_trayMenuRoot.Background(winrt::Microsoft::UI::Xaml::Media::SolidColorBrush(winrt::Microsoft::UI::Colors::Transparent()));
        s_trayMenuWindow.Content(s_trayMenuRoot);

        s_trayMenuFlyout = winrt::Microsoft::UI::Xaml::Controls::MenuFlyout();

        // 1. Toggle Mode
        s_toggleItem = winrt::Microsoft::UI::Xaml::Controls::MenuFlyoutItem();
        s_toggleItem.Text(g_Loc.Get(L"Tray_Toggle", L"Toggle Mode"));
        s_toggleItem.Height(32.0);
        s_toggleItem.Padding({ 11.0, 4.0, 11.0, 5.0 });
        auto iconToggle = winrt::Microsoft::UI::Xaml::Controls::FontIcon();
        iconToggle.Glyph(L"\xE790");
        s_toggleItem.Icon(iconToggle);
        s_toggleItem.Click([](auto&&, auto&&) {
            if (s_trayMenuFlyout) {
                s_trayMenuFlyout.Hide();
            }
            ThemeManager::Toggle();
        });
        s_trayMenuFlyout.Items().Append(s_toggleItem);

        // Separator
        s_trayMenuFlyout.Items().Append(winrt::Microsoft::UI::Xaml::Controls::MenuFlyoutSeparator());

        // 2. Settings...
        s_settingsItem = winrt::Microsoft::UI::Xaml::Controls::MenuFlyoutItem();
        s_settingsItem.Text(g_Loc.Get(L"Tray_Settings", L"Settings..."));
        s_settingsItem.Height(32.0);
        s_settingsItem.Padding({ 11.0, 4.0, 11.0, 5.0 });
        auto iconSettings = winrt::Microsoft::UI::Xaml::Controls::FontIcon();
        iconSettings.Glyph(L"\xE713");
        s_settingsItem.Icon(iconSettings);
        s_settingsItem.Click([](auto&&, auto&&) {
            if (s_trayMenuFlyout) {
                s_trayMenuFlyout.Hide();
            }
            ShowSettingsWindow();
        });
        s_trayMenuFlyout.Items().Append(s_settingsItem);

        // Separator
        s_trayMenuFlyout.Items().Append(winrt::Microsoft::UI::Xaml::Controls::MenuFlyoutSeparator());

        // 3. About info (disabled item)
        s_aboutItem = winrt::Microsoft::UI::Xaml::Controls::MenuFlyoutItem();
        s_aboutItem.Text(L"ClickDarkMode v1.0.0");
        s_aboutItem.Height(32.0);
        s_aboutItem.Padding({ 11.0, 4.0, 11.0, 5.0 });
        auto iconInfo = winrt::Microsoft::UI::Xaml::Controls::FontIcon();
        iconInfo.Glyph(L"\xE946");
        s_aboutItem.Icon(iconInfo);
        s_aboutItem.IsEnabled(false);
        s_trayMenuFlyout.Items().Append(s_aboutItem);

        // 4. Exit
        s_exitItem = winrt::Microsoft::UI::Xaml::Controls::MenuFlyoutItem();
        s_exitItem.Text(g_Loc.Get(L"Tray_Exit", L"Exit"));
        s_exitItem.Height(32.0);
        s_exitItem.Padding({ 11.0, 4.0, 11.0, 5.0 });
        auto iconExit = winrt::Microsoft::UI::Xaml::Controls::FontIcon();
        iconExit.Glyph(L"\xE7E8");
        s_exitItem.Icon(iconExit);
        s_exitItem.Click([](auto&&, auto&&) {
            if (s_trayMenuFlyout) {
                s_trayMenuFlyout.Hide();
            }
            ExitApp();
        });
        s_trayMenuFlyout.Items().Append(s_exitItem);

        s_trayMenuFlyout.Opened([](auto&&, auto&&) {
            s_isMenuOpen = true;
        });

        s_trayMenuFlyout.Closed([](auto&&, auto&&) {
            s_isMenuOpen = false;
            s_lastMenuCloseTick = GetTickCount64();
            if (s_hTrayMenuWnd) {
                ShowWindow(s_hTrayMenuWnd, SW_HIDE);
            }
        });

        // Position window offscreen first so pre-activation never flickers or steals focus
        if (s_hTrayMenuWnd) {
            SetWindowPos(s_hTrayMenuWnd, HWND_TOPMOST, -32000, -32000, 1, 1, SWP_NOACTIVATE | SWP_HIDEWINDOW);
        }

        HWND hPrevFocus = GetForegroundWindow();
        s_trayMenuWindow.Activate();
        if (s_hTrayMenuWnd) {
            ShowWindow(s_hTrayMenuWnd, SW_HIDE);
        }
        if (hPrevFocus && hPrevFocus != s_hTrayMenuWnd) {
            SetForegroundWindow(hPrevFocus);
        }

        // Reset so pre-activation does not block the user's first click
        s_lastMenuCloseTick = 0;
        s_isMenuOpen = false;
    } catch (...) {}
}

bool Init(HWND hWnd) {
    s_hOwnerWnd = hWnd;
    ZeroMemory(&s_nid, sizeof(s_nid));
    s_nid.cbSize = sizeof(NOTIFYICONDATAW);
    s_nid.hWnd = hWnd;
    s_nid.uID = 1;
    s_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    s_nid.uCallbackMessage = WM_APP_TRAYMSG;
    s_nid.uVersion = NOTIFYICON_VERSION_4;

    SyncUI();

    BOOL bRes = Shell_NotifyIconW(NIM_ADD, &s_nid);
    if (!bRes && GetLastError() == ERROR_ALREADY_EXISTS) {
        bRes = Shell_NotifyIconW(NIM_MODIFY, &s_nid);
    }
    if (bRes) {
        Shell_NotifyIconW(NIM_SETVERSION, &s_nid);
        s_isInstalled = true;
    }
    InitTrayMenu();
    return (bRes == TRUE);
}

void Remove() {
    CloseTrayMenu();
    if (s_isInstalled) {
        Shell_NotifyIconW(NIM_DELETE, &s_nid);
        s_isInstalled = false;
    }
    if (s_hCurrentIcon) {
        DestroyIcon(s_hCurrentIcon);
        s_hCurrentIcon = NULL;
    }
}

void SyncUI() {
    int state = ThemeManager::GetState();
    int effectiveState = (state == -1) ? ThemeManager::ReadRegKey(L"AppsUseLightTheme", 1) : state;
    bool useDark = g_Settings.dualIcons && (effectiveState == 0);

    std::wstring targetFile = useDark ? g_Settings.darkIconFile : g_Settings.iconFile;
    int targetNum = useDark ? g_Settings.darkIconNumber : g_Settings.iconNumber;

    int cx = GetSystemMetrics(SM_CXSMICON);
    int cy = GetSystemMetrics(SM_CYSMICON);
    if (cx <= 0) cx = 16;
    if (cy <= 0) cy = 16;

    HICON hNewIcon = LoadIconResource(targetFile, targetNum, cx, cy);
    if (hNewIcon) {
        if (s_hCurrentIcon) {
            DestroyIcon(s_hCurrentIcon);
        }
        s_hCurrentIcon = hNewIcon;
        s_nid.hIcon = s_hCurrentIcon;
        s_activeIconFile = targetFile;
        s_activeIconNumber = targetNum;
    }

    std::wstring tip = ThemeManager::GetTooltipText();
    wcsncpy_s(s_nid.szTip, ARRAYSIZE(s_nid.szTip), tip.c_str(), _TRUNCATE);

    if (s_isInstalled) {
        s_nid.uFlags = NIF_ICON | NIF_TIP | NIF_SHOWTIP;
        Shell_NotifyIconW(NIM_MODIFY, &s_nid);
    }
}

void ShowContextMenu() {
    try {
        InitTrayMenu();
        if (!s_hTrayMenuWnd || !s_trayMenuFlyout || !s_trayMenuRoot) return;

        // 1. If menu is already open, right-clicking toggles it closed cleanly
        if (s_isMenuOpen.load() || s_trayMenuFlyout.IsOpen()) {
            try {
                s_trayMenuFlyout.Hide();
            } catch (...) {}
            s_isMenuOpen = false;
            return;
        }

        // 2. Debounce: if the menu closed very recently, avoid re-opening during exit animation
        ULONGLONG now = GetTickCount64();
        if (s_lastMenuCloseTick.load() > 0 && (now - s_lastMenuCloseTick.load() < 250)) {
            return;
        }

        // Update localized texts for menu items
        if (s_toggleItem) s_toggleItem.Text(g_Loc.Get(L"Tray_Toggle", L"Toggle Mode"));
        if (s_settingsItem) s_settingsItem.Text(g_Loc.Get(L"Tray_Settings", L"Settings..."));
        if (s_exitItem) s_exitItem.Text(g_Loc.Get(L"Tray_Exit", L"Exit"));

        RECT rcIcon = { 0 };
        NOTIFYICONIDENTIFIER nii = { sizeof(NOTIFYICONIDENTIFIER) };
        nii.hWnd = s_hOwnerWnd;
        nii.uID = 1;
        HRESULT hr = Shell_NotifyIconGetRect(&nii, &rcIcon);

        int iconX = 0, iconY = 0, iconW = 32, iconH = 32;
        if (SUCCEEDED(hr) && (rcIcon.right > rcIcon.left) && (rcIcon.bottom > rcIcon.top)) {
            iconX = rcIcon.left;
            iconY = rcIcon.top;
            iconW = rcIcon.right - rcIcon.left;
            iconH = rcIcon.bottom - rcIcon.top;
        } else {
            POINT pt;
            GetCursorPos(&pt);
            iconX = pt.x - 16;
            iconY = pt.y - 16;
            iconW = 32;
            iconH = 32;
        }

        int iconCenterX = iconX + iconW / 2;

        s_trayMenuRoot.Width(1);
        s_trayMenuRoot.Height(1);

        bool isDark = ThemeHelper::IsTrayMenuInDarkMode(g_Settings.trayMenuTheme);
        auto theme = isDark ? winrt::Microsoft::UI::Xaml::ElementTheme::Dark : winrt::Microsoft::UI::Xaml::ElementTheme::Light;
        s_trayMenuRoot.RequestedTheme(theme);
        for (auto item : s_trayMenuFlyout.Items()) {
            if (auto fe = item.try_as<winrt::Microsoft::UI::Xaml::FrameworkElement>()) {
                fe.RequestedTheme(theme);
            }
        }

        if (g_Settings.transparencyEnabled) {
            s_trayMenuFlyout.MenuFlyoutPresenterStyle(nullptr);
        } else {
            winrt::Microsoft::UI::Xaml::Style presenterStyle(winrt::xaml_typename<winrt::Microsoft::UI::Xaml::Controls::MenuFlyoutPresenter>());
            winrt::Windows::UI::Color solidBg = isDark
                ? winrt::Microsoft::UI::ColorHelper::FromArgb(255, 44, 44, 44)
                : winrt::Microsoft::UI::ColorHelper::FromArgb(255, 243, 243, 243);
            presenterStyle.Setters().Append(winrt::Microsoft::UI::Xaml::Setter(
                winrt::Microsoft::UI::Xaml::Controls::Control::BackgroundProperty(),
                winrt::Microsoft::UI::Xaml::Media::SolidColorBrush(solidBg)
            ));
            s_trayMenuFlyout.MenuFlyoutPresenterStyle(presenterStyle);
        }

        int anchorY = (iconY < 100) ? (iconY + iconH + 1) : (iconY - 1);
        SetWindowPos(s_hTrayMenuWnd, HWND_TOPMOST, iconCenterX, anchorY, 1, 1, SWP_SHOWWINDOW);
        ShowWindow(s_hTrayMenuWnd, SW_SHOW);
        SetForegroundWindow(s_hTrayMenuWnd);

        winrt::Microsoft::UI::Xaml::Controls::Primitives::FlyoutShowOptions options;
        if (iconY < 100) {
            options.Placement(winrt::Microsoft::UI::Xaml::Controls::Primitives::FlyoutPlacementMode::Bottom);
        } else {
            options.Placement(winrt::Microsoft::UI::Xaml::Controls::Primitives::FlyoutPlacementMode::Top);
        }
        options.ShowMode(winrt::Microsoft::UI::Xaml::Controls::Primitives::FlyoutShowMode::Standard);
        s_trayMenuFlyout.ShowAt(s_trayMenuRoot, options);
        s_isMenuOpen = true;
    } catch (...) {
        s_isMenuOpen = false;
        if (s_hTrayMenuWnd) {
            ShowWindow(s_hTrayMenuWnd, SW_HIDE);
        }
    }
}

void CloseTrayMenu() {
    try {
        s_isMenuOpen = false;
        if (s_trayMenuFlyout) {
            s_trayMenuFlyout.Hide();
            s_trayMenuFlyout = nullptr;
        }
        if (s_trayMenuRoot) {
            s_trayMenuRoot = nullptr;
        }
        if (s_trayMenuWindow) {
            s_trayMenuWindow.Close();
            s_trayMenuWindow = nullptr;
        }
        s_hTrayMenuWnd = NULL;
    } catch (...) {}
}

void HandleTrayMessage(WPARAM, LPARAM lParam) {
    UINT msg = LOWORD(lParam);

    switch (msg) {
        case WM_LBUTTONUP:
        case NIN_SELECT:
            ThemeManager::Toggle();
            break;

        case WM_RBUTTONUP:
        case WM_CONTEXTMENU:
        {
            ULONGLONG now = GetTickCount64();
            if (now - s_lastTrayRightClick < 250) break;
            s_lastTrayRightClick = now;
            ShowContextMenu();
            break;
        }

        case WM_LBUTTONDBLCLK:
            ThemeManager::Toggle();
            break;
    }
}

} // namespace TrayIcon
