#include "MQTTManager.h"
#include "LEDManager.h"
#include <WiFi.h>

const String DEVICE_MODEL = "M5Stack Atom";
const String DEVICE_MANUFACTURER = "SmartHome-Assistant.info";

MQTTManager mqttManager;

MQTTManager::MQTTManager() 
    : mqttClient(wifiClient), lastReconnectAttempt(0), lastHeartbeat(0), autoDiscoveryPublished(false), ledCallback(nullptr) {
}

void MQTTManager::begin() {
    if (strlen(Config::MQTT_SERVER) == 0) {
        Serial.println("MQTT: No server configured");
        return;
    }
    
    mqttClient.setServer(Config::MQTT_SERVER, Config::MQTT_PORT);
    mqttClient.setBufferSize(2048);
    mqttClient.setCallback([this](char* topic, byte* payload, unsigned int length) {
        this->handleIncomingMessage(topic, payload, length);
    });
    
    Serial.println("MQTT: Initialized with server " + String(Config::MQTT_SERVER) + ":" + String(Config::MQTT_PORT));
}

void MQTTManager::loop() {
    if (strlen(Config::MQTT_SERVER) == 0 || WiFi.status() != WL_CONNECTED) {
        return;
    }
    
    if (!mqttClient.connected()) {
        unsigned long now = millis();
        if (now - lastReconnectAttempt > 30000) {
            lastReconnectAttempt = now;
            if (connect()) {
                Serial.println("MQTT: Connected successfully");
                subscribeToCommands();
                publishAutoDiscovery();
                publishDeviceState();
                autoDiscoveryPublished = true;
            }
        }
    } else {
        mqttClient.loop();
        
        unsigned long now = millis();
        if (now - lastHeartbeat > 30000) {
            lastHeartbeat = now;
            publishDeviceState();
        }
    }
}

bool MQTTManager::isConnected() {
    return mqttClient.connected();
}

void MQTTManager::disconnect() {
    if (mqttClient.connected()) {
        String statusTopic = getBaseTopic() + "/status";
        mqttClient.publish(statusTopic.c_str(), "offline", true);
        mqttClient.disconnect();
    }
    autoDiscoveryPublished = false;
}

void MQTTManager::setLEDCallback(void (*callback)(bool state)) {
    ledCallback = callback;
}

bool MQTTManager::connect() {
    String clientId = "ESP32-" + getDeviceId();
    String statusTopic = getBaseTopic() + "/status";
    
    bool connected;
    if (strlen(Config::MQTT_USER) > 0) {
        connected = mqttClient.connect(clientId.c_str(), Config::MQTT_USER, Config::MQTT_PASS, 
                                     statusTopic.c_str(), 1, true, "offline");
    } else {
        connected = mqttClient.connect(clientId.c_str(), statusTopic.c_str(), 1, true, "offline");
    }
    
    if (connected) {
        mqttClient.publish(statusTopic.c_str(), "online", true);
        Serial.println("MQTT: Connected as " + clientId);
    } else {
        Serial.println("MQTT: Connection failed, rc=" + String(mqttClient.state()));
    }
    
    return connected;
}

void MQTTManager::publishAutoDiscovery() {
    if (!mqttClient.connected()) return;
    
    Serial.println("MQTT: Publishing Home Assistant Auto Discovery...");
    
    publishDeviceInfo();
    
    publishSwitchDiscovery();
    publishSensorDiscovery();
    publishButtonDiscovery();
    
    Serial.println("MQTT: Auto Discovery published");
}

void MQTTManager::publishDeviceInfo() {
    String deviceId = getDeviceId();
    String baseTopic = getBaseTopic();
    
    DynamicJsonDocument device(1024);
    device["identifiers"][0] = deviceId;
    device["name"] = Config::DEVICE_NAME;
    device["model"] = DEVICE_MODEL;
    device["manufacturer"] = DEVICE_MANUFACTURER;
    device["sw_version"] = Config::FIRMWARE_VERSION;
    device["hw_version"] = "1.0";
    device["configuration_url"] = "http://" + WiFi.localIP().toString();
    
    JsonObject connections = device.createNestedObject("connections");
    JsonArray wifi = connections.createNestedArray("wifi");
    wifi.add(getMacAddress());
}

