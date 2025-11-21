#include "KMeterManager.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "KMeter";

// KMeter-ISO Register Adressen (basierend auf offizieller M5Stack Library)
#define KMETER_REG_TEMP_CELSIUS         0x00  // 4 bytes - int32_t
#define KMETER_REG_TEMP_FAHRENHEIT      0x04  // 4 bytes - int32_t
#define KMETER_REG_INTERNAL_TEMP        0x10  // 4 bytes - int32_t
#define KMETER_REG_STATUS               0x20  // 1 byte
#define KMETER_REG_FIRMWARE             0xFE  // 1 byte
#define KMETER_REG_I2C_ADDR             0xFF  // 1 byte

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
    i2cSpeed = 100000;
    readInterval = 5000000; // 5 seconds in microseconds
    i2c_port = I2C_NUM_0;
    
    // DEBUGGING: Test verschiedene I2C Geschwindigkeiten
    // Arduino verwendet standardmäßig 100kHz, aber ESP-IDF könnte anders sein
    ESP_LOGI(TAG, "NOTE: Testing with 100kHz I2C speed (Arduino default)");
}

esp_err_t KMeterManager::i2c_read_register(uint8_t reg, uint8_t* data, size_t len) {
    // EXACT Arduino Wire.h behavior replication:
    // 1. beginTransmission(addr) + write(reg) + endTransmission(false)
    // 2. requestFrom(addr, len) - reads len bytes with ACK, except last byte gets NACK
    
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    if (cmd == NULL) {
        return ESP_FAIL;
    }
    
    // Phase 1: Write register address
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (i2cAddress << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    
    // Phase 2: Repeated START + Read data (Arduino requestFrom behavior)
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (i2cAddress << 1) | I2C_MASTER_READ, true);
    
    // WICHTIG: Arduino requestFrom() liest BULK-Data, nicht byte-by-byte!
    // Verwende i2c_master_read() statt i2c_master_read_byte() für bessere Performance
    if (len > 1) {
        i2c_master_read(cmd, data, len - 1, I2C_MASTER_ACK);
        i2c_master_read_byte(cmd, &data[len - 1], I2C_MASTER_NACK);
    } else if (len == 1) {
        i2c_master_read_byte(cmd, data, I2C_MASTER_NACK);
    }
    
    i2c_master_stop(cmd);
    
    esp_err_t ret = i2c_master_cmd_begin(i2c_port, cmd, pdMS_TO_TICKS(50));
    i2c_cmd_link_delete(cmd);
    
    return ret;
}

esp_err_t KMeterManager::i2c_write_register(uint8_t reg, uint8_t data) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (i2cAddress << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_write_byte(cmd, data, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(i2c_port, cmd, pdMS_TO_TICKS(50));
    i2c_cmd_link_delete(cmd);
    return ret;
}

esp_err_t KMeterManager::i2c_scan() {
    ESP_LOGI(TAG, "Scanning I2C bus...");
    int deviceCount = 0;
    
    for (uint8_t address = 1; address < 127; address++) {
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (address << 1) | I2C_MASTER_WRITE, true);
        i2c_master_stop(cmd);
        esp_err_t ret = i2c_master_cmd_begin(i2c_port, cmd, pdMS_TO_TICKS(50));
        i2c_cmd_link_delete(cmd);
        
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "I2C device found at address 0x%02X", address);
            deviceCount++;
        }
    }
    
    if (deviceCount == 0) {
        ESP_LOGW(TAG, "No I2C devices found on the bus!");
        return ESP_FAIL;
    } else {
        ESP_LOGI(TAG, "Found %d I2C device(s)", deviceCount);
        return ESP_OK;
    }
}

