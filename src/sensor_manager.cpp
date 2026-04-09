#include "sensor_manager.h"

#include <SensirionCore.h>

namespace {
#ifdef NO_ERROR
#undef NO_ERROR
#endif
constexpr int16_t kNoError = 0;
}

bool SensorManager::begin(TwoWire& wire) {
    wire_ = &wire;
    return initializeSensor();
}

void SensorManager::loop() {
    if (wire_ == nullptr) {
        return;
    }

    if (!initialized_) {
        if (millis() - lastInitAttemptMs_ > 10000 || lastInitAttemptMs_ == 0) {
            initializeSensor();
        }
        return;
    }

    ensureMeasurementMode();

    if (millis() - lastPollMs_ < 500) {
        return;
    }
    lastPollMs_ = millis();

    bool dataReady = false;
    int16_t error = sensor_.getDataReadyStatus(dataReady);
    if (error != kNoError) {
        recordError(error, "getDataReadyStatus");
        initialized_ = false;
        available_ = false;
        return;
    }
    if (!dataReady) {
        return;
    }

    uint16_t co2 = 0;
    float temperature = 0.0f;
    float humidity = 0.0f;
    error = sensor_.readMeasurement(co2, temperature, humidity);
    if (error != kNoError) {
        recordError(error, "readMeasurement");
        initialized_ = false;
        available_ = false;
        return;
    }

    clearError();
    latest_.valid = true;
    latest_.uptimeSec = millis() / 1000UL;
    latest_.epoch = currentEpoch();
    latest_.co2 = co2;
    latest_.temperatureC = temperature;
    latest_.humidity = humidity;
    lastSuccessfulReadEpoch_ = latest_.epoch;
    lastSuccessfulReadUptimeSec_ = latest_.uptimeSec;
    pushRollingWindow(latest_.uptimeSec, co2);
}

void SensorManager::setMeasurementMode(SensorMeasurementMode mode) {
    desiredMode_ = mode;
}

uint16_t SensorManager::rollingAverage60Min() const {
    if (rollingCount_ == 0) {
        return 0;
    }
    return static_cast<uint16_t>(rollingSum_ / rollingCount_);
}

bool SensorManager::isHealthy() const {
    if (!initialized_ || !latest_.valid) {
        return false;
    }
    const uint32_t nowUptime = millis() / 1000UL;
    const uint32_t budget =
        (activeMode_ == SensorMeasurementMode::Standard5s) ? 20UL : 90UL;
    return (nowUptime - lastSuccessfulReadUptimeSec_) <= budget;
}

const char* SensorManager::modeLabel() const {
    return activeMode_ == SensorMeasurementMode::Standard5s ? "5s" : "30s";
}

bool SensorManager::performFreshAirCalibration(uint16_t referencePpm,
                                               String& message) {
    if (!initialized_) {
        message = "传感器未初始化";
        return false;
    }
    if (!ensureIdleMode()) {
        message = "无法停止测量进入校准模式";
        return false;
    }

    uint16_t frcCorrection = 0;
    int16_t error = sensor_.performForcedRecalibration(referencePpm, frcCorrection);
    if (error != kNoError) {
        recordError(error, "performForcedRecalibration");
        startDesiredMeasurementMode();
        message = String("室外校准失败: ") + lastError_;
        return false;
    }

    startDesiredMeasurementMode();
    message = "室外校准完成";
    return true;
}

bool SensorManager::performFactoryReset(String& message) {
    if (!initialized_) {
        message = "传感器未初始化";
        return false;
    }
    if (!ensureIdleMode()) {
        message = "无法停止测量进入重置模式";
        return false;
    }

    int16_t error = sensor_.performFactoryReset();
    if (error != kNoError) {
        recordError(error, "performFactoryReset");
        startDesiredMeasurementMode();
        message = String("工厂重置失败: ") + lastError_;
        return false;
    }

    delay(1200);
    initialized_ = false;
    available_ = false;
    ascDisabledThisBoot_ = false;
    if (!initializeSensor()) {
        message = String("工厂重置后重连失败: ") + lastError_;
        return false;
    }

    message = "工厂重置完成";
    return true;
}

