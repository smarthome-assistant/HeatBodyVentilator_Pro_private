#pragma once
#include <WiFi.h>
#include "Config.h"

class WiFiManager {
public:
    void beginAP();
    void stopAP();
    void beginSTA(const char* ssid, const char* pass);
    void beginAutoSTA();
    bool isConnected();
    void checkWiFiConnection();
};
extern WiFiManager wifi;
