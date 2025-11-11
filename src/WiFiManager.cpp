#include "WiFiManager.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lwip/ip4_addr.h"
#include <string.h>

static const char *TAG = "WiFiManager";

WiFiManager wifi;

WiFiManager::WiFiManager() 
    : sta_netif(nullptr), ap_netif(nullptr), sta_connected(false), ap_started(false) {
}

void WiFiManager::wifi_event_handler(void* arg, esp_event_base_t event_base,
                                     int32_t event_id, void* event_data) {
    WiFiManager* manager = static_cast<WiFiManager*>(arg);
    
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_AP_START:
                ESP_LOGI(TAG, "Access Point started");
                manager->ap_started = true;
                break;
            case WIFI_EVENT_AP_STOP:
                ESP_LOGI(TAG, "Access Point stopped");
                manager->ap_started = false;
                break;
            case WIFI_EVENT_AP_STACONNECTED: {
                wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
                ESP_LOGI(TAG, "Station %02x:%02x:%02x:%02x:%02x:%02x joined, AID=%d",
                         event->mac[0], event->mac[1], event->mac[2],
                         event->mac[3], event->mac[4], event->mac[5], event->aid);
                break;
            }
            case WIFI_EVENT_STA_START:
                esp_wifi_connect();
                break;
            case WIFI_EVENT_STA_DISCONNECTED:
                ESP_LOGI(TAG, "Station disconnected, retrying...");
                manager->sta_connected = false;
                esp_wifi_connect();
                break;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
        manager->sta_connected = true;
    }
}

void WiFiManager::beginAP() {
    ESP_LOGI(TAG, "Starting Access Point...");
    ESP_LOGI(TAG, "AP_ENABLED = %d", Config::AP_ENABLED);
    
    if (!ap_netif) {
        ap_netif = esp_netif_create_default_wifi_ap();
    }
    
    wifi_config_t ap_config = {};
    strncpy((char*)ap_config.ap.ssid, Config::AP_SSID, sizeof(ap_config.ap.ssid));
    strncpy((char*)ap_config.ap.password, Config::AP_PASS, sizeof(ap_config.ap.password));
    ap_config.ap.ssid_len = strlen(Config::AP_SSID);
    ap_config.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;
    ap_config.ap.max_connection = 4;
    ap_config.ap.channel = 0;
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    
    char ip[16];
    getAPIP(ip, sizeof(ip));
    ESP_LOGI(TAG, "Access Point started successfully");
    ESP_LOGI(TAG, "AP IP address: %s", ip);
    ESP_LOGI(TAG, "AP SSID: %s", Config::AP_SSID);
}

void WiFiManager::stopAP() {
    ESP_LOGI(TAG, "Stopping Access Point...");
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ap_started = false;
    ESP_LOGI(TAG, "Access Point stopped - WiFi mode set to STA only");
}

void WiFiManager::beginSTA(const char* ssid, const char* pass) {
    ESP_LOGI(TAG, "Connecting to WiFi: %s", ssid);
    
    if (!sta_netif) {
        sta_netif = esp_netif_create_default_wifi_sta();
    }
    
    wifi_config_t sta_config = {};
    strncpy((char*)sta_config.sta.ssid, ssid, sizeof(sta_config.sta.ssid));
    strncpy((char*)sta_config.sta.password, pass, sizeof(sta_config.sta.password));
    sta_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    
    // Wait for connection
    int max_retry = 20;
    while (!sta_connected && max_retry-- > 0) {
        vTaskDelay(pdMS_TO_TICKS(500));
        ESP_LOGI(TAG, "Connecting to WiFi...");
    }
    
    if (sta_connected) {
        char ip[16];
        getLocalIP(ip, sizeof(ip));
        ESP_LOGI(TAG, "Connected to WiFi! Station IP Address: %s", ip);
    } else {
        ESP_LOGE(TAG, "Failed to connect to WiFi");
    }
}

