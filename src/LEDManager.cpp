#include "LEDManager.h"
#include "driver/rmt_tx.h"
#include "esp_log.h"

static const char *TAG = "LED";

// Simple RGB LED control without led_strip for now
// We'll use GPIO for simple on/off control instead

void LEDManager::begin() {
    ESP_LOGI(TAG, "Initializing LED Manager (Pin %d)", Config::LED_PIN);
    
    // Configure LED pin as output
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (1ULL << Config::LED_PIN);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&io_conf);
    
    // LED ausschalten beim Start
    current_r = 0;
    current_g = 0;
    current_b = 0;
    gpio_set_level((gpio_num_t)Config::LED_PIN, 0);
    
    ESP_LOGI(TAG, "LED Manager initialized (simple GPIO mode)");
}

void LEDManager::setColor(uint8_t r, uint8_t g, uint8_t b) {
    current_r = r;
    current_g = g;
    current_b = b;
    
    // Simple on/off for now (any color = on, all zero = off)
    if (r > 0 || g > 0 || b > 0) {
        gpio_set_level((gpio_num_t)Config::LED_PIN, 1);
    } else {
        gpio_set_level((gpio_num_t)Config::LED_PIN, 0);
    }
    
    ESP_LOGD(TAG, "LED Color set: R=%d G=%d B=%d", r, g, b);
}

void LEDManager::off() {
    current_r = 0;
    current_g = 0;
    current_b = 0;
    gpio_set_level((gpio_num_t)Config::LED_PIN, 0);
    ESP_LOGD(TAG, "LED turned off");
}