void MQTTManager::publishSwitchDiscovery() {
    String deviceId = getDeviceId();
    String baseTopic = getHomeAssistantBaseTopic();
    
    DynamicJsonDocument ledSwitch(512);
    ledSwitch["unique_id"] = deviceId + "_led";
    ledSwitch["name"] = String(Config::DEVICE_NAME) + " LED";
    ledSwitch["state_topic"] = baseTopic + "/led/state";
    ledSwitch["command_topic"] = baseTopic + "/led/set";
    ledSwitch["payload_on"] = "ON";
    ledSwitch["payload_off"] = "OFF";
    ledSwitch["optimistic"] = false;
    ledSwitch["retain"] = true;
    
    JsonObject device = ledSwitch.createNestedObject("device");
    device["identifiers"][0] = deviceId;
    device["name"] = Config::DEVICE_NAME;
    device["model"] = DEVICE_MODEL;
    device["manufacturer"] = DEVICE_MANUFACTURER;
    device["sw_version"] = Config::FIRMWARE_VERSION;
    device["configuration_url"] = "http://" + WiFi.localIP().toString();
    
    String discoveryTopic = getDiscoveryTopic("switch", "led");
    String payload;
    serializeJson(ledSwitch, payload);
    
    Serial.println("MQTT: Publishing to topic: " + discoveryTopic);
    Serial.println("MQTT: Payload: " + payload);
    
    bool result = mqttClient.publish(discoveryTopic.c_str(), payload.c_str(), true);
    Serial.println("MQTT: Publish result: " + String(result ? "SUCCESS" : "FAILED"));
    
    Serial.println("MQTT: Published LED switch discovery");
}

