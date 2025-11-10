#pragma once
#include <WebServer.h>
#include "Config.h"
#include "KMeterManager.h"

class ServerManager {
public:
    ServerManager();
    void begin();
    void handleClient();
    void initializePWM();
    void setPWMDuty(int duty);
    void updateSensors();
    KMeterManager* getKMeterManager() { return &kmeterManager; }
private:
    WebServer server;
    void setupRoutes();
    void handleRoot();
    void handleMain();
    void handleLogin();
    void handleNotFound();
    void handleSetting();
    void handleSettingWifi();
    void handleWifiConnect();
    
    // New API endpoints
    void handleDeviceName();
    void handleReboot();
    void handleFactoryReset();
    void handleChangePassword();
    void handleTempUnit();
    void handleToggleAP();
    void handleMQTTSettings();
    void handleMQTTTest();
    void handleMQTTStatus();
    void handleWiFiStatus();
    void handleWiFiScan();
    void handleWiFiDisconnect();
    void handleWiFiClear();
    void handleLEDToggle();
    void handleStatus();
    void handleGetSettings();
    void handleFirmwareUpdate();
    void handlePWMControl();
    void handlePWMStatus();
    void handleSystemInfo();
    void handleKMeterStatus();
    void handleKMeterConfig();
    void handleTempMapping();
    void handleTempMappingStatus();
    void handleAutoPWM();
    
    bool isAuthenticated();
    
    int mapTemperatureToPWM(float temperature);
    void updateAutoPWM();
    
    KMeterManager kmeterManager;
};
