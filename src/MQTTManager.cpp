#include "MQTTManager.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "WiFiManager.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "MQTT";
static const char* DEVICE_MODEL = "M5Stack Atom";
static const char* DEVICE_MANUFACTURER = "SmartHome-Assistant.info";

MQTTManager mqttManager;
extern WiFiManager wifi;

MQTTManager::MQTTManager() 
    : mqtt_client(nullptr), lastReconnectAttempt(0), lastHeartbeat(0), 
      autoDiscoveryPublished(false), connected(false), ledCallback(nullptr), ledColorCallback(nullptr) {
}

void MQTTManager::mqtt_event_handler(void *handler_args, esp_event_base_t base, 
                                     int32_t event_id, void *event_data) {
    MQTTManager* manager = static_cast<MQTTManager*>(handler_args);
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    
    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT Connected");
            manager->connected = true;
            manager->subscribeToCommands();
            manager->publishAutoDiscovery();
            manager->publishDeviceState();
            manager->publishFanControlState();
            break;
            
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT Disconnected");
            manager->connected = false;
            manager->autoDiscoveryPublished = false;
            break;
            
        case MQTT_EVENT_DATA: {
            ESP_LOGI(TAG, "MQTT Data received - Topic: %.*s, Data: %.*s", 
                     event->topic_len, event->topic, event->data_len, (char*)event->data);
            
            // LED Control Handler
            char topic[256];
            char baseTopic[128];
            manager->getHomeAssistantBaseTopic(baseTopic, sizeof(baseTopic));
            snprintf(topic, sizeof(topic), "%s/led/set", baseTopic);
            
            if (strncmp(event->topic, topic, event->topic_len) == 0) {
                // Check if it's simple text (ON/OFF) or JSON
                char payload[256] = {0};
                int copyLen = (event->data_len < sizeof(payload) - 1) ? event->data_len : sizeof(payload) - 1;
                strncpy(payload, (char*)event->data, copyLen);
                payload[copyLen] = '\0';
                
                bool isSimpleText = (strcmp(payload, "ON") == 0 || strcmp(payload, "OFF") == 0);
                
                if (isSimpleText) {
                    // Handle simple ON/OFF command
                    bool isOn = (strcmp(payload, "ON") == 0);
                    ESP_LOGI(TAG, "LED simple command: %s", isOn ? "ON" : "OFF");
                    if (manager->ledCallback) {
                        manager->ledCallback(isOn);
                    }
                    
                    // Publish state back (JSON format for Home Assistant)
                    char stateTopic[256];
                    snprintf(stateTopic, sizeof(stateTopic), "%s/led/state", baseTopic);
                    
                    DynamicJsonDocument stateDoc(256);
                    stateDoc["state"] = isOn ? "ON" : "OFF";
                    
                    char statePayload[256];
                    serializeJson(stateDoc, statePayload, sizeof(statePayload));
                    esp_mqtt_client_publish(manager->mqtt_client, stateTopic, statePayload, 0, 1, true);
                } else {
                    // Try to parse as JSON
                    DynamicJsonDocument doc(256);
                    DeserializationError error = deserializeJson(doc, (char*)event->data, event->data_len);
                    
                    if (!error) {
                        // Handle state (ON/OFF)
                        if (doc.containsKey("state")) {
                            const char* state = doc["state"];
                            bool isOn = (strcmp(state, "ON") == 0);
                            if (manager->ledCallback) {
                                manager->ledCallback(isOn);
                            }
                        }
                        
                        // Handle RGB color
                        if (doc.containsKey("color") && doc["color"].containsKey("r") && 
                            doc["color"].containsKey("g") && doc["color"].containsKey("b")) {
                            int r = doc["color"]["r"];
                            int g = doc["color"]["g"];
                            int b = doc["color"]["b"];
                            
                            r = (r < 0) ? 0 : (r > 255) ? 255 : r;
                            g = (g < 0) ? 0 : (g > 255) ? 255 : g;
                            b = (b < 0) ? 0 : (b > 255) ? 255 : b;
                            
                            ESP_LOGI(TAG, "MQTT RGB: R=%d G=%d B=%d", r, g, b);
                            if (manager->ledColorCallback) {
                                manager->ledColorCallback(r, g, b);
                            }
                        }
                        
                        // Publish state back (JSON format)
                        char stateTopic[256];
                        snprintf(stateTopic, sizeof(stateTopic), "%s/led/state", baseTopic);
                        
                        DynamicJsonDocument stateDoc(256);
                        if (doc.containsKey("state")) {
                            stateDoc["state"] = doc["state"];
                        }
                        if (doc.containsKey("color")) {
                            stateDoc["color"]["r"] = doc["color"]["r"];
                            stateDoc["color"]["g"] = doc["color"]["g"];
                            stateDoc["color"]["b"] = doc["color"]["b"];
                        }
                        
                        char statePayload[256];
                        serializeJson(stateDoc, statePayload, sizeof(statePayload));
                        esp_mqtt_client_publish(manager->mqtt_client, stateTopic, statePayload, 0, 1, true);
                    } else {
                        ESP_LOGW(TAG, "Failed to parse as JSON and not simple ON/OFF command");
                    }
                }
            }
            
            // Fan Mode Control Handler
            snprintf(topic, sizeof(topic), "%s/fan/mode/set", baseTopic);
            if (strncmp(event->topic, topic, event->topic_len) == 0) {
                char payload[128] = {0};
                int copyLen = (event->data_len < sizeof(payload) - 1) ? event->data_len : sizeof(payload) - 1;
                strncpy(payload, (char*)event->data, copyLen);
                payload[copyLen] = '\0';
                
                bool isManual = (strstr(payload, "Manuell") != NULL);
                Config::saveManualPWMMode(isManual);
                ESP_LOGI(TAG, "Fan mode changed to: %s", isManual ? "Manual" : "Auto");
                
                // Publish state back
                manager->publishFanControlState();
            }
            
            // Temperature Start Control Handler
            snprintf(topic, sizeof(topic), "%s/fan/temp_start/set", baseTopic);
            if (strncmp(event->topic, topic, event->topic_len) == 0) {
                char payload[16] = {0};
                int copyLen = (event->data_len < sizeof(payload) - 1) ? event->data_len : sizeof(payload) - 1;
                strncpy(payload, (char*)event->data, copyLen);
                payload[copyLen] = '\0';
                
                float tempStart = atof(payload);
                if (tempStart >= 0 && tempStart <= 100) {
                    Config::saveTempMapping(tempStart, Config::TEMP_MAX);
                    ESP_LOGI(TAG, "Start temperature changed to: %.0f°C", tempStart);
                    
                    // Publish state back
                    manager->publishFanControlState();
                }
            }
            
            // Temperature Max Control Handler
            snprintf(topic, sizeof(topic), "%s/fan/temp_max/set", baseTopic);
            if (strncmp(event->topic, topic, event->topic_len) == 0) {
                char payload[16] = {0};
                int copyLen = (event->data_len < sizeof(payload) - 1) ? event->data_len : sizeof(payload) - 1;
                strncpy(payload, (char*)event->data, copyLen);
                payload[copyLen] = '\0';
                
                float tempMax = atof(payload);
                if (tempMax > Config::TEMP_START && tempMax <= 150) {
                    Config::saveTempMapping(Config::TEMP_START, tempMax);
                    ESP_LOGI(TAG, "Max temperature changed to: %.0f°C", tempMax);
                    
                    // Publish state back
                    manager->publishFanControlState();
                }
            }
            
            break;
        }
            
        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT Error");
            break;
            
        default:
            break;
    }
}

