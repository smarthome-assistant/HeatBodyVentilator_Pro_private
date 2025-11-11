#include "BluetoothProxyManager.h"
#include "Config.h"

BluetoothProxyManager::BluetoothProxyManager() 
    : pServer(nullptr), running(false), connectionCount(0) {
}

bool BluetoothProxyManager::begin(const char* deviceName) {
    if (running) {
        Serial.println("Bluetooth Proxy already running");
        return true;
    }
    
    this->deviceName = String(deviceName);
    
    Serial.println("Initializing Bluetooth Proxy...");
    
    try {
        // Initialize BLE Device
        BLEDevice::init(deviceName);
        
        // Create BLE Server
        pServer = BLEDevice::createServer();
        pServer->setCallbacks(new ServerCallbacks(this));
        
        // Create BLE Service for Home Assistant
        BLEService *pService = pServer->createService("0000181a-0000-1000-8000-00805f9b34fb");
        
        // Create characteristic for advertising
        BLECharacteristic *pCharacteristic = pService->createCharacteristic(
            "00002a6e-0000-1000-8000-00805f9b34fb",
            BLECharacteristic::PROPERTY_READ | 
            BLECharacteristic::PROPERTY_NOTIFY
        );
        
        pCharacteristic->addDescriptor(new BLE2902());
        
        // Start the service
        pService->start();
        
        // Start advertising
        BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
        pAdvertising->addServiceUUID("0000181a-0000-1000-8000-00805f9b34fb");
        pAdvertising->setScanResponse(true);
        pAdvertising->setMinPreferred(0x06);
        pAdvertising->setMinPreferred(0x12);
        BLEDevice::startAdvertising();
        
        running = true;
        Serial.println("Bluetooth Proxy started successfully");
        Serial.printf("Device name: %s\n", deviceName);
        
        return true;
    } catch (const std::exception& e) {
        Serial.printf("ERROR: Failed to start Bluetooth Proxy: %s\n", e.what());
        running = false;
        return false;
    }
}

void BluetoothProxyManager::stop() {
    if (!running) {
        Serial.println("Bluetooth Proxy not running");
        return;
    }
    
    Serial.println("Stopping Bluetooth Proxy...");
    
    try {
        BLEDevice::deinit(true);
        pServer = nullptr;
        running = false;
        connectionCount = 0;
        
        Serial.println("Bluetooth Proxy stopped");
    } catch (const std::exception& e) {
        Serial.printf("ERROR: Failed to stop Bluetooth Proxy: %s\n", e.what());
    }
}

bool BluetoothProxyManager::isRunning() {
    return running;
}

void BluetoothProxyManager::setDeviceName(const char* name) {
    deviceName = String(name);
    if (running) {
        // Restart to apply new name
        stop();
        begin(name);
    }
}

String BluetoothProxyManager::getDeviceName() {
    return deviceName;
}

uint16_t BluetoothProxyManager::getConnectionCount() {
    return connectionCount;
}

// Server Callbacks Implementation
void BluetoothProxyManager::ServerCallbacks::onConnect(BLEServer* pServer) {
    manager->connectionCount++;
    Serial.printf("BT Client connected. Total connections: %d\n", manager->connectionCount);
}

void BluetoothProxyManager::ServerCallbacks::onDisconnect(BLEServer* pServer) {
    if (manager->connectionCount > 0) {
        manager->connectionCount--;
    }
    Serial.printf("BT Client disconnected. Total connections: %d\n", manager->connectionCount);
    
    // Restart advertising
    BLEDevice::startAdvertising();
    Serial.println("BT Advertising restarted");
}
