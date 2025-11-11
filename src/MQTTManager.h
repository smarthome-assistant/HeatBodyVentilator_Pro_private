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
    
    void publishAutoDiscovery();
    void publishDeviceState();
    
    void publishFanSpeed(int pwmDuty);
    void publishTemperature(float tempCelsius);
    
    void publishSensorData(const char* sensor, float value, const char* unit = "");
    void setLEDCallback(void (*callback)(bool state));
    
private:
    esp_mqtt_client_handle_t mqtt_client;
    int64_t lastReconnectAttempt;
    int64_t lastHeartbeat;
    bool autoDiscoveryPublished;
    bool connected;
    void (*ledCallback)(bool state);
    
    bool connect();
    void publishDeviceInfo();
    void publishSwitchDiscovery();
    void publishSensorDiscovery();
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
