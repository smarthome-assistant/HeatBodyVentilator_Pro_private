#include "LEDManager.h"

void LEDManager::begin() {
    FastLED.addLeds<WS2812, Config::LED_PIN, GRB>(leds, Config::NUM_LEDS);
    setColor(CRGB::Black);
}

void LEDManager::setColor(const CRGB& color) {
    for (uint16_t i = 0; i < Config::NUM_LEDS; ++i) {
        leds[i] = color;
    }
    FastLED.show();
}
