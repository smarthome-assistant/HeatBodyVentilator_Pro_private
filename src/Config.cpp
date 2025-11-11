#include "Config.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include <string.h>
#include <time.h>
#include <sys/time.h>

static const char *TAG = "CONFIG";
static nvs_handle_t config_handle;

namespace Config {
    char WEB_PASSWORD[32] = "smarthome-assistant.info";
    char DEVICE_NAME[32] = "#1";
    char MQTT_SERVER[64] = "";
    uint16_t MQTT_PORT = 1883;
    char MQTT_USER[32] = "";
    char MQTT_PASS[32] = "";
    char MQTT_TOPIC[64] = "home/esp32";
    char TEMP_UNIT[16] = "celsius";
    bool AP_ENABLED = true;
    bool AP_EMERGENCY_MODE = false; // Emergency fallback mode
    char LAST_PASSWORD_CHANGE[32] = "Nie geändert";
    
    float TEMP_START = 30.0;
    float TEMP_MAX = 80.0;
    bool AUTO_PWM_ENABLED = true;
    
    // Manual PWM Mode settings
    bool MANUAL_PWM_MODE = false;     // false = Auto, true = Manual
    uint32_t MANUAL_PWM_FREQ = 1000;  // Default 1000 Hz
    uint8_t MANUAL_PWM_DUTY = 0;      // Default 0%
    
    // Bluetooth Proxy settings
    bool BT_PROXY_ENABLED = false;    // Disabled by default
    char BT_PROXY_NAME[32] = "HeatBodyVentilator-BT";

    // Helper: String aus NVS lesen
    static void nvs_get_string(nvs_handle_t handle, const char* key, char* dest, size_t dest_size, const char* default_val) {
        size_t required_size = 0;
        esp_err_t err = nvs_get_str(handle, key, NULL, &required_size);
        if (err == ESP_OK && required_size > 0 && required_size <= dest_size) {
            nvs_get_str(handle, key, dest, &required_size);
            dest[dest_size - 1] = '\0';
        } else {
            strncpy(dest, default_val, dest_size - 1);
            dest[dest_size - 1] = '\0';
        }
    }