void MQTTManager::begin() {
    if (strlen(Config::MQTT_SERVER) == 0) {
        ESP_LOGI(TAG, "No MQTT server configured");
        return;
    }
    
    char uri[128];
    snprintf(uri, sizeof(uri), "mqtt://%s:%d", Config::MQTT_SERVER, Config::MQTT_PORT);
    
    char clientId[64];
    char deviceId[32];
    getDeviceId(deviceId, sizeof(deviceId));
    snprintf(clientId, sizeof(clientId), "ESP32-%s", deviceId);
    
    char statusTopic[256];
    char baseTopic[128];
    getBaseTopic(baseTopic, sizeof(baseTopic));
    snprintf(statusTopic, sizeof(statusTopic), "%s/status", baseTopic);
    
    esp_mqtt_client_config_t mqtt_cfg = {};
    // ESP-IDF 4.4 alte MQTT API
    mqtt_cfg.uri = uri;
    mqtt_cfg.client_id = clientId;
    
    if (strlen(Config::MQTT_USER) > 0) {
        mqtt_cfg.username = Config::MQTT_USER;
        mqtt_cfg.password = Config::MQTT_PASS;
    }
    
    mqtt_cfg.lwt_topic = statusTopic;
    mqtt_cfg.lwt_msg = "offline";
    mqtt_cfg.lwt_qos = 1;
    mqtt_cfg.lwt_retain = true;
    
    mqtt_cfg.buffer_size = 2048;
    mqtt_cfg.task_stack = 8192;
    mqtt_cfg.reconnect_timeout_ms = 10000;
    mqtt_cfg.disable_auto_reconnect = false;
    
    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(mqtt_client, (esp_mqtt_event_id_t)ESP_EVENT_ANY_ID, 
                                   mqtt_event_handler, this);
    esp_mqtt_client_start(mqtt_client);
    
    ESP_LOGI(TAG, "Initialized with server %s:%d", Config::MQTT_SERVER, Config::MQTT_PORT);
}

