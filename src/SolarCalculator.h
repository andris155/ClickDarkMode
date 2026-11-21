#pragma once

#include <cmath>
#include <windows.h>
#include <string>

namespace SolarCalculator {

struct SolarTimes {
    bool hasSunrise = false;
    bool hasSunset = false;
    int sunriseHour = 0;
    int sunriseMinute = 0;
    int sunsetHour = 0;
    int sunsetMinute = 0;
};

inline SolarTimes Calculate(double lat, double lon, const SYSTEMTIME& stLocal) {
    SolarTimes result;
    const double pi = 3.14159265358979323846;
    const double deg2rad = pi / 180.0;
    const double rad2deg = 180.0 / pi;
    const double zenith = 90.83333333333333; // Official NOAA solar zenith

    static const int daysBeforeMonth[12] = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };
    bool isLeap = (stLocal.wYear % 4 == 0 && (stLocal.wYear % 100 != 0 || stLocal.wYear % 400 == 0));
    int monthIdx = stLocal.wMonth - 1;
    if (monthIdx < 0) monthIdx = 0;
    if (monthIdx > 11) monthIdx = 11;
    int n = daysBeforeMonth[monthIdx] + stLocal.wDay;
    if (isLeap && stLocal.wMonth > 2) n++;

    TIME_ZONE_INFORMATION tzi = { 0 };
    DWORD tzRes = GetTimeZoneInformation(&tzi);
    double tzOffsetHours = 0.0;
    if (tzRes != TIME_ZONE_ID_INVALID) {
        tzOffsetHours = -(double)tzi.Bias / 60.0;
        if (tzRes == TIME_ZONE_ID_DAYLIGHT) {
            tzOffsetHours -= (double)tzi.DaylightBias / 60.0;
        } else if (tzRes == TIME_ZONE_ID_STANDARD) {
            tzOffsetHours -= (double)tzi.StandardBias / 60.0;
        }
    }

    auto calcSunTime = [&](bool isSunrise, double& outLocalHour) -> bool {
        double lngHour = lon / 15.0;
        double t = isSunrise ? ((double)n + ((6.0 - lngHour) / 24.0)) : ((double)n + ((18.0 - lngHour) / 24.0));
        double M = (0.9856 * t) - 3.289;
        double L = M + (1.916 * std::sin(M * deg2rad)) + (0.020 * std::sin(2.0 * M * deg2rad)) + 282.634;
        L = std::fmod(L, 360.0);
        if (L < 0) L += 360.0;

        double RA = std::atan(0.91764 * std::tan(L * deg2rad)) * rad2deg;
        RA = std::fmod(RA, 360.0);
        if (RA < 0) RA += 360.0;

        double lQuadrant = std::floor(L / 90.0) * 90.0;
        double raQuadrant = std::floor(RA / 90.0) * 90.0;
        RA = (RA + (lQuadrant - raQuadrant)) / 15.0;

        double sinDec = 0.39782 * std::sin(L * deg2rad);
        double cosDec = std::cos(std::asin(sinDec));

        double cosH = (std::cos(zenith * deg2rad) - (sinDec * std::sin(lat * deg2rad))) / (cosDec * std::cos(lat * deg2rad));
        if (cosH > 1.0 || cosH < -1.0) {
            return false;
        }

        double H = isSunrise ? (360.0 - std::acos(cosH) * rad2deg) : (std::acos(cosH) * rad2deg);
        H /= 15.0;

        double T = H + RA - (0.06571 * t) - 6.622;
        double UT = std::fmod(T - lngHour, 24.0);
        if (UT < 0) UT += 24.0;

        double localTime = std::fmod(UT + tzOffsetHours, 24.0);
        if (localTime < 0) localTime += 24.0;
        outLocalHour = localTime;
        return true;
    };

    double riseHour = 0, setHour = 0;
    if (calcSunTime(true, riseHour)) {
        result.hasSunrise = true;
        result.sunriseHour = (int)riseHour;
        result.sunriseMinute = (int)std::round((riseHour - (double)result.sunriseHour) * 60.0);
        if (result.sunriseMinute >= 60) {
            result.sunriseMinute -= 60;
            result.sunriseHour = (result.sunriseHour + 1) % 24;
        }
    }

    if (calcSunTime(false, setHour)) {
        result.hasSunset = true;
        result.sunsetHour = (int)setHour;
        result.sunsetMinute = (int)std::round((setHour - (double)result.sunsetHour) * 60.0);
        if (result.sunsetMinute >= 60) {
            result.sunsetMinute -= 60;
            result.sunsetHour = (result.sunsetHour + 1) % 24;
        }
    }

    return result;
}

} // namespace SolarCalculator
