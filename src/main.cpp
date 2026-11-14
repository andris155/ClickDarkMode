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

    // Single instance mutex check & message forwarding
    HANDLE hMutex = CreateMutexW(NULL, TRUE, L"ClickDarkMode_SingleInstance_Mutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        const wchar_t* targetEvent = nullptr;
        if (pCmdLine && (wcsstr(pCmdLine, L"--exit") || wcsstr(pCmdLine, L"-e")))
        {
            targetEvent = EVENT_NAME_EXIT;
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

    return 0;
}
