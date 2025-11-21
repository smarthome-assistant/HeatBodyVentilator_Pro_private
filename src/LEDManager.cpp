#include "LEDManager.h"
#include "esp_log.h"
#include "driver/rmt.h"
#include "driver/gpio.h"

static const char *TAG = "LED";

// WS2812B LED (M5Stack Atom) using RMT peripheral - ESP-IDF 4.4 API
#define RMT_TX_CHANNEL    RMT_CHANNEL_0
#define RMT_CLK_DIV       4  // 80MHz / 4 = 20MHz (0.05us per tick)

// WS2812B timing at 20MHz clock (each tick = 0.05us):
// T0H: 0.4us = 8 ticks, T0L: 0.85us = 17 ticks
// T1H: 0.8us = 16 ticks, T1L: 0.45us = 9 ticks
#define WS2812_T0H_TICKS  8
#define WS2812_T0L_TICKS  17
#define WS2812_T1H_TICKS  16
#define WS2812_T1L_TICKS  9

static rmt_item32_t led_data_items[24]; // 24 bits for GRB

// Convert RGB to RMT items (WS2812B uses GRB order)
static void ws2812_set_pixel(uint8_t r, uint8_t g, uint8_t b) {
    uint32_t grb = ((uint32_t)g << 16) | ((uint32_t)r << 8) | b;
    
    for (int i = 0; i < 24; i++) {
        bool bit_val = (grb >> (23 - i)) & 0x01;
        if (bit_val) {
            // Bit 1
            led_data_items[i].level0 = 1;
            led_data_items[i].duration0 = WS2812_T1H_TICKS;
            led_data_items[i].level1 = 0;
            led_data_items[i].duration1 = WS2812_T1L_TICKS;
        } else {
            // Bit 0
            led_data_items[i].level0 = 1;
            led_data_items[i].duration0 = WS2812_T0H_TICKS;
            led_data_items[i].level1 = 0;
            led_data_items[i].duration1 = WS2812_T0L_TICKS;
        }
    }
}

void LEDManager::begin() {
    ESP_LOGI(TAG, "Initializing LED Manager (Pin %d - WS2812B ESP-IDF 4.4)", Config::LED_PIN);
    
    // RMT configuration for ESP-IDF 4.4
    rmt_config_t config = RMT_DEFAULT_CONFIG_TX((gpio_num_t)Config::LED_PIN, RMT_TX_CHANNEL);
    config.clk_div = RMT_CLK_DIV;
    
    esp_err_t ret = rmt_config(&config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure RMT: %s", esp_err_to_name(ret));
        return;
    }
    
    ret = rmt_driver_install(config.channel, 0, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install RMT driver: %s", esp_err_to_name(ret));
        return;
    }
    
    // Turn LED off at startup
    current_r = 0;
    current_g = 0;
    current_b = 0;
    led_is_on = false;
    
    ws2812_set_pixel(0, 0, 0);
    rmt_write_items(RMT_TX_CHANNEL, led_data_items, 24, true);
    rmt_wait_tx_done(RMT_TX_CHANNEL, portMAX_DELAY);
    
    ESP_LOGI(TAG, "LED Manager initialized successfully");
}

void LEDManager::setColor(uint8_t r, uint8_t g, uint8_t b) {
    current_r = r;
    current_g = g;
    current_b = b;
    led_is_on = (r > 0 || g > 0 || b > 0);
    
    ws2812_set_pixel(r, g, b);
    
    esp_err_t ret = rmt_write_items(RMT_TX_CHANNEL, led_data_items, 24, true);
    if (ret == ESP_OK) {
        rmt_wait_tx_done(RMT_TX_CHANNEL, portMAX_DELAY);
    } else {
        ESP_LOGE(TAG, "Failed to write RMT items: %s", esp_err_to_name(ret));
    }
}

void LEDManager::off() {
    led_is_on = false;
    setColor(0, 0, 0);
}
