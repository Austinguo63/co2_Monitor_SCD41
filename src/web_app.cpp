#include "web_app.h"

#include <LittleFS.h>

namespace {
constexpr uint32_t k24hWindowSec = 24UL * 60UL * 60UL;
constexpr uint32_t k7dWindowSec = 7UL * 24UL * 60UL * 60UL;
constexpr uint32_t k30dWindowSec = 30UL * 24UL * 60UL * 60UL;
constexpr uint32_t k6moWindowSec = 183UL * 24UL * 60UL * 60UL;

uint32_t windowForRange(HistoryRange range) {
    switch (range) {
        case HistoryRange::Range24h:
            return k24hWindowSec;
        case HistoryRange::Range7d:
            return k7dWindowSec;
        case HistoryRange::Range30d:
            return k30dWindowSec;
        case HistoryRange::Range6mo:
            return k6moWindowSec;
    }
    return k24hWindowSec;
}
}  // namespace

WebApp::WebApp(ConfigStore& configStore, HistoryStore& historyStore,
               SensorManager& sensorManager, AlarmManager& alarmManager,
               WifiManager& wifiManager)
    : configStore_(configStore),
      historyStore_(historyStore),
      sensorManager_(sensorManager),
      alarmManager_(alarmManager),
      wifiManager_(wifiManager),
      server_(80) {}

bool WebApp::begin() {
    registerRoutes();
    server_.begin();
    return true;
}

void WebApp::loop() { server_.handleClient(); }

void WebApp::registerRoutes() {
    server_.on("/", HTTP_GET, [this]() { serveIndex(); });
    server_.on("/index.html", HTTP_GET, [this]() { serveIndex(); });

    server_.on("/generate_204", HTTP_GET, [this]() { handleCaptivePortal(); });
    server_.on("/hotspot-detect.html", HTTP_GET,
               [this]() { handleCaptivePortal(); });
    server_.on("/connecttest.txt", HTTP_GET, [this]() { handleCaptivePortal(); });
    server_.on("/fwlink", HTTP_GET, [this]() { handleCaptivePortal(); });
    server_.on("/ncsi.txt", HTTP_GET, [this]() { handleCaptivePortal(); });

    server_.on("/api/status", HTTP_GET, [this]() { handleStatus(); });
    server_.on("/api/wifi/status", HTTP_GET, [this]() { handleWifiStatus(); });
    server_.on("/api/wifi/scan", HTTP_GET, [this]() { handleWifiScan(); });
    server_.on("/api/wifi/config", HTTP_POST,
               [this]() { handleWifiConfig(); });
    server_.on("/api/settings", HTTP_PUT, [this]() { handleSettings(); });
    server_.on("/api/settings", HTTP_POST, [this]() { handleSettings(); });
    server_.on("/api/history", HTTP_GET, [this]() { handleHistory(); });
    server_.on("/api/raw", HTTP_GET, [this]() { handleRaw(); });
    server_.on("/api/calibration/fresh-air", HTTP_POST,
               [this]() { handleFreshAirCalibration(); });
    server_.on("/api/calibration/factory-reset", HTTP_POST,
               [this]() { handleFactoryReset(); });
    server_.on("/export/history.csv", HTTP_GET,
               [this]() { handleExportHistory(); });
    server_.on("/export/raw24h.csv", HTTP_GET, [this]() { handleExportRaw(); });

    server_.onNotFound([this]() { handleNotFound(); });
}

void WebApp::serveIndex() {
    File file = LittleFS.open("/index.html", FILE_READ);
    if (!file) {
        server_.send(500, "text/plain; charset=utf-8",
                     "index.html not found. Run: pio run -t uploadfs");
        return;
    }
    server_.streamFile(file, "text/html; charset=utf-8");
    file.close();
}

void WebApp::handleNotFound() {
    if (wifiManager_.isPortalActive()) {
        handleCaptivePortal();
        return;
    }
    server_.send(404, "application/json; charset=utf-8",
                 "{\"ok\":false,\"message\":\"Not found\"}");
}

void WebApp::handleCaptivePortal() {
    server_.sendHeader("Location", String("http://") + wifiManager_.apIp() + "/");
    server_.send(302, "text/plain", "");
}

