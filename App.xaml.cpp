#include "pch.h"
#include "App.xaml.h"
#if __has_include("App.g.cpp")
#include "App.g.cpp"
#endif

#if __has_include("module.g.cpp")
#include "module.g.cpp"
#endif

#if __has_include("XamlMetaDataProvider.g.cpp")
#include "XamlMetaDataProvider.g.cpp"
#endif

#include "XamlTypeInfo.Impl.g.cpp"
#include "XamlTypeInfo.g.cpp"

#include "Settings.h"
#include "LocalizationManager.h"
#include "ThemeManager.h"
#include "TrayIcon.h"
#include "HotkeyManager.h"
#include "SettingsWindow.xaml.h"
#include "OverlayWindow.h"
#include "ThemeHelper.h"
#include "ScheduleManager.h"
#include "resource.h"

#include <strsafe.h>
#include <roerrorapi.h>
#include <thread>
#include <atomic>

static const wchar_t* MAIN_WND_CLASS = L"ClickDarkMode_MessageWndClass";
static HWND g_hMainWnd = NULL;
static UINT g_uTaskbarRestartMsg = 0;
static HANDLE s_hEvents[3] = { NULL, NULL, NULL };
static std::atomic<bool> s_ipcRunning{ false };
static std::thread s_ipcThread;

static LRESULT CALLBACK MainWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    try
    {
        if (uMsg == g_uTaskbarRestartMsg && g_uTaskbarRestartMsg != 0)
        {
            TrayIcon::Init(hwnd);
            return 0;
        }

        switch (uMsg)
        {
        case WM_SETTINGCHANGE:
        {
            ThemeManager::OnSettingChange(lParam);
            return 0;
        }

        case WM_DISPLAYCHANGE:
        {
            g_OverlayWindow.ResetGraphicsResources();
            return 0;
        }

        case WM_APP_TRAYMSG:
        {
            TrayIcon::HandleTrayMessage(wParam, lParam);
            return 0;
        }

        case WM_HOTKEY:
        {
            if (wParam == ID_HOTKEY_TOGGLE)
            {
                ThemeManager::Toggle(true);
            }
            return 0;
        }

        case WM_COMMAND:
        {
            WORD id = LOWORD(wParam);
            switch (id)
            {
            case IDM_TRAY_TOGGLE:
                ThemeManager::Toggle();
                break;
            case IDM_TRAY_SETTINGS:
                ShowSettingsWindow();
                break;
            case IDM_TRAY_EXIT:
                ExitApp();
                break;
            }
            return 0;
        }

        case WM_TIMECHANGE:
        {
            ScheduleManager::Refresh();
            return 0;
        }

        case WM_TIMER:
        {
            if (wParam == 1005)
            {
                ScheduleManager::Tick();
                return 0;
            }
            break;
        }

        case WM_CLOSE:
        {
            ExitApp();
            return 0;
        }

        case WM_DESTROY:
        {
            ScheduleManager::Uninit();
            TrayIcon::Remove();
            HotkeyManager::Unregister(hwnd);
            g_OverlayWindow.Destroy();
            return 0;
        }
        }
    }
    catch (...)
    {
    }

    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

void ExitApp()
{
    // 1. Terminate IPC listener thread
    s_ipcRunning = false;
    for (int i = 0; i < 3; ++i)
    {
        if (s_hEvents[i])
        {
            SetEvent(s_hEvents[i]);
        }
    }
    if (s_ipcThread.joinable())
    {
        s_ipcThread.join();
    }
    for (int i = 0; i < 3; ++i)
    {
        if (s_hEvents[i])
        {
            CloseHandle(s_hEvents[i]);
            s_hEvents[i] = NULL;
        }
    }

    // 2. Tear down UI windows cleanly
    CloseSettingsWindow();
    g_OverlayWindow.Destroy();
    TrayIcon::CloseTrayMenu();

    // 3. Destroy message window
    if (g_hMainWnd)
    {
        HWND h = g_hMainWnd;
        g_hMainWnd = NULL;
        DestroyWindow(h);
    }

    // 4. Request WinUI 3 message loop exit
    try
    {
        winrt::Microsoft::UI::Xaml::Application::Current().Exit();
    }
    catch (...) {}
}

