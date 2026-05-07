#include <Arduino.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <Wire.h>
#include <esp_bt.h>

#include "alarm_manager.h"
#include "config_store.h"
#include "device_api.h"
#include "display_manager.h"
#include "history_store.h"
#include "pins.h"
#include "sensor_manager.h"
#include "serial_rpc_server.h"

namespace {
TwoWire gScdWire(0);
TwoWire gOledWire(1);

ConfigStore gConfigStore;
HistoryStore gHistoryStore;
SensorManager gSensorManager;
AlarmManager gAlarmManager;
DisplayManager gDisplayManager;
DeviceApi gDeviceApi(gConfigStore, gHistoryStore, gSensorManager, gAlarmManager);
SerialRpcServer gSerialRpcServer(gDeviceApi);

uint32_t gLastProcessedReadingUptimeSec = 0;
uint32_t gLastRawPublishMs = 0;
uint32_t gLastStatsRefreshMs = 0;
HistoryStats gCached24hStats;

void disableWirelessRadios() {
    WiFi.persistent(false);
    WiFi.disconnect(true, true);
    WiFi.mode(WIFI_OFF);
    esp_bt_controller_mem_release(ESP_BT_MODE_BTDM);
}
}  // namespace

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println();
    Serial.println("Starting CO2 monitor in local USB mode...");

    disableWirelessRadios();

    LittleFS.begin(true, "/littlefs", 10, kFilesystemPartitionLabel);
    gConfigStore.begin();
    gHistoryStore.begin();

    const AppConfig& config = gConfigStore.config();
    gAlarmManager.configure(config.alarmThresholdPpm, config.alarmDelaySec);

    gScdWire.begin(kScd41SdaPin, kScd41SclPin, 100000);
    gOledWire.begin(kOledSdaPin, kOledSclPin, 400000);

    gSensorManager.begin(gScdWire);
    gSensorManager.setMeasurementMode(
        measurementModeForRefresh(config.refreshIntervalSec));
    gDisplayManager.begin(gOledWire, kOledAddress);
    gSerialRpcServer.begin(Serial);
}

void loop() {
    const AppConfig& config = gConfigStore.config();

    gSerialRpcServer.loop();

    gSensorManager.setMeasurementMode(
        measurementModeForRefresh(config.refreshIntervalSec));
    gSensorManager.loop();

    gAlarmManager.configure(config.alarmThresholdPpm, config.alarmDelaySec);

    if (gSensorManager.hasValidReading()) {
        const SensorReading& reading = gSensorManager.latestReading();
        if (reading.uptimeSec != 0 &&
            reading.uptimeSec != gLastProcessedReadingUptimeSec) {
            gLastProcessedReadingUptimeSec = reading.uptimeSec;
            gAlarmManager.update(reading);
            if (reading.epoch != 0) {
                gHistoryStore.addSensorSample(reading.epoch, reading.co2);
            }
        }
    }

    const uint32_t refreshMs =
        static_cast<uint32_t>(config.refreshIntervalSec) * 1000UL;
    if ((millis() - gLastRawPublishMs) >= refreshMs) {
        gLastRawPublishMs = millis();
        if (gSensorManager.hasValidReading()) {
            const SensorReading& reading = gSensorManager.latestReading();
            if (reading.epoch != 0) {
                gHistoryStore.addPublishedRawPoint(reading.epoch, reading.co2,
                                                   reading.temperatureC,
                                                   reading.humidity);
            }
        }
    }

    if ((millis() - gLastStatsRefreshMs) >= 30000UL) {
        gLastStatsRefreshMs = millis();
        gCached24hStats = gHistoryStore.compute24HourStats(currentEpoch());
    }

    DisplaySnapshot snapshot;
    if (gSensorManager.hasValidReading()) {
        const SensorReading& reading = gSensorManager.latestReading();
        snapshot.sensorValid = reading.valid;
        snapshot.co2 = reading.co2;
        snapshot.temperatureC = reading.temperatureC;
        snapshot.humidity = reading.humidity;
    }
    snapshot.avg60m = gSensorManager.rollingAverage60Min();
    snapshot.stats24h = gCached24hStats;
    snapshot.wifiConnected = false;
    snapshot.portalActive = false;
    snapshot.alarmActive = gAlarmManager.isActive();
    snapshot.timeSynced = isTimeSynchronized();

    gDisplayManager.loop(snapshot, config.displayMode);
    delay(10);
}
