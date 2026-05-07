#pragma once

#include <Arduino.h>

#include "device_api.h"

class SerialRpcServer {
  public:
    explicit SerialRpcServer(DeviceApi& deviceApi) : deviceApi_(deviceApi) {}

    void begin(HardwareSerial& serial = Serial);
    void loop();

  private:
    static constexpr size_t kMaxLineLength = 2048;

    void handleLine(const String& line);
    bool parseRequest(const String& payload, ApiRequest& request, uint32_t& requestId);
    bool extractStringField(const String& json, const char* key, String& value) const;
    bool extractUIntField(const String& json, const char* key, uint32_t& value) const;
    bool findValueStart(const String& json, const char* key, int& start) const;
    bool parseJsonString(const String& json, int start, String& value) const;
    void sendProtocolError(uint32_t requestId, const String& message);
    void handleSetTime(uint32_t requestId, const ApiRequest& request);

    DeviceApi& deviceApi_;
    HardwareSerial* serial_ = nullptr;
    String lineBuffer_;
    bool discardingLine_ = false;
};
