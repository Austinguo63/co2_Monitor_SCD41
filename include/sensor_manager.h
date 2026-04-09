#pragma once

#include <SensirionI2cScd4x.h>
#include <Wire.h>

#include "app_types.h"

class SensorManager {
  public:
    bool begin(TwoWire& wire);
    void loop();
    void setMeasurementMode(SensorMeasurementMode mode);

    bool hasValidReading() const { return latest_.valid; }
    const SensorReading& latestReading() const { return latest_; }
    uint16_t rollingAverage60Min() const;

    bool isAvailable() const { return available_; }
    bool isHealthy() const;
    const char* lastError() const { return lastError_; }
    const char* modeLabel() const;
    uint32_t lastSuccessfulReadEpoch() const { return lastSuccessfulReadEpoch_; }
    uint32_t lastSuccessfulReadUptimeSec() const {
        return lastSuccessfulReadUptimeSec_;
    }

    bool performFreshAirCalibration(uint16_t referencePpm, String& message);
    bool performFactoryReset(String& message);

  private:
    struct RollingEntry {
        uint32_t uptimeSec;
        uint16_t co2;
    };

    static constexpr size_t kRollingCapacity = 800;

    bool initializeSensor();
    bool ensureMeasurementMode();
    bool startDesiredMeasurementMode();
    bool ensureIdleMode();
    void recordError(int16_t errorCode, const char* context);
    void clearError();
    void pushRollingWindow(uint32_t uptimeSec, uint16_t co2);
    void pruneRollingWindow(uint32_t uptimeSec);

    TwoWire* wire_ = nullptr;
    SensirionI2cScd4x sensor_;
    SensorReading latest_;
    SensorMeasurementMode desiredMode_ = SensorMeasurementMode::LowPower30s;
    SensorMeasurementMode activeMode_ = SensorMeasurementMode::LowPower30s;
    bool available_ = false;
    bool initialized_ = false;
    bool ascDisabledThisBoot_ = false;
    uint32_t lastInitAttemptMs_ = 0;
    uint32_t lastPollMs_ = 0;
    uint32_t lastSuccessfulReadEpoch_ = 0;
    uint32_t lastSuccessfulReadUptimeSec_ = 0;
    char lastError_[96] = {0};
    RollingEntry rolling_[kRollingCapacity] = {};
    size_t rollingHead_ = 0;
    size_t rollingCount_ = 0;
    uint32_t rollingSum_ = 0;
};

