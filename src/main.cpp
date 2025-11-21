/**
 * ============================================================================
 * HeatBodyVentilator Pro - HYBRID VERSION (Arduino + ESP-IDF)
 * ============================================================================
 * 
 * Dieser Code nutzt:
 * - Arduino Framework: setup()/loop(), Serial, Wire, Arduino-Libraries
 * - ESP-IDF APIs: Bestehende Manager-Klassen (WiFiManager, ServerManager, etc.)
 * - M5Unit-KMeterISO: Arduino-Library für KMeter-Sensor
 * 
 * Architektur:
 * - setup(): Initialisierung aller Komponenten (wie app_main() vorher)
 * - loop(): Hauptschleife mit Update-Zyklen (ersetzt while(1) in app_main)
 * - Bestehende ESP-IDF-Manager werden direkt verwendet (keine Änderung nötig)
 */

#include <Arduino.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_timer.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "driver/ledc.h"

// Bestehende ESP-IDF Manager (unverändert)
#include "Config.h"
#include "WiFiManager.h"
#include "ServerManager.h"
#include "LEDManager.h"
#include "MQTTManager.h"

// Neuer Arduino-Wrapper für KMeter
#include "KMeterIsoComponent.h"

static const char *TAG = "MAIN_HYBRID";

// Manager-Instanzen
ServerManager web;
LEDManager led;
KMeterIsoComponent kmeterIso;    // Arduino-basierter KMeter-Sensor

// Externe Manager (in anderen Dateien definiert)
extern WiFiManager wifi;
extern MQTTManager mqttManager;

// Timing für verschiedene Tasks
unsigned long lastSensorUpdate = 0;
unsigned long lastMqttPublish = 0;
unsigned long lastHeartbeat = 0;

const unsigned long SENSOR_UPDATE_INTERVAL = 100;    // 100ms
const unsigned long MQTT_PUBLISH_INTERVAL = 10000;   // 10s
const unsigned long HEARTBEAT_INTERVAL = 10000;      // 10s

void setup() {
    // ========================================================================
    // 1. SERIAL & LOGGING
    // ========================================================================
    Serial.begin(115200);
    while (!Serial && millis() < 3000) {
        delay(10);  // Warte max. 3s auf Serial
    }
    
    // Reduziere WiFi-Log-Spam (nur Errors, keine Warnings)
    esp_log_level_set("wifi", ESP_LOG_ERROR);
    esp_log_level_set("wifi_init", ESP_LOG_INFO);
    
    ESP_LOGI(TAG, "=== HeatBodyVentilator Pro - HYBRID Version (Arduino + ESP-IDF) ===");
    ESP_LOGI(TAG, "Arduino Core: %s", ARDUINO_BOARD);
    ESP_LOGI(TAG, "ESP-IDF Version: %s", esp_get_idf_version());
    
    // ========================================================================
    // 2. NVS FLASH (für WiFi und Config)
    // ========================================================================
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGI(TAG, "Erasing NVS flash...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "NVS Flash initialisiert");
    
    // ========================================================================
    // 3. NETWORK INTERFACE (ESP-IDF)
    // ========================================================================
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    // ========================================================================
    // 4. WIFI INITIALISIERUNG (ESP-IDF)
    // ========================================================================
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, 
                                               &WiFiManager::wifi_event_handler, &wifi));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, 
                                               &WiFiManager::wifi_event_handler, &wifi));
    
    // ========================================================================
    // 5. CONFIG LADEN
    // ========================================================================
    Config::load();
    ESP_LOGI(TAG, "Konfiguration geladen");
    ESP_LOGI(TAG, "Device Name: %s", Config::DEVICE_NAME);
    ESP_LOGI(TAG, "BT Proxy: %s", Config::BT_PROXY_ENABLED ? "AKTIVIERT" : "DEAKTIVIERT");
    
    // ========================================================================
    // 6. LED MANAGER (vor WiFi, damit Status-LED funktioniert)
    // ========================================================================
    led.begin();
    ESP_LOGI(TAG, "LED Manager initialisiert");
    led.setColor(255, 165, 0);  // Orange = Initialisierung
    
    // ========================================================================
    // 7. WIFI STARTEN
    // ========================================================================
    wifi.beginAutoSTA();
    
    // Warte kurz auf WiFi (max 5 Sekunden), aber blockiere nicht
    ESP_LOGI(TAG, "Waiting for WiFi connection...");
    for (int i = 0; i < 10 && !wifi.isConnected(); i++) {
        delay(500);
        ESP_LOGI(TAG, "  Attempt %d/10...", i + 1);
    }
    
    if (wifi.isConnected()) {
        ESP_LOGI(TAG, "WiFi connected successfully");
        led.setColor(0, 255, 0);  // Grün = WiFi OK
    } else {
        ESP_LOGW(TAG, "WiFi not connected yet (will retry in background)");
        led.setColor(255, 255, 0);  // Gelb = WiFi Warnung
    }
    delay(1000);
    
    // ========================================================================
    // 8. KMETER-ISO SENSOR (Arduino-Library)
    // ========================================================================
    // M5Stack Atom I2C Pins: SDA=26, SCL=32
    // Wichtig: Initialisierung NACH WiFi, damit keine Interferenzen auftreten
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "STARTING I2C SENSOR INITIALIZATION");
    ESP_LOGI(TAG, "========================================");
    
    led.setColor(0, 0, 255);  // Blau = Sensor-Init
    
    if (kmeterIso.begin(0x66, 26, 32, 100000)) {
        ESP_LOGI(TAG, "✓ KMeterISO Sensor initialisiert (via Arduino Library)");
        kmeterIso.setReadInterval(1000);  // 1s Leseintervall
        led.setColor(0, 255, 0);  // Grün = Alles OK
    } else {
        ESP_LOGE(TAG, "✗ KMeterISO Sensor initialization FAILED!");
        ESP_LOGE(TAG, "System will continue without temperature sensor.");
        led.setColor(255, 0, 0);  // Rot = Sensor-Fehler
    }
    delay(1000);
    led.off();  // LED aus nach Initialisierung
    
    // ========================================================================
    // 9. WEBSERVER
    // ========================================================================
    web.setLEDManager(&led);
    web.setExternalSensor(&kmeterIso);  // HYBRID MODE: Use Arduino sensor instead of ESP-IDF driver
    ESP_LOGI(TAG, "ServerManager configured to use Arduino KMeterISO sensor");
    web.begin();
    ESP_LOGI(TAG, "Webserver gestartet");
    
    // ========================================================================
    // 10. MQTT MIT CALLBACKS
    // ========================================================================
    mqttManager.setLEDCallback([](bool state) {
        if (state) {
            led.setColorWhite();
            ESP_LOGI(TAG, "LED turned ON via MQTT");
        } else {
            led.off();
            ESP_LOGI(TAG, "LED turned OFF via MQTT");
        }
        
        uint8_t r, g, b;
        led.getColor(&r, &g, &b);
        mqttManager.publishLEDState(led.isOn(), r, g, b);
    });
    
    mqttManager.setLEDColorCallback([](uint8_t r, uint8_t g, uint8_t b) {
        led.setColor(r, g, b);
        ESP_LOGI(TAG, "LED color changed via MQTT: R=%d G=%d B=%d", r, g, b);
        mqttManager.publishLEDState(led.isOn(), r, g, b);
    });
    
    mqttManager.begin();
    ESP_LOGI(TAG, "MQTT Manager initialisiert");
    
    // ========================================================================
    // SETUP COMPLETE
    // ========================================================================
    ESP_LOGI(TAG, "✓ System vollständig initialisiert!");
    ESP_LOGI(TAG, "Entering main loop()...");
}

