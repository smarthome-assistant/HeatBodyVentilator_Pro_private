#ifndef KMETER_ISO_COMPONENT_H
#define KMETER_ISO_COMPONENT_H

#include <Arduino.h>
#include <Wire.h>
#include <M5UnitKmeterISO.h>
#include "esp_log.h"

/**
 * @brief Arduino-Wrapper für M5Stack KMeterISO Library
 * 
 * Diese Klasse kapselt die M5Unit-KMeterISO Arduino-Library und bietet
 * ein Interface, das kompatibel zum bisherigen KMeterManager ist.
 * 
 * Verwendung:
 *   KMeterIsoComponent kmeter;
 *   kmeter.begin(0x66, 26, 32);  // addr, SDA, SCL
 *   kmeter.update();
 *   float temp = kmeter.getTemperatureCelsius();
 */
class KMeterIsoComponent {
private:
    M5UnitKmeterISO sensor;
    bool initialized;
    uint8_t i2cAddress;
    uint8_t sdaPin;
    uint8_t sclPin;
    
    // Cached values
    float currentTempCelsius;
    float currentTempFahrenheit;
    float internalTempCelsius;
    uint8_t errorStatus;
    
    // Timing
    unsigned long lastReadTime;
    unsigned long readInterval;  // in milliseconds
    void readSensorValues();
    void logTroubleshootingHints() const;
    
public:
    KMeterIsoComponent();
    
    /**
     * @brief Scannt den I2C-Bus und listet alle gefundenen Geräte auf
     */
    void scanI2CBus();
    
    /**
     * @brief Initialisiert den KMeterISO-Sensor mit der Arduino-Library
     * @param addr I2C-Adresse (Standard: 0x66)
     * @param sda SDA-Pin (M5Stack Atom: 26)
     * @param scl SCL-Pin (M5Stack Atom: 32)
     * @param speed I2C-Geschwindigkeit (Standard: 100000)
     * @return true wenn erfolgreich, false bei Fehler
     */
    bool begin(uint8_t addr = 0x66, uint8_t sda = 26, uint8_t scl = 32, uint32_t speed = 100000L);
    
    /**
     * @brief Liest aktuelle Sensor-Daten (nicht-blockierend, respektiert readInterval)
     * Rufe diese Funktion regelmäßig in loop() auf
     */
    void update();
    
    /**
     * @brief Erzwingt sofortiges Lesen der Sensor-Daten (blockierend)
     */
    void forceUpdate();
    
    // Status
    bool isInitialized() const { return initialized; }
    bool isReady() const { return errorStatus == 0; }
    const char* getStatusString() const;
    
    // Temperatur-Daten
    float getTemperatureCelsius() const { return currentTempCelsius; }
    float getTemperatureFahrenheit() const { return currentTempFahrenheit; }
    float getInternalTemperature() const { return internalTempCelsius; }
    uint8_t getErrorStatus() const { return errorStatus; }
    
    // Konfiguration
    void setReadInterval(unsigned long interval) { readInterval = interval; }
    unsigned long getReadInterval() const { return readInterval; }
    uint8_t getI2CAddress() const { return i2cAddress; }
    
    // Firmware Info
    uint8_t getFirmwareVersion();
};

#endif // KMETER_ISO_COMPONENT_H
