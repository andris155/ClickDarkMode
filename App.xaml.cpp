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
#include <dbt.h>
#include <thread>
#include <atomic>

static const wchar_t* MAIN_WND_CLASS = L"ClickDarkMode_MessageWndClass";
static HWND g_hMainWnd = NULL;
static UINT g_uTaskbarRestartMsg = 0;
static constexpr UINT_PTR IDT_GPU_RESET_COOLDOWN = 1006;
static HANDLE s_hEvents[5] = { NULL, NULL, NULL, NULL, NULL };
static HANDLE s_hStopEvent = NULL;
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
            ThemeHelper::GetAccentColors(true);
            ThemeManager::OnSettingChange(lParam);
            return 0;
        }

        case WM_THEMECHANGED:
        case WM_DWMCOLORIZATIONCOLORCHANGED:
        case WM_SYSCOLORCHANGE:
        {
            ThemeHelper::GetAccentColors(true);
            ThemeManager::OnSettingChange(0); // Explicit fallback sync: lParam is numeric, not a string
            return 0;
        }

        case WM_DISPLAYCHANGE:
        {
            // Debounce graphics reset on display change
            SetTimer(hwnd, IDT_GPU_RESET_COOLDOWN, 1000, NULL);
            return 0;
        }

        case WM_DEVICECHANGE:
        {
            // DBT_DEVNODES_CHANGED (0x0007) is broadcast when GPU driver restarts/reloads or display devices change
            if (wParam == DBT_DEVNODES_CHANGED || wParam == 0x0007)
            {
                SetTimer(hwnd, IDT_GPU_RESET_COOLDOWN, 1500, NULL);
            }
            return 0;
        }

        case WM_POWERBROADCAST:
        {
            if (wParam == PBT_APMRESUMEAUTOMATIC || wParam == PBT_APMRESUMESUSPEND)
            {
                // Computer woke up from sleep or hibernation: immediately refresh schedules and theme
                ScheduleManager::Refresh();
                ScheduleManager::Tick();
                ThemeHelper::GetAccentColors(true);
                ThemeManager::OnSettingChange(0);
                SetTimer(hwnd, IDT_GPU_RESET_COOLDOWN, 500, NULL);
            }
            return TRUE;
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
            case IDM_TRAY_DARK:
                ThemeManager::SetState(0, g_Settings.osdHotkeyEnabled);
                break;
            case IDM_TRAY_LIGHT:
                ThemeManager::SetState(1, g_Settings.osdHotkeyEnabled);
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
            else if (wParam == IDT_GPU_RESET_COOLDOWN)
            {
                KillTimer(hwnd, IDT_GPU_RESET_COOLDOWN);
                g_OverlayWindow.ResetGraphicsResources();
                TrayIcon::CloseTrayMenu();
                TrayIcon::SyncUI();
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
            KillTimer(hwnd, IDT_GPU_RESET_COOLDOWN);
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
    static bool s_exiting = false;
    if (s_exiting) return;
    s_exiting = true;

    // 1. Terminate IPC listener thread
    s_ipcRunning = false;
    if (s_hStopEvent)
    {
        SetEvent(s_hStopEvent);
    }
    if (s_ipcThread.joinable())
    {
        s_ipcThread.join();
    }
    if (s_hStopEvent)
    {
        CloseHandle(s_hStopEvent);
        s_hStopEvent = NULL;
    }
    for (int i = 0; i < 5; ++i)
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

            // Setup IPC named events and dedicated termination event
            s_hStopEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
            s_hEvents[0] = CreateEventW(NULL, FALSE, FALSE, EVENT_NAME_SHOW_SETTINGS);
            s_hEvents[1] = CreateEventW(NULL, FALSE, FALSE, EVENT_NAME_EXIT);
            s_hEvents[2] = CreateEventW(NULL, FALSE, FALSE, EVENT_NAME_TOGGLE);
            s_hEvents[3] = CreateEventW(NULL, FALSE, FALSE, EVENT_NAME_DARK);
            s_hEvents[4] = CreateEventW(NULL, FALSE, FALSE, EVENT_NAME_LIGHT);
            for (int i = 0; i < 5; ++i)
            {
                if (s_hEvents[i]) ResetEvent(s_hEvents[i]);
            }
            s_ipcRunning = true;

            s_ipcThread = std::thread([]()
            {
                HANDLE waitHandles[6] = {
                    s_hStopEvent,
                    s_hEvents[0],
                    s_hEvents[1],
                    s_hEvents[2],
                    s_hEvents[3],
                    s_hEvents[4]
                };

                while (s_ipcRunning)
                {
                    DWORD dwWait = WaitForMultipleObjects(6, waitHandles, FALSE, INFINITE);
                    if (!s_ipcRunning || dwWait == WAIT_OBJECT_0) break;

                    if (dwWait == WAIT_OBJECT_0 + 1) // Show Settings
                    {
                        if (g_hMainWnd) PostMessageW(g_hMainWnd, WM_COMMAND, MAKEWPARAM(IDM_TRAY_SETTINGS, 0), 0);
                    }
                    else if (dwWait == WAIT_OBJECT_0 + 2) // Exit
                    {
                        if (g_hMainWnd) PostMessageW(g_hMainWnd, WM_COMMAND, MAKEWPARAM(IDM_TRAY_EXIT, 0), 0);
                        break;
                    }
                    else if (dwWait == WAIT_OBJECT_0 + 3) // Toggle
                    {
                        if (g_hMainWnd) PostMessageW(g_hMainWnd, WM_COMMAND, MAKEWPARAM(IDM_TRAY_TOGGLE, 0), 0);
                    }
                    else if (dwWait == WAIT_OBJECT_0 + 4) // Set Dark
                    {
                        if (g_hMainWnd) PostMessageW(g_hMainWnd, WM_COMMAND, MAKEWPARAM(IDM_TRAY_DARK, 0), 0);
                    }
                    else if (dwWait == WAIT_OBJECT_0 + 5) // Set Light
                    {
                        if (g_hMainWnd) PostMessageW(g_hMainWnd, WM_COMMAND, MAKEWPARAM(IDM_TRAY_LIGHT, 0), 0);
                    }
                }
            });

            // Check command line for --settings / -s, --dark / -d, --light / -l, or --toggle / -t
            LPWSTR cmdLine = GetCommandLineW();
            bool openSettings = (cmdLine && (wcsstr(cmdLine, L"--settings") || wcsstr(cmdLine, L"-s")));
            bool darkImmediate = (cmdLine && (wcsstr(cmdLine, L"--dark") || wcsstr(cmdLine, L"-d")));
            bool lightImmediate = (cmdLine && (wcsstr(cmdLine, L"--light") || wcsstr(cmdLine, L"-l")));
            bool toggleImmediate = (cmdLine && (wcsstr(cmdLine, L"--toggle") || wcsstr(cmdLine, L"-t")));

            if (openSettings)
            {
                ShowSettingsWindow();
            }
            else if (darkImmediate)
            {
                ThemeManager::SetState(0, g_Settings.osdIconClickEnabled);
                TrimProcessWorkingSetAsync();
            }
            else if (lightImmediate)
            {
                ThemeManager::SetState(1, g_Settings.osdIconClickEnabled);
                TrimProcessWorkingSetAsync();
            }
            else if (toggleImmediate)
            {
                ThemeManager::Toggle();
                TrimProcessWorkingSetAsync();
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
