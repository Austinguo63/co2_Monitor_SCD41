#include "serial_rpc_server.h"

#include <ctype.h>
#include <string.h>

namespace {
constexpr const char* kFramePrefix = "@CO2MON ";
constexpr size_t kChunkPayloadBytes = 768;
constexpr char kBase64Table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

String jsonEscape(const String& input) {
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

String base64Encode(const uint8_t* data, size_t len) {
    String out;
    out.reserve(((len + 2) / 3) * 4);

    for (size_t i = 0; i < len; i += 3) {
        const uint32_t octetA = data[i];
        const uint32_t octetB = (i + 1 < len) ? data[i + 1] : 0;
        const uint32_t octetC = (i + 2 < len) ? data[i + 2] : 0;
        const uint32_t triple = (octetA << 16) | (octetB << 8) | octetC;

        out += kBase64Table[(triple >> 18) & 0x3F];
        out += kBase64Table[(triple >> 12) & 0x3F];
        out += (i + 1 < len) ? kBase64Table[(triple >> 6) & 0x3F] : '=';
        out += (i + 2 < len) ? kBase64Table[triple & 0x3F] : '=';
    }
    return out;
}

class SerialResponseSink : public ResponseSink {
  public:
    using ResponseSink::write;

    SerialResponseSink(HardwareSerial& serial, uint32_t requestId)
        : serial_(serial), requestId_(requestId) {}

    void begin(int statusCode, const char* contentType,
               const String& filename = String()) override {
        if (begun_) {
            return;
        }
        begun_ = true;

        String line = "{\"id\":";
        line += String(requestId_);
        line += ",\"event\":\"begin\",\"status\":";
        line += String(statusCode);
        line += ",\"contentType\":\"";
        line += jsonEscape(contentType);
        line += "\"";
        if (!filename.isEmpty()) {
            line += ",\"filename\":\"";
            line += jsonEscape(filename);
            line += "\"";
        }
        line += "}";
        sendLine(line);
    }

    void write(const char* data, size_t len) override {
        size_t offset = 0;
        while (offset < len) {
            const size_t available = kChunkPayloadBytes - buffered_;
            const size_t toCopy = min(available, len - offset);
            memcpy(buffer_ + buffered_, data + offset, toCopy);
            buffered_ += toCopy;
            offset += toCopy;
            if (buffered_ == kChunkPayloadBytes) {
                flushChunk();
            }
        }
    }

    void end() override {
        if (!begun_) {
            begin(200, "application/json; charset=utf-8");
        }
        flushChunk();
        String line = "{\"id\":";
        line += String(requestId_);
        line += ",\"event\":\"end\"}";
        sendLine(line);
    }

  private:
    void flushChunk() {
        if (buffered_ == 0) {
            return;
        }

        String line = "{\"id\":";
        line += String(requestId_);
        line += ",\"event\":\"chunk\",\"data\":\"";
        line += base64Encode(buffer_, buffered_);
        line += "\"}";
        sendLine(line);
        buffered_ = 0;
    }

    void sendLine(const String& line) {
        serial_.print(kFramePrefix);
        serial_.println(line);
    }

    HardwareSerial& serial_;
    uint32_t requestId_;
    uint8_t buffer_[kChunkPayloadBytes] = {};
    size_t buffered_ = 0;
    bool begun_ = false;
};
}  // namespace

void SerialRpcServer::begin(HardwareSerial& serial) {
    serial_ = &serial;
    lineBuffer_.reserve(kMaxLineLength);
}

void SerialRpcServer::loop() {
    if (serial_ == nullptr) {
        return;
    }

    while (serial_->available() > 0) {
        const char c = static_cast<char>(serial_->read());
        if (c == '\r') {
            continue;
        }
        if (c == '\n') {
            if (!discardingLine_ && !lineBuffer_.isEmpty()) {
                handleLine(lineBuffer_);
            }
            lineBuffer_ = "";
            discardingLine_ = false;
            continue;
        }

        if (discardingLine_) {
            continue;
        }
        if (lineBuffer_.length() >= kMaxLineLength) {
            lineBuffer_ = "";
            discardingLine_ = true;
            continue;
        }
        lineBuffer_ += c;
    }
}

void SerialRpcServer::handleLine(const String& line) {
    if (!line.startsWith(kFramePrefix)) {
        return;
    }

    const String payload = line.substring(strlen(kFramePrefix));
    ApiRequest request;
    uint32_t requestId = 0;
    if (!parseRequest(payload, request, requestId)) {
        if (requestId != 0) {
            sendProtocolError(requestId, "Bad request");
        }
        return;
    }

    if (request.path == "set-time") {
        handleSetTime(requestId, request);
        return;
    }

    SerialResponseSink sink(*serial_, requestId);
    deviceApi_.handleRequest(request, sink);
}

bool SerialRpcServer::parseRequest(const String& payload, ApiRequest& request,
                                   uint32_t& requestId) {
    if (!extractUIntField(payload, "id", requestId)) {
        return false;
    }

    if (!extractStringField(payload, "method", request.method) ||
        !extractStringField(payload, "path", request.path)) {
        return false;
    }

    if (!extractStringField(payload, "query", request.query)) {
        request.query = "";
    }
    if (!extractStringField(payload, "body", request.body)) {
        request.body = "";
    }
    return true;
}

bool SerialRpcServer::extractStringField(const String& json, const char* key,
                                         String& value) const {
    int start = 0;
    if (!findValueStart(json, key, start)) {
        return false;
    }
    return parseJsonString(json, start, value);
}

bool SerialRpcServer::extractUIntField(const String& json, const char* key,
                                       uint32_t& value) const {
    int start = 0;
    if (!findValueStart(json, key, start)) {
        return false;
    }
    if (start >= json.length()) {
        return false;
    }

    int end = start;
    while (end < json.length() && isdigit(static_cast<unsigned char>(json[end]))) {
        ++end;
    }
    if (end == start) {
        return false;
    }

    value = static_cast<uint32_t>(json.substring(start, end).toInt());
    return true;
}

bool SerialRpcServer::findValueStart(const String& json, const char* key,
                                     int& start) const {
    const String pattern = String("\"") + key + "\"";
    const int keyPos = json.indexOf(pattern);
    if (keyPos < 0) {
        return false;
    }

    int cursor = keyPos + pattern.length();
    while (cursor < json.length() && isspace(static_cast<unsigned char>(json[cursor]))) {
        ++cursor;
    }
    if (cursor >= json.length() || json[cursor] != ':') {
        return false;
    }
    ++cursor;
    while (cursor < json.length() && isspace(static_cast<unsigned char>(json[cursor]))) {
        ++cursor;
    }
    start = cursor;
    return cursor < json.length();
}

bool SerialRpcServer::parseJsonString(const String& json, int start,
                                      String& value) const {
    if (start >= json.length() || json[start] != '"') {
        return false;
    }

    value = "";
    bool escaping = false;
    for (int i = start + 1; i < json.length(); ++i) {
        const char c = json[i];
        if (escaping) {
            switch (c) {
                case '"':
                case '\\':
                case '/':
                    value += c;
                    break;
                case 'n':
                    value += '\n';
                    break;
                case 'r':
                    value += '\r';
                    break;
                case 't':
                    value += '\t';
                    break;
                default:
                    return false;
            }
            escaping = false;
            continue;
        }

        if (c == '\\') {
            escaping = true;
            continue;
        }
        if (c == '"') {
            return true;
        }
        value += c;
    }
    return false;
}

void SerialRpcServer::sendProtocolError(uint32_t requestId, const String& message) {
    if (serial_ == nullptr) {
        return;
    }
    SerialResponseSink sink(*serial_, requestId);
    sink.begin(400, "application/json; charset=utf-8");
    String body = "{\"ok\":false,\"message\":\"";
    body += jsonEscape(message);
    body += "\"}";
    sink.write(body);
    sink.end();
}

void SerialRpcServer::handleSetTime(uint32_t requestId, const ApiRequest& request) {
    if (serial_ == nullptr) {
        return;
    }

    String epochArg;
    const String queryPattern = "epoch=";
    if (request.query.startsWith(queryPattern)) {
        epochArg = request.query.substring(queryPattern.length());
    } else if (request.body.startsWith(queryPattern)) {
        epochArg = request.body.substring(queryPattern.length());
    }

    SerialResponseSink sink(*serial_, requestId);
    if (epochArg.isEmpty()) {
        sink.begin(400, "application/json; charset=utf-8");
        sink.write("{\"ok\":false,\"message\":\"epoch 参数无效\"}");
        sink.end();
        return;
    }

    String message;
    if (!deviceApi_.setTime(static_cast<uint32_t>(epochArg.toInt()), message)) {
        sink.begin(500, "application/json; charset=utf-8");
        String body = "{\"ok\":false,\"message\":\"";
        body += jsonEscape(message);
        body += "\"}";
        sink.write(body);
        sink.end();
        return;
    }

    sink.begin(200, "application/json; charset=utf-8");
    String body = "{\"ok\":true,\"message\":\"";
    body += jsonEscape(message);
    body += "\"}";
    sink.write(body);
    sink.end();
}