void MQTTManager::loop() {
    if (strlen(Config::MQTT_SERVER) == 0 || !wifi.isConnected()) {
        return;
    }
    
    int64_t now = esp_timer_get_time() / 1000000; // Convert to seconds
    
    if (!connected) {
        if (now - lastReconnectAttempt > 30) {
            lastReconnectAttempt = now;
            // Reconnection is automatic via esp_mqtt_client
        }
    } else {
        if (now - lastHeartbeat > 30) {
            lastHeartbeat = now;
            publishDeviceState();
        }
    }
}

bool MQTTManager::isConnected() {
    return connected;
}

void MQTTManager::disconnect() {
    if (connected && mqtt_client) {
        char statusTopic[256];
        char baseTopic[128];
        getBaseTopic(baseTopic, sizeof(baseTopic));
        snprintf(statusTopic, sizeof(statusTopic), "%s/status", baseTopic);
        
        esp_mqtt_client_publish(mqtt_client, statusTopic, "offline", 0, 1, true);
        esp_mqtt_client_stop(mqtt_client);
        esp_mqtt_client_destroy(mqtt_client);
        mqtt_client = nullptr;
    }
    connected = false;
    autoDiscoveryPublished = false;
}

void MQTTManager::reconnect() {
    ESP_LOGI(TAG, "Reconnecting MQTT with new settings...");
    
    disconnect();
    
    // Wait for socket cleanup
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    begin();
}

void MQTTManager::setLEDCallback(void (*callback)(bool state)) {
    ledCallback = callback;
}

void MQTTManager::setLEDColorCallback(void (*callback)(uint8_t r, uint8_t g, uint8_t b)) {
    ledColorCallback = callback;
}

void MQTTManager::getDeviceId(char* buf, size_t len) {
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    snprintf(buf, len, "%02X%02X%02X", mac[3], mac[4], mac[5]);
}