    void load() {
        esp_err_t err = nvs_open("settings", NVS_READONLY, &config_handle);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "NVS settings nicht gefunden, verwende Defaults");
            return;
        }

        nvs_get_string(config_handle, "webpw", WEB_PASSWORD, sizeof(WEB_PASSWORD), "smarthome-assistant.info");
        nvs_get_string(config_handle, "device_name", DEVICE_NAME, sizeof(DEVICE_NAME), "#1");
        nvs_get_string(config_handle, "mqtt_server", MQTT_SERVER, sizeof(MQTT_SERVER), "");
        
        uint16_t port_tmp = 1883;
        if (nvs_get_u16(config_handle, "mqtt_port", &port_tmp) == ESP_OK) {
            MQTT_PORT = port_tmp;
        }
        
        nvs_get_string(config_handle, "mqtt_user", MQTT_USER, sizeof(MQTT_USER), "");
        nvs_get_string(config_handle, "mqtt_pass", MQTT_PASS, sizeof(MQTT_PASS), "");
        nvs_get_string(config_handle, "mqtt_topic", MQTT_TOPIC, sizeof(MQTT_TOPIC), "home/esp32");
        nvs_get_string(config_handle, "temp_unit", TEMP_UNIT, sizeof(TEMP_UNIT), "celsius");
        
        uint8_t ap_enabled_u8 = 1;
        if (nvs_get_u8(config_handle, "ap_enabled", &ap_enabled_u8) == ESP_OK) {
            AP_ENABLED = (ap_enabled_u8 != 0);
        }
        
        nvs_get_string(config_handle, "last_pw_change", LAST_PASSWORD_CHANGE, sizeof(LAST_PASSWORD_CHANGE), "Nie geändert");
        
        // Temperature settings (als int32 mit 2 Dezimalstellen gespeichert)
        int32_t temp_start_i32 = 3000;
        int32_t temp_max_i32 = 8000;
        if (nvs_get_i32(config_handle, "temp_start", &temp_start_i32) == ESP_OK) {
            TEMP_START = temp_start_i32 / 100.0f;
        }
        if (nvs_get_i32(config_handle, "temp_max", &temp_max_i32) == ESP_OK) {
            TEMP_MAX = temp_max_i32 / 100.0f;
        }
        
        uint8_t auto_pwm_u8 = 1;
        if (nvs_get_u8(config_handle, "auto_pwm", &auto_pwm_u8) == ESP_OK) {
            AUTO_PWM_ENABLED = (auto_pwm_u8 != 0);
        }
        
        // Manual PWM settings
        uint8_t manual_mode_u8 = 0;
        if (nvs_get_u8(config_handle, "manual_mode", &manual_mode_u8) == ESP_OK) {
            MANUAL_PWM_MODE = (manual_mode_u8 != 0);
        }
        
        uint32_t freq_tmp = 1000;
        if (nvs_get_u32(config_handle, "manual_freq", &freq_tmp) == ESP_OK) {
            MANUAL_PWM_FREQ = freq_tmp;
        }
        
        uint8_t duty_tmp = 0;
        if (nvs_get_u8(config_handle, "manual_duty", &duty_tmp) == ESP_OK) {
            MANUAL_PWM_DUTY = duty_tmp;
        }
        
        // Bluetooth Proxy settings
        uint8_t bt_proxy_en_u8 = 0;
        if (nvs_get_u8(config_handle, "bt_proxy_en", &bt_proxy_en_u8) == ESP_OK) {
            BT_PROXY_ENABLED = (bt_proxy_en_u8 != 0);
        }
        nvs_get_string(config_handle, "bt_proxy_name", BT_PROXY_NAME, sizeof(BT_PROXY_NAME), "HeatBodyVentilator-BT");
        
        nvs_close(config_handle);
        ESP_LOGI(TAG, "Einstellungen geladen");
    }

    void saveWebPassword(const char* newPassword) {
        esp_err_t err = nvs_open("settings", NVS_READWRITE, &config_handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Fehler beim Öffnen von NVS");
            return;
        }
        
        nvs_set_str(config_handle, "webpw", newPassword);
        strncpy(WEB_PASSWORD, newPassword, sizeof(WEB_PASSWORD) - 1);
        WEB_PASSWORD[sizeof(WEB_PASSWORD) - 1] = '\0';
        
        time_t now = time(NULL);
        struct tm timeinfo;
        localtime_r(&now, &timeinfo);
        char timestamp[32];
        strftime(timestamp, sizeof(timestamp), "%d.%m.%Y %H:%M", &timeinfo);
        nvs_set_str(config_handle, "last_pw_change", timestamp);
        strncpy(LAST_PASSWORD_CHANGE, timestamp, sizeof(LAST_PASSWORD_CHANGE) - 1);
        LAST_PASSWORD_CHANGE[sizeof(LAST_PASSWORD_CHANGE) - 1] = '\0';
        
        nvs_commit(config_handle);
        nvs_close(config_handle);
        ESP_LOGI(TAG, "Passwort gespeichert");
    }

    void saveDeviceName(const char* newName) {
        esp_err_t err = nvs_open("settings", NVS_READWRITE, &config_handle);
        if (err != ESP_OK) return;
        
        nvs_set_str(config_handle, "device_name", newName);
        strncpy(DEVICE_NAME, newName, sizeof(DEVICE_NAME) - 1);
        DEVICE_NAME[sizeof(DEVICE_NAME) - 1] = '\0';
        
        nvs_commit(config_handle);
        nvs_close(config_handle);
        ESP_LOGI(TAG, "Device Name gespeichert: %s", newName);
    }

    void saveMQTTSettings(const char* server, uint16_t port, const char* user, const char* pass, const char* topic) {
        esp_err_t err = nvs_open("settings", NVS_READWRITE, &config_handle);
        if (err != ESP_OK) return;
        
        nvs_set_str(config_handle, "mqtt_server", server);
        nvs_set_u16(config_handle, "mqtt_port", port);
        nvs_set_str(config_handle, "mqtt_user", user);
        nvs_set_str(config_handle, "mqtt_pass", pass);
        nvs_set_str(config_handle, "mqtt_topic", topic);
        
        strncpy(MQTT_SERVER, server, sizeof(MQTT_SERVER) - 1);
        MQTT_SERVER[sizeof(MQTT_SERVER) - 1] = '\0';
        MQTT_PORT = port;
        strncpy(MQTT_USER, user, sizeof(MQTT_USER) - 1);
        MQTT_USER[sizeof(MQTT_USER) - 1] = '\0';
        strncpy(MQTT_PASS, pass, sizeof(MQTT_PASS) - 1);
        MQTT_PASS[sizeof(MQTT_PASS) - 1] = '\0';
        strncpy(MQTT_TOPIC, topic, sizeof(MQTT_TOPIC) - 1);
        MQTT_TOPIC[sizeof(MQTT_TOPIC) - 1] = '\0';
        
        nvs_commit(config_handle);
        nvs_close(config_handle);
        ESP_LOGI(TAG, "MQTT Einstellungen gespeichert");
    }

    void saveTempUnit(const char* unit) {
        esp_err_t err = nvs_open("settings", NVS_READWRITE, &config_handle);
        if (err != ESP_OK) return;
        
        nvs_set_str(config_handle, "temp_unit", unit);
        strncpy(TEMP_UNIT, unit, sizeof(TEMP_UNIT) - 1);
        TEMP_UNIT[sizeof(TEMP_UNIT) - 1] = '\0';
        
        nvs_commit(config_handle);
        nvs_close(config_handle);
        ESP_LOGI(TAG, "Temperatureinheit gespeichert: %s", unit);
    }

    void saveAPEnabled(bool enabled) {
        esp_err_t err = nvs_open("settings", NVS_READWRITE, &config_handle);
        if (err != ESP_OK) return;
        
        nvs_set_u8(config_handle, "ap_enabled", enabled ? 1 : 0);
        AP_ENABLED = enabled;
        
        nvs_commit(config_handle);
        nvs_close(config_handle);
        ESP_LOGI(TAG, "AP Modus gespeichert: %s", enabled ? "AN" : "AUS");
    }

    void factoryReset() {
        ESP_LOGI(TAG, "Factory Reset wird durchgeführt...");
        
        // NVS Namespaces löschen
        nvs_handle_t handle;
        if (nvs_open("settings", NVS_READWRITE, &handle) == ESP_OK) {
            nvs_erase_all(handle);
            nvs_commit(handle);
            nvs_close(handle);
            ESP_LOGI(TAG, "Settings gelöscht");
        }
        
        if (nvs_open("wifi", NVS_READWRITE, &handle) == ESP_OK) {
            nvs_erase_all(handle);
            nvs_commit(handle);
            nvs_close(handle);
            ESP_LOGI(TAG, "WiFi Daten gelöscht");
        }
        
        // Standardwerte setzen
        strcpy(WEB_PASSWORD, "smarthome-assistant.info");
        strcpy(DEVICE_NAME, "#1");
        strcpy(MQTT_SERVER, "");
        MQTT_PORT = 1883;
        strcpy(MQTT_USER, "");
        strcpy(MQTT_PASS, "");
        strcpy(MQTT_TOPIC, "home/esp32");
        strcpy(TEMP_UNIT, "celsius");
        AP_ENABLED = true;
        strcpy(LAST_PASSWORD_CHANGE, "Nie geändert");
        
        TEMP_START = 30.0;
        TEMP_MAX = 80.0;
        AUTO_PWM_ENABLED = true;
        MANUAL_PWM_MODE = false;
        MANUAL_PWM_FREQ = 1000;
        MANUAL_PWM_DUTY = 0;
        BT_PROXY_ENABLED = false;
        strcpy(BT_PROXY_NAME, "HeatBodyVentilator-BT");
        
        ESP_LOGI(TAG, "Factory Reset abgeschlossen");
    }

    void saveTempMapping(float startTemp, float maxTemp) {
        esp_err_t err = nvs_open("settings", NVS_READWRITE, &config_handle);
        if (err != ESP_OK) return;
        
        nvs_set_i32(config_handle, "temp_start", (int32_t)(startTemp * 100));
        nvs_set_i32(config_handle, "temp_max", (int32_t)(maxTemp * 100));
        
        TEMP_START = startTemp;
        TEMP_MAX = maxTemp;
        
        nvs_commit(config_handle);
        nvs_close(config_handle);
        ESP_LOGI(TAG, "Temperatur-Mapping gespeichert: Start=%.1f°C, Max=%.1f°C", startTemp, maxTemp);
    }

    void saveAutoPWMEnabled(bool enabled) {
        esp_err_t err = nvs_open("settings", NVS_READWRITE, &config_handle);
        if (err != ESP_OK) return;
        
        nvs_set_u8(config_handle, "auto_pwm", enabled ? 1 : 0);
        AUTO_PWM_ENABLED = enabled;
        
        nvs_commit(config_handle);
        nvs_close(config_handle);
        ESP_LOGI(TAG, "Auto PWM Einstellung gespeichert: %s", enabled ? "AN" : "AUS");
    }

    const char* getLastPasswordChange() {
        return LAST_PASSWORD_CHANGE;
    }

    void saveManualPWMMode(bool enabled) {
        esp_err_t err = nvs_open("settings", NVS_READWRITE, &config_handle);
        if (err != ESP_OK) return;
        
        nvs_set_u8(config_handle, "manual_mode", enabled ? 1 : 0);
        MANUAL_PWM_MODE = enabled;
        
        nvs_commit(config_handle);
        nvs_close(config_handle);
        ESP_LOGI(TAG, "Manueller PWM Modus gespeichert: %s", enabled ? "MANUAL" : "AUTO");
    }

    void saveManualPWMSettings(uint32_t frequency, uint8_t dutyCycle) {
        esp_err_t err = nvs_open("settings", NVS_READWRITE, &config_handle);
        if (err != ESP_OK) return;
        
        nvs_set_u32(config_handle, "manual_freq", frequency);
        nvs_set_u8(config_handle, "manual_duty", dutyCycle);
        
        MANUAL_PWM_FREQ = frequency;
        MANUAL_PWM_DUTY = dutyCycle;
        
        nvs_commit(config_handle);
        nvs_close(config_handle);
        ESP_LOGI(TAG, "Manuelle PWM Einstellungen gespeichert: Freq=%u Hz, Duty=%u%%", frequency, dutyCycle);
    }

    void saveBTProxyEnabled(bool enabled) {
        esp_err_t err = nvs_open("settings", NVS_READWRITE, &config_handle);
        if (err != ESP_OK) return;
        
        nvs_set_u8(config_handle, "bt_proxy_en", enabled ? 1 : 0);
        BT_PROXY_ENABLED = enabled;
        
        nvs_commit(config_handle);
        nvs_close(config_handle);
        ESP_LOGI(TAG, "Bluetooth Proxy Aktivierung gespeichert: %s", enabled ? "AN" : "AUS");
    }

    void saveBTProxyName(const char* name) {
        esp_err_t err = nvs_open("settings", NVS_READWRITE, &config_handle);
        if (err != ESP_OK) return;
        
        nvs_set_str(config_handle, "bt_proxy_name", name);
        strncpy(BT_PROXY_NAME, name, sizeof(BT_PROXY_NAME) - 1);
        BT_PROXY_NAME[sizeof(BT_PROXY_NAME) - 1] = '\0';
        
        nvs_commit(config_handle);
        nvs_close(config_handle);
        ESP_LOGI(TAG, "Bluetooth Proxy Name gespeichert: %s", name);
    }
}