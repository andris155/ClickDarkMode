#include "pch.h"
#include "ScheduleManager.h"
#include "SolarCalculator.h"
#include "ThemeManager.h"
#include "Settings.h"
#include "LocalizationManager.h"

namespace ScheduleManager {

static HWND s_hWnd = NULL;
static const UINT_PTR IDT_SCHEDULE_TICK = 1005;
static int s_lastEvaluatedState = -1;

void Init(HWND hWnd) {
    s_hWnd = hWnd;
    if (s_hWnd) {
        SetTimer(s_hWnd, IDT_SCHEDULE_TICK, 30000, NULL); // Check every 30 seconds
    }
    Tick();
}

void Uninit() {
    if (s_hWnd) {
        KillTimer(s_hWnd, IDT_SCHEDULE_TICK);
        s_hWnd = NULL;
    }
}

TodaySunInfo GetTodaySunInfo(double lat, double lon) {
    TodaySunInfo info;
    SYSTEMTIME st;
    GetLocalTime(&st);
    auto times = SolarCalculator::Calculate(lat, lon, st);
    if (times.hasSunrise && times.hasSunset) {
        info.valid = true;
        info.riseHour = times.sunriseHour;
        info.riseMin = times.sunriseMinute;
        info.setHour = times.sunsetHour;
        info.setMin = times.sunsetMinute;

        int effRise = (times.sunriseHour * 60 + times.sunriseMinute + g_Settings.scheduleSunriseOffset) % 1440;
        if (effRise < 0) effRise += 1440;
        int effSet = (times.sunsetHour * 60 + times.sunsetMinute + g_Settings.scheduleSunsetOffset) % 1440;
        if (effSet < 0) effSet += 1440;

        info.effectiveRiseHour = effRise / 60;
        info.effectiveRiseMin = effRise % 60;
        info.effectiveSetHour = effSet / 60;
        info.effectiveSetMin = effSet % 60;

        wchar_t buf[128];
        swprintf_s(buf, L"%02d:%02d | %02d:%02d", info.riseHour, info.riseMin, info.setHour, info.setMin);
        info.text = buf;
    }
    return info;
}

int GetDesiredThemeState() {
    if (g_Settings.scheduleMode == 0) {
        return -1; // Disabled (Manual only)
    }

    SYSTEMTIME st;
    GetLocalTime(&st);
    int nowM = st.wHour * 60 + st.wMinute;

    if (g_Settings.scheduleMode == 1) {
        // Custom Time Schedule
        int lightM = g_Settings.scheduleLightHour * 60 + g_Settings.scheduleLightMinute;
        int darkM = g_Settings.scheduleDarkHour * 60 + g_Settings.scheduleDarkMinute;

        if (lightM < darkM) {
            // Light during day, Dark during night
            return (nowM >= lightM && nowM < darkM) ? 1 : 0;
        } else if (darkM < lightM) {
            // Inverted schedule
            return (nowM >= darkM && nowM < lightM) ? 0 : 1;
        } else {
            return 1;
        }
    } else if (g_Settings.scheduleMode == 2) {
        // Sunrise / Sunset Sync
        auto times = SolarCalculator::Calculate(g_Settings.scheduleLatitude, g_Settings.scheduleLongitude, st);
        if (times.hasSunrise && times.hasSunset) {
            int riseM = (times.sunriseHour * 60 + times.sunriseMinute + g_Settings.scheduleSunriseOffset) % 1440;
            if (riseM < 0) riseM += 1440;
            int setM = (times.sunsetHour * 60 + times.sunsetMinute + g_Settings.scheduleSunsetOffset) % 1440;
            if (setM < 0) setM += 1440;

            if (riseM < setM) {
                return (nowM >= riseM && nowM < setM) ? 1 : 0;
            } else {
                return (nowM >= setM && nowM < riseM) ? 0 : 1;
            }
        }
    }

    return -1;
}

void Tick() {
    int desired = GetDesiredThemeState();
    if (desired == -1) {
        s_lastEvaluatedState = -1;
        return;
    }

    if (s_lastEvaluatedState != desired) {
        int current = ThemeManager::GetState();
        if (current != desired) {
            ThemeManager::SetState(desired, g_Settings.scheduleShowOsd);
        }
        s_lastEvaluatedState = desired;
    }
}

void Refresh() {
    s_lastEvaluatedState = -1;
    Tick();
}

} // namespace ScheduleManager