void MQTTManager::publishSensorDiscovery() {
    String deviceId = getDeviceId();
    String baseTopic = getHomeAssistantBaseTopic();
    
    DynamicJsonDocument wifiSensor(512);
    wifiSensor["unique_id"] = deviceId + "_wifi_signal";
    wifiSensor["name"] = String(Config::DEVICE_NAME) + " WiFi";
    wifiSensor["state_topic"] = baseTopic + "/sensor/wifi_signal";
    wifiSensor["unit_of_measurement"] = "dBm";
    wifiSensor["device_class"] = "signal_strength";
    wifiSensor["state_class"] = "measurement";
    wifiSensor["entity_category"] = "diagnostic";
    
    JsonObject device = wifiSensor.createNestedObject("device");
    device["identifiers"][0] = deviceId;
    device["name"] = Config::DEVICE_NAME;
    device["model"] = DEVICE_MODEL;
    device["manufacturer"] = DEVICE_MANUFACTURER;
    device["sw_version"] = Config::FIRMWARE_VERSION;
    device["configuration_url"] = "http://" + WiFi.localIP().toString();
    
    String discoveryTopic = getDiscoveryTopic("sensor", "wifi_signal");
    String payload;
    serializeJson(wifiSensor, payload);
    
    Serial.println("MQTT: Publishing sensor to topic: " + discoveryTopic);
    bool result = mqttClient.publish(discoveryTopic.c_str(), payload.c_str(), true);
    Serial.println("MQTT: Sensor publish result: " + String(result ? "SUCCESS" : "FAILED"));
    
    DynamicJsonDocument uptimeSensor(512);
    uptimeSensor["unique_id"] = deviceId + "_uptime";
    uptimeSensor["name"] = String(Config::DEVICE_NAME) + " Uptime";
    uptimeSensor["state_topic"] = baseTopic + "/sensor/uptime";
    uptimeSensor["unit_of_measurement"] = "s";
    uptimeSensor["device_class"] = "duration";
    uptimeSensor["state_class"] = "total_increasing";
    uptimeSensor["entity_category"] = "diagnostic";
    
    JsonObject device2 = uptimeSensor.createNestedObject("device");
    device2["identifiers"][0] = deviceId;
    device2["name"] = Config::DEVICE_NAME;
    device2["model"] = DEVICE_MODEL;
    device2["manufacturer"] = DEVICE_MANUFACTURER;
    device2["sw_version"] = Config::FIRMWARE_VERSION;
    device2["configuration_url"] = "http://" + WiFi.localIP().toString();
    
    discoveryTopic = getDiscoveryTopic("sensor", "uptime");
    String uptimePayload;
    serializeJson(uptimeSensor, uptimePayload);
    
    Serial.println("MQTT: Publishing uptime sensor to topic: " + discoveryTopic);
    result = mqttClient.publish(discoveryTopic.c_str(), uptimePayload.c_str(), true);
    Serial.println("MQTT: Uptime sensor publish result: " + String(result ? "SUCCESS" : "FAILED"));
    
    DynamicJsonDocument fanSensor(512);
    fanSensor["unique_id"] = deviceId + "_fan_speed";
    fanSensor["name"] = String(Config::DEVICE_NAME) + " Lüfter";
    fanSensor["state_topic"] = baseTopic + "/sensor/fan_speed";
    fanSensor["unit_of_measurement"] = "%";
    fanSensor["icon"] = "mdi:fan";
    fanSensor["state_class"] = "measurement";
    
    JsonObject device3 = fanSensor.createNestedObject("device");
    device3["identifiers"][0] = deviceId;
    device3["name"] = Config::DEVICE_NAME;
    device3["model"] = DEVICE_MODEL;
    device3["manufacturer"] = DEVICE_MANUFACTURER;
    device3["sw_version"] = Config::FIRMWARE_VERSION;
    device3["configuration_url"] = "http://" + WiFi.localIP().toString();
    
    discoveryTopic = getDiscoveryTopic("sensor", "fan_speed");
    String fanPayload;
    serializeJson(fanSensor, fanPayload);
    
    Serial.println("MQTT: Publishing fan speed sensor to topic: " + discoveryTopic);
    result = mqttClient.publish(discoveryTopic.c_str(), fanPayload.c_str(), true);
    Serial.println("MQTT: Fan speed sensor publish result: " + String(result ? "SUCCESS" : "FAILED"));
    
    DynamicJsonDocument tempSensor(512);
    tempSensor["unique_id"] = deviceId + "_temperature";
    tempSensor["name"] = String(Config::DEVICE_NAME) + " Temperatur";
    tempSensor["state_topic"] = baseTopic + "/sensor/temperature";
    tempSensor["unit_of_measurement"] = "°C";
    tempSensor["device_class"] = "temperature";
    tempSensor["state_class"] = "measurement";
    
    JsonObject device4 = tempSensor.createNestedObject("device");
    device4["identifiers"][0] = deviceId;
    device4["name"] = Config::DEVICE_NAME;
    device4["model"] = DEVICE_MODEL;
    device4["manufacturer"] = DEVICE_MANUFACTURER;
    device4["sw_version"] = Config::FIRMWARE_VERSION;
    device4["configuration_url"] = "http://" + WiFi.localIP().toString();
    
    discoveryTopic = getDiscoveryTopic("sensor", "temperature");
    String tempPayload;
    serializeJson(tempSensor, tempPayload);
    
    Serial.println("MQTT: Publishing temperature sensor to topic: " + discoveryTopic);
    result = mqttClient.publish(discoveryTopic.c_str(), tempPayload.c_str(), true);
    Serial.println("MQTT: Temperature sensor publish result: " + String(result ? "SUCCESS" : "FAILED"));
    
    Serial.println("MQTT: Published sensor discoveries");
}

void MQTTManager::publishButtonDiscovery() {
    String deviceId = getDeviceId();
    String baseTopic = getHomeAssistantBaseTopic();
    
    DynamicJsonDocument restartButton(1024);
    restartButton["unique_id"] = deviceId + "_restart";
    restartButton["name"] = String(Config::DEVICE_NAME) + " Restart";
    restartButton["command_topic"] = baseTopic + "/button/restart";
    restartButton["device_class"] = "restart";
    restartButton["entity_category"] = "config";
    
    JsonObject device = restartButton.createNestedObject("device");
    device["identifiers"][0] = deviceId;
    device["name"] = Config::DEVICE_NAME;
    device["model"] = DEVICE_MODEL;
    device["manufacturer"] = DEVICE_MANUFACTURER;
    device["sw_version"] = Config::FIRMWARE_VERSION;
    device["configuration_url"] = "http://" + WiFi.localIP().toString();
    
    String discoveryTopic = getDiscoveryTopic("button", "restart");
    String payload;
    serializeJson(restartButton, payload);
    mqttClient.publish(discoveryTopic.c_str(), payload.c_str(), true);
    
    Serial.println("MQTT: Published button discoveries");
}

