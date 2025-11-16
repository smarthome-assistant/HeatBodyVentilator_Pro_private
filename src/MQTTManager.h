#pragma once
#include "mqtt_client.h"
#include "esp_log.h"
#include "ArduinoJson.h"
#include "Config.h"

class MQTTManager {
public:
    MQTTManager();
    void begin();
    void loop();
    bool isConnected();
    void disconnect();
    void reconnect();  // Restart MQTT with new settings
    
    void publishAutoDiscovery();
    void publishDeviceState();
    void publishLEDState(bool isOn, uint8_t r, uint8_t g, uint8_t b);
    
    void publishFanSpeed(int pwmDuty);
    void publishTemperature(float tempCelsius);
    void publishFanControlState();
    
    void publishSensorData(const char* sensor, float value, const char* unit = "");
    void setLEDCallback(void (*callback)(bool state));
    void setLEDColorCallback(void (*callback)(uint8_t r, uint8_t g, uint8_t b));
    
private:
    esp_mqtt_client_handle_t mqtt_client;
    int64_t lastReconnectAttempt;
    int64_t lastHeartbeat;
    bool autoDiscoveryPublished;
    bool connected;
    void (*ledCallback)(bool state);
    void (*ledColorCallback)(uint8_t r, uint8_t g, uint8_t b);
    
    bool connect();
    void publishDeviceInfo();
    void publishSwitchDiscovery();
    void publishSensorDiscovery();
    void publishControlDiscovery();
    void publishButtonDiscovery();
    void subscribeToCommands();
    
    void getDeviceId(char* buf, size_t len);
    void getMacAddress(char* buf, size_t len);
    void getBaseTopic(char* buf, size_t len);
    void getDiscoveryTopic(const char* component, const char* objectId, char* buf, size_t len);
    void getHomeAssistantBaseTopic(char* buf, size_t len);
    
    static void mqtt_event_handler(void *handler_args, esp_event_base_t base, 
                                   int32_t event_id, void *event_data);
};

extern MQTTManager mqttManager;
