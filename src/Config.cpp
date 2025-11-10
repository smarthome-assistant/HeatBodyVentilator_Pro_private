#include <Preferences.h>
#include "Config.h"
#include <time.h>

namespace Config {
    char WEB_PASSWORD[32] = "your_password_here";
    char DEVICE_NAME[32] = "ESP32-Test";
    char MQTT_SERVER[64] = "";
    uint16_t MQTT_PORT = 1883;
    char MQTT_USER[32] = "";
    char MQTT_PASS[32] = "";
    char MQTT_TOPIC[64] = "home/esp32";
    char TEMP_UNIT[16] = "celsius";
    bool AP_ENABLED = true;
    char LAST_PASSWORD_CHANGE[32] = "Nie geändert";
    
    float TEMP_START = 30.0;
    float TEMP_MAX = 80.0;
    bool AUTO_PWM_ENABLED = true;
    
    // Manual PWM Mode settings
    bool MANUAL_PWM_MODE = false;     // false = Auto, true = Manual
    uint32_t MANUAL_PWM_FREQ = 1000;  // Default 1000 Hz
    uint8_t MANUAL_PWM_DUTY = 0;      // Default 0%

    void loadSettings() {
        Preferences prefs;
        if (!prefs.begin("settings", true)) {
            Serial.println("Warning: Failed to open settings preferences, using defaults");
            return;
        }
        
        String pw = prefs.getString("webpw", "smarthome-assistant.info");
        strncpy(WEB_PASSWORD, pw.c_str(), sizeof(WEB_PASSWORD));
        WEB_PASSWORD[sizeof(WEB_PASSWORD)-1] = '\0';
        
        String deviceName = prefs.getString("device_name", "ESP32-Test");
        strncpy(DEVICE_NAME, deviceName.c_str(), sizeof(DEVICE_NAME));
        DEVICE_NAME[sizeof(DEVICE_NAME)-1] = '\0';
        
        String mqttServer = prefs.getString("mqtt_server", "");
        strncpy(MQTT_SERVER, mqttServer.c_str(), sizeof(MQTT_SERVER));
        MQTT_SERVER[sizeof(MQTT_SERVER)-1] = '\0';
        
        MQTT_PORT = prefs.getUShort("mqtt_port", 1883);
        
        String mqttUser = prefs.getString("mqtt_user", "");
        strncpy(MQTT_USER, mqttUser.c_str(), sizeof(MQTT_USER));
        MQTT_USER[sizeof(MQTT_USER)-1] = '\0';
        
        String mqttPass = prefs.getString("mqtt_pass", "");
        strncpy(MQTT_PASS, mqttPass.c_str(), sizeof(MQTT_PASS));
        MQTT_PASS[sizeof(MQTT_PASS)-1] = '\0';
        
        String mqttTopic = prefs.getString("mqtt_topic", "home/esp32");
        strncpy(MQTT_TOPIC, mqttTopic.c_str(), sizeof(MQTT_TOPIC));
        MQTT_TOPIC[sizeof(MQTT_TOPIC)-1] = '\0';
        
        String tempUnit = prefs.getString("temp_unit", "celsius");
        strncpy(TEMP_UNIT, tempUnit.c_str(), sizeof(TEMP_UNIT));
        TEMP_UNIT[sizeof(TEMP_UNIT)-1] = '\0';
        
        AP_ENABLED = prefs.getBool("ap_enabled", true);
        
        String lastPwChange = prefs.getString("last_pw_change", "Nie geändert");
        strncpy(LAST_PASSWORD_CHANGE, lastPwChange.c_str(), sizeof(LAST_PASSWORD_CHANGE));
        LAST_PASSWORD_CHANGE[sizeof(LAST_PASSWORD_CHANGE)-1] = '\0';
        
        TEMP_START = prefs.getFloat("temp_start", 30.0);
        TEMP_MAX = prefs.getFloat("temp_max", 80.0);
        AUTO_PWM_ENABLED = prefs.getBool("auto_pwm", true);
        
        MANUAL_PWM_MODE = prefs.getBool("manual_mode", false);
        MANUAL_PWM_FREQ = prefs.getUInt("manual_freq", 1000);
        MANUAL_PWM_DUTY = prefs.getUChar("manual_duty", 0);
        
        prefs.end();
        Serial.println("Settings loaded successfully");
    }

    void saveWebPassword(const char* newPassword) {
        Preferences prefs;
        if (!prefs.begin("settings", false)) {
            Serial.println("Error: Failed to open settings preferences for writing");
            return;
        }
        prefs.putString("webpw", newPassword);
        strncpy(WEB_PASSWORD, newPassword, sizeof(WEB_PASSWORD));
        WEB_PASSWORD[sizeof(WEB_PASSWORD)-1] = '\0';
        
        time_t now = time(nullptr);
        struct tm* timeinfo = localtime(&now);
        char timestamp[32];
        strftime(timestamp, sizeof(timestamp), "%d.%m.%Y %H:%M", timeinfo);
        prefs.putString("last_pw_change", timestamp);
        strncpy(LAST_PASSWORD_CHANGE, timestamp, sizeof(LAST_PASSWORD_CHANGE));
        LAST_PASSWORD_CHANGE[sizeof(LAST_PASSWORD_CHANGE)-1] = '\0';
        
        prefs.end();
    }

