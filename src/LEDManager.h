#pragma once
#include <FastLED.h>
#include "Config.h"

class LEDManager {
public:
    void begin();
    void setColor(const CRGB& color);
private:
    CRGB leds[Config::NUM_LEDS];
};
