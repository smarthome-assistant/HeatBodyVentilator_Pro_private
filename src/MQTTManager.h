#pragma once
#include <WiFiClient.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
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
    
    void publishSensorData(const String& sensor, float value, const String& unit = "");
    void handleIncomingMessage(char* topic, byte* payload, unsigned int length);
    void setLEDCallback(void (*callback)(bool state));
    
private:
    WiFiClient wifiClient;
    PubSubClient mqttClient;
    unsigned long lastReconnectAttempt;
    unsigned long lastHeartbeat;
    bool autoDiscoveryPublished;
    void (*ledCallback)(bool state);
    
    bool connect();
    void publishDeviceInfo();
    void publishSwitchDiscovery();
    void publishSensorDiscovery();
    void publishButtonDiscovery();
    void subscribeToCommands();
    
    String getDeviceId();
    String getMacAddress();
    String getBaseTopic();
    String getDiscoveryTopic(const String& component, const String& objectId);
    String getHomeAssistantBaseTopic();
};

extern MQTTManager mqttManager;
