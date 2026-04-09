#include "alarm_manager.h"

void AlarmManager::configure(uint16_t thresholdPpm, uint16_t delaySec) {
    thresholdPpm_ = sanitizeAlarmThreshold(thresholdPpm);
    delaySec_ = sanitizeAlarmDelay(delaySec);
}

void AlarmManager::update(const SensorReading& reading) {
    if (!reading.valid) {
        return;
    }

    const uint16_t clearThreshold =
        thresholdPpm_ > 50 ? thresholdPpm_ - 50 : thresholdPpm_;

    if (active_) {
        if (reading.co2 <= clearThreshold) {
            active_ = false;
            pendingStartUptimeSec_ = 0;
        }
        return;
    }

    if (reading.co2 >= thresholdPpm_) {
        if (pendingStartUptimeSec_ == 0) {
            pendingStartUptimeSec_ = reading.uptimeSec;
            return;
        }
        if ((reading.uptimeSec - pendingStartUptimeSec_) >= delaySec_) {
            active_ = true;
        }
        return;
    }

    pendingStartUptimeSec_ = 0;
}