namespace winrt::ThemeSwitcherApp::implementation
{
    App::App()
    {
        UnhandledException([](auto const&, Microsoft::UI::Xaml::UnhandledExceptionEventArgs const& args)
        {
            args.Handled(true);
        });

        g_Settings.Load();
        g_Loc.Initialize();
        g_Loc.SetLanguage(g_Settings.language);
        bool isDark = ThemeHelper::IsAppInDarkMode(g_Settings.appTheme);
        RequestedTheme(isDark ? Microsoft::UI::Xaml::ApplicationTheme::Dark : Microsoft::UI::Xaml::ApplicationTheme::Light);

        try
        {
            InitializeComponent();
        }
        catch (...)
        {
        }
    }

    void App::OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const&)
    {
        try
        {
            g_uTaskbarRestartMsg = RegisterWindowMessageW(L"TaskbarCreated");

            g_Settings.Load();
            g_Loc.Initialize();
            g_Loc.SetLanguage(g_Settings.language);

            HINSTANCE hInstance = GetModuleHandleW(NULL);
            WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
            wc.lpfnWndProc = MainWndProc;
            wc.hInstance = hInstance;
            wc.lpszClassName = MAIN_WND_CLASS;
            RegisterClassExW(&wc);

            g_hMainWnd = CreateWindowExW(
                0,
                MAIN_WND_CLASS,
                L"ClickDarkMode_HiddenMsgWindow",
                WS_OVERLAPPED,
                0, 0, 0, 0,
                NULL, NULL, hInstance, NULL
            );

            if (g_hMainWnd)
            {
                TrayIcon::Init(g_hMainWnd);
                HotkeyManager::Register(g_hMainWnd);
                g_OverlayWindow.Create();
                ScheduleManager::Init(g_hMainWnd);
            }

            // Setup IPC named events
            s_hEvents[0] = CreateEventW(NULL, FALSE, FALSE, EVENT_NAME_SHOW_SETTINGS);
            s_hEvents[1] = CreateEventW(NULL, FALSE, FALSE, EVENT_NAME_EXIT);
            s_hEvents[2] = CreateEventW(NULL, FALSE, FALSE, EVENT_NAME_TOGGLE);
            for (int i = 0; i < 3; ++i)
            {
                if (s_hEvents[i]) ResetEvent(s_hEvents[i]);
            }
            s_ipcRunning = true;

            s_ipcThread = std::thread([]()
            {
                while (s_ipcRunning)
                {
                    DWORD dwWait = WaitForMultipleObjects(3, s_hEvents, FALSE, 250);
                    if (!s_ipcRunning) break;
                    if (dwWait == WAIT_OBJECT_0) // Show Settings
                    {
                        if (g_hMainWnd) PostMessageW(g_hMainWnd, WM_COMMAND, MAKEWPARAM(IDM_TRAY_SETTINGS, 0), 0);
                    }
                    else if (dwWait == WAIT_OBJECT_0 + 1) // Exit
                    {
                        if (g_hMainWnd) PostMessageW(g_hMainWnd, WM_COMMAND, MAKEWPARAM(IDM_TRAY_EXIT, 0), 0);
                        break;
                    }
                    else if (dwWait == WAIT_OBJECT_0 + 2) // Toggle
                    {
                        if (g_hMainWnd) PostMessageW(g_hMainWnd, WM_COMMAND, MAKEWPARAM(IDM_TRAY_TOGGLE, 0), 0);
                    }
                }
            });

            // Check command line for --settings / -s
            LPWSTR cmdLine = GetCommandLineW();
            bool openSettings = (cmdLine && (wcsstr(cmdLine, L"--settings") || wcsstr(cmdLine, L"-s")));

            if (openSettings)
            {
                ShowSettingsWindow();
            }
            else
            {
                TrimProcessWorkingSetAsync();
            }
        }
        catch (...)
        {
        }
    }
}