bool SensorManager::initializeSensor() {
    lastInitAttemptMs_ = millis();
    if (wire_ == nullptr) {
        return false;
    }

    sensor_.begin(*wire_, SCD41_I2C_ADDR_62);
    delay(30);

    int16_t error = sensor_.wakeUp();
    if (error != kNoError) {
        recordError(error, "wakeUp");
    }

    error = sensor_.stopPeriodicMeasurement();
    if (error != kNoError) {
        recordError(error, "stopPeriodicMeasurement");
    }

    error = sensor_.reinit();
    if (error != kNoError) {
        recordError(error, "reinit");
        initialized_ = false;
        available_ = false;
        return false;
    }

    if (!ascDisabledThisBoot_) {
        error = sensor_.setAutomaticSelfCalibrationEnabled(0);
        if (error != kNoError) {
            recordError(error, "setAutomaticSelfCalibrationEnabled");
        } else {
            ascDisabledThisBoot_ = true;
        }
    }

    initialized_ = startDesiredMeasurementMode();
    available_ = initialized_;
    if (initialized_) {
        clearError();
    }
    return initialized_;
}

bool SensorManager::ensureMeasurementMode() {
    if (!initialized_) {
        return false;
    }
    if (desiredMode_ == activeMode_) {
        return true;
    }
    return startDesiredMeasurementMode();
}

bool SensorManager::startDesiredMeasurementMode() {
    if (!ensureIdleMode()) {
        return false;
    }

    int16_t error = kNoError;
    if (desiredMode_ == SensorMeasurementMode::Standard5s) {
        error = sensor_.startPeriodicMeasurement();
    } else {
        error = sensor_.startLowPowerPeriodicMeasurement();
    }

    if (error != kNoError) {
        recordError(error,
                    desiredMode_ == SensorMeasurementMode::Standard5s
                        ? "startPeriodicMeasurement"
                        : "startLowPowerPeriodicMeasurement");
        return false;
    }

    activeMode_ = desiredMode_;
    initialized_ = true;
    available_ = true;
    clearError();
    return true;
}

bool SensorManager::ensureIdleMode() {
    int16_t error = sensor_.stopPeriodicMeasurement();
    if (error != kNoError) {
        recordError(error, "stopPeriodicMeasurement");
    }
    delay(500);
    return true;
}

void SensorManager::recordError(int16_t errorCode, const char* context) {
    char errorMessage[64] = {0};
    errorToString(errorCode, errorMessage, sizeof(errorMessage));
    snprintf(lastError_, sizeof(lastError_), "%s: %s", context, errorMessage);
}

void SensorManager::clearError() { lastError_[0] = '\0'; }

void SensorManager::pushRollingWindow(uint32_t uptimeSec, uint16_t co2) {
    pruneRollingWindow(uptimeSec);
    if (rollingCount_ == kRollingCapacity) {
        rollingSum_ -= rolling_[rollingHead_].co2;
        rollingHead_ = (rollingHead_ + 1) % kRollingCapacity;
        --rollingCount_;
    }

    const size_t tail = (rollingHead_ + rollingCount_) % kRollingCapacity;
    rolling_[tail].uptimeSec = uptimeSec;
    rolling_[tail].co2 = co2;
    ++rollingCount_;
    rollingSum_ += co2;
}

void SensorManager::pruneRollingWindow(uint32_t uptimeSec) {
    while (rollingCount_ > 0) {
        const RollingEntry& oldest = rolling_[rollingHead_];
        if ((uptimeSec - oldest.uptimeSec) <= 3600UL) {
            break;
        }
        rollingSum_ -= oldest.co2;
        rollingHead_ = (rollingHead_ + 1) % kRollingCapacity;
        --rollingCount_;
    }
}
