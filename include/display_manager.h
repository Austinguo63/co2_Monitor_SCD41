#pragma once

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>

#include "app_types.h"

struct DisplaySnapshot {
    bool sensorValid = false;
    uint16_t co2 = 0;
    float temperatureC = 0.0f;
    float humidity = 0.0f;
    uint16_t avg60m = 0;
    HistoryStats stats24h;
    bool wifiConnected = false;
    bool portalActive = false;
    bool alarmActive = false;
    bool timeSynced = false;
};

class DisplayManager {
  public:
    DisplayManager() = default;
    bool begin(TwoWire& wire, uint8_t address);
    void loop(const DisplaySnapshot& snapshot, DisplayMode mode);
    bool isAvailable() const { return available_; }

  private:
    void renderLive(const DisplaySnapshot& snapshot);
    void renderStats(const DisplaySnapshot& snapshot);

    Adafruit_SSD1306* display_ = nullptr;
    bool available_ = false;
    bool showStatsPage_ = false;
    uint32_t lastRedrawMs_ = 0;
    uint32_t lastPageSwitchMs_ = 0;
};
