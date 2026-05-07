#pragma once

#include <Arduino.h>

#include "alarm_manager.h"
#include "config_store.h"
#include "history_store.h"
#include "sensor_manager.h"

struct ApiRequest {
    String method;
    String path;
    String query;
    String body;
};

class ResponseSink {
  public:
    virtual ~ResponseSink() = default;

    virtual void begin(int statusCode, const char* contentType,
                       const String& filename = String()) = 0;
    virtual void write(const char* data, size_t len) = 0;
    virtual void end() = 0;

    void write(const String& data) { write(data.c_str(), data.length()); }
};

class DeviceApi {
  public:
    DeviceApi(ConfigStore& configStore, HistoryStore& historyStore,
              SensorManager& sensorManager, AlarmManager& alarmManager);

    void handleRequest(const ApiRequest& request, ResponseSink& sink);
    bool setTime(uint32_t epoch, String& message);

  private:
    struct StreamContext {
        ResponseSink* sink = nullptr;
        bool first = true;
    };

    void handleStatus(ResponseSink& sink);
    void handleHistory(const ApiRequest& request, ResponseSink& sink);
    void handleRaw(ResponseSink& sink);
    void handleExportHistory(const ApiRequest& request, ResponseSink& sink);
    void handleExportRaw(ResponseSink& sink);
    void handleSettings(const ApiRequest& request, ResponseSink& sink);
    void handleFreshAirCalibration(ResponseSink& sink);
    void handleFactoryReset(ResponseSink& sink);

    void sendJsonMessage(ResponseSink& sink, int statusCode, bool ok,
                         const String& message);
    bool methodAllowed(const ApiRequest& request, ResponseSink& sink,
                       const char* allowedMethod) const;
    String jsonEscape(const String& input) const;
    bool getParam(const ApiRequest& request, const char* key, String& value) const;
    bool getParam(const String& encoded, const char* key, String& value) const;
    String urlDecode(const String& value) const;

    static bool streamBucketJson(const BucketRecord& record, void* userData);
    static bool streamRawJson(const RawRecord& record, void* userData);
    static bool streamBucketCsv(const BucketRecord& record, void* userData);
    static bool streamRawCsv(const RawRecord& record, void* userData);

    ConfigStore& configStore_;
    HistoryStore& historyStore_;
    SensorManager& sensorManager_;
    AlarmManager& alarmManager_;
};
