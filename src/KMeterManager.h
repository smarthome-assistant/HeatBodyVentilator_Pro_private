#ifndef KMETER_MANAGER_H
#define KMETER_MANAGER_H

#include "driver/i2c.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <stdint.h>

class KMeterManager {
private:
    i2c_port_t i2c_port;
    bool initialized;
    int64_t lastReadTime;
    float currentTempCelsius;
    float currentTempFahrenheit;
    float internalTempCelsius;
    uint8_t errorStatus;
    
    // Konfiguration
    uint8_t i2cAddress;
    uint8_t sdaPin;
    uint8_t sclPin;
    uint32_t i2cSpeed;
    int64_t readInterval;
    
    // I2C Helper functions
    esp_err_t i2c_read_register(uint8_t reg, uint8_t* data, size_t len);
    esp_err_t i2c_write_register(uint8_t reg, uint8_t data);
    esp_err_t i2c_scan();
    bool diagnoseSensor();  // Comprehensive sensor diagnostics
    
public:
    KMeterManager();
    
    // Initialisierung
    bool begin(uint8_t addr = 0x66, uint8_t sda = 21, uint8_t scl = 22, uint32_t speed = 100000L);
    
    // Sensor-Operationen
    void update();
    bool isInitialized() const { return initialized; }
    
    // Temperatur-Daten
    float getTemperatureCelsius() const { return currentTempCelsius; }
    float getTemperatureFahrenheit() const { return currentTempFahrenheit; }
    float getInternalTemperature() const { return internalTempCelsius; }
    uint8_t getErrorStatus() const { return errorStatus; }
    
    // Status
    bool isReady() const { return errorStatus == 0; }
    const char* getStatusString() const;
    
    // Konfiguration
    void setReadInterval(int64_t interval) { readInterval = interval; }
    int64_t getReadInterval() const { return readInterval; }
    
    // I2C Konfiguration
    bool setI2CAddress(uint8_t addr);
    uint8_t getI2CAddress() const { return i2cAddress; }
    
    // Firmware Info
    uint8_t getFirmwareVersion();
};

#endif // KMETER_MANAGER_H
