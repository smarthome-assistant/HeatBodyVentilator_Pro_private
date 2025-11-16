#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_timer.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "Config.h"
#include "WiFiManager.h"
#include "ServerManager.h"
#include "LEDManager.h"
#include "MQTTManager.h"
#include "driver/ledc.h"
// #include "BluetoothProxyManager.h"  // TEMPORÄR DEAKTIVIERT

static const char *TAG = "MAIN";

// Manager-Instanzen
ServerManager web;
LEDManager led;
// BluetoothProxyManager btProxy;  // TEMPORÄR DEAKTIVIERT
extern WiFiManager wifi;
extern MQTTManager mqttManager;

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "=== HeatBodyVentilator Pro - ESP-IDF Version ===");
    
    // NVS initialisieren (für WiFi und Config)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGI(TAG, "Erasing NVS flash...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "NVS Flash initialisiert");

    // Network interface initialisieren
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    // WiFi initialisieren
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, 
                                               &WiFiManager::wifi_event_handler, &wifi));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, 
                                               &WiFiManager::wifi_event_handler, &wifi));

    // Config laden
    Config::load();
    ESP_LOGI(TAG, "Konfiguration geladen");
    ESP_LOGI(TAG, "Device Name: %s", Config::DEVICE_NAME);
    ESP_LOGI(TAG, "BT Proxy: %s", Config::BT_PROXY_ENABLED ? "AKTIVIERT" : "DEAKTIVIERT");

    // WiFi starten
    wifi.beginAutoSTA();
    
    // LED Manager initialisieren
    led.begin();

    // Webserver starten
    web.setLEDManager(&led);  // LED-Manager mit Webserver verbinden
    web.begin();

    // MQTT initialisieren und LED-Callback registrieren
    mqttManager.setLEDCallback([](bool state) {
        if (state) {
            // Use stored color from web interface (accessible via ServerManager)
            // For now, we'll use white as default - color will be set via RGB callback
            led.setColorWhite();
            ESP_LOGI(TAG, "LED turned ON via MQTT");
        } else {
            led.off();
            ESP_LOGI(TAG, "LED turned OFF via MQTT");
        }
        
        // Publish current state back to MQTT
        uint8_t r, g, b;
        led.getColor(&r, &g, &b);
        mqttManager.publishLEDState(led.isOn(), r, g, b);
    });
    
    mqttManager.setLEDColorCallback([](uint8_t r, uint8_t g, uint8_t b) {
        led.setColor(r, g, b);
        ESP_LOGI(TAG, "LED color changed via MQTT: R=%d G=%d B=%d", r, g, b);
        
        // Publish updated state back to MQTT
        mqttManager.publishLEDState(led.isOn(), r, g, b);
    });
    
    mqttManager.begin();

    // Bluetooth Proxy TEMPORÄR DEAKTIVIERT
    // if (Config::BT_PROXY_ENABLED) {
    //     btProxy.begin(Config::BT_PROXY_NAME);
    //     ESP_LOGI(TAG, "Bluetooth Proxy gestartet: %s", Config::BT_PROXY_NAME);
    // }

    ESP_LOGI(TAG, "System vollständig initialisiert!");
    
    // Main Loop
    static int counter = 0;
    static int sensorPublishCounter = 0;
    
    while (1) {
        // WiFi Connection Check
        wifi.checkWiFiConnection();
        
        // MQTT Loop
        mqttManager.loop();
        
        // Sensor Updates
        web.updateSensors();
        web.updateAutoPWM();
        
        // Publish sensor data to MQTT every 10 seconds (100 * 100ms)
        if (++sensorPublishCounter >= 100) {
            sensorPublishCounter = 0;
            
            if (mqttManager.isConnected()) {
                // Get current temperature
                float temp = web.getKMeterManager()->getTemperatureCelsius();
                if (temp > 0) {
                    mqttManager.publishTemperature(temp);
                }
                
                // Get current fan speed (PWM duty cycle)
                // Note: This is read directly from the hardware
                uint32_t duty = ledc_get_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
                mqttManager.publishFanSpeed(duty);
            }
        }
        
        // Bluetooth Loop TEMPORÄR DEAKTIVIERT
        // if (Config::BT_PROXY_ENABLED) {
        //     btProxy.loop();
        // }
        
        // Heartbeat Log
        if (++counter % 100 == 0) {
            ESP_LOGI(TAG, "Heartbeat... System läuft");
        }
        
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