void WebApp::handleStatus() {
    const AppConfig& config = configStore_.config();
    const SensorReading& reading = sensorManager_.latestReading();
    const HistoryStats stats24h = historyStore_.compute24HourStats(currentEpoch());
    const uint32_t uptimeSec = millis() / 1000UL;
    const uint32_t lastReadAgeSec =
        reading.valid ? (uptimeSec - sensorManager_.lastSuccessfulReadUptimeSec()) : 0;

    String json;
    json.reserve(1400);
    json += "{";
    json += "\"ok\":true";
    json += ",\"reading\":{";
    json += "\"valid\":";
    json += reading.valid ? "true" : "false";
    json += ",\"epoch\":";
    json += String(reading.epoch);
    json += ",\"co2\":";
    json += String(reading.co2);
    json += ",\"tempC\":";
    json += String(reading.temperatureC, 2);
    json += ",\"rh\":";
    json += String(reading.humidity, 2);
    json += ",\"avg60m\":";
    json += String(sensorManager_.rollingAverage60Min());
    json += "}";

    json += ",\"stats24h\":{";
    json += "\"valid\":";
    json += stats24h.valid ? "true" : "false";
    json += ",\"avg\":";
    json += String(stats24h.avgPpm);
    json += ",\"min\":";
    json += String(stats24h.minPpm);
    json += ",\"max\":";
    json += String(stats24h.maxPpm);
    json += "}";

    json += ",\"sensor\":{";
    json += "\"available\":";
    json += sensorManager_.isAvailable() ? "true" : "false";
    json += ",\"healthy\":";
    json += sensorManager_.isHealthy() ? "true" : "false";
    json += ",\"mode\":\"";
    json += sensorManager_.modeLabel();
    json += "\"";
    json += ",\"lastReadAgeSec\":";
    json += String(lastReadAgeSec);
    json += ",\"lastError\":\"";
    json += jsonEscape(sensorManager_.lastError());
    json += "\"}";

    json += ",\"alarm\":{";
    json += "\"active\":";
    json += alarmManager_.isActive() ? "true" : "false";
    json += ",\"pending\":";
    json += alarmManager_.isPending() ? "true" : "false";
    json += ",\"thresholdPpm\":";
    json += String(config.alarmThresholdPpm);
    json += ",\"delaySec\":";
    json += String(config.alarmDelaySec);
    json += "}";

    json += ",\"wifi\":{";
    json += "\"connected\":";
    json += wifiManager_.isConnected() ? "true" : "false";
    json += ",\"portalActive\":";
    json += wifiManager_.isPortalActive() ? "true" : "false";
    json += ",\"rescuePortal\":";
    json += wifiManager_.isRescuePortal() ? "true" : "false";
    json += ",\"ssid\":\"";
    json += jsonEscape(wifiManager_.stationSsid());
    json += "\"";
    json += ",\"ip\":\"";
    json += jsonEscape(wifiManager_.localIp());
    json += "\"";
    json += ",\"rssi\":";
    json += String(wifiManager_.rssi());
    json += ",\"apSsid\":\"";
    json += jsonEscape(wifiManager_.apSsid());
    json += "\"";
    json += ",\"apIp\":\"";
    json += jsonEscape(wifiManager_.apIp());
    json += "\"}";

    json += ",\"config\":{";
    json += "\"refreshIntervalSec\":";
    json += String(config.refreshIntervalSec);
    json += ",\"alarmThresholdPpm\":";
    json += String(config.alarmThresholdPpm);
    json += ",\"alarmDelaySec\":";
    json += String(config.alarmDelaySec);
    json += ",\"displayMode\":\"";
    json += displayModeToString(config.displayMode);
    json += "\"}";

    json += ",\"system\":{";
    json += "\"uptimeSec\":";
    json += String(uptimeSec);
    json += ",\"timeSynced\":";
    json += wifiManager_.isTimeSynced() ? "true" : "false";
    json += ",\"lastCalibrationEpoch\":";
    json += String(config.lastCalibrationEpoch);
    json += "}";
    json += "}";

    server_.send(200, "application/json; charset=utf-8", json);
}

void WebApp::handleWifiStatus() {
    String json = "{";
    json += "\"ok\":true";
    json += ",\"connected\":";
    json += wifiManager_.isConnected() ? "true" : "false";
    json += ",\"portalActive\":";
    json += wifiManager_.isPortalActive() ? "true" : "false";
    json += ",\"ssid\":\"";
    json += jsonEscape(wifiManager_.stationSsid());
    json += "\"";
    json += ",\"ip\":\"";
    json += jsonEscape(wifiManager_.localIp());
    json += "\"";
    json += ",\"apSsid\":\"";
    json += jsonEscape(wifiManager_.apSsid());
    json += "\"";
    json += ",\"apIp\":\"";
    json += jsonEscape(wifiManager_.apIp());
    json += "\"";
    json += "}";
    server_.send(200, "application/json; charset=utf-8", json);
}

void WebApp::handleWifiScan() {
    server_.send(200, "application/json; charset=utf-8",
                 wifiManager_.scanNetworksJson());
}

void WebApp::handleWifiConfig() {
    const String ssid = server_.arg("ssid");
    const String password = server_.arg("password");
    if (ssid.isEmpty()) {
        sendJsonMessage(400, false, "SSID 不能为空");
        return;
    }
    if (!configStore_.updateWifi(ssid, password)) {
        sendJsonMessage(500, false, "保存 Wi-Fi 配置失败");
        return;
    }
    wifiManager_.reconnect(configStore_.config());
    sendJsonMessage(200, true, "Wi-Fi 配置已保存，设备正在重连");
}

void WebApp::handleSettings() {
    const AppConfig& current = configStore_.config();
    const uint16_t refreshIntervalSec =
        server_.hasArg("refreshIntervalSec")
            ? static_cast<uint16_t>(server_.arg("refreshIntervalSec").toInt())
            : current.refreshIntervalSec;
    const uint16_t alarmThresholdPpm =
        server_.hasArg("alarmThresholdPpm")
            ? static_cast<uint16_t>(server_.arg("alarmThresholdPpm").toInt())
            : current.alarmThresholdPpm;
    const uint16_t alarmDelaySec =
        server_.hasArg("alarmDelaySec")
            ? static_cast<uint16_t>(server_.arg("alarmDelaySec").toInt())
            : current.alarmDelaySec;
    const DisplayMode displayMode =
        server_.hasArg("displayMode")
            ? parseDisplayMode(server_.arg("displayMode"))
            : current.displayMode;

    if (!configStore_.updateRuntimeSettings(refreshIntervalSec, alarmThresholdPpm,
                                            alarmDelaySec, displayMode)) {
        sendJsonMessage(500, false, "保存设置失败");
        return;
    }

    sensorManager_.setMeasurementMode(
        measurementModeForRefresh(configStore_.config().refreshIntervalSec));
    alarmManager_.configure(configStore_.config().alarmThresholdPpm,
                            configStore_.config().alarmDelaySec);
    sendJsonMessage(200, true, "设置已保存");
}

void WebApp::handleHistory() {
    HistoryRange range = HistoryRange::Range24h;
    if (!parseHistoryRange(server_.arg("range"), range)) {
        sendJsonMessage(400, false, "range 参数无效");
        return;
    }

    const uint32_t nowEpoch = currentEpoch();
    const uint32_t cutoff =
        nowEpoch > windowForRange(range) ? nowEpoch - windowForRange(range) : 0;

    beginChunkedJson();
    server_.sendContent("{\"range\":\"");
    server_.sendContent(historyRangeToString(range));
    server_.sendContent("\",\"bucketSeconds\":");
    server_.sendContent(String(historyStore_.bucketSecondsForRange(range)));
    server_.sendContent(",\"points\":[");
    StreamContext context;
    context.server = &server_;
    context.first = true;
    historyStore_.forEachBucket(range, &WebApp::streamBucketJson, &context, cutoff);
    server_.sendContent("]}");
    server_.sendContent("");
}

void WebApp::handleRaw() {
    beginChunkedJson();
    server_.sendContent("{\"range\":\"24h\",\"points\":[");
    StreamContext context;
    context.server = &server_;
    context.first = true;
    const uint32_t nowEpoch = currentEpoch();
    const uint32_t cutoff = nowEpoch > k24hWindowSec ? nowEpoch - k24hWindowSec : 0;
    historyStore_.forEachRaw(&WebApp::streamRawJson, &context, cutoff);
    server_.sendContent("]}");
    server_.sendContent("");
}

void WebApp::handleExportHistory() {
    HistoryRange range = HistoryRange::Range24h;
    if (!parseHistoryRange(server_.arg("range"), range)) {
        sendJsonMessage(400, false, "range 参数无效");
        return;
    }

    const uint32_t nowEpoch = currentEpoch();
    const uint32_t cutoff =
        nowEpoch > windowForRange(range) ? nowEpoch - windowForRange(range) : 0;

    beginChunkedCsv(String("co2-history-") + historyRangeToString(range) + ".csv");
    server_.sendContent("ts,co2_avg_ppm,co2_min_ppm,co2_max_ppm,count\n");
    StreamContext context;
    context.server = &server_;
    context.first = true;
    historyStore_.forEachBucket(range, &WebApp::streamBucketCsv, &context, cutoff);
    server_.sendContent("");
}

