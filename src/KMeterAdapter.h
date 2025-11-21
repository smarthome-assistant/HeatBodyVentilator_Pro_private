/**
 * ============================================================================
 * Adapter-Klasse: KMeterManager Interface für KMeterIsoComponent
 * ============================================================================
 * 
 * Diese Klasse ermöglicht es, den neuen KMeterIsoComponent (Arduino-basiert)
 * mit dem bestehenden ServerManager zu verwenden, ohne ServerManager.cpp
 * umschreiben zu müssen.
 * 
 * Verwendung:
 *   KMeterIsoComponent kmeterIso;  // Arduino-basiert
 *   kmeterIso.begin(...);
 *   
 *   KMeterAdapter adapter(&kmeterIso);
 *   serverManager.setKMeterAdapter(&adapter);
 */

#ifndef KMETER_ADAPTER_H
#define KMETER_ADAPTER_H

#include "KMeterIsoComponent.h"

/**
 * @brief Adapter-Klasse die KMeterManager-Interface emuliert
 * 
 * Diese Klasse delegiert alle Aufrufe an einen KMeterIsoComponent
 * und bietet das gleiche Interface wie der alte KMeterManager.
 */
class KMeterAdapter {
private:
    KMeterIsoComponent* component;
    
public:
    explicit KMeterAdapter(KMeterIsoComponent* comp) : component(comp) {}
    
    // Interface-Kompatibilität zu altem KMeterManager
    bool begin(uint8_t addr = 0x66, uint8_t sda = 26, uint8_t scl = 32, uint32_t speed = 100000L) {
        // Sensor ist bereits in main.cpp initialisiert, nichts tun
        return component != nullptr && component->isInitialized();
    }
    
    void update() {
        if (component) component->update();
    }
    
    bool isInitialized() const {
        return component != nullptr && component->isInitialized();
    }
    
    float getTemperatureCelsius() const {
        return component ? component->getTemperatureCelsius() : 0.0f;
    }
    
    float getTemperatureFahrenheit() const {
        return component ? component->getTemperatureFahrenheit() : 0.0f;
    }
    
    float getInternalTemperature() const {
        return component ? component->getInternalTemperature() : 0.0f;
    }
    
    uint8_t getErrorStatus() const {
        return component ? component->getErrorStatus() : 255;
    }
    
    bool isReady() const {
        return component ? component->isReady() : false;
    }
    
    const char* getStatusString() const {
        return component ? component->getStatusString() : "No Sensor";
    }
    
    void setReadInterval(int64_t interval) {
        if (component) component->setReadInterval(interval / 1000);  // us -> ms
    }
    
    int64_t getReadInterval() const {
        return component ? (component->getReadInterval() * 1000) : 0;  // ms -> us
    }
    
    bool setI2CAddress(uint8_t addr) {
        // Nicht unterstützt im Arduino-Mode
        return false;
    }
    
    uint8_t getI2CAddress() const {
        return component ? component->getI2CAddress() : 0;
    }
    
    uint8_t getFirmwareVersion() {
        return component ? component->getFirmwareVersion() : 0;
    }
};

#endif // KMETER_ADAPTER_H
