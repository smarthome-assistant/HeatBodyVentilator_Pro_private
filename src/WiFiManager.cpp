#include "WiFiManager.h"
#include <EEPROM.h>
#include <Preferences.h>

WiFiManager wifi;

void WiFiManager::beginAP() {
    Serial.println("DEBUG: Starting Access Point...");
    Serial.println("DEBUG: AP_ENABLED = " + String(Config::AP_ENABLED));
    
    WiFi.mode(WIFI_AP_STA);
    bool result = WiFi.softAP(Config::AP_SSID, Config::AP_PASS);
    
    if (result) {
        Serial.println("Access Point started successfully");
        Serial.print("AP IP address: ");
        Serial.println(WiFi.softAPIP());
        Serial.print("AP SSID: ");
        Serial.println(Config::AP_SSID);
        Serial.print("Number of connected stations: ");
        Serial.println(WiFi.softAPgetStationNum());
    } else {
        Serial.println("ERROR: Failed to start Access Point!");
    }
}

void WiFiManager::stopAP() {
    Serial.println("Stopping Access Point...");
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    Serial.println("Access Point stopped - WiFi mode set to STA only");
}

void WiFiManager::beginSTA(const char* ssid, const char* pass) {
    WiFi.mode(WIFI_AP_STA);
    WiFi.begin(ssid, pass);

    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.println("Connecting to WiFi...");
    }

    Serial.println("Connected to WiFi!");
    Serial.println("Station IP Address: " + WiFi.localIP().toString());
}

void WiFiManager::beginAutoSTA() {
    Preferences prefs;
    prefs.begin("wifi", true);
    String savedSsid = prefs.getString("ssid", "");
    String savedPassword = prefs.getString("password", "");
    prefs.end();
    
    if (savedSsid.length() > 0 && savedPassword.length() > 0) {
        WiFi.mode(WIFI_STA);
        WiFi.begin(savedSsid.c_str(), savedPassword.c_str());
        
        unsigned long startAttemptTime = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000) {
            delay(500);
            Serial.println("Connecting to saved WiFi...");
        }
        
        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("Connected to saved WiFi!");
            Serial.println("Station IP Address: " + WiFi.localIP().toString());
        } else {
            Serial.println("Could not connect to saved WiFi.");
            Serial.println("Starting emergency Access Point for initial setup/troubleshooting...");
            WiFi.mode(WIFI_AP_STA);
            WiFi.softAP(Config::AP_SSID, Config::AP_PASS);
            Serial.println("Emergency AP started - connect to " + String(Config::AP_SSID) + " network");
            Serial.print("Emergency AP IP: ");
            Serial.println(WiFi.softAPIP());
        }
    } else {
        Serial.println("No saved WiFi credentials found. Starting in AP mode for initial setup.");
        beginAP();
    }
}

bool WiFiManager::isConnected() {
    return WiFi.status() == WL_CONNECTED;
}

void WiFiManager::checkWiFiConnection() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi connection lost. Status: " + String(WiFi.status()));
        Serial.println("Attempting to reconnect...");
        
        Preferences prefs;
        prefs.begin("wifi", true);
        String savedSsid = prefs.getString("ssid", "");
        String savedPassword = prefs.getString("password", "");
        prefs.end();
        
        if (savedSsid.length() > 0 && savedPassword.length() > 0) {
            WiFi.disconnect();
            delay(1000);
            
            if (Config::AP_ENABLED) {
                Serial.println("DEBUG: Preserving dual mode (AP+STA) for WiFi reconnect");
                WiFi.mode(WIFI_AP_STA);
            } else {
                WiFi.mode(WIFI_STA);
            }
            
            WiFi.begin(savedSsid.c_str(), savedPassword.c_str());
            
            unsigned long startAttemptTime = millis();
            while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 15000) {
                delay(500);
                Serial.println("Reconnecting...");
            }
            
            if (WiFi.status() == WL_CONNECTED) {
                Serial.println("Reconnected to WiFi!");
                Serial.println("Station IP Address: " + WiFi.localIP().toString());
            } else {
                Serial.println("Reconnect failed. Starting emergency Access Point for user access...");
                WiFi.mode(WIFI_AP_STA);
                WiFi.softAP(Config::AP_SSID, Config::AP_PASS);
                Serial.println("Emergency AP started - user can access via " + String(Config::AP_SSID) + " network");
                Serial.print("Emergency AP IP: ");
                Serial.println(WiFi.softAPIP());
            }
        }
    } else {
        static unsigned long lastIPOutput = 0;
        if (millis() - lastIPOutput > 60000) {
            Serial.println("WiFi connected. IP: " + WiFi.localIP().toString());
            lastIPOutput = millis();
        }
    }
}

