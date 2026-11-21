#include "pch.h"
#include "App.xaml.h"
#include "resource.h"

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shcore.lib")
#pragma comment(lib, "comctl32.lib")

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow)
{
    UNREFERENCED_PARAMETER(hInstance);
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(pCmdLine);
    UNREFERENCED_PARAMETER(nCmdShow);

    // Ensure Per-Monitor V2 DPI awareness is active
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // Check for help switch
    if (pCmdLine && (wcsstr(pCmdLine, L"--help") || wcsstr(pCmdLine, L"-h") || wcsstr(pCmdLine, L"/?")))
    {
        MessageBoxW(NULL,
            L"ClickDarkMode Command-Line Options:\n\n"
            L"  --dark, -d\t\tSwitch to Dark Mode\n"
            L"  --light, -l\t\tSwitch to Light Mode\n"
            L"  --toggle, -t\tToggle between Dark and Light Mode\n"
            L"  --settings, -s\tOpen Settings window\n"
            L"  --exit, -e\t\tExit running instance\n"
            L"  --help, -h\t\tShow this help dialog",
            L"ClickDarkMode", MB_OK | MB_ICONINFORMATION);
        return 0;
    }

    // Check for exit switch upfront: if running, signal exit; if not running, exit cleanly without launching
    if (pCmdLine && (wcsstr(pCmdLine, L"--exit") || wcsstr(pCmdLine, L"-e")))
    {
        HANDLE hEvt = OpenEventW(EVENT_MODIFY_STATE, FALSE, EVENT_NAME_EXIT);
        if (hEvt)
        {
            SetEvent(hEvt);
            CloseHandle(hEvt);
        }

        HWND hExisting = FindWindowW(L"ClickDarkMode_MessageWndClass", L"ClickDarkMode_HiddenMsgWindow");
        if (!hExisting)
        {
            hExisting = FindWindowW(L"ClickDarkMode_MessageWndClass", NULL);
        }
        if (hExisting)
        {
            PostMessageW(hExisting, WM_COMMAND, MAKEWPARAM(IDM_TRAY_EXIT, 0), 0);
        }
        return 0;
    }

    // Single instance mutex check & message forwarding
    HANDLE hMutex = CreateMutexW(NULL, TRUE, L"ClickDarkMode_SingleInstance_Mutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        const wchar_t* targetEvent = nullptr;
        if (pCmdLine && (wcsstr(pCmdLine, L"--dark") || wcsstr(pCmdLine, L"-d")))
        {
            targetEvent = EVENT_NAME_DARK;
        }
        else if (pCmdLine && (wcsstr(pCmdLine, L"--light") || wcsstr(pCmdLine, L"-l")))
        {
            targetEvent = EVENT_NAME_LIGHT;
        }
        else if (pCmdLine && (wcsstr(pCmdLine, L"--toggle") || wcsstr(pCmdLine, L"-t")))
        {
            targetEvent = EVENT_NAME_TOGGLE;
        }
        else if (pCmdLine && (wcsstr(pCmdLine, L"--settings") || wcsstr(pCmdLine, L"-s")))
        {
            targetEvent = EVENT_NAME_SHOW_SETTINGS;
        }
        else
        {
            targetEvent = EVENT_NAME_SHOW_SETTINGS;
        }

        if (targetEvent)
        {
            HANDLE hEvt = OpenEventW(EVENT_MODIFY_STATE, FALSE, targetEvent);
            if (hEvt)
            {
                SetEvent(hEvt);
                CloseHandle(hEvt);
            }

            HWND hExisting = FindWindowW(L"ClickDarkMode_MessageWndClass", L"ClickDarkMode_HiddenMsgWindow");
            if (!hExisting)
            {
                hExisting = FindWindowW(L"ClickDarkMode_MessageWndClass", NULL);
            }
            if (hExisting)
            {
                if (targetEvent == EVENT_NAME_EXIT)
                {
                    PostMessageW(hExisting, WM_COMMAND, MAKEWPARAM(IDM_TRAY_EXIT, 0), 0);
                }
                else if (targetEvent == EVENT_NAME_DARK)
                {
                    PostMessageW(hExisting, WM_COMMAND, MAKEWPARAM(IDM_TRAY_DARK, 0), 0);
                }
                else if (targetEvent == EVENT_NAME_LIGHT)
                {
                    PostMessageW(hExisting, WM_COMMAND, MAKEWPARAM(IDM_TRAY_LIGHT, 0), 0);
                }
                else if (targetEvent == EVENT_NAME_TOGGLE)
                {
                    PostMessageW(hExisting, WM_COMMAND, MAKEWPARAM(IDM_TRAY_TOGGLE, 0), 0);
                }
                else if (targetEvent == EVENT_NAME_SHOW_SETTINGS)
                {
                    PostMessageW(hExisting, WM_COMMAND, MAKEWPARAM(IDM_TRAY_SETTINGS, 0), 0);
                }
            }
        }
        if (hMutex) CloseHandle(hMutex);
        return 0;
    }

    try
    {
        winrt::init_apartment(winrt::apartment_type::single_threaded);

        winrt::Microsoft::UI::Xaml::Application::Start([](auto&&)
        {
            winrt::make<winrt::ThemeSwitcherApp::implementation::App>();
        });
    }
    catch (...)
    {
    }

    if (hMutex)
    {
        ReleaseMutex(hMutex);
        CloseHandle(hMutex);
    }

    // Clean, instant exit without DLL unload stalls
    ExitProcess(0);
    return 0;
}
