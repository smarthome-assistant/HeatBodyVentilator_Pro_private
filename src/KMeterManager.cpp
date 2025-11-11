#include "KMeterManager.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "KMeter";

// KMeter-ISO Register Adressen (basierend auf M5UnitKmeterISO)
#define KMETER_REG_TEMP_C_MSB       0x00
#define KMETER_REG_TEMP_C_LSB       0x01
#define KMETER_REG_TEMP_F_MSB       0x04
#define KMETER_REG_TEMP_F_LSB       0x05
#define KMETER_REG_INTERNAL_TEMP    0x08
#define KMETER_REG_STATUS           0x0C
#define KMETER_REG_FIRMWARE         0x0E
#define KMETER_REG_I2C_ADDR         0x0F

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
}

esp_err_t KMeterManager::i2c_read_register(uint8_t reg, uint8_t* data, size_t len) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (i2cAddress << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (i2cAddress << 1) | I2C_MASTER_READ, true);
    if (len > 1) {
        i2c_master_read(cmd, data, len - 1, I2C_MASTER_ACK);
    }
    i2c_master_read_byte(cmd, data + len - 1, I2C_MASTER_NACK);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(i2c_port, cmd, pdMS_TO_TICKS(1000));
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
    esp_err_t ret = i2c_master_cmd_begin(i2c_port, cmd, pdMS_TO_TICKS(1000));
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
    ESP_LOGI(TAG, "I2C Config - Addr: 0x%02X, SDA: %d, SCL: %d, Speed: %lu Hz", 
             addr, sda, scl, speed);
    
    i2cAddress = addr;
    sdaPin = sda;
    sclPin = scl;
    i2cSpeed = speed;
    
    // Configure I2C
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
    
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Scan I2C bus
    i2c_scan();
    
    // Try to read firmware version
    uint8_t fwVersion = 0;
    ret = i2c_read_register(KMETER_REG_FIRMWARE, &fwVersion, 1);
    
    if (ret == ESP_OK) {
        initialized = true;
        ESP_LOGI(TAG, "KMeter-ISO initialized successfully at address 0x%02X", i2cAddress);
        ESP_LOGI(TAG, "Firmware Version: %d", fwVersion);
        
        // Initial read
        update();
        return true;
    } else {
        initialized = false;
        ESP_LOGE(TAG, "Failed to communicate with KMeter-ISO at address 0x%02X", i2cAddress);
        ESP_LOGI(TAG, "Troubleshooting steps:");
        ESP_LOGI(TAG, "1. Check if KMeter-ISO module is properly connected");
        ESP_LOGI(TAG, "2. Verify SDA (Pin %d) and SCL (Pin %d) connections", sda, scl);
        ESP_LOGI(TAG, "3. Check if I2C address is correct (default: 0x66)");
        ESP_LOGI(TAG, "4. Ensure proper power supply to the module");
        
        return false;
    }
}

void KMeterManager::update() {
    if (!initialized) {
        return;
    }
    
    int64_t currentTime = esp_timer_get_time();
    if (currentTime - lastReadTime < readInterval) {
        return;
    }
    
    // Read status register
    uint8_t status = 0;
    esp_err_t ret = i2c_read_register(KMETER_REG_STATUS, &status, 1);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to read status register");
        errorStatus = 255;
        return;
    }
    
    errorStatus = status;
    
    if (errorStatus == 0) {
        // Read Celsius temperature (2 bytes, MSB first)
        uint8_t tempC_data[2];
        ret = i2c_read_register(KMETER_REG_TEMP_C_MSB, tempC_data, 2);
        if (ret == ESP_OK) {
            int32_t tempC = (int32_t)((tempC_data[0] << 8) | tempC_data[1]);
            currentTempCelsius = (float)tempC / 100.0f;
        }
        
        // Read Fahrenheit temperature (2 bytes, MSB first)
        uint8_t tempF_data[2];
        ret = i2c_read_register(KMETER_REG_TEMP_F_MSB, tempF_data, 2);
        if (ret == ESP_OK) {
            int32_t tempF = (int32_t)((tempF_data[0] << 8) | tempF_data[1]);
            currentTempFahrenheit = (float)tempF / 100.0f;
        }
        
        // Read internal temperature
        uint8_t internalTemp = 0;
        ret = i2c_read_register(KMETER_REG_INTERNAL_TEMP, &internalTemp, 1);
        if (ret == ESP_OK) {
            internalTempCelsius = (float)internalTemp;
        }
        
        ESP_LOGI(TAG, "Temperature: %.2f°C / %.2f°F (Internal: %.1f°C)", 
                 currentTempCelsius, currentTempFahrenheit, internalTempCelsius);
    } else {
        ESP_LOGW(TAG, "Sensor not ready, status: %d", errorStatus);
    }
    
    lastReadTime = currentTime;
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
