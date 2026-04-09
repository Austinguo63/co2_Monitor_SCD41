#include "display_manager.h"

bool DisplayManager::begin(TwoWire& wire, uint8_t address) {
    if (display_ == nullptr) {
        display_ = new Adafruit_SSD1306(128, 64, &wire, -1);
    }
    available_ = display_ != nullptr &&
                 display_->begin(SSD1306_SWITCHCAPVCC, address, true, false);
    if (!available_) {
        return false;
    }

    display_->clearDisplay();
    display_->setTextColor(SSD1306_WHITE);
    display_->display();
    return true;
}

void DisplayManager::loop(const DisplaySnapshot& snapshot, DisplayMode mode) {
    if (!available_ || display_ == nullptr) {
        return;
    }

    const uint32_t now = millis();
    if (mode == DisplayMode::Auto && (now - lastPageSwitchMs_) > 8000UL) {
        showStatsPage_ = !showStatsPage_;
        lastPageSwitchMs_ = now;
        lastRedrawMs_ = 0;
    }

    if ((now - lastRedrawMs_) < 500UL) {
        return;
    }
    lastRedrawMs_ = now;

    display_->clearDisplay();
    const bool showStats =
        mode == DisplayMode::Stats || (mode == DisplayMode::Auto && showStatsPage_);
    if (showStats) {
        renderStats(snapshot);
    } else {
        renderLive(snapshot);
    }
    display_->display();
}

void DisplayManager::renderLive(const DisplaySnapshot& snapshot) {
    if (display_ == nullptr) {
        return;
    }

    if (snapshot.alarmActive && ((millis() / 500UL) % 2 == 0)) {
        display_->fillRect(0, 0, 128, 10, SSD1306_WHITE);
        display_->setTextColor(SSD1306_BLACK);
        display_->setCursor(2, 1);
        display_->setTextSize(1);
        display_->print("ALARM");
        display_->setTextColor(SSD1306_WHITE);
    } else {
        display_->setTextSize(1);
        display_->setCursor(0, 0);
        display_->print("CO2 Monitor");
    }

    display_->setTextSize(3);
    display_->setCursor(8, 16);
    if (snapshot.sensorValid) {
        display_->print(snapshot.co2);
    } else {
        display_->print("--");
    }
    display_->setTextSize(1);
    display_->setCursor(96, 40);
    display_->print("ppm");

    display_->setCursor(0, 48);
    display_->printf("T %.1fC  H %.0f%%", snapshot.temperatureC, snapshot.humidity);

    display_->setCursor(0, 57);
    if (snapshot.portalActive) {
        display_->print("AP");
    } else if (snapshot.wifiConnected) {
        display_->print("WiFi");
    } else {
        display_->print("OFF");
    }
    display_->print(" 60m ");
    if (snapshot.avg60m > 0) {
        display_->print(snapshot.avg60m);
    } else {
        display_->print("--");
    }
}

void DisplayManager::renderStats(const DisplaySnapshot& snapshot) {
    if (display_ == nullptr) {
        return;
    }

    display_->setTextSize(1);
    display_->setCursor(0, 0);
    display_->print("60min avg");
    display_->setCursor(74, 0);
    if (snapshot.avg60m > 0) {
        display_->print(snapshot.avg60m);
        display_->print("ppm");
    } else {
        display_->print("--");
    }

    display_->setCursor(0, 18);
    display_->print("24h avg");
    display_->setCursor(74, 18);
    if (snapshot.stats24h.valid) {
        display_->print(snapshot.stats24h.avgPpm);
        display_->print("ppm");
    } else {
        display_->print("--");
    }

    display_->setCursor(0, 34);
    display_->print("24h max");
    display_->setCursor(74, 34);
    if (snapshot.stats24h.valid) {
        display_->print(snapshot.stats24h.maxPpm);
        display_->print("ppm");
    } else {
        display_->print("--");
    }

    display_->setCursor(0, 50);
    display_->print("24h min");
    display_->setCursor(74, 50);
    if (snapshot.stats24h.valid) {
        display_->print(snapshot.stats24h.minPpm);
        display_->print("ppm");
    } else {
        display_->print("--");
    }
}

