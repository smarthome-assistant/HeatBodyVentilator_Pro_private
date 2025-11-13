#include "WiFiManager.h"
#include "ServerManager.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lwip/ip4_addr.h"
#include <string.h>

static const char *TAG = "WiFiManager";

WiFiManager wifi;
extern ServerManager web;

WiFiManager::WiFiManager() 
    : sta_netif(nullptr), ap_netif(nullptr), sta_connected(false), ap_started(false), ip_wait_start_time(0) {
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
                // Only auto-connect if we have credentials
                // Otherwise we're in pure AP mode and shouldn't try to connect
                break;
            case WIFI_EVENT_STA_DISCONNECTED:
                manager->sta_connected = false;
                // Don't auto-reconnect here - let checkWiFiConnection handle it
                // to avoid spamming reconnect attempts that interfere with AP
                break;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
        manager->sta_connected = true;
        manager->ip_wait_start_time = 0;  // Reset IP wait timer
        
        // Restart HTTP server to bind to new network interface
        ESP_LOGI(TAG, "Restarting HTTP server for STA interface...");
        web.restart();
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
    // Set scan method to fast scan for quicker connection
    sta_config.sta.scan_method = WIFI_FAST_SCAN;
    // Set connection timeout
    sta_config.sta.failure_retry_cnt = 3;
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    
    // Start connection attempt
    esp_err_t connect_err = esp_wifi_connect();
    if (connect_err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start WiFi connect: %s", esp_err_to_name(connect_err));
    }
    
    // Wait for connection with 8 second timeout
    int max_retry = 16;
    while (!sta_connected && max_retry-- > 0) {
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    
    if (sta_connected) {
        char ip[16];
        getLocalIP(ip, sizeof(ip));
        ESP_LOGI(TAG, "Connected to WiFi! Station IP Address: %s", ip);
    } else {
        ESP_LOGE(TAG, "Failed to connect to WiFi within timeout");
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
            // Set scan method to fast scan for quicker connection
            sta_config.sta.scan_method = WIFI_FAST_SCAN;
            // Set connection timeout - give up after 5 seconds if no response
            sta_config.sta.failure_retry_cnt = 3;
            
            ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
            ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));
            ESP_ERROR_CHECK(esp_wifi_start());
            
            // Start connection attempt
            esp_err_t connect_err = esp_wifi_connect();
            if (connect_err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to start WiFi connect: %s", esp_err_to_name(connect_err));
            }
            
            // Wait for connection with 8 second timeout
            int max_retry = 16;
            while (!sta_connected && max_retry-- > 0) {
                vTaskDelay(pdMS_TO_TICKS(500));
            }
            
            if (sta_connected) {
                char ip[16];
                getLocalIP(ip, sizeof(ip));
                ESP_LOGI(TAG, "Connected to saved WiFi! Station IP: %s", ip);
            } else {
                ESP_LOGI(TAG, "Could not connect to saved WiFi within timeout.");
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
    const int64_t IP_WAIT_TIMEOUT = 60; // Wait max 60 seconds for IP address
    
    if (!sta_connected) {
        // Check if we're currently trying to connect (to avoid interrupting connection process)
        wifi_ap_record_t ap_info;
        esp_err_t status = esp_wifi_sta_get_ap_info(&ap_info);
        
        // If we're in the process of connecting (status == ESP_OK means associated but no IP yet)
        if (status == ESP_OK) {
            // Start timer on first detection of association
            if (ip_wait_start_time == 0) {
                ip_wait_start_time = now;
                ESP_LOGI(TAG, "WiFi associated, waiting for IP address...");
            }
            
            // Check if we've been waiting too long for IP
            if (now - ip_wait_start_time > IP_WAIT_TIMEOUT) {
                ESP_LOGW(TAG, "IP address timeout after %lld seconds. Forcing reconnect...", IP_WAIT_TIMEOUT);
                ip_wait_start_time = 0;
                esp_wifi_disconnect();
                return;
            }
            
            // Log every 5 seconds to reduce spam
            if ((now - ip_wait_start_time) % 5 == 0) {
                ESP_LOGI(TAG, "Still waiting for IP address... (%lld seconds)", now - ip_wait_start_time);
            }
            return;
        }
        
        // Reset IP wait timer if not associated
        ip_wait_start_time = 0;
        
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
                ESP_LOGI(TAG, "Attempting to reconnect to saved WiFi: %s", ssid);
                
                wifi_config_t sta_config = {};
                strncpy((char*)sta_config.sta.ssid, ssid, sizeof(sta_config.sta.ssid));
                strncpy((char*)sta_config.sta.password, password, sizeof(sta_config.sta.password));
                sta_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
                // Set scan method to fast scan for quicker reconnection
                sta_config.sta.scan_method = WIFI_FAST_SCAN;
                // Set connection timeout for faster failure detection
                sta_config.sta.failure_retry_cnt = 3;
                
                // Ensure we're in APSTA mode to keep AP running
                wifi_mode_t current_mode;
                esp_wifi_get_mode(&current_mode);
                if (current_mode != WIFI_MODE_APSTA) {
                    ESP_LOGI(TAG, "Switching to APSTA mode for reconnect");
                    esp_wifi_set_mode(WIFI_MODE_APSTA);
                    vTaskDelay(pdMS_TO_TICKS(100));
                }
                
                esp_wifi_set_config(WIFI_IF_STA, &sta_config);
                
                // Start connection attempt - but don't block
                esp_err_t connect_err = esp_wifi_connect();
                if (connect_err != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to start WiFi connect: %s", esp_err_to_name(connect_err));
                }
                
                // Don't wait here - let the event handler update sta_connected
                // This keeps the main loop responsive and AP functioning
                ESP_LOGI(TAG, "WiFi reconnect initiated, waiting for connection...");
            } else {
                ESP_LOGI(TAG, "No saved WiFi credentials found");
            }
        }
    } else {
        // Connected - check periodically and handle emergency mode exit
        static int64_t lastIPOutput = 0;
        if (now - lastIPOutput > 60) {
            char ip[16];
            getLocalIP(ip, sizeof(ip));
            ESP_LOGI(TAG, "WiFi connected. IP: %s", ip);
            lastIPOutput = now;
        }
        
        // If we were in emergency mode, exit it now
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