bool KMeterManager::begin(uint8_t addr, uint8_t sda, uint8_t scl, uint32_t speed) {
    ESP_LOGI(TAG, "Initializing KMeter-ISO...");
    ESP_LOGI(TAG, "I2C Config - Addr: 0x%02X, SDA: %d, SCL: %d, Speed: %u Hz",
             addr, sda, scl, (unsigned int)speed);    i2cAddress = addr;
    sdaPin = sda;
    sclPin = scl;
    i2cSpeed = speed;
    
    // Configure I2C - EXACTLY like Arduino Wire.begin()
    i2c_config_t conf = {};
    conf.mode = I2C_MODE_MASTER;
    conf.sda_io_num = (gpio_num_t)sdaPin;
    conf.scl_io_num = (gpio_num_t)sclPin;
    conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
    conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
    conf.master.clk_speed = i2cSpeed;
    conf.clk_flags = 0;
    
    esp_err_t ret = i2c_param_config(i2c_port, &conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C param config failed: %s", esp_err_to_name(ret));
        return false;
    }
    
    ret = i2c_driver_install(i2c_port, conf.mode, 0, 0, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C driver install failed: %s", esp_err_to_name(ret));
        return false;
    }
    
    // WICHTIG: Warte auf I2C-Stabilisierung (länger als Arduino!)
    ESP_LOGI(TAG, "Waiting for I2C bus to stabilize...");
    vTaskDelay(pdMS_TO_TICKS(500));  // Arduino hat 10ms delay() - wir nehmen 500ms
    
    // Scan I2C bus
    esp_err_t scanResult = i2c_scan();
    
    if (scanResult != ESP_OK) {
        ESP_LOGE(TAG, "No I2C devices found!");
        initialized = false;
        return false;
    }
    
    // KRITISCH: EXAKT wie Arduino M5UnitKmeterISO Library!
    // NUR Ping-Test während begin(), KEINE Register-Reads!
    // Die Arduino Library liest Register erst später in update()!
    ESP_LOGI(TAG, "Testing sensor communication (ping only)...");
    
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (i2cAddress << 1) | I2C_MASTER_WRITE, true);
    i2c_master_stop(cmd);
    ret = i2c_master_cmd_begin(i2c_port, cmd, pdMS_TO_TICKS(50));
    i2c_cmd_link_delete(cmd);
    
    if (ret == ESP_OK) {
        initialized = true;
        ESP_LOGI(TAG, "✓ KMeter-ISO initialized successfully - device ACK received");
        
        // WICHTIG: NICHT sofort update() aufrufen!
        // Der Sensor braucht Zeit zum Aufwärmen nach I2C-Init
        // Arduino Demo wartet bis loop() für erste Messung
        
        // Setze lastReadTime so dass update() beim nächsten Aufruf läuft
        lastReadTime = 0;
        
        return true;
    } else {
        initialized = false;
        ESP_LOGE(TAG, "✗ Sensor does not respond to I2C ping at address 0x%02X", i2cAddress);
        ESP_LOGE(TAG, "   Error: %s", esp_err_to_name(ret));
        ESP_LOGE(TAG, "Possible causes:");
        ESP_LOGE(TAG, "  - Sensor not connected");
        ESP_LOGE(TAG, "  - Wrong I2C address");
        ESP_LOGE(TAG, "  - Wiring error (SDA/SCL)");
        ESP_LOGE(TAG, "  - Insufficient power supply");
        return false;
    }
}

void KMeterManager::update() {
    // TEMPORÄR KOMPLETT DEAKTIVIERT
    // Grund: I2C-Operationen blockieren den Watchdog (sowohl alter als auch neuer Driver)
    // Lösung: Muss in separaten Task ausgelagert werden
    
    if (!initialized) {
        return;
    }
    
    // Setze Status auf "Sensor not ready" damit UI weiß dass keine Daten kommen
    errorStatus = 255;
    return;
}

