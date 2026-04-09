#include "app_types.h"

#include <time.h>

namespace {
constexpr uint16_t kAllowedRefreshIntervals[] = {5, 10, 30, 60, 120, 300};
}

const char* displayModeToString(DisplayMode mode) {
    switch (mode) {
        case DisplayMode::Auto:
            return "auto";
        case DisplayMode::Live:
            return "live";
        case DisplayMode::Stats:
            return "stats";
    }
    return "auto";
}

DisplayMode parseDisplayMode(const String& value) {
    if (value == "live") {
        return DisplayMode::Live;
    }
    if (value == "stats") {
        return DisplayMode::Stats;
    }
    return DisplayMode::Auto;
}

const char* historyRangeToString(HistoryRange range) {
    switch (range) {
        case HistoryRange::Range24h:
            return "24h";
        case HistoryRange::Range7d:
            return "7d";
        case HistoryRange::Range30d:
            return "30d";
        case HistoryRange::Range6mo:
            return "6mo";
    }
    return "24h";
}

bool parseHistoryRange(const String& value, HistoryRange& outRange) {
    if (value == "24h") {
        outRange = HistoryRange::Range24h;
        return true;
    }
    if (value == "7d") {
        outRange = HistoryRange::Range7d;
        return true;
    }
    if (value == "30d") {
        outRange = HistoryRange::Range30d;
        return true;
    }
    if (value == "6mo") {
        outRange = HistoryRange::Range6mo;
        return true;
    }
    return false;
}

bool isRefreshIntervalValid(uint16_t refreshIntervalSec) {
    for (uint16_t allowed : kAllowedRefreshIntervals) {
        if (allowed == refreshIntervalSec) {
            return true;
        }
    }
    return false;
}

uint16_t sanitizeRefreshInterval(uint16_t refreshIntervalSec) {
    return isRefreshIntervalValid(refreshIntervalSec) ? refreshIntervalSec : 30;
}

uint16_t sanitizeAlarmThreshold(uint16_t thresholdPpm) {
    if (thresholdPpm < 400) {
        return 400;
    }
    if (thresholdPpm > 5000) {
        return 5000;
    }
    return thresholdPpm;
}

uint16_t sanitizeAlarmDelay(uint16_t delaySec) {
    if (delaySec > 600) {
        return 600;
    }
    return delaySec;
}

SensorMeasurementMode measurementModeForRefresh(uint16_t refreshIntervalSec) {
    return refreshIntervalSec <= 10 ? SensorMeasurementMode::Standard5s
                                    : SensorMeasurementMode::LowPower30s;
}

uint32_t currentEpoch() {
    time_t now = time(nullptr);
    return now > 1700000000 ? static_cast<uint32_t>(now) : 0;
}

bool isTimeSynchronized() { return currentEpoch() != 0; }

