#include "KMeterIsoComponent.h"

static const char *TAG = "KMeterISO";

KMeterIsoComponent::KMeterIsoComponent() {
    initialized = false;
    i2cAddress = 0x66;
    sdaPin = 26;
    sclPin = 32;

    currentTempCelsius = 0.0f;
    currentTempFahrenheit = 0.0f;
    internalTempCelsius = 0.0f;
    errorStatus = 255;

    lastReadTime = 0;
    readInterval = 1000;  // default to 1s updates; configurable via setReadInterval
}

void KMeterIsoComponent::scanI2CBus() {
    ESP_LOGI(TAG, "Scanning I2C bus on SDA=%d, SCL=%d...", sdaPin, sclPin);

    int devicesFound = 0;
    for (uint8_t address = 1; address < 127; address++) {
        Wire.beginTransmission(address);
        uint8_t error = Wire.endTransmission();

        if (error == 0) {
            ESP_LOGI(TAG, "  I2C device found at address 0x%02X", address);
            devicesFound++;
        }
        delay(5);
    }

    if (devicesFound == 0) {
        ESP_LOGW(TAG, "No I2C devices detected. Check wiring and power.");
    } else {
        ESP_LOGI(TAG, "Found %d I2C device(s)", devicesFound);
    }
}

bool KMeterIsoComponent::begin(uint8_t addr, uint8_t sda, uint8_t scl, uint32_t speed) {
    ESP_LOGI(TAG, "KMeterIsoComponent::begin() called");
    ESP_LOGI(TAG, "Config: addr=0x%02X, SDA=%d, SCL=%d, speed=%u Hz", addr, sda, scl, (unsigned int)speed);

    i2cAddress = addr;
    sdaPin = sda;
    sclPin = scl;

    Wire.begin(sdaPin, sclPin);
    Wire.setClock(speed);
    delay(100);

    Wire.beginTransmission(i2cAddress);
    uint8_t error = Wire.endTransmission();
    if (error != 0) {
        ESP_LOGE(TAG, "No device ACK at 0x%02X (error %d)", i2cAddress, error);
        scanI2CBus();
        logTroubleshootingHints();
        initialized = false;
        errorStatus = 2;
        return false;
    }

    ESP_LOGI(TAG, "Initializing KMeter-ISO via M5Unit library...");
    if (sensor.begin(&Wire, i2cAddress, sdaPin, sclPin, speed)) {
        initialized = true;
        errorStatus = 0;

        uint8_t fwVersion = sensor.getFirmwareVersion();
        ESP_LOGI(TAG, "KMeter-ISO firmware version: %u", fwVersion);

        forceUpdate();
        ESP_LOGI(TAG, "Initial reading: %.2f°C (status=%s)", currentTempCelsius, getStatusString());
        return true;
    }

    ESP_LOGE(TAG, "Failed to initialize KMeter-ISO at 0x%02X", i2cAddress);
    logTroubleshootingHints();
    initialized = false;
    errorStatus = 2;
    return false;
}

void KMeterIsoComponent::update() {
    if (!initialized) {
        return;
    }

    unsigned long now = millis();
    if (now - lastReadTime < readInterval) {
        return;
    }

    readSensorValues();
}

void KMeterIsoComponent::forceUpdate() {
    if (!initialized) {
        return;
    }

    readSensorValues();
}

void KMeterIsoComponent::readSensorValues() {
    lastReadTime = millis();

    errorStatus = sensor.getReadyStatus();
    if (errorStatus != 0) {
        ESP_LOGW(TAG, "Sensor not ready (status=%d)", errorStatus);
        return;
    }

    int32_t rawC = sensor.getCelsiusTempValue();
    int32_t rawF = sensor.getFahrenheitTempValue();
    int32_t rawInternalC = sensor.getInternalCelsiusTempValue();

    currentTempCelsius = static_cast<float>(rawC) / 100.0f;
    currentTempFahrenheit = static_cast<float>(rawF) / 100.0f;
    internalTempCelsius = static_cast<float>(rawInternalC) / 100.0f;

    ESP_LOGD(TAG, "KMeter reading: %.2f°C / %.2f°F (internal %.2f°C)",
             currentTempCelsius, currentTempFahrenheit, internalTempCelsius);
}

void KMeterIsoComponent::logTroubleshootingHints() const {
    ESP_LOGE(TAG, "Troubleshooting tips:");
    ESP_LOGE(TAG, "  1. Ensure KMeter-ISO is powered");
    ESP_LOGE(TAG, "  2. Verify SDA (GPIO %d) and SCL (GPIO %d) wiring", sdaPin, sclPin);
    ESP_LOGE(TAG, "  3. Confirm I2C address 0x%02X is correct", i2cAddress);
    ESP_LOGE(TAG, "  4. Check pull-up resistors on SDA/SCL");
}

const char* KMeterIsoComponent::getStatusString() const {
    if (!initialized) return "Not Initialized";

    switch (errorStatus) {
        case 0: return "Ready";
        case 1: return "Sensor Error";
        case 2: return "Communication Error";
        case 3: return "Data Not Ready";
        default: {
            static char buf[32];
            snprintf(buf, sizeof(buf), "Unknown Error (%d)", errorStatus);
            return buf;
        }
    }
}

uint8_t KMeterIsoComponent::getFirmwareVersion() {
    if (!initialized) return 0;
    return sensor.getFirmwareVersion();
}
