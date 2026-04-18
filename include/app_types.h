#pragma once

#include <Arduino.h>

enum class DisplayMode : uint8_t {
    Auto = 0,
    Live = 1,
    Stats = 2,
};

enum class SensorMeasurementMode : uint8_t {
    Standard5s = 0,
    LowPower30s = 1,
};

enum class HistoryRange : uint8_t {
    Range24h = 0,
    Range7d = 1,
    Range30d = 2,
    Range6mo = 3,
};

struct AppConfig {
    uint16_t refreshIntervalSec = 5;
    uint16_t alarmThresholdPpm = 1200;
    uint16_t alarmDelaySec = 30;
    DisplayMode displayMode = DisplayMode::Auto;
    char wifiSsid[33] = {0};
    char wifiPassword[65] = {0};
    uint32_t lastCalibrationEpoch = 0;

    bool hasWifiCredentials() const { return wifiSsid[0] != '\0'; }
};

struct SensorReading {
    bool valid = false;
    uint32_t epoch = 0;
    uint32_t uptimeSec = 0;
    uint16_t co2 = 0;
    float temperatureC = 0.0f;
    float humidity = 0.0f;
};

struct HistoryStats {
    bool valid = false;
    uint16_t avgPpm = 0;
    uint16_t minPpm = 0;
    uint16_t maxPpm = 0;
};

constexpr const char* kFilesystemPartitionLabel = "storage";

const char* displayModeToString(DisplayMode mode);
DisplayMode parseDisplayMode(const String& value);

const char* historyRangeToString(HistoryRange range);
bool parseHistoryRange(const String& value, HistoryRange& outRange);

bool isRefreshIntervalValid(uint16_t refreshIntervalSec);
uint16_t sanitizeRefreshInterval(uint16_t refreshIntervalSec);
uint16_t sanitizeAlarmThreshold(uint16_t thresholdPpm);
uint16_t sanitizeAlarmDelay(uint16_t delaySec);

SensorMeasurementMode measurementModeForRefresh(uint16_t refreshIntervalSec);
uint32_t currentEpoch();
bool isTimeSynchronized();
