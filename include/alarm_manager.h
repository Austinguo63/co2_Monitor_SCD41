#pragma once

#include "app_types.h"

class AlarmManager {
  public:
    void configure(uint16_t thresholdPpm, uint16_t delaySec);
    void update(const SensorReading& reading);

    bool isActive() const { return active_; }
    bool isPending() const { return pendingStartUptimeSec_ != 0; }
    uint16_t thresholdPpm() const { return thresholdPpm_; }
    uint16_t delaySec() const { return delaySec_; }
    uint32_t pendingStartUptimeSec() const { return pendingStartUptimeSec_; }

  private:
    uint16_t thresholdPpm_ = 1200;
    uint16_t delaySec_ = 30;
    bool active_ = false;
    uint32_t pendingStartUptimeSec_ = 0;
};

