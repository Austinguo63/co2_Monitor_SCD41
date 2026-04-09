#pragma once

#include <WebServer.h>

#include "alarm_manager.h"
#include "config_store.h"
#include "history_store.h"
#include "sensor_manager.h"
#include "wifi_manager.h"

class WebApp {
  public:
    WebApp(ConfigStore& configStore, HistoryStore& historyStore,
           SensorManager& sensorManager, AlarmManager& alarmManager,
           WifiManager& wifiManager);

    bool begin();
    void loop();

  private:
    struct StreamContext {
        WebServer* server;
        bool first = true;
    };

    void registerRoutes();
    void serveIndex();
    void handleNotFound();
    void handleCaptivePortal();
    void handleStatus();
    void handleWifiStatus();
    void handleWifiScan();
    void handleWifiConfig();
    void handleSettings();
    void handleHistory();
    void handleRaw();
    void handleExportHistory();
    void handleExportRaw();
    void handleFreshAirCalibration();
    void handleFactoryReset();

    void beginChunkedJson();
    void beginChunkedCsv(const String& filename);
    void sendJsonMessage(int statusCode, bool ok, const String& message);
    bool ensureMethod(const char* routeName, HTTPMethod allowedMethod);
    String jsonEscape(const String& input) const;
    String ipToString(const IPAddress& ip) const;

    static bool streamBucketJson(const BucketRecord& record, void* userData);
    static bool streamRawJson(const RawRecord& record, void* userData);
    static bool streamBucketCsv(const BucketRecord& record, void* userData);
    static bool streamRawCsv(const RawRecord& record, void* userData);

    ConfigStore& configStore_;
    HistoryStore& historyStore_;
    SensorManager& sensorManager_;
    AlarmManager& alarmManager_;
    WifiManager& wifiManager_;
    WebServer server_;
};
