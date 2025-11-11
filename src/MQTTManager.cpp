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
      autoDiscoveryPublished(false), connected(false), ledCallback(nullptr) {
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

void MQTTManager::setLEDCallback(void (*callback)(bool state)) {
    ledCallback = callback;
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
    char macAddr[32];
    char localIP[16];
    
    getDeviceId(deviceId, sizeof(deviceId));
    getHomeAssistantBaseTopic(baseTopic, sizeof(baseTopic));
    getDiscoveryTopic("switch", "led", discoveryTopic, sizeof(discoveryTopic));
    getMacAddress(macAddr, sizeof(macAddr));
    wifi.getLocalIP(localIP, sizeof(localIP));
    
    DynamicJsonDocument doc(1024);
    
    char uniqueId[64];
    snprintf(uniqueId, sizeof(uniqueId), "%s_led", deviceId);
    doc["unique_id"] = uniqueId;
    
    char name[128];
    snprintf(name, sizeof(name), "%s LED", Config::DEVICE_NAME);
    doc["name"] = name;
    
    char stateTopic[256];
    snprintf(stateTopic, sizeof(stateTopic), "%s/led/state", baseTopic);
    doc["state_topic"] = stateTopic;
    
    char commandTopic[256];
    snprintf(commandTopic, sizeof(commandTopic), "%s/led/set", baseTopic);
    doc["command_topic"] = commandTopic;
    
    doc["payload_on"] = "ON";
    doc["payload_off"] = "OFF";
    doc["optimistic"] = false;
    doc["retain"] = true;
    
    JsonObject device = doc.createNestedObject("device");
    device["identifiers"][0] = deviceId;
    device["name"] = Config::DEVICE_NAME;
    device["model"] = DEVICE_MODEL;
    device["manufacturer"] = DEVICE_MANUFACTURER;
    device["sw_version"] = Config::FIRMWARE_VERSION;
    
    char configUrl[64];
    snprintf(configUrl, sizeof(configUrl), "http://%s", localIP);
    device["configuration_url"] = configUrl;
    
    char payload[1024];
    serializeJson(doc, payload, sizeof(payload));
    
    esp_mqtt_client_publish(mqtt_client, discoveryTopic, payload, 0, 1, true);
    ESP_LOGI(TAG, "Published LED switch discovery");
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
