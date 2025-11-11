#pragma once
#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

class BluetoothProxyManager {
public:
    BluetoothProxyManager();
    bool begin(const char* deviceName);
    void stop();
    bool isRunning();
    void setDeviceName(const char* name);
    String getDeviceName();
    uint16_t getConnectionCount();
    
private:
    BLEServer* pServer;
    bool running;
    String deviceName;
    uint16_t connectionCount;
    
    class ServerCallbacks : public BLEServerCallbacks {
        BluetoothProxyManager* manager;
    public:
        ServerCallbacks(BluetoothProxyManager* mgr) : manager(mgr) {}
        void onConnect(BLEServer* pServer);
        void onDisconnect(BLEServer* pServer);
    };
};
