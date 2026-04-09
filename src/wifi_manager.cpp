#include "wifi_manager.h"

#include <Arduino.h>

namespace {
constexpr uint32_t kConnectTimeoutMs = 30000UL;
constexpr const char* kHostname = "co2-monitor";

String escapeJson(const String& input) {
    String out;
    out.reserve(input.length() + 8);
    for (size_t i = 0; i < input.length(); ++i) {
        const char c = input[i];
        if (c == '\\') {
            out += "\\\\";
        } else if (c == '"') {
            out += "\\\"";
        } else {
            out += c;
        }
    }
    return out;
}
}

bool WifiManager::begin(const AppConfig& config) {
    WiFi.persistent(false);
    WiFi.setAutoReconnect(true);
    WiFi.setSleep(false);
    WiFi.setHostname(kHostname);

    if (config.hasWifiCredentials()) {
        connectStation(config);
    } else {
        startPortal(config, false);
    }
    return true;
}

void WifiManager::loop(const AppConfig& config) {
    processDns();

    if (WiFi.status() == WL_CONNECTED) {
        stationConnecting_ = false;
        ensureMdns();
        ensureTimeSync();
        if (portalActive_ && config.hasWifiCredentials()) {
            stopPortal();
        }
        return;
    }

    if (mdnsStarted_) {
        MDNS.end();
        mdnsStarted_ = false;
    }
    ntpConfigured_ = false;

    if (!config.hasWifiCredentials()) {
        if (!portalActive_) {
            startPortal(config, false);
        }
        return;
    }

    if (!stationConnecting_) {
        connectStation(config);
    }

    if (!portalActive_ && (millis() - connectStartedMs_) > kConnectTimeoutMs) {
        startPortal(config, true);
    }
}

void WifiManager::reconnect(const AppConfig& config) {
    if (!config.hasWifiCredentials()) {
        startPortal(config, false);
        return;
    }

    startPortal(config, true);
    WiFi.disconnect(false, true);
    delay(100);
    connectStation(config);
}

bool WifiManager::isConnected() const { return WiFi.status() == WL_CONNECTED; }

bool WifiManager::isTimeSynced() const { return ::isTimeSynchronized(); }

String WifiManager::stationSsid() const {
    return isConnected() ? WiFi.SSID() : String();
}

String WifiManager::localIp() const {
    return isConnected() ? WiFi.localIP().toString() : String();
}

String WifiManager::apIp() const { return WiFi.softAPIP().toString(); }

int32_t WifiManager::rssi() const { return isConnected() ? WiFi.RSSI() : 0; }

String WifiManager::scanNetworksJson() {
    int count = WiFi.scanNetworks();
    if (count <= 0) {
        return "[]";
    }
    String json = "[";
    for (int i = 0; i < count; ++i) {
        if (i > 0) {
            json += ",";
        }
        json += "{\"ssid\":\"";
        json += escapeJson(WiFi.SSID(i));
        json += "\",\"rssi\":";
        json += WiFi.RSSI(i);
        json += ",\"secure\":";
        json += WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "false" : "true";
        json += "}";
    }
    json += "]";
    WiFi.scanDelete();
    return json;
}

void WifiManager::processDns() {
    if (portalActive_) {
        dnsServer_.processNextRequest();
    }
}

void WifiManager::connectStation(const AppConfig& config) {
    WiFi.mode(portalActive_ ? WIFI_AP_STA : WIFI_STA);
    WiFi.begin(config.wifiSsid, config.wifiPassword);
    stationConnecting_ = true;
    connectStartedMs_ = millis();
}

void WifiManager::startPortal(const AppConfig& config, bool rescueMode) {
    if (!portalActive_) {
        uint32_t chip = static_cast<uint32_t>(ESP.getEfuseMac() & 0xFFFFFF);
        char apName[32] = {0};
        snprintf(apName, sizeof(apName), "CO2-Monitor-%06lX",
                 static_cast<unsigned long>(chip));
        apSsid_ = apName;
    }

    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(apSsid_.c_str());
    dnsServer_.start(53, "*", WiFi.softAPIP());
    portalActive_ = true;
    rescuePortal_ = rescueMode || config.hasWifiCredentials();
}

void WifiManager::stopPortal() {
    dnsServer_.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    portalActive_ = false;
    rescuePortal_ = false;
}

void WifiManager::ensureMdns() {
    if (mdnsStarted_ || !isConnected()) {
        return;
    }
    if (MDNS.begin(kHostname)) {
        MDNS.addService("http", "tcp", 80);
        mdnsStarted_ = true;
    }
}

void WifiManager::ensureTimeSync() {
    if (ntpConfigured_ || !isConnected()) {
        return;
    }
    configTime(0, 0, "pool.ntp.org", "time.nist.gov", "time.google.com");
    ntpConfigured_ = true;
}
