#pragma once

#include <windows.h>
#include <shellscalingapi.h>

#pragma comment(lib, "shcore.lib")

class DpiHelper {
public:
    static UINT GetDpiForHwnd(HWND hwnd) {
        if (hwnd) {
            UINT dpi = ::GetDpiForWindow(hwnd);
            if (dpi > 0) return dpi;
        }
        UINT dpi = ::GetDpiForSystem();
        return (dpi > 0) ? dpi : 96;
    }

    static UINT GetDpiForMonitor(HMONITOR hMon) {
        if (hMon) {
            UINT dpiX = 0, dpiY = 0;
            if (SUCCEEDED(::GetDpiForMonitor(hMon, MDT_EFFECTIVE_DPI, &dpiX, &dpiY)) && dpiY > 0) {
                return dpiY;
            }
        }
        return 0;
    }

    static int Scale(int value, UINT dpi) {
        return MulDiv(value, dpi, 96);
    }

    static float ScaleF(float value, UINT dpi) {
        return value * (float)dpi / 96.0f;
    }
};
