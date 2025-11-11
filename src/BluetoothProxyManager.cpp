#include "BluetoothProxyManager.h"
#include "Config.h"
#include <string.h>

static const char *TAG = "BT_PROXY";
static BluetoothProxyManager* btProxyInstance = nullptr;

BluetoothProxyManager::BluetoothProxyManager() 
    : running(false), connectionCount(0) {
    btProxyInstance = this;
    memset(deviceName, 0, sizeof(deviceName));
    
    // Initialize advertising parameters
    adv_params.adv_int_min = 0x20;
    adv_params.adv_int_max = 0x40;
    adv_params.adv_type = ADV_TYPE_IND;
    adv_params.own_addr_type = BLE_ADDR_TYPE_PUBLIC;
    adv_params.channel_map = ADV_CHNL_ALL;
    adv_params.adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY;
}

void BluetoothProxyManager::gap_event_handler(esp_gap_ble_cb_event_t event, 
                                               esp_ble_gap_cb_param_t *param) {
    switch (event) {
        case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
            ESP_LOGI(TAG, "Advertising data set complete");
            esp_ble_gap_start_advertising(&btProxyInstance->adv_params);
            break;
            
        case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
            if (param->adv_start_cmpl.status == ESP_BT_STATUS_SUCCESS) {
                ESP_LOGI(TAG, "Advertising started");
            } else {
                ESP_LOGE(TAG, "Advertising start failed");
            }
            break;
            
        default:
            break;
    }
}

void BluetoothProxyManager::gatts_event_handler(esp_gatts_cb_event_t event, 
                                                esp_gatt_if_t gatts_if,
                                                esp_ble_gatts_cb_param_t *param) {
    switch (event) {
        case ESP_GATTS_REG_EVT:
            ESP_LOGI(TAG, "GATT server registered");
            break;
            
        case ESP_GATTS_CONNECT_EVT:
            ESP_LOGI(TAG, "Client connected");
            if (btProxyInstance) {
                btProxyInstance->connectionCount++;
            }
            break;
            
        case ESP_GATTS_DISCONNECT_EVT:
            ESP_LOGI(TAG, "Client disconnected, restarting advertising");
            if (btProxyInstance) {
                btProxyInstance->connectionCount--;
                btProxyInstance->startAdvertising();
            }
            break;
            
        default:
            break;
    }
}

bool BluetoothProxyManager::begin(const char* devName) {
    if (running) {
        ESP_LOGI(TAG, "Already running");
        return true;
    }
    
    strncpy(deviceName, devName, sizeof(deviceName) - 1);
    
    ESP_LOGI(TAG, "Initializing Bluetooth Proxy...");
    ESP_LOGI(TAG, "Device name: %s", deviceName);
    
    // Initialize NVS (required for Bluetooth)
    // Already done in main.cpp
    
    // Release classic BT memory (we only need BLE)
    esp_err_t ret = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to release classic BT memory: %s", esp_err_to_name(ret));
    }
    
    // Initialize Bluetooth controller
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bluetooth controller init failed: %s", esp_err_to_name(ret));
        return false;
    }
    
    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bluetooth controller enable failed: %s", esp_err_to_name(ret));
        return false;
    }
    
    // Initialize Bluedroid
    ret = esp_bluedroid_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bluedroid init failed: %s", esp_err_to_name(ret));
        return false;
    }
    
    ret = esp_bluedroid_enable();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bluedroid enable failed: %s", esp_err_to_name(ret));
        return false;
    }
    
    // Register callbacks
    esp_ble_gap_register_callback(gap_event_handler);
    esp_ble_gatts_register_callback(gatts_event_handler);
    
    // Set device name
    esp_ble_gap_set_device_name(deviceName);
    
    startAdvertising();
    
    running = true;
    ESP_LOGI(TAG, "Started successfully");
    ESP_LOGI(TAG, "Home Assistant should now detect this device");
    
    return true;
}

void BluetoothProxyManager::startAdvertising() {
    if (!running) return;
    
    // Configure advertising data
    esp_ble_adv_data_t adv_data = {};
    adv_data.set_scan_rsp = false;
    adv_data.include_name = true;
    adv_data.include_txpower = true;
    adv_data.min_interval = 0x0006; // Minimum interval
    adv_data.max_interval = 0x0010; // Maximum interval
    adv_data.appearance = 0x00;
    adv_data.manufacturer_len = 0;
    adv_data.p_manufacturer_data = NULL;
    adv_data.service_data_len = 0;
    adv_data.p_service_data = NULL;
    adv_data.service_uuid_len = 0;
    adv_data.p_service_uuid = NULL;
    adv_data.flag = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT);
    
    esp_ble_gap_config_adv_data(&adv_data);
    
    ESP_LOGI(TAG, "Advertising configured");
}

void BluetoothProxyManager::loop() {
    // Nothing needed here
    // Bluetooth stack handles everything in the background
}

void BluetoothProxyManager::stop() {
    if (!running) {
        ESP_LOGI(TAG, "Not running");
        return;
    }
    
    ESP_LOGI(TAG, "Stopping...");
    
    esp_bluedroid_disable();
    esp_bluedroid_deinit();
    esp_bt_controller_disable();
    esp_bt_controller_deinit();
    
    running = false;
    connectionCount = 0;
    
    ESP_LOGI(TAG, "Stopped");
}

bool BluetoothProxyManager::isRunning() {
    return running;
}

void BluetoothProxyManager::setDeviceName(const char* name) {
    strncpy(deviceName, name, sizeof(deviceName) - 1);
    if (running) {
        esp_ble_gap_set_device_name(deviceName);
        // Restart advertising to broadcast new name
        stop();
        vTaskDelay(pdMS_TO_TICKS(500));
        begin(deviceName);
    }
}

const char* BluetoothProxyManager::getDeviceName() {
    return deviceName;
}

uint16_t BluetoothProxyManager::getConnectionCount() {
    return connectionCount;
}
