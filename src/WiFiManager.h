#pragma once
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "Config.h"

class WiFiManager {
public:
    WiFiManager();
    void beginAP();
    void stopAP();
    void beginSTA(const char* ssid, const char* pass);
    void beginAutoSTA();
    bool isConnected();
    void disconnect();
    void clearCredentials();
    void checkWiFiConnection();
    void getLocalIP(char* ipStr, size_t maxLen);
    void getAPIP(char* ipStr, size_t maxLen);
    int getStationNum();
    
    static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                   int32_t event_id, void* event_data);
    
private:
    esp_netif_t* sta_netif;
    esp_netif_t* ap_netif;
    bool sta_connected;
    bool ap_started;
    int64_t ip_wait_start_time;  // Track when we started waiting for IP
};
extern WiFiManager wifi;
