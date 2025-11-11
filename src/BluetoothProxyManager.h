#pragma once
#include "esp_bt.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_bt_main.h"
#include "esp_gatt_common_api.h"
#include "esp_log.h"

class BluetoothProxyManager {
public:
    BluetoothProxyManager();
    bool begin(const char* deviceName);
    void stop();
    void loop();
    bool isRunning();
    void setDeviceName(const char* name);
    const char* getDeviceName();
    uint16_t getConnectionCount();
    
private:
    bool running;
    char deviceName[32];
    uint16_t connectionCount;
    esp_ble_adv_params_t adv_params;
    
    void startAdvertising();
    static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param);
    static void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, 
                                    esp_ble_gatts_cb_param_t *param);
};