    void saveDeviceName(const char* newName) {
        Preferences prefs;
        if (!prefs.begin("settings", false)) {
            Serial.println("Error: Failed to open settings preferences for writing");
            return;
        }
        prefs.putString("device_name", newName);
        strncpy(DEVICE_NAME, newName, sizeof(DEVICE_NAME));
        DEVICE_NAME[sizeof(DEVICE_NAME)-1] = '\0';
        prefs.end();
    }

    void saveMQTTSettings(const char* server, uint16_t port, const char* user, const char* pass, const char* topic) {
        Preferences prefs;
        if (!prefs.begin("settings", false)) {
            Serial.println("Error: Failed to open settings preferences for writing");
            return;
        }
        prefs.putString("mqtt_server", server);
        prefs.putUShort("mqtt_port", port);
        prefs.putString("mqtt_user", user);
        prefs.putString("mqtt_pass", pass);
        prefs.putString("mqtt_topic", topic);
        
        strncpy(MQTT_SERVER, server, sizeof(MQTT_SERVER));
        MQTT_SERVER[sizeof(MQTT_SERVER)-1] = '\0';
        MQTT_PORT = port;
        strncpy(MQTT_USER, user, sizeof(MQTT_USER));
        MQTT_USER[sizeof(MQTT_USER)-1] = '\0';
        strncpy(MQTT_PASS, pass, sizeof(MQTT_PASS));
        MQTT_PASS[sizeof(MQTT_PASS)-1] = '\0';
        strncpy(MQTT_TOPIC, topic, sizeof(MQTT_TOPIC));
        MQTT_TOPIC[sizeof(MQTT_TOPIC)-1] = '\0';
        
        prefs.end();
    }

    void saveTempUnit(const char* unit) {
        Preferences prefs;
        if (!prefs.begin("settings", false)) {
            Serial.println("Error: Failed to open settings preferences for writing");
            return;
        }
        prefs.putString("temp_unit", unit);
        strncpy(TEMP_UNIT, unit, sizeof(TEMP_UNIT));
        TEMP_UNIT[sizeof(TEMP_UNIT)-1] = '\0';
        prefs.end();
    }

    void saveAPEnabled(bool enabled) {
        Preferences prefs;
        if (!prefs.begin("settings", false)) {
            Serial.println("Error: Failed to open settings preferences for writing");
            return;
        }
        prefs.putBool("ap_enabled", enabled);
        AP_ENABLED = enabled;
        prefs.end();
    }

    void factoryReset() {
        Serial.println("Performing factory reset...");
        
        Preferences prefs;
        if (prefs.begin("settings", false)) {
            prefs.clear();
            prefs.end();
            Serial.println("Settings preferences cleared");
        } else {
            Serial.println("Warning: Failed to clear settings preferences");
        }
        
        if (prefs.begin("wifi", false)) {
            prefs.clear();
            prefs.end();
            Serial.println("WiFi preferences cleared");
        } else {
            Serial.println("Warning: Failed to clear WiFi preferences");
        }
        
        strcpy(WEB_PASSWORD, "your_password_here");
        strcpy(DEVICE_NAME, "ESP32-Test");
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
        
        Serial.println("Factory reset completed");
    }

    void saveTempMapping(float startTemp, float maxTemp) {
        Preferences prefs;
        if (prefs.begin("settings", false)) {
            prefs.putFloat("temp_start", startTemp);
            prefs.putFloat("temp_max", maxTemp);
            prefs.end();
            
            TEMP_START = startTemp;
            TEMP_MAX = maxTemp;
            
            Serial.printf("Temperature mapping saved: Start=%.1f°C, Max=%.1f°C\n", startTemp, maxTemp);
        } else {
            Serial.println("Error: Failed to save temperature mapping");
        }
    }

    void saveAutoPWMEnabled(bool enabled) {
        Preferences prefs;
        if (prefs.begin("settings", false)) {
            prefs.putBool("auto_pwm", enabled);
            prefs.end();
            
            AUTO_PWM_ENABLED = enabled;
            
            Serial.printf("Auto PWM setting saved: %s\n", enabled ? "Enabled" : "Disabled");
        } else {
            Serial.println("Error: Failed to save auto PWM setting");
        }
    }

    String getLastPasswordChange() {
        return String(LAST_PASSWORD_CHANGE);
    }

    void saveManualPWMMode(bool enabled) {
        Preferences prefs;
        if (prefs.begin("settings", false)) {
            prefs.putBool("manual_mode", enabled);
            prefs.end();
            
            MANUAL_PWM_MODE = enabled;
            
            Serial.printf("Manual PWM mode saved: %s\n", enabled ? "Manual" : "Auto");
        } else {
            Serial.println("Error: Failed to save manual PWM mode");
        }
    }

    void saveManualPWMSettings(uint32_t frequency, uint8_t dutyCycle) {
        Preferences prefs;
        if (prefs.begin("settings", false)) {
            prefs.putUInt("manual_freq", frequency);
            prefs.putUChar("manual_duty", dutyCycle);
            prefs.end();
            
            MANUAL_PWM_FREQ = frequency;
            MANUAL_PWM_DUTY = dutyCycle;
            
            Serial.printf("Manual PWM settings saved: Freq=%u Hz, Duty=%u%%\n", frequency, dutyCycle);
        } else {
            Serial.println("Error: Failed to save manual PWM settings");
        }
    }
}