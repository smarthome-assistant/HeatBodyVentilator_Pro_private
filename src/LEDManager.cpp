#include "LEDManager.h"
#include "esp_log.h"
#include "driver/rmt_tx.h"
#include "driver/gpio.h"

static const char *TAG = "LED";

// WS2812B LED (M5Stack Atom) using RMT peripheral
static rmt_channel_handle_t led_chan = NULL;
static rmt_transmit_config_t tx_config;
static rmt_symbol_word_t led_data[24]; // 24 bits: 8R + 8G + 8B

// WS2812B timing (T0H: 0.4us, T0L: 0.85us, T1H: 0.8us, T1L: 0.45us)
#define RMT_LED_STRIP_RESOLUTION_HZ 10000000 // 10MHz = 0.1us per tick

// Encoder for WS2812B: Convert bit to RMT symbol
static void ws2812_encode_bit(bool bit, rmt_symbol_word_t *symbol) {
    if (bit) {
        // Bit 1: High for 0.8us (8 ticks), Low for 0.45us (4-5 ticks)
        symbol->level0 = 1;
        symbol->duration0 = 8; // 0.8us
        symbol->level1 = 0;
        symbol->duration1 = 4; // 0.4us
    } else {
        // Bit 0: High for 0.4us (4 ticks), Low for 0.85us (8-9 ticks)
        symbol->level0 = 1;
        symbol->duration0 = 4; // 0.4us
        symbol->level1 = 0;
        symbol->duration1 = 8; // 0.8us
    }
}

// Encode RGB to WS2812B data (GRB format)
static void ws2812_encode_rgb(uint8_t r, uint8_t g, uint8_t b) {
    // WS2812B uses GRB order
    uint32_t grb = (g << 16) | (r << 8) | b;
    
    for (int i = 0; i < 24; i++) {
        bool bit = (grb & (1 << (23 - i))) != 0;
        ws2812_encode_bit(bit, &led_data[i]);
    }
}

void LEDManager::begin() {
    ESP_LOGI(TAG, "Initializing LED Manager (Pin %d - WS2812B)", Config::LED_PIN);
    
    // Configure RMT TX channel for WS2812B
    rmt_tx_channel_config_t tx_chan_config = {
        .gpio_num = (gpio_num_t)Config::LED_PIN,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = RMT_LED_STRIP_RESOLUTION_HZ,
        .mem_block_symbols = 64,
        .trans_queue_depth = 4,
        .flags = {
            .invert_out = false,
            .with_dma = false,
        }
    };
    
    esp_err_t ret = rmt_new_tx_channel(&tx_chan_config, &led_chan);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create RMT TX channel: %s", esp_err_to_name(ret));
        return;
    }
    
    ret = rmt_enable(led_chan);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable RMT channel: %s", esp_err_to_name(ret));
        return;
    }
    
    // Configure transmit settings
    tx_config.loop_count = 0; // No loop
    
    // Turn LED off at startup
    current_r = 0;
    current_g = 0;
    current_b = 0;
    led_is_on = false;
    ws2812_encode_rgb(0, 0, 0);
    
    // Transmit using copy encoder
    rmt_copy_encoder_config_t copy_encoder_config = {};
    rmt_encoder_handle_t copy_encoder = NULL;
    ESP_ERROR_CHECK(rmt_new_copy_encoder(&copy_encoder_config, &copy_encoder));
    rmt_transmit(led_chan, copy_encoder, led_data, sizeof(led_data), &tx_config);
    rmt_tx_wait_all_done(led_chan, 100);
    rmt_del_encoder(copy_encoder);
    
    ESP_LOGI(TAG, "LED Manager initialized (WS2812B/RMT mode)");
}

void LEDManager::setColor(uint8_t r, uint8_t g, uint8_t b) {
    if (!led_chan) {
        ESP_LOGE(TAG, "LED channel not initialized");
        return;
    }
    
    current_r = r;
    current_g = g;
    current_b = b;
    led_is_on = (r > 0 || g > 0 || b > 0);  // LED is on if any color component is non-zero
    
    ws2812_encode_rgb(r, g, b);
    
    // Transmit data using copy encoder
    rmt_copy_encoder_config_t copy_encoder_config = {};
    rmt_encoder_handle_t copy_encoder = NULL;
    ESP_ERROR_CHECK(rmt_new_copy_encoder(&copy_encoder_config, &copy_encoder));
    
    esp_err_t ret = rmt_transmit(led_chan, copy_encoder, led_data, sizeof(led_data), &tx_config);
    if (ret == ESP_OK) {
        rmt_tx_wait_all_done(led_chan, 100);
        ESP_LOGI(TAG, "LED set to R=%d G=%d B=%d", r, g, b);
    } else {
        ESP_LOGE(TAG, "Failed to transmit LED data: %s", esp_err_to_name(ret));
    }
    
    rmt_del_encoder(copy_encoder);
}

void LEDManager::off() {
    led_is_on = false;
    setColor(0, 0, 0);
    ESP_LOGI(TAG, "LED turned OFF");
}