void WiFiManager::beginAutoSTA() {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("wifi", NVS_READONLY, &nvs_handle);
    
    if (err == ESP_OK) {
        char ssid[33] = {0};
        char password[64] = {0};
        size_t ssid_len = sizeof(ssid);
        size_t pass_len = sizeof(password);
        
        esp_err_t ssid_err = nvs_get_str(nvs_handle, "ssid", ssid, &ssid_len);
        esp_err_t pass_err = nvs_get_str(nvs_handle, "password", password, &pass_len);
        nvs_close(nvs_handle);
        
        if (ssid_err == ESP_OK && pass_err == ESP_OK && strlen(ssid) > 0) {
            ESP_LOGI(TAG, "Found saved WiFi credentials, connecting...");
            
            if (!sta_netif) {
                sta_netif = esp_netif_create_default_wifi_sta();
            }
            
            wifi_config_t sta_config = {};
            strncpy((char*)sta_config.sta.ssid, ssid, sizeof(sta_config.sta.ssid));
            strncpy((char*)sta_config.sta.password, password, sizeof(sta_config.sta.password));
            sta_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
            
            ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
            ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));
            ESP_ERROR_CHECK(esp_wifi_start());
            
            // Wait for connection
            int max_retry = 20;
            while (!sta_connected && max_retry-- > 0) {
                vTaskDelay(pdMS_TO_TICKS(500));
                ESP_LOGI(TAG, "Connecting to saved WiFi...");
            }
            
            if (sta_connected) {
                char ip[16];
                getLocalIP(ip, sizeof(ip));
                ESP_LOGI(TAG, "Connected to saved WiFi! Station IP: %s", ip);
            } else {
                ESP_LOGI(TAG, "Could not connect to saved WiFi.");
                ESP_LOGI(TAG, "Starting emergency Access Point for initial setup/troubleshooting...");
                beginAP();
                char apip[16];
                getAPIP(apip, sizeof(apip));
                ESP_LOGI(TAG, "Emergency AP started - connect to %s network", Config::AP_SSID);
                ESP_LOGI(TAG, "Emergency AP IP: %s", apip);
            }
        } else {
            ESP_LOGI(TAG, "No saved WiFi credentials found. Starting in AP mode for initial setup.");
            beginAP();
        }
    } else {
        ESP_LOGI(TAG, "No saved WiFi credentials found. Starting in AP mode for initial setup.");
        beginAP();
    }
}

bool WiFiManager::isConnected() {
    return sta_connected;
}

