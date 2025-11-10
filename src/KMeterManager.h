#ifndef KMETER_MANAGER_H
#define KMETER_MANAGER_H

#include <Arduino.h>
#include <Wire.h>
#include "M5UnitKmeterISO.h"

class KMeterManager {
private:
    M5UnitKmeterISO kmeter;
    bool initialized;
    unsigned long lastReadTime;
    float currentTempCelsius;
    float currentTempFahrenheit;
    float internalTempCelsius;
    uint8_t errorStatus;
    
    // Konfiguration
    uint8_t i2cAddress;
    uint8_t sdaPin;
    uint8_t sclPin;
    uint32_t i2cSpeed;
    unsigned long readInterval;
    
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
    String getStatusString() const;
    
    // Konfiguration
    void setReadInterval(unsigned long interval) { readInterval = interval; }
    unsigned long getReadInterval() const { return readInterval; }
    
    // I2C Konfiguration
    bool setI2CAddress(uint8_t addr);
    uint8_t getI2CAddress() const { return i2cAddress; }
    
    // Firmware Info
    uint8_t getFirmwareVersion();
};

#endif // KMETER_MANAGER_H
