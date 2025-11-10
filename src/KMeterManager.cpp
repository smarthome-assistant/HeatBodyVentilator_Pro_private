#include "KMeterManager.h"

KMeterManager::KMeterManager() {
    initialized = false;
    lastReadTime = 0;
    currentTempCelsius = 0.0;
    currentTempFahrenheit = 0.0;
    internalTempCelsius = 0.0;
    errorStatus = 255;
    
    i2cAddress = 0x66;
    sdaPin = 26;
    sclPin = 32;
    i2cSpeed = 100000L;
    readInterval = 5000;
}

bool KMeterManager::begin(uint8_t addr, uint8_t sda, uint8_t scl, uint32_t speed) {
    Serial.println("DEBUG: KMeterManager::begin() called");
    Serial.printf("DEBUG: Attempting I2C connection - Addr: 0x%02X, SDA: %d, SCL: %d, Speed: %lu\n", 
                  addr, sda, scl, speed);
    
    i2cAddress = addr;
    sdaPin = sda;
    sclPin = scl;
    i2cSpeed = speed;
    
    Wire.begin(sdaPin, sclPin);
    Wire.setClock(i2cSpeed);
    delay(100);
    
    Serial.println("DEBUG: Scanning I2C bus...");
    Wire.beginTransmission(i2cAddress);
    uint8_t error = Wire.endTransmission();
    
    if (error == 0) {
        Serial.printf("DEBUG: I2C device found at address 0x%02X\n", i2cAddress);
    } else {
        Serial.printf("DEBUG: No I2C device found at address 0x%02X (error: %d)\n", i2cAddress, error);
        Serial.println("DEBUG: Scanning all I2C addresses...");
        
        int deviceCount = 0;
        for (uint8_t address = 1; address < 127; address++) {
            Wire.beginTransmission(address);
            uint8_t scanError = Wire.endTransmission();
            if (scanError == 0) {
                Serial.printf("DEBUG: I2C device found at address 0x%02X\n", address);
                deviceCount++;
            }
        }
        
        if (deviceCount == 0) {
            Serial.println("DEBUG: No I2C devices found on the bus!");
        } else {
            Serial.printf("DEBUG: Found %d I2C device(s)\n", deviceCount);
        }
    }
    
    Serial.println("DEBUG: Initializing KMeter-ISO sensor...");
    if (kmeter.begin(&Wire, i2cAddress, sdaPin, sclPin, i2cSpeed)) {
        initialized = true;
        Serial.printf("KMeter-ISO initialized successfully on I2C address 0x%02X\n", i2cAddress);
        
        uint8_t fwVersion = kmeter.getFirmwareVersion();
        Serial.printf("DEBUG: KMeter-ISO Firmware Version: %d\n", fwVersion);
        
        update();
        return true;
    } else {
        initialized = false;
        Serial.printf("Failed to initialize KMeter-ISO on I2C address 0x%02X\n", i2cAddress);
        
        Serial.println("DEBUG: Troubleshooting steps:");
        Serial.println("1. Check if KMeter-ISO module is properly connected");
        Serial.println("2. Verify SDA (Pin G25) and SCL (Pin G21) connections");
        Serial.println("3. Check if I2C address is correct (default: 0x66)");
        Serial.println("4. Ensure proper power supply to the module");
        
        return false;
    }
}

void KMeterManager::update() {
    if (!initialized) {
        return;
    }
    
    unsigned long currentTime = millis();
    if (currentTime - lastReadTime < readInterval) {
        return;
    }
    
    errorStatus = kmeter.getReadyStatus();
    
    if (errorStatus == 0) {
        int32_t tempC = kmeter.getCelsiusTempValue();
        int32_t tempF = kmeter.getFahrenheitTempValue();
        int32_t internalC = kmeter.getInternalCelsiusTempValue();
        
        currentTempCelsius = (float)tempC / 100.0;
        currentTempFahrenheit = (float)tempF / 100.0;
        internalTempCelsius = (float)internalC / 100.0;
        
        Serial.printf("KMeter: %.2f°C / %.2f°F\n", currentTempCelsius, currentTempFahrenheit);
    }
    
    lastReadTime = currentTime;
}

String KMeterManager::getStatusString() const {
    if (!initialized) return "Not Initialized";
    
    switch (errorStatus) {
        case 0: return "Ready";
        case 1: return "Sensor Error";
        case 2: return "Communication Error";
        case 3: return "Data Not Ready";
        default: return "Unknown Error (" + String(errorStatus) + ")";
    }
}

bool KMeterManager::setI2CAddress(uint8_t addr) {
    if (!initialized) return false;
    
    if (kmeter.setI2CAddress(addr)) {
        i2cAddress = addr;
        Serial.printf("KMeter-ISO I2C address changed to 0x%02X\n", addr);
        return true;
    } else {
        Serial.printf("Failed to change KMeter-ISO I2C address to 0x%02X\n", addr);
        return false;
    }
}

uint8_t KMeterManager::getFirmwareVersion() {
    if (!initialized) return 0;
    return kmeter.getFirmwareVersion();
}