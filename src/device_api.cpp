#include "device_api.h"

#include <stdlib.h>
#include <sys/time.h>

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

DeviceApi::DeviceApi(ConfigStore& configStore, HistoryStore& historyStore,
                     SensorManager& sensorManager, AlarmManager& alarmManager)
    : configStore_(configStore),
      historyStore_(historyStore),
      sensorManager_(sensorManager),
      alarmManager_(alarmManager) {}

void DeviceApi::handleRequest(const ApiRequest& request, ResponseSink& sink) {
    if (request.path == "/api/status") {
        if (!methodAllowed(request, sink, "GET")) {
            return;
        }
        handleStatus(sink);
        return;
    }
    if (request.path == "/api/history") {
        if (!methodAllowed(request, sink, "GET")) {
            return;
        }
        handleHistory(request, sink);
        return;
    }
    if (request.path == "/api/raw") {
        if (!methodAllowed(request, sink, "GET")) {
            return;
        }
        handleRaw(sink);
        return;
    }
    if (request.path == "/api/settings") {
        if (request.method != "PUT" && request.method != "POST") {
            sink.begin(405, "application/json; charset=utf-8");
            sink.write("{\"ok\":false,\"message\":\"Method not allowed\"}");
            sink.end();
            return;
        }
        handleSettings(request, sink);
        return;
    }
    if (request.path == "/api/calibration/fresh-air") {
        if (!methodAllowed(request, sink, "POST")) {
            return;
        }
        handleFreshAirCalibration(sink);
        return;
    }
    if (request.path == "/api/calibration/factory-reset") {
        if (!methodAllowed(request, sink, "POST")) {
            return;
        }
        handleFactoryReset(sink);
        return;
    }
    if (request.path == "/export/history.csv") {
        if (!methodAllowed(request, sink, "GET")) {
            return;
        }
        handleExportHistory(request, sink);
        return;
    }
    if (request.path == "/export/raw24h.csv") {
        if (!methodAllowed(request, sink, "GET")) {
            return;
        }
        handleExportRaw(sink);
        return;
    }

    sink.begin(404, "application/json; charset=utf-8");
    sink.write("{\"ok\":false,\"message\":\"Not found\"}");
    sink.end();
}

bool DeviceApi::setTime(uint32_t epoch, String& message) {
    if (epoch < 1700000000UL) {
        message = "epoch 参数无效";
        return false;
    }

    timeval value = {};
    value.tv_sec = static_cast<time_t>(epoch);
    value.tv_usec = 0;
    if (settimeofday(&value, nullptr) != 0) {
        message = "设置系统时间失败";
        return false;
    }

    message = "系统时间已同步";
    return true;
}

void DeviceApi::handleStatus(ResponseSink& sink) {
    const AppConfig& config = configStore_.config();
    const SensorReading& reading = sensorManager_.latestReading();
    const HistoryStats stats24h = historyStore_.compute24HourStats(currentEpoch());
    const uint32_t uptimeSec = millis() / 1000UL;
    const uint32_t lastReadAgeSec =
        reading.valid ? (uptimeSec - sensorManager_.lastSuccessfulReadUptimeSec()) : 0;

    String json;
    json.reserve(1200);
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
    json += "\"connected\":false";
    json += ",\"portalActive\":false";
    json += ",\"rescuePortal\":false";
    json += ",\"ssid\":\"\"";
    json += ",\"ip\":\"\"";
    json += ",\"rssi\":0";
    json += ",\"apSsid\":\"\"";
    json += ",\"apIp\":\"\"";
    json += "}";

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
    json += isTimeSynchronized() ? "true" : "false";
    json += ",\"lastCalibrationEpoch\":";
    json += String(config.lastCalibrationEpoch);
    json += "}";
    json += "}";

    sink.begin(200, "application/json; charset=utf-8");
    sink.write(json);
    sink.end();
}

void DeviceApi::handleHistory(const ApiRequest& request, ResponseSink& sink) {
    String rangeArg;
    if (!getParam(request, "range", rangeArg)) {
        sendJsonMessage(sink, 400, false, "range 参数无效");
        return;
    }

    HistoryRange range = HistoryRange::Range24h;
    if (!parseHistoryRange(rangeArg, range)) {
        sendJsonMessage(sink, 400, false, "range 参数无效");
        return;
    }

    const uint32_t nowEpoch = currentEpoch();
    const uint32_t cutoff =
        nowEpoch > windowForRange(range) ? nowEpoch - windowForRange(range) : 0;

    sink.begin(200, "application/json; charset=utf-8");
    sink.write("{\"range\":\"");
    sink.write(historyRangeToString(range));
    sink.write("\",\"bucketSeconds\":");
    sink.write(String(historyStore_.bucketSecondsForRange(range)));
    sink.write(",\"points\":[");
    StreamContext context;
    context.sink = &sink;
    context.first = true;
    historyStore_.forEachBucket(range, &DeviceApi::streamBucketJson, &context, cutoff);
    sink.write("]}");
    sink.end();
}

void DeviceApi::handleRaw(ResponseSink& sink) {
    sink.begin(200, "application/json; charset=utf-8");
    sink.write("{\"range\":\"24h\",\"points\":[");
    StreamContext context;
    context.sink = &sink;
    context.first = true;
    const uint32_t nowEpoch = currentEpoch();
    const uint32_t cutoff = nowEpoch > k24hWindowSec ? nowEpoch - k24hWindowSec : 0;
    historyStore_.forEachRaw(&DeviceApi::streamRawJson, &context, cutoff);
    sink.write("]}");
    sink.end();
}

void DeviceApi::handleExportHistory(const ApiRequest& request, ResponseSink& sink) {
    String rangeArg;
    if (!getParam(request, "range", rangeArg)) {
        sendJsonMessage(sink, 400, false, "range 参数无效");
        return;
    }

    HistoryRange range = HistoryRange::Range24h;
    if (!parseHistoryRange(rangeArg, range)) {
        sendJsonMessage(sink, 400, false, "range 参数无效");
        return;
    }

    const uint32_t nowEpoch = currentEpoch();
    const uint32_t cutoff =
        nowEpoch > windowForRange(range) ? nowEpoch - windowForRange(range) : 0;

    sink.begin(200, "text/csv; charset=utf-8",
               String("co2-history-") + historyRangeToString(range) + ".csv");
    sink.write("ts,co2_avg_ppm,co2_min_ppm,co2_max_ppm,count\n");
    StreamContext context;
    context.sink = &sink;
    context.first = true;
    historyStore_.forEachBucket(range, &DeviceApi::streamBucketCsv, &context, cutoff);
    sink.end();
}

void DeviceApi::handleExportRaw(ResponseSink& sink) {
    sink.begin(200, "text/csv; charset=utf-8", "co2-raw24h.csv");
    sink.write("ts,co2_ppm,temp_c,humidity_pct\n");
    StreamContext context;
    context.sink = &sink;
    context.first = true;
    const uint32_t nowEpoch = currentEpoch();
    const uint32_t cutoff = nowEpoch > k24hWindowSec ? nowEpoch - k24hWindowSec : 0;
    historyStore_.forEachRaw(&DeviceApi::streamRawCsv, &context, cutoff);
    sink.end();
}

void DeviceApi::handleSettings(const ApiRequest& request, ResponseSink& sink) {
    const AppConfig& current = configStore_.config();
    String refreshArg;
    String alarmThresholdArg;
    String alarmDelayArg;
    String displayModeArg;

    const uint16_t refreshIntervalSec =
        getParam(request, "refreshIntervalSec", refreshArg)
            ? static_cast<uint16_t>(refreshArg.toInt())
            : current.refreshIntervalSec;
    const uint16_t alarmThresholdPpm =
        getParam(request, "alarmThresholdPpm", alarmThresholdArg)
            ? static_cast<uint16_t>(alarmThresholdArg.toInt())
            : current.alarmThresholdPpm;
    const uint16_t alarmDelaySec =
        getParam(request, "alarmDelaySec", alarmDelayArg)
            ? static_cast<uint16_t>(alarmDelayArg.toInt())
            : current.alarmDelaySec;
    const DisplayMode displayMode =
        getParam(request, "displayMode", displayModeArg)
            ? parseDisplayMode(displayModeArg)
            : current.displayMode;

    if (!configStore_.updateRuntimeSettings(refreshIntervalSec, alarmThresholdPpm,
                                            alarmDelaySec, displayMode)) {
        sendJsonMessage(sink, 500, false, "保存设置失败");
        return;
    }

    sensorManager_.setMeasurementMode(
        measurementModeForRefresh(configStore_.config().refreshIntervalSec));
    alarmManager_.configure(configStore_.config().alarmThresholdPpm,
                            configStore_.config().alarmDelaySec);
    sendJsonMessage(sink, 200, true, "设置已保存");
}

void DeviceApi::handleFreshAirCalibration(ResponseSink& sink) {
    String message;
    if (!sensorManager_.performFreshAirCalibration(420, message)) {
        sendJsonMessage(sink, 500, false, message);
        return;
    }

    const uint32_t nowEpoch = currentEpoch();
    if (nowEpoch != 0) {
        configStore_.setLastCalibrationEpoch(nowEpoch);
    }
    sendJsonMessage(sink, 200, true, message);
}

void DeviceApi::handleFactoryReset(ResponseSink& sink) {
    String message;
    if (!sensorManager_.performFactoryReset(message)) {
        sendJsonMessage(sink, 500, false, message);
        return;
    }
    sendJsonMessage(sink, 200, true, message);
}

void DeviceApi::sendJsonMessage(ResponseSink& sink, int statusCode, bool ok,
                                const String& message) {
    String json = "{";
    json += "\"ok\":";
    json += ok ? "true" : "false";
    json += ",\"message\":\"";
    json += jsonEscape(message);
    json += "\"}";
    sink.begin(statusCode, "application/json; charset=utf-8");
    sink.write(json);
    sink.end();
}

bool DeviceApi::methodAllowed(const ApiRequest& request, ResponseSink& sink,
                              const char* allowedMethod) const {
    if (request.method == allowedMethod) {
        return true;
    }

    sink.begin(405, "application/json; charset=utf-8");
    sink.write("{\"ok\":false,\"message\":\"Method not allowed\"}");
    sink.end();
    return false;
}

String DeviceApi::jsonEscape(const String& input) const {
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

bool DeviceApi::getParam(const ApiRequest& request, const char* key,
                         String& value) const {
    return getParam(request.query, key, value) || getParam(request.body, key, value);
}

bool DeviceApi::getParam(const String& encoded, const char* key, String& value) const {
    if (encoded.isEmpty()) {
        return false;
    }

    const String pattern = String(key) + "=";
    int start = 0;
    while (start < encoded.length()) {
        int end = encoded.indexOf('&', start);
        if (end < 0) {
            end = encoded.length();
        }

        const String part = encoded.substring(start, end);
        if (part.startsWith(pattern)) {
            value = urlDecode(part.substring(pattern.length()));
            return true;
        }
        start = end + 1;
    }
    return false;
}

String DeviceApi::urlDecode(const String& value) const {
    String decoded;
    decoded.reserve(value.length());

    for (size_t i = 0; i < value.length(); ++i) {
        const char c = value[i];
        if (c == '+') {
            decoded += ' ';
            continue;
        }
        if (c == '%' && i + 2 < value.length()) {
            char hex[3] = {value[i + 1], value[i + 2], '\0'};
            decoded += static_cast<char>(strtol(hex, nullptr, 16));
            i += 2;
            continue;
        }
        decoded += c;
    }
    return decoded;
}

bool DeviceApi::streamBucketJson(const BucketRecord& record, void* userData) {
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
    context->sink->write(line);
    return true;
}

bool DeviceApi::streamRawJson(const RawRecord& record, void* userData) {
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
    context->sink->write(line);
    return true;
}

bool DeviceApi::streamBucketCsv(const BucketRecord& record, void* userData) {
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
    context->sink->write(line);
    return true;
}

bool DeviceApi::streamRawCsv(const RawRecord& record, void* userData) {
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
    context->sink->write(line);
    return true;
}
