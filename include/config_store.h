#pragma once

#include <Preferences.h>

#include "app_types.h"

class ConfigStore {
  public:
    bool begin();

    const AppConfig& config() const { return config_; }
    bool saveAll();
    bool updateRuntimeSettings(uint16_t refreshIntervalSec,
                               uint16_t alarmThresholdPpm,
                               uint16_t alarmDelaySec, DisplayMode displayMode);
    bool updateWifi(const String& ssid, const String& password);
    bool clearWifi();
    bool setLastCalibrationEpoch(uint32_t epoch);

  private:
    bool writeString(const char* key, const char* value);
    void loadStrings();

    Preferences preferences_;
    AppConfig config_;
};

