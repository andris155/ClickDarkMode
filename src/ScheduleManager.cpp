#include "pch.h"
#include "ScheduleManager.h"
#include "SolarCalculator.h"
#include "ThemeManager.h"
#include "Settings.h"
#include "LocalizationManager.h"
#include <winrt/Windows.System.Power.h>
#include <powersetting.h>
#include <atomic>

#pragma comment(lib, "Powrprof.lib")

namespace ScheduleManager {

static HWND s_hWnd = NULL;
static const UINT_PTR IDT_SCHEDULE_TICK = 1005;
static int s_lastEvaluatedState = -1;
static bool s_lastOnBattery = false;
static bool s_lastEnergySaver = false;
static bool s_powerInitialized = false;
static bool s_switchedToDarkByPower = false;
static bool s_hasAcDcOverride = false;
static bool s_acDcOverride = false;
static bool s_hasEnergySaverOverride = false;
static bool s_energySaverOverride = false;
static winrt::event_token s_energySaverToken{};
static PVOID s_epmHandle = nullptr;
static std::atomic<int> s_effectivePowerMode{ -1 };

static VOID WINAPI EffectivePowerModeCallback(EFFECTIVE_POWER_MODE mode, VOID*) {
    s_effectivePowerMode.store(static_cast<int>(mode));
    if (s_hWnd) {
        PostMessageW(s_hWnd, WM_APP + 10, static_cast<WPARAM>(mode), 0);
    }
}

static bool IsOnBatteryOnly() {
    if (s_hasAcDcOverride) {
        return s_acDcOverride;
    }
    SYSTEM_POWER_STATUS sps{};
    if (GetSystemPowerStatus(&sps)) {
        return (sps.ACLineStatus == 0) && ((sps.BatteryFlag & 128) == 0);
    }
    return false;
}

static bool IsEnergySaverEnabled() {
    SYSTEM_POWER_STATUS sps{};
    bool hasSps = (GetSystemPowerStatus(&sps) != FALSE);
    bool noBatteryDesktop = hasSps && ((sps.BatteryFlag & 128) != 0);

    int epm = s_effectivePowerMode.load();
    if (epm == EffectivePowerModeEnergySaverHighSavings ||
        (noBatteryDesktop && epm == EffectivePowerModeEnergySaverStandard)) {
        return true;
    }
    if (s_hasEnergySaverOverride) {
        return s_energySaverOverride;
    }
    if (hasSps && sps.SystemStatusFlag != 0) {
        return true;
    }
    try {
        return winrt::Windows::System::Power::PowerManager::EnergySaverStatus() ==
               winrt::Windows::System::Power::EnergySaverStatus::On;
    } catch (...) {}
    return false;
}

void Init(HWND hWnd) {
    s_hWnd = hWnd;
    if (s_hWnd) {
        SetTimer(s_hWnd, IDT_SCHEDULE_TICK, 30000, NULL); // Check every 30 seconds
    }
    PowerRegisterForEffectivePowerModeNotifications(
        EFFECTIVE_POWER_MODE_V2,
        EffectivePowerModeCallback,
        nullptr,
        &s_epmHandle
    );
    try {
        s_energySaverToken = winrt::Windows::System::Power::PowerManager::EnergySaverStatusChanged(
            [](auto&&, auto&&) {
                s_hasEnergySaverOverride = false;
                if (s_hWnd) {
                    PostMessageW(s_hWnd, WM_POWERBROADCAST, PBT_APMPOWERSTATUSCHANGE, 0);
                }
            });
    } catch (...) {}
    Tick();
    CheckPowerRules(false);
}

void Uninit() {
    if (s_epmHandle) {
        PowerUnregisterFromEffectivePowerModeNotifications(s_epmHandle);
        s_epmHandle = nullptr;
    }
    if (s_energySaverToken.value != 0) {
        try {
            winrt::Windows::System::Power::PowerManager::EnergySaverStatusChanged(s_energySaverToken);
        } catch (...) {}
        s_energySaverToken = {};
    }
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

static int GetScheduleOnlyDesiredState() {
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

int GetDesiredThemeState() {
    if (g_Settings.scheduleMode == 0) {
        return -1; // Disabled (Manual only)
    }

    if ((g_Settings.darkOnBattery && IsOnBatteryOnly()) ||
        (g_Settings.darkOnEnergySaver && IsEnergySaverEnabled())) {
        return 0; // Power/Battery rule keeps Dark mode active
    }

    return GetScheduleOnlyDesiredState();
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

void CheckPowerRules(bool triggeredByToggleChange) {
    bool onBattery = IsOnBatteryOnly();
    bool energySaver = IsEnergySaverEnabled();

    bool batteryActive = g_Settings.darkOnBattery && onBattery;
    bool energyActive = g_Settings.darkOnEnergySaver && energySaver;
    bool anyPowerRuleActive = batteryActive || energyActive;

    bool batteryJustActivated = batteryActive && (!s_powerInitialized || !s_lastOnBattery || triggeredByToggleChange);
    bool energySaverJustActivated = energyActive && (!s_powerInitialized || !s_lastEnergySaver || triggeredByToggleChange);

    bool powerReleased = (s_powerInitialized && !onBattery && s_lastOnBattery && g_Settings.darkOnBattery) ||
                         (s_powerInitialized && !energySaver && s_lastEnergySaver && g_Settings.darkOnEnergySaver);

    s_lastOnBattery = onBattery;
    s_lastEnergySaver = energySaver;
    s_powerInitialized = true;

    if (batteryJustActivated || energySaverJustActivated) {
        if (ThemeManager::GetState() != 0) {
            ThemeManager::SetState(0, g_Settings.scheduleShowOsd);
            s_switchedToDarkByPower = true;
        }
        s_lastEvaluatedState = 0;
    } else if (powerReleased && !anyPowerRuleActive) {
        bool wasSwitchedByPower = s_switchedToDarkByPower;
        s_switchedToDarkByPower = false;

        if (!wasSwitchedByPower) {
            return;
        }

        int schedDesired = GetScheduleOnlyDesiredState();
        if (schedDesired == 0) {
            // Schedule is enabled and currently in Dark mode period: stay Dark
            s_lastEvaluatedState = 0;
            return;
        }

        if (ThemeManager::GetState() != 1) {
            ThemeManager::SetState(1, g_Settings.scheduleShowOsd);
        }
        if (schedDesired == 1) {
            s_lastEvaluatedState = 1;
        }
    } else if (triggeredByToggleChange && !anyPowerRuleActive && s_switchedToDarkByPower) {
        s_switchedToDarkByPower = false;
        if (g_Settings.scheduleMode != 0) {
            Refresh();
        }
    }
}

void OnPowerResume() {
    s_hasAcDcOverride = false;
    s_hasEnergySaverOverride = false;
    CheckPowerRules(false);
    Refresh();
}

void ClearPowerDarkOverride() {
    s_switchedToDarkByPower = false;
}

void OnPowerSavingBroadcast(bool enabled) {
    s_hasEnergySaverOverride = true;
    s_energySaverOverride = enabled;
    CheckPowerRules(false);
}

void OnAcDcBroadcast(bool onBattery) {
    s_hasAcDcOverride = true;
    s_acDcOverride = onBattery;
    CheckPowerRules(false);
}

} // namespace ScheduleManager