void WebApp::handleExportRaw() {
    beginChunkedCsv("co2-raw24h.csv");
    server_.sendContent("ts,co2_ppm,temp_c,humidity_pct\n");
    StreamContext context;
    context.server = &server_;
    context.first = true;
    const uint32_t nowEpoch = currentEpoch();
    const uint32_t cutoff = nowEpoch > k24hWindowSec ? nowEpoch - k24hWindowSec : 0;
    historyStore_.forEachRaw(&WebApp::streamRawCsv, &context, cutoff);
    server_.sendContent("");
}

void WebApp::handleFreshAirCalibration() {
    String message;
    if (!sensorManager_.performFreshAirCalibration(420, message)) {
        sendJsonMessage(500, false, message);
        return;
    }

    const uint32_t nowEpoch = currentEpoch();
    if (nowEpoch != 0) {
        configStore_.setLastCalibrationEpoch(nowEpoch);
    }
    sendJsonMessage(200, true, message);
}

void WebApp::handleFactoryReset() {
    String message;
    if (!sensorManager_.performFactoryReset(message)) {
        sendJsonMessage(500, false, message);
        return;
    }
    sendJsonMessage(200, true, message);
}

void WebApp::beginChunkedJson() {
    server_.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server_.send(200, "application/json; charset=utf-8", "");
}

void WebApp::beginChunkedCsv(const String& filename) {
    server_.sendHeader("Content-Disposition",
                       "attachment; filename=\"" + filename + "\"");
    server_.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server_.send(200, "text/csv; charset=utf-8", "");
}

void WebApp::sendJsonMessage(int statusCode, bool ok, const String& message) {
    String json = "{";
    json += "\"ok\":";
    json += ok ? "true" : "false";
    json += ",\"message\":\"";
    json += jsonEscape(message);
    json += "\"}";
    server_.send(statusCode, "application/json; charset=utf-8", json);
}

bool WebApp::ensureMethod(const char* routeName, HTTPMethod allowedMethod) {
    if (server_.method() != allowedMethod) {
        server_.send(405, "application/json; charset=utf-8",
                     "{\"ok\":false,\"message\":\"Method not allowed\"}");
        return false;
    }
    (void)routeName;
    return true;
}

String WebApp::jsonEscape(const String& input) const {
    String out;
    out.reserve(input.length() + 8);
    for (size_t i = 0; i < input.length(); ++i) {
        const char c = input[i];
        switch (c) {
            case '\\':
                out += "\\\\";
                break;
            case '"':
                out += "\\\"";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                break;
            default:
                out += c;
                break;
        }
    }
    return out;
}

String WebApp::ipToString(const IPAddress& ip) const { return ip.toString(); }

bool WebApp::streamBucketJson(const BucketRecord& record, void* userData) {
    auto* context = static_cast<StreamContext*>(userData);
    String line;
    line.reserve(96);
    if (!context->first) {
        line += ",";
    }
    context->first = false;
    line += "{\"ts\":";
    line += String(record.startEpoch);
    line += ",\"co2Avg\":";
    line += String(record.avgPpm);
    line += ",\"co2Min\":";
    line += String(record.minPpm);
    line += ",\"co2Max\":";
    line += String(record.maxPpm);
    line += ",\"count\":";
    line += String(record.count);
    line += "}";
    context->server->sendContent(line);
    return true;
}

bool WebApp::streamRawJson(const RawRecord& record, void* userData) {
    auto* context = static_cast<StreamContext*>(userData);
    String line;
    line.reserve(100);
    if (!context->first) {
        line += ",";
    }
    context->first = false;
    line += "{\"ts\":";
    line += String(record.epoch);
    line += ",\"co2\":";
    line += String(record.co2);
    line += ",\"tempC\":";
    line += String(record.tempCenti / 100.0f, 2);
    line += ",\"rh\":";
    line += String(record.humidityCenti / 100.0f, 2);
    line += "}";
    context->server->sendContent(line);
    return true;
}

bool WebApp::streamBucketCsv(const BucketRecord& record, void* userData) {
    auto* context = static_cast<StreamContext*>(userData);
    String line;
    line.reserve(64);
    line += String(record.startEpoch);
    line += ",";
    line += String(record.avgPpm);
    line += ",";
    line += String(record.minPpm);
    line += ",";
    line += String(record.maxPpm);
    line += ",";
    line += String(record.count);
    line += "\n";
    context->server->sendContent(line);
    return true;
}

bool WebApp::streamRawCsv(const RawRecord& record, void* userData) {
    auto* context = static_cast<StreamContext*>(userData);
    String line;
    line.reserve(64);
    line += String(record.epoch);
    line += ",";
    line += String(record.co2);
    line += ",";
    line += String(record.tempCenti / 100.0f, 2);
    line += ",";
    line += String(record.humidityCenti / 100.0f, 2);
    line += "\n";
    context->server->sendContent(line);
    return true;
}