const char* KMeterManager::getStatusString() const {
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

bool KMeterManager::setI2CAddress(uint8_t addr) {
    if (!initialized) return false;
    
    esp_err_t ret = i2c_write_register(KMETER_REG_I2C_ADDR, addr);
    if (ret == ESP_OK) {
        i2cAddress = addr;
        ESP_LOGI(TAG, "KMeter-ISO I2C address changed to 0x%02X", addr);
        return true;
    } else {
        ESP_LOGE(TAG, "Failed to change KMeter-ISO I2C address to 0x%02X", addr);
        return false;
    }
}

uint8_t KMeterManager::getFirmwareVersion() {
    if (!initialized) return 0;
    
    uint8_t fwVersion = 0;
    esp_err_t ret = i2c_read_register(KMETER_REG_FIRMWARE, &fwVersion, 1);
    if (ret == ESP_OK) {
        return fwVersion;
    }
    return 0;
}

bool KMeterManager::diagnoseSensor() {
    ESP_LOGI(TAG, "=== KMeter-ISO Sensor Diagnostics ===");
    
    // Test 1: Check I2C communication
    ESP_LOGI(TAG, "Test 1: Checking basic I2C communication...");
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (i2cAddress << 1) | I2C_MASTER_WRITE, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(i2c_port, cmd, pdMS_TO_TICKS(1000));
    i2c_cmd_link_delete(cmd);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ FAILED: Device not responding at address 0x%02X", i2cAddress);
        ESP_LOGE(TAG, "   Possible causes:");
        ESP_LOGE(TAG, "   - Sensor not connected");
        ESP_LOGE(TAG, "   - Wrong I2C address");
        ESP_LOGE(TAG, "   - Wiring error (SDA/SCL swapped or loose)");
        ESP_LOGE(TAG, "   - Insufficient power supply");
        return false;
    }
    ESP_LOGI(TAG, "✓ PASSED: Device responds at address 0x%02X", i2cAddress);
    
    // Test 2: Read all registers 0x00-0x0F to see what data is available
    ESP_LOGI(TAG, "Test 2: Reading all registers (0x00-0x0F) - Single byte reads...");
    for (uint8_t reg = 0x00; reg <= 0x0F; reg++) {
        uint8_t data = 0;
        ret = i2c_read_register(reg, &data, 1);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "   Reg 0x%02X: 0x%02X (dec: %d)", reg, data, data);
        } else {
            ESP_LOGW(TAG, "   Reg 0x%02X: Read failed", reg);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    // Test 2b: Try direct read without register pointer (some sensors work this way)
    ESP_LOGI(TAG, "Test 2b: Trying direct read (no register address)...");
    i2c_cmd_handle_t cmd2 = i2c_cmd_link_create();
    i2c_master_start(cmd2);
    i2c_master_write_byte(cmd2, (i2cAddress << 1) | I2C_MASTER_READ, true);
    uint8_t direct_data[4];
    i2c_master_read(cmd2, direct_data, 3, I2C_MASTER_ACK);
    i2c_master_read_byte(cmd2, &direct_data[3], I2C_MASTER_NACK);
    i2c_master_stop(cmd2);
    ret = i2c_master_cmd_begin(i2c_port, cmd2, pdMS_TO_TICKS(1000));
    i2c_cmd_link_delete(cmd2);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "   Direct read: [0]=0x%02X [1]=0x%02X [2]=0x%02X [3]=0x%02X",
                 direct_data[0], direct_data[1], direct_data[2], direct_data[3]);
    } else {
        ESP_LOGW(TAG, "   Direct read failed: %s", esp_err_to_name(ret));
    }
    
    // Test 2: Try to read firmware version
    ESP_LOGI(TAG, "Test 2: Reading firmware version...");
    uint8_t fwVersion = 0;
    ret = i2c_read_register(KMETER_REG_FIRMWARE, &fwVersion, 1);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "⚠ WARNING: Cannot read firmware register (error: %s)", esp_err_to_name(ret));
        ESP_LOGW(TAG, "   This may indicate communication issues");
    } else {
        ESP_LOGI(TAG, "✓ PASSED: Firmware version = %d", fwVersion);
    }
    
    // Test 3: Try to read status register
    ESP_LOGI(TAG, "Test 3: Reading status register...");
    uint8_t status = 0;
    ret = i2c_read_register(KMETER_REG_STATUS, &status, 1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ FAILED: Cannot read status register (error: %s)", esp_err_to_name(ret));
        ESP_LOGE(TAG, "   This indicates the sensor is not functioning properly");
        return false;
    }
    ESP_LOGI(TAG, "✓ PASSED: Status register = %d", status);
    
    if (status != 0) {
        ESP_LOGW(TAG, "⚠ WARNING: Sensor reports error status: %d", status);
        ESP_LOGW(TAG, "   The sensor may need time to initialize or has a hardware issue");
    }
    
    // Test 4: Try to read temperature registers
    ESP_LOGI(TAG, "Test 4: Reading temperature registers...");
    uint8_t tempData[4];
    ret = i2c_read_register(KMETER_REG_TEMP_CELSIUS, tempData, 4);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "⚠ WARNING: Cannot read temperature registers (error: %s)", esp_err_to_name(ret));
    } else {
        int32_t tempRaw = (int32_t)(tempData[0] | (tempData[1] << 8) | 
                                    (tempData[2] << 16) | (tempData[3] << 24));
        float temp = (float)tempRaw / 100.0f;
        ESP_LOGI(TAG, "✓ PASSED: Temperature reading = %.2f°C (raw: %d)", temp, tempRaw);
    }
    
    // Test 5: Check I2C bus health
    ESP_LOGI(TAG, "Test 5: Checking I2C bus health...");
    ESP_LOGI(TAG, "   SDA Pin: %d, SCL Pin: %d", sdaPin, sclPin);
    ESP_LOGI(TAG, "   I2C Speed: %u Hz", (unsigned int)i2cSpeed);
    ESP_LOGI(TAG, "   I2C Port: %d", i2c_port);
    
    ESP_LOGI(TAG, "=== Diagnostics Complete ===");
    
    // Sensor is considered functional if it responds and we can read registers
    return true;
}
