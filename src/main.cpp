#include <Arduino.h>
#include "WiFiManager.h"
#include "ServerManager.h"
#include "LEDManager.h"
#include "MQTTManager.h"
#include "Config.h"
#include <Preferences.h>
#include <nvs_flash.h>
#include <time.h>

ServerManager web;
LEDManager led;
extern WiFiManager wifi;
extern MQTTManager mqttManager;
bool hasWifiCredentials = false;
unsigned long lastWifiCheck = 0;
const unsigned long WIFI_CHECK_INTERVAL = 10000;

void onLEDStateChange(bool state) {
    if (state) {
        led.setColor(CRGB::White);
    } else {
        led.setColor(CRGB::Black);
    }
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        Serial.println("Erasing NVS flash...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    Serial.println("NVS initialized successfully");
    
    led.begin();
    
    Config::loadSettings();
    
    Preferences prefs;
    if (prefs.begin("wifi", true)) {
        String savedSsid = prefs.getString("ssid", "");
        hasWifiCredentials = (savedSsid.length() > 0);
        prefs.end();
        Serial.println("WiFi credentials " + String(hasWifiCredentials ? "found" : "not found"));
    } else {
        Serial.println("Failed to open WiFi preferences");
        hasWifiCredentials = false;
    }
    
    Serial.println("DEBUG: Checking AP_ENABLED setting...");
    Serial.println("DEBUG: Config::AP_ENABLED = " + String(Config::AP_ENABLED ? "true" : "false"));
    
    if (Config::AP_ENABLED) {
        Serial.println("DEBUG: Starting dual mode (AP + STA)...");
        wifi.beginAP();
        Serial.println("Access Point started");
        wifi.beginAutoSTA();
        Serial.println("Starting in dual mode (Access Point + Station enabled)");
    } else {
        Serial.println("DEBUG: AP disabled, STA only mode...");
        wifi.stopAP();
        Serial.println("AP mode disabled");
        wifi.beginAutoSTA();
    }
    
    web.begin();
    mqttManager.begin();
    
    configTime(1 * 3600, 0, "pool.ntp.org", "time.nist.gov");
    Serial.println("NTP time configured");
    
    mqttManager.setLEDCallback(onLEDStateChange);
    
    Serial.println("Setup completed");
}

void loop() {
    web.handleClient();
    mqttManager.loop();
    
    static unsigned long lastSensorUpdate = 0;
    if (millis() - lastSensorUpdate > 2000) {
        web.updateSensors();
        lastSensorUpdate = millis();
    }
    
    if (hasWifiCredentials && (millis() - lastWifiCheck > WIFI_CHECK_INTERVAL)) {
        wifi.checkWiFiConnection();
        lastWifiCheck = millis();
    }
    
    delay(1);
}
