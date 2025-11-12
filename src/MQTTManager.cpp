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
            break;
            
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT Disconnected");
            manager->connected = false;
            manager->autoDiscoveryPublished = false;
            break;
            
        case MQTT_EVENT_DATA: {
            ESP_LOGI(TAG, "MQTT Data received - Topic: %.*s", event->topic_len, event->topic);
            
            // LED Control Handler
            char topic[256];
            char baseTopic[128];
            manager->getHomeAssistantBaseTopic(baseTopic, sizeof(baseTopic));
            snprintf(topic, sizeof(topic), "%s/led/set", baseTopic);
            
            if (strncmp(event->topic, topic, event->topic_len) == 0) {
                if (manager->ledCallback) {
                    bool state = (strncmp((char*)event->data, "ON", event->data_len) == 0);
                    manager->ledCallback(state);
                    
                    // Publish state back
                    char stateTopic[256];
                    snprintf(stateTopic, sizeof(stateTopic), "%s/led/state", baseTopic);
                    esp_mqtt_client_publish(manager->mqtt_client, stateTopic, 
                                          state ? "ON" : "OFF", 0, 1, true);
                }
            }
            
            // RGB Color Handler
            char rgbTopic[256];
            snprintf(rgbTopic, sizeof(rgbTopic), "%s/led/rgb/set", baseTopic);
            
            if (strncmp(event->topic, rgbTopic, event->topic_len) == 0 && manager->ledColorCallback) {
                // Parse RGB values (format: "r,g,b" e.g., "255,128,0")
                char data[64];
                int len = (event->data_len < sizeof(data) - 1) ? event->data_len : sizeof(data) - 1;
                strncpy(data, (char*)event->data, len);
                data[len] = '\0';
                
                int r, g, b;
                if (sscanf(data, "%d,%d,%d", &r, &g, &b) == 3) {
                    r = (r < 0) ? 0 : (r > 255) ? 255 : r;
                    g = (g < 0) ? 0 : (g > 255) ? 255 : g;
                    b = (b < 0) ? 0 : (b > 255) ? 255 : b;
                    
                    ESP_LOGI(TAG, "RGB command: R=%d G=%d B=%d", r, g, b);
                    manager->ledColorCallback(r, g, b);
                    
                    // Publish RGB state back
                    char rgbStateTopic[256];
                    snprintf(rgbStateTopic, sizeof(rgbStateTopic), "%s/led/rgb/state", baseTopic);
                    char rgbState[32];
                    snprintf(rgbState, sizeof(rgbState), "%d,%d,%d", r, g, b);
                    esp_mqtt_client_publish(manager->mqtt_client, rgbStateTopic, rgbState, 0, 1, true);
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
    mqtt_cfg.broker.address.uri = uri;
    mqtt_cfg.credentials.client_id = clientId;
    
    if (strlen(Config::MQTT_USER) > 0) {
        mqtt_cfg.credentials.username = Config::MQTT_USER;
        mqtt_cfg.credentials.authentication.password = Config::MQTT_PASS;
    }
    
    mqtt_cfg.session.last_will.topic = statusTopic;
    mqtt_cfg.session.last_will.msg = "offline";
    mqtt_cfg.session.last_will.qos = 1;
    mqtt_cfg.session.last_will.retain = true;
    
    mqtt_cfg.buffer.size = 2048;
    mqtt_cfg.network.reconnect_timeout_ms = 10000;  // Wait 10s between reconnect attempts
    mqtt_cfg.network.disable_auto_reconnect = false;
    
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
    
    // Subscribe to LED control
    snprintf(topic, sizeof(topic), "%s/led/set", baseTopic);
    esp_mqtt_client_subscribe(mqtt_client, topic, 1);
    ESP_LOGI(TAG, "Subscribed to: %s", topic);
    
    // Subscribe to RGB control
    snprintf(topic, sizeof(topic), "%s/led/rgb/set", baseTopic);
    esp_mqtt_client_subscribe(mqtt_client, topic, 1);
    ESP_LOGI(TAG, "Subscribed to: %s", topic);
}

void MQTTManager::publishAutoDiscovery() {
    if (!mqtt_client || !connected) return;
    
    ESP_LOGI(TAG, "Publishing Home Assistant Auto Discovery...");
    
    publishSwitchDiscovery();
    publishSensorDiscovery();
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
    getDiscoveryTopic("light", "led", discoveryTopic, sizeof(discoveryTopic));
    
    // Use smaller buffer - simplified payload
    DynamicJsonDocument doc(768);
    
    char uniqueId[64];
    snprintf(uniqueId, sizeof(uniqueId), "%s_led", deviceId);
    doc["uniq_id"] = uniqueId;
    doc["name"] = "LED";
    
    char stateTopic[256];
    snprintf(stateTopic, sizeof(stateTopic), "%s/led/state", baseTopic);
    doc["stat_t"] = stateTopic;
    
    char commandTopic[256];
    snprintf(commandTopic, sizeof(commandTopic), "%s/led/set", baseTopic);
    doc["cmd_t"] = commandTopic;
    
    // RGB support
    char rgbStateTopic[256];
    snprintf(rgbStateTopic, sizeof(rgbStateTopic), "%s/led/rgb/state", baseTopic);
    doc["rgb_stat_t"] = rgbStateTopic;
    
    char rgbCommandTopic[256];
    snprintf(rgbCommandTopic, sizeof(rgbCommandTopic), "%s/led/rgb/set", baseTopic);
    doc["rgb_cmd_t"] = rgbCommandTopic;
    
    doc["pl_on"] = "ON";
    doc["pl_off"] = "OFF";
    doc["opt"] = false;
    
    JsonArray modes = doc.createNestedArray("sup_clrm");
    modes.add("rgb");
    
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
    // TODO: Publish temperature and other sensor discoveries
    ESP_LOGI(TAG, "Sensor discovery (placeholder)");
}

void MQTTManager::publishButtonDiscovery() {
    // TODO: Publish button discoveries if needed
    ESP_LOGI(TAG, "Button discovery (placeholder)");
}

void MQTTManager::publishDeviceState() {
    if (!mqtt_client || !connected) return;
    
    char statusTopic[256];
    char baseTopic[128];
    getBaseTopic(baseTopic, sizeof(baseTopic));
    snprintf(statusTopic, sizeof(statusTopic), "%s/status", baseTopic);
    
    esp_mqtt_client_publish(mqtt_client, statusTopic, "online", 0, 1, true);
}

void MQTTManager::publishFanSpeed(int pwmDuty) {
    if (!mqtt_client || !connected) return;
    
    char topic[256];
    char baseTopic[128];
    getBaseTopic(baseTopic, sizeof(baseTopic));
    snprintf(topic, sizeof(topic), "%s/fan/speed", baseTopic);
    
    char payload[32];
    snprintf(payload, sizeof(payload), "%d", pwmDuty);
    
    esp_mqtt_client_publish(mqtt_client, topic, payload, 0, 0, false);
}

void MQTTManager::publishTemperature(float tempCelsius) {
    if (!mqtt_client || !connected) return;
    
    char topic[256];
    char baseTopic[128];
    getBaseTopic(baseTopic, sizeof(baseTopic));
    snprintf(topic, sizeof(topic), "%s/temperature", baseTopic);
    
    char payload[32];
    snprintf(payload, sizeof(payload), "%.2f", tempCelsius);
    
    esp_mqtt_client_publish(mqtt_client, topic, payload, 0, 0, false);
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