void MQTTManager::subscribeToCommands() {
    String baseTopic = getHomeAssistantBaseTopic();
    
    String ledTopic = baseTopic + "/led/set";
    mqttClient.subscribe(ledTopic.c_str());
    
    String restartTopic = baseTopic + "/button/restart";
    mqttClient.subscribe(restartTopic.c_str());
    
    Serial.println("MQTT: Subscribed to command topics");
}

void MQTTManager::publishDeviceState() {
    if (!mqttClient.connected()) return;
    
    String baseTopic = getHomeAssistantBaseTopic();
    
    int rssi = WiFi.RSSI();
    String wifiTopic = baseTopic + "/sensor/wifi_signal";
    mqttClient.publish(wifiTopic.c_str(), String(rssi).c_str());
    
    unsigned long uptime = millis() / 1000;
    String uptimeTopic = baseTopic + "/sensor/uptime";
    mqttClient.publish(uptimeTopic.c_str(), String(uptime).c_str());
    
    int currentPWM = ledcRead(0);
    publishFanSpeed(currentPWM);
    
    String statusTopic = baseTopic + "/status";
    mqttClient.publish(statusTopic.c_str(), "online", true);
}

void MQTTManager::publishFanSpeed(int pwmDuty) {
    if (!mqttClient.connected()) return;
    
    String baseTopic = getHomeAssistantBaseTopic();
    String fanTopic = baseTopic + "/sensor/fan_speed";
    
    float percentage = (pwmDuty / 255.0) * 100.0;
    
    mqttClient.publish(fanTopic.c_str(), String(percentage, 1).c_str());
    Serial.printf("MQTT: Published fan speed: %.1f%%\n", percentage);
}

void MQTTManager::publishTemperature(float tempCelsius) {
    if (!mqttClient.connected()) return;
    
    String baseTopic = getHomeAssistantBaseTopic();
    String tempTopic = baseTopic + "/sensor/temperature";
    
    mqttClient.publish(tempTopic.c_str(), String(tempCelsius, 1).c_str());
    Serial.printf("MQTT: Published temperature: %.1f°C\n", tempCelsius);
}

void MQTTManager::publishSensorData(const String& sensor, float value, const String& unit) {
    if (!mqttClient.connected()) return;
    
    String topic = getBaseTopic() + "/sensor/" + sensor;
    mqttClient.publish(topic.c_str(), String(value).c_str());
}

void MQTTManager::handleIncomingMessage(char* topic, byte* payload, unsigned int length) {
    String message;
    for (unsigned int i = 0; i < length; i++) {
        message += (char)payload[i];
    }
    
    String topicStr = String(topic);
    String baseTopic = getHomeAssistantBaseTopic();
    
    Serial.println("MQTT: Received message on " + topicStr + ": " + message);
    
    if (topicStr == baseTopic + "/led/set") {
        if (message == "ON") {
            if (ledCallback) {
                ledCallback(true);
            }
            mqttClient.publish((baseTopic + "/led/state").c_str(), "ON", true);
        } else if (message == "OFF") {
            if (ledCallback) {
                ledCallback(false);
            }
            mqttClient.publish((baseTopic + "/led/state").c_str(), "OFF", true);
        }
    }
    
    if (topicStr == baseTopic + "/button/restart") {
        Serial.println("MQTT: Restart requested via Home Assistant");
        delay(1000);
        ESP.restart();
    }
}

String MQTTManager::getDeviceId() {
    String mac = getMacAddress().substring(9);
    mac.replace(":", "_");
    return mac;
}

String MQTTManager::getMacAddress() {
    return WiFi.macAddress();
}

String MQTTManager::getBaseTopic() {
    if (strlen(Config::MQTT_TOPIC) > 0) {
        return String(Config::MQTT_TOPIC) + "/" + getDeviceId();
    }
    return "homeassistant/esp32/" + getDeviceId();
}

String MQTTManager::getHomeAssistantBaseTopic() {
    return "homeassistant/esp32/" + getDeviceId();
}

String MQTTManager::getDiscoveryTopic(const String& component, const String& objectId) {
    String nodeId = getDeviceId();
    return "homeassistant/" + component + "/" + nodeId + "/" + objectId + "/config";
}