void loop() {
    unsigned long now = millis();
    
    // ========================================================================
    // WIFI CONNECTION CHECK
    // ========================================================================
    wifi.checkWiFiConnection();
    
    // ========================================================================
    // MQTT LOOP
    // ========================================================================
    mqttManager.loop();
    
    // ========================================================================
    // SENSOR UPDATES (alle 100ms)
    // ========================================================================
    if (now - lastSensorUpdate >= SENSOR_UPDATE_INTERVAL) {
        lastSensorUpdate = now;
        
        // KMeter-Sensor aktualisieren (respektiert eigenes readInterval intern)
        kmeterIso.update();
        
        // Webserver-Sensor-Updates (bestehende Logik)
        web.updateSensors();
        web.updateAutoPWM();
    }
    
    // ========================================================================
    // MQTT PUBLISH (alle 10 Sekunden)
    // ========================================================================
    if (now - lastMqttPublish >= MQTT_PUBLISH_INTERVAL) {
        lastMqttPublish = now;
        
        if (mqttManager.isConnected()) {
            // Temperatur vom neuen Arduino-Sensor
            float temp = kmeterIso.getTemperatureCelsius();
            if (kmeterIso.isReady() && temp > 0) {
                mqttManager.publishTemperature(temp);
                ESP_LOGD(TAG, "Published temperature: %.2f°C", temp);
            }
            
            // Fan Speed (PWM duty cycle)
            uint32_t duty = ledc_get_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
            mqttManager.publishFanSpeed(duty);
        }
    }
    
    // ========================================================================
    // HEARTBEAT LOG (alle 10 Sekunden)
    // ========================================================================
    if (now - lastHeartbeat >= HEARTBEAT_INTERVAL) {
        lastHeartbeat = now;
        ESP_LOGI(TAG, "Heartbeat - System running | Temp: %.2f°C | Status: %s", 
                 kmeterIso.getTemperatureCelsius(),
                 kmeterIso.getStatusString());
    }
    
    // Kleine Pause für RTOS
    delay(1);
}
