#pragma once
#include "Config.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include <stdint.h>

class LEDManager {
public:
    void begin();
    void setColor(uint8_t r, uint8_t g, uint8_t b);
    void off();
    
    // Kompatibilitäts-Helper für einfache Farben
    void setColorBlack() { setColor(0, 0, 0); }
    void setColorWhite() { setColor(255, 255, 255); }
    void setColorRed() { setColor(255, 0, 0); }
    void setColorGreen() { setColor(0, 255, 0); }
    void setColorBlue() { setColor(0, 0, 255); }
    
private:
    uint8_t current_r, current_g, current_b;
};