void WiFiManager::checkWiFiConnection() {
    // Throttle reconnection attempts to avoid log spam
    static int64_t lastReconnectAttempt = 0;
    int64_t now = esp_timer_get_time() / 1000000; // Convert to seconds
    const int64_t RECONNECT_INTERVAL = 30; // Try to reconnect every 30 seconds
    
    if (!sta_connected) {
        // Only attempt reconnection if enough time has passed
        if (now - lastReconnectAttempt < RECONNECT_INTERVAL) {
            return;
        }
        
        lastReconnectAttempt = now;
        ESP_LOGI(TAG, "WiFi connection lost. Attempting to reconnect...");
        
        nvs_handle_t nvs_handle;
        esp_err_t err = nvs_open("wifi", NVS_READONLY, &nvs_handle);
        
        if (err == ESP_OK) {
            char ssid[33] = {0};
            char password[64] = {0};
            size_t ssid_len = sizeof(ssid);
            size_t pass_len = sizeof(password);
            
            esp_err_t ssid_err = nvs_get_str(nvs_handle, "ssid", ssid, &ssid_len);
            esp_err_t pass_err = nvs_get_str(nvs_handle, "password", password, &pass_len);
            nvs_close(nvs_handle);
            
            if (ssid_err == ESP_OK && pass_err == ESP_OK && strlen(ssid) > 0) {
                ESP_LOGI(TAG, "Preserving dual mode (AP+STA) for WiFi reconnect");
                
                wifi_config_t sta_config = {};
                strncpy((char*)sta_config.sta.ssid, ssid, sizeof(sta_config.sta.ssid));
                strncpy((char*)sta_config.sta.password, password, sizeof(sta_config.sta.password));
                sta_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
                
                esp_wifi_disconnect();
                vTaskDelay(pdMS_TO_TICKS(1000));
                
                if (Config::AP_ENABLED) {
                    esp_wifi_set_mode(WIFI_MODE_APSTA);
                } else {
                    esp_wifi_set_mode(WIFI_MODE_STA);
                }
                
                esp_wifi_set_config(WIFI_IF_STA, &sta_config);
                esp_wifi_connect();
                
                int max_retry = 30;
                while (!sta_connected && max_retry-- > 0) {
                    vTaskDelay(pdMS_TO_TICKS(500));
                    ESP_LOGI(TAG, "Reconnecting...");
                }
                
                if (sta_connected) {
                    char ip[16];
                    getLocalIP(ip, sizeof(ip));
                    ESP_LOGI(TAG, "Reconnected to WiFi! Station IP: %s", ip);
                    
                    // If we were in emergency mode, we can switch back to user preference
                    if (Config::AP_EMERGENCY_MODE) {
                        ESP_LOGI(TAG, "WiFi reconnected - exiting emergency AP mode");
                        Config::AP_EMERGENCY_MODE = false;
                        
                        // If user had disabled AP, switch back to STA only
                        if (!Config::AP_ENABLED) {
                            ESP_LOGI(TAG, "User preference: AP disabled. Switching to STA only mode.");
                            stopAP();
                            esp_wifi_set_mode(WIFI_MODE_STA);
                        }
                    }
                } else {
                    ESP_LOGI(TAG, "Reconnect failed. Starting emergency Access Point for user access...");
                    
                    // Mark as emergency mode so it can be disabled later when WiFi reconnects
                    Config::AP_EMERGENCY_MODE = true;
                    
                    // Ensure we're in APSTA mode
                    esp_wifi_set_mode(WIFI_MODE_APSTA);
                    beginAP();
                    
                    char apip[16];
                    getAPIP(apip, sizeof(apip));
                    ESP_LOGI(TAG, "Emergency AP started - user can access via %s network", Config::AP_SSID);
                    ESP_LOGI(TAG, "Emergency AP IP: %s", apip);
                    ESP_LOGI(TAG, "Emergency mode active - AP will auto-disable when WiFi reconnects (if user disabled it)");
                }
            }
        }
    } else {
        static int64_t lastIPOutput = 0;
        if (now - lastIPOutput > 60) {
            char ip[16];
            getLocalIP(ip, sizeof(ip));
            ESP_LOGI(TAG, "WiFi connected. IP: %s", ip);
            lastIPOutput = now;
        }
    }
}

void WiFiManager::getLocalIP(char* ipStr, size_t maxLen) {
    if (sta_netif) {
        esp_netif_ip_info_t ip_info;
        esp_netif_get_ip_info(sta_netif, &ip_info);
        snprintf(ipStr, maxLen, IPSTR, IP2STR(&ip_info.ip));
    } else {
        strncpy(ipStr, "0.0.0.0", maxLen);
    }
}

void WiFiManager::getAPIP(char* ipStr, size_t maxLen) {
    if (ap_netif) {
        esp_netif_ip_info_t ip_info;
        esp_netif_get_ip_info(ap_netif, &ip_info);
        snprintf(ipStr, maxLen, IPSTR, IP2STR(&ip_info.ip));
    } else {
        strncpy(ipStr, "0.0.0.0", maxLen);
    }
}

int WiFiManager::getStationNum() {
    wifi_sta_list_t sta_list;
    esp_wifi_ap_get_sta_list(&sta_list);
    return sta_list.num;
}

