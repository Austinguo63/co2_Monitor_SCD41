#pragma once

#include <DNSServer.h>
#include <ESPmDNS.h>
#include <WiFi.h>

#include "app_types.h"

class WifiManager {
  public:
    bool begin(const AppConfig& config);
    void loop(const AppConfig& config);
    void reconnect(const AppConfig& config);

    bool isConnected() const;
    bool isPortalActive() const { return portalActive_; }
    bool isRescuePortal() const { return rescuePortal_; }
    bool hasCredentials(const AppConfig& config) const {
        return config.hasWifiCredentials();
    }
    bool isTimeSynced() const;

    String stationSsid() const;
    String localIp() const;
    String apSsid() const { return apSsid_; }
    String apIp() const;
    int32_t rssi() const;
    String scanNetworksJson();

    void processDns();

  private:
    void connectStation(const AppConfig& config);
    void startPortal(const AppConfig& config, bool rescueMode);
    void stopPortal();
    void ensureMdns();
    void ensureTimeSync();

    DNSServer dnsServer_;
    bool portalActive_ = false;
    bool rescuePortal_ = false;
    bool stationConnecting_ = false;
    bool mdnsStarted_ = false;
    bool ntpConfigured_ = false;
    String apSsid_;
    uint32_t connectStartedMs_ = 0;
};

