#include "config_store.h"

#include <Arduino.h>

namespace {
constexpr const char* kNamespace = "co2mon";
constexpr const char* kRefreshKey = "refresh";
constexpr const char* kAlarmPpmKey = "alarmppm";
constexpr const char* kAlarmDelayKey = "alarmdelay";
constexpr const char* kDisplayKey = "display";
constexpr const char* kWifiSsidKey = "ssid";
constexpr const char* kWifiPassKey = "pass";
constexpr const char* kLastCalKey = "lastcal";
}

bool ConfigStore::begin() {
    if (!preferences_.begin(kNamespace, false)) {
        return false;
    }

    config_.refreshIntervalSec =
        sanitizeRefreshInterval(preferences_.getUShort(kRefreshKey, 30));
    config_.alarmThresholdPpm =
        sanitizeAlarmThreshold(preferences_.getUShort(kAlarmPpmKey, 1200));
    config_.alarmDelaySec =
        sanitizeAlarmDelay(preferences_.getUShort(kAlarmDelayKey, 30));
    config_.displayMode =
        parseDisplayMode(preferences_.getString(kDisplayKey, "auto"));
    config_.lastCalibrationEpoch = preferences_.getULong(kLastCalKey, 0);
    loadStrings();
    return true;
}

bool ConfigStore::saveAll() {
    return updateRuntimeSettings(config_.refreshIntervalSec,
                                 config_.alarmThresholdPpm,
                                 config_.alarmDelaySec, config_.displayMode) &&
           writeString(kWifiSsidKey, config_.wifiSsid) &&
           writeString(kWifiPassKey, config_.wifiPassword) &&
           preferences_.putULong(kLastCalKey, config_.lastCalibrationEpoch);
}

bool ConfigStore::updateRuntimeSettings(uint16_t refreshIntervalSec,
                                        uint16_t alarmThresholdPpm,
                                        uint16_t alarmDelaySec,
                                        DisplayMode displayMode) {
    config_.refreshIntervalSec = sanitizeRefreshInterval(refreshIntervalSec);
    config_.alarmThresholdPpm = sanitizeAlarmThreshold(alarmThresholdPpm);
    config_.alarmDelaySec = sanitizeAlarmDelay(alarmDelaySec);
    config_.displayMode = displayMode;

    bool ok = true;
    ok = preferences_.putUShort(kRefreshKey, config_.refreshIntervalSec) && ok;
    ok = preferences_.putUShort(kAlarmPpmKey, config_.alarmThresholdPpm) && ok;
    ok = preferences_.putUShort(kAlarmDelayKey, config_.alarmDelaySec) && ok;
    ok = writeString(kDisplayKey, displayModeToString(config_.displayMode)) &&
         ok;
    return ok;
}

bool ConfigStore::updateWifi(const String& ssid, const String& password) {
    String trimmedSsid = ssid;
    trimmedSsid.trim();
    String trimmedPassword = password;
    trimmedPassword.trim();

    if (trimmedSsid.isEmpty()) {
        return false;
    }

    trimmedSsid.toCharArray(config_.wifiSsid, sizeof(config_.wifiSsid));
    trimmedPassword.toCharArray(config_.wifiPassword,
                                sizeof(config_.wifiPassword));

    return writeString(kWifiSsidKey, config_.wifiSsid) &&
           writeString(kWifiPassKey, config_.wifiPassword);
}

bool ConfigStore::clearWifi() {
    config_.wifiSsid[0] = '\0';
    config_.wifiPassword[0] = '\0';
    return writeString(kWifiSsidKey, "") && writeString(kWifiPassKey, "");
}

bool ConfigStore::setLastCalibrationEpoch(uint32_t epoch) {
    config_.lastCalibrationEpoch = epoch;
    return preferences_.putULong(kLastCalKey, config_.lastCalibrationEpoch);
}

bool ConfigStore::writeString(const char* key, const char* value) {
    return preferences_.putString(key, value) > 0 || strlen(value) == 0;
}

void ConfigStore::loadStrings() {
    String ssid = preferences_.getString(kWifiSsidKey, "");
    String password = preferences_.getString(kWifiPassKey, "");
    ssid.toCharArray(config_.wifiSsid, sizeof(config_.wifiSsid));
    password.toCharArray(config_.wifiPassword, sizeof(config_.wifiPassword));
}