void MQTTManager::getMacAddress(char* buf, size_t len) {
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    snprintf(buf, len, "%02X:%02X:%02X:%02X:%02X:%02X", 
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

void MQTTManager::getBaseTopic(char* buf, size_t len) {
    if (strlen(Config::MQTT_TOPIC) > 0) {
        strncpy(buf, Config::MQTT_TOPIC, len);
    } else {
        char deviceId[32];
        getDeviceId(deviceId, sizeof(deviceId));
        snprintf(buf, len, "esp32/%s", deviceId);
    }
}

void MQTTManager::getHomeAssistantBaseTopic(char* buf, size_t len) {
    getBaseTopic(buf, len);
}

void MQTTManager::getDiscoveryTopic(const char* component, const char* objectId, 
                                   char* buf, size_t len) {
    char deviceId[32];
    getDeviceId(deviceId, sizeof(deviceId));
    snprintf(buf, len, "homeassistant/%s/%s_%s/config", component, deviceId, objectId);
}

void MQTTManager::subscribeToCommands() {
    if (!mqtt_client || !connected) return;
    
    char topic[256];
    char baseTopic[128];
    getHomeAssistantBaseTopic(baseTopic, sizeof(baseTopic));
    
    // Subscribe to LED control (JSON format)
    snprintf(topic, sizeof(topic), "%s/led/set", baseTopic);
    esp_mqtt_client_subscribe(mqtt_client, topic, 1);
    ESP_LOGI(TAG, "Subscribed to: %s", topic);
    
    // Subscribe to Fan Mode control
    snprintf(topic, sizeof(topic), "%s/fan/mode/set", baseTopic);
    esp_mqtt_client_subscribe(mqtt_client, topic, 1);
    ESP_LOGI(TAG, "Subscribed to: %s", topic);
    
    // Subscribe to Temperature Start control
    snprintf(topic, sizeof(topic), "%s/fan/temp_start/set", baseTopic);
    esp_mqtt_client_subscribe(mqtt_client, topic, 1);
    ESP_LOGI(TAG, "Subscribed to: %s", topic);
    
    // Subscribe to Temperature Max control
    snprintf(topic, sizeof(topic), "%s/fan/temp_max/set", baseTopic);
    esp_mqtt_client_subscribe(mqtt_client, topic, 1);
    ESP_LOGI(TAG, "Subscribed to: %s", topic);
}

void MQTTManager::publishAutoDiscovery() {
    if (!mqtt_client || !connected) return;
    
    ESP_LOGI(TAG, "Publishing Home Assistant Auto Discovery...");
    
    // Delete old switch and light discoveries (from previous versions)
    char deviceId[32];
    char oldSwitchTopic[256];
    char oldLightTopic[256];
    getDeviceId(deviceId, sizeof(deviceId));
    getDiscoveryTopic("switch", "led", oldSwitchTopic, sizeof(oldSwitchTopic));
    esp_mqtt_client_publish(mqtt_client, oldSwitchTopic, "", 0, 1, true);
    getDiscoveryTopic("light", "led", oldLightTopic, sizeof(oldLightTopic));
    esp_mqtt_client_publish(mqtt_client, oldLightTopic, "", 0, 1, true);
    ESP_LOGI(TAG, "Removed old switch/light discovery");
    
    publishSwitchDiscovery();
    publishSensorDiscovery();
    publishControlDiscovery();
    publishButtonDiscovery();
    
    autoDiscoveryPublished = true;
    ESP_LOGI(TAG, "Auto Discovery published");
}

void MQTTManager::publishSwitchDiscovery() {
    char deviceId[32];
    char baseTopic[128];
    char discoveryTopic[256];
    
    getDeviceId(deviceId, sizeof(deviceId));
    getHomeAssistantBaseTopic(baseTopic, sizeof(baseTopic));
    getDiscoveryTopic("light", "led_rgb", discoveryTopic, sizeof(discoveryTopic));
    
    // Use smaller buffer - simplified payload
    DynamicJsonDocument doc(768);
    
    char uniqueId[64];
    snprintf(uniqueId, sizeof(uniqueId), "%s_led_rgb", deviceId);
    doc["uniq_id"] = uniqueId;
    doc["name"] = "LED";
    doc["schema"] = "json";
    
    char stateTopic[256];
    snprintf(stateTopic, sizeof(stateTopic), "%s/led/state", baseTopic);
    doc["stat_t"] = stateTopic;
    
    char commandTopic[256];
    snprintf(commandTopic, sizeof(commandTopic), "%s/led/set", baseTopic);
    doc["cmd_t"] = commandTopic;
    
    // Supported color modes for JSON schema
    JsonArray colorModes = doc.createNestedArray("sup_clrm");
    colorModes.add("rgb");
    
    JsonObject dev = doc.createNestedObject("dev");
    dev["ids"][0] = deviceId;
    dev["name"] = Config::DEVICE_NAME;
    dev["mdl"] = "M5Stack Atom";
    dev["mf"] = "SmartHome";
    
    char payload[768];
    size_t len = serializeJson(doc, payload, sizeof(payload));
    
    ESP_LOGI(TAG, "LED Discovery payload size: %d bytes", len);
    esp_mqtt_client_publish(mqtt_client, discoveryTopic, payload, 0, 1, true);
    ESP_LOGI(TAG, "Published LED light discovery");
}

void MQTTManager::publishSensorDiscovery() {
    char deviceId[32];
    char baseTopic[128];
    
    getDeviceId(deviceId, sizeof(deviceId));
    getHomeAssistantBaseTopic(baseTopic, sizeof(baseTopic));
    
    // 1. Temperature Sensor Discovery
    {
        char discoveryTopic[256];
        getDiscoveryTopic("sensor", "temperature", discoveryTopic, sizeof(discoveryTopic));
        
        DynamicJsonDocument doc(512);
        
        char uniqueId[64];
        snprintf(uniqueId, sizeof(uniqueId), "%s_temperature", deviceId);
        doc["uniq_id"] = uniqueId;
        doc["name"] = "Temperatur";
        doc["dev_cla"] = "temperature";
        doc["unit_of_meas"] = "°C";
        doc["stat_cla"] = "measurement";
        
        char stateTopic[256];
        snprintf(stateTopic, sizeof(stateTopic), "%s/sensor/temperature", baseTopic);
        doc["stat_t"] = stateTopic;
        doc["val_tpl"] = "{{ value_json.temperature }}";
        
        JsonObject dev = doc.createNestedObject("dev");
        dev["ids"][0] = deviceId;
        dev["name"] = Config::DEVICE_NAME;
        dev["mdl"] = "M5Stack Atom";
        dev["mf"] = "SmartHome-Assistant.info";
        
        char payload[512];
        serializeJson(doc, payload, sizeof(payload));
        
        esp_mqtt_client_publish(mqtt_client, discoveryTopic, payload, 0, 1, true);
        ESP_LOGI(TAG, "Published Temperature sensor discovery");
    }
    
    // 2. Fan Speed Sensor Discovery (PWM Duty Cycle as percentage)
    {
        char discoveryTopic[256];
        getDiscoveryTopic("sensor", "fan", discoveryTopic, sizeof(discoveryTopic));
        
        DynamicJsonDocument doc(512);
        
        char uniqueId[64];
        snprintf(uniqueId, sizeof(uniqueId), "%s_fan", deviceId);
        doc["uniq_id"] = uniqueId;
        doc["name"] = "Lüfter";
        doc["icon"] = "mdi:fan";
        doc["unit_of_meas"] = "%";
        doc["stat_cla"] = "measurement";
        
        char stateTopic[256];
        snprintf(stateTopic, sizeof(stateTopic), "%s/sensor/fan", baseTopic);
        doc["stat_t"] = stateTopic;
        doc["val_tpl"] = "{{ value_json.speed }}";
        
        JsonObject dev = doc.createNestedObject("dev");
        dev["ids"][0] = deviceId;
        dev["name"] = Config::DEVICE_NAME;
        dev["mdl"] = "M5Stack Atom";
        dev["mf"] = "SmartHome-Assistant.info";
        
        char payload[512];
        serializeJson(doc, payload, sizeof(payload));
        
        esp_mqtt_client_publish(mqtt_client, discoveryTopic, payload, 0, 1, true);
        ESP_LOGI(TAG, "Published Fan sensor discovery");
    }
    
    ESP_LOGI(TAG, "Sensor discovery completed");
}

void MQTTManager::publishControlDiscovery() {
    char deviceId[32];
    char baseTopic[128];
    
    getDeviceId(deviceId, sizeof(deviceId));
    getHomeAssistantBaseTopic(baseTopic, sizeof(baseTopic));
    
    // 1. Select for Fan Mode (Auto/Manual)
    {
        char discoveryTopic[256];
        getDiscoveryTopic("select", "fan_mode", discoveryTopic, sizeof(discoveryTopic));
        
        DynamicJsonDocument doc(768);
        
        char uniqueId[64];
        snprintf(uniqueId, sizeof(uniqueId), "%s_fan_mode", deviceId);
        doc["uniq_id"] = uniqueId;
        doc["name"] = "Lüfter Status";
        doc["icon"] = "mdi:fan-auto";
        
        char stateTopic[256];
        snprintf(stateTopic, sizeof(stateTopic), "%s/fan/mode/state", baseTopic);
        doc["stat_t"] = stateTopic;
        
        char commandTopic[256];
        snprintf(commandTopic, sizeof(commandTopic), "%s/fan/mode/set", baseTopic);
        doc["cmd_t"] = commandTopic;
        
        JsonArray options = doc.createNestedArray("options");
        options.add("Automatische temperaturbasierte Steuerung");
        options.add("Manueller Modus (PWM-Frequenz, Tastverhältnis)");
        
        JsonObject dev = doc.createNestedObject("dev");
        dev["ids"][0] = deviceId;
        dev["name"] = Config::DEVICE_NAME;
        dev["mdl"] = "M5Stack Atom";
        dev["mf"] = "SmartHome-Assistant.info";
        
        char payload[768];
        serializeJson(doc, payload, sizeof(payload));
        
        esp_mqtt_client_publish(mqtt_client, discoveryTopic, payload, 0, 1, true);
        ESP_LOGI(TAG, "Published Fan Mode select discovery");
    }
    
    // 2. Number for Start Temperature
    {
        char discoveryTopic[256];
        getDiscoveryTopic("number", "temp_start", discoveryTopic, sizeof(discoveryTopic));
        
        DynamicJsonDocument doc(768);
        
        char uniqueId[64];
        snprintf(uniqueId, sizeof(uniqueId), "%s_temp_start", deviceId);
        doc["uniq_id"] = uniqueId;
        doc["name"] = "Starttemperatur";
        doc["icon"] = "mdi:thermometer-low";
        doc["unit_of_meas"] = "°C";
        doc["min"] = 0;
        doc["max"] = 100;
        doc["step"] = 1;
        doc["mode"] = "box";
        
        char stateTopic[256];
        snprintf(stateTopic, sizeof(stateTopic), "%s/fan/temp_start/state", baseTopic);
        doc["stat_t"] = stateTopic;
        
        char commandTopic[256];
        snprintf(commandTopic, sizeof(commandTopic), "%s/fan/temp_start/set", baseTopic);
        doc["cmd_t"] = commandTopic;
        
        JsonObject dev = doc.createNestedObject("dev");
        dev["ids"][0] = deviceId;
        dev["name"] = Config::DEVICE_NAME;
        dev["mdl"] = "M5Stack Atom";
        dev["mf"] = "SmartHome-Assistant.info";
        
        char payload[768];
        serializeJson(doc, payload, sizeof(payload));
        
        esp_mqtt_client_publish(mqtt_client, discoveryTopic, payload, 0, 1, true);
        ESP_LOGI(TAG, "Published Start Temperature number discovery");
    }
    
    // 3. Number for Max Temperature
    {
        char discoveryTopic[256];
        getDiscoveryTopic("number", "temp_max", discoveryTopic, sizeof(discoveryTopic));
        
        DynamicJsonDocument doc(768);
        
        char uniqueId[64];
        snprintf(uniqueId, sizeof(uniqueId), "%s_temp_max", deviceId);
        doc["uniq_id"] = uniqueId;
        doc["name"] = "Maximaltemperatur";
        doc["icon"] = "mdi:thermometer-high";
        doc["unit_of_meas"] = "°C";
        doc["min"] = 0;
        doc["max"] = 150;
        doc["step"] = 1;
        doc["mode"] = "box";
        
        char stateTopic[256];
        snprintf(stateTopic, sizeof(stateTopic), "%s/fan/temp_max/state", baseTopic);
        doc["stat_t"] = stateTopic;
        
        char commandTopic[256];
        snprintf(commandTopic, sizeof(commandTopic), "%s/fan/temp_max/set", baseTopic);
        doc["cmd_t"] = commandTopic;
        
        JsonObject dev = doc.createNestedObject("dev");
        dev["ids"][0] = deviceId;
        dev["name"] = Config::DEVICE_NAME;
        dev["mdl"] = "M5Stack Atom";
        dev["mf"] = "SmartHome-Assistant.info";
        
        char payload[768];
        serializeJson(doc, payload, sizeof(payload));
        
        esp_mqtt_client_publish(mqtt_client, discoveryTopic, payload, 0, 1, true);
        ESP_LOGI(TAG, "Published Max Temperature number discovery");
    }
    
    ESP_LOGI(TAG, "Control discovery completed");
}

void MQTTManager::publishButtonDiscovery() {
    // No physical buttons on M5Stack Atom in this application
    // This can be implemented later if needed for virtual buttons or triggers
    ESP_LOGI(TAG, "Button discovery skipped (no buttons configured)");
}

void MQTTManager::publishDeviceState() {
    if (!mqtt_client || !connected) return;
    
    char statusTopic[256];
    char baseTopic[128];
    getBaseTopic(baseTopic, sizeof(baseTopic));
    snprintf(statusTopic, sizeof(statusTopic), "%s/status", baseTopic);
    
    esp_mqtt_client_publish(mqtt_client, statusTopic, "online", 0, 1, true);
}

void MQTTManager::publishLEDState(bool isOn, uint8_t r, uint8_t g, uint8_t b) {
    if (!mqtt_client || !connected) return;
    
    char stateTopic[256];
    char baseTopic[128];
    getHomeAssistantBaseTopic(baseTopic, sizeof(baseTopic));
    snprintf(stateTopic, sizeof(stateTopic), "%s/led/state", baseTopic);
    
    DynamicJsonDocument doc(256);
    doc["state"] = isOn ? "ON" : "OFF";
    
    if (isOn) {
        doc["color_mode"] = "rgb";
        JsonObject color = doc.createNestedObject("color");
        color["r"] = r;
        color["g"] = g;
        color["b"] = b;
    }
    
    char payload[256];
    serializeJson(doc, payload, sizeof(payload));
    
    esp_mqtt_client_publish(mqtt_client, stateTopic, payload, 0, 1, true);
    ESP_LOGI(TAG, "Published LED state: %s, RGB(%d,%d,%d)", isOn ? "ON" : "OFF", r, g, b);
}

void MQTTManager::publishFanSpeed(int pwmDuty) {
    if (!mqtt_client || !connected) return;
    
    char topic[256];
    char baseTopic[128];
    getBaseTopic(baseTopic, sizeof(baseTopic));
    snprintf(topic, sizeof(topic), "%s/sensor/fan", baseTopic);
    
    // Calculate percentage (pwmDuty is 0-255)
    int percentage = (pwmDuty * 100) / 255;
    
    // Send as JSON for Home Assistant
    DynamicJsonDocument doc(128);
    doc["speed"] = percentage;
    
    char payload[128];
    serializeJson(doc, payload, sizeof(payload));
    
    esp_mqtt_client_publish(mqtt_client, topic, payload, 0, 0, false);
}

void MQTTManager::publishTemperature(float tempCelsius) {
    if (!mqtt_client || !connected) return;
    
    char topic[256];
    char baseTopic[128];
    getBaseTopic(baseTopic, sizeof(baseTopic));
    snprintf(topic, sizeof(topic), "%s/sensor/temperature", baseTopic);
    
    // Send as JSON for Home Assistant
    DynamicJsonDocument doc(128);
    doc["temperature"] = ((int)(tempCelsius * 10 + 0.5)) / 10.0; // Round to 1 decimal
    
    char payload[128];
    serializeJson(doc, payload, sizeof(payload));
    
    esp_mqtt_client_publish(mqtt_client, topic, payload, 0, 0, false);
}

void MQTTManager::publishFanControlState() {
    if (!mqtt_client || !connected) return;
    
    char topic[256];
    char baseTopic[128];
    getBaseTopic(baseTopic, sizeof(baseTopic));
    
    // Publish Fan Mode
    snprintf(topic, sizeof(topic), "%s/fan/mode/state", baseTopic);
    const char* mode = Config::MANUAL_PWM_MODE ? 
        "Manueller Modus (PWM-Frequenz, Tastverhältnis)" : 
        "Automatische temperaturbasierte Steuerung";
    esp_mqtt_client_publish(mqtt_client, topic, mode, 0, 1, true);
    
    // Publish Start Temperature
    snprintf(topic, sizeof(topic), "%s/fan/temp_start/state", baseTopic);
    char tempStart[16];
    snprintf(tempStart, sizeof(tempStart), "%.0f", Config::TEMP_START);
    esp_mqtt_client_publish(mqtt_client, topic, tempStart, 0, 1, true);
    
    // Publish Max Temperature
    snprintf(topic, sizeof(topic), "%s/fan/temp_max/state", baseTopic);
    char tempMax[16];
    snprintf(tempMax, sizeof(tempMax), "%.0f", Config::TEMP_MAX);
    esp_mqtt_client_publish(mqtt_client, topic, tempMax, 0, 1, true);
    
    ESP_LOGI(TAG, "Published Fan Control State");
}

void MQTTManager::publishSensorData(const char* sensor, float value, const char* unit) {
    if (!mqtt_client || !connected) return;
    
    char topic[256];
    char baseTopic[128];
    getBaseTopic(baseTopic, sizeof(baseTopic));
    snprintf(topic, sizeof(topic), "%s/sensor/%s", baseTopic, sensor);
    
    char payload[64];
    if (strlen(unit) > 0) {
        snprintf(payload, sizeof(payload), "%.2f %s", value, unit);
    } else {
        snprintf(payload, sizeof(payload), "%.2f", value);
    }
    
    esp_mqtt_client_publish(mqtt_client, topic, payload, 0, 0, false);
}
