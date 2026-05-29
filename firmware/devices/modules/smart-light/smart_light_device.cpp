/*
 * =============================================================================
 * FILE:        smart_light_device.cpp
 * AUTHOR:      AbedX69
 * CREATED:     2026-05-05
 * VERSION:     1.0.0
 * =============================================================================
 */

#include "smart_light_device.h"
#include <esp_log.h>

static const char* TAG = "SmartLightDevice";


/* ─── HSV → RGB (S=100%, V=100%) ──────────────────────────────────────────── */

static void hueToRgb(uint16_t hue, uint8_t& r, uint8_t& g, uint8_t& b) {
    hue = hue % 360;
    uint8_t region    = hue / 60;
    uint8_t remainder = (hue % 60) * 255 / 60;
    uint8_t q = 255 - remainder;
    uint8_t t = remainder;

    switch (region) {
        case 0:  r = 255; g = t;   b = 0;   break;
        case 1:  r = q;   g = 255; b = 0;   break;
        case 2:  r = 0;   g = 255; b = t;   break;
        case 3:  r = 0;   g = q;   b = 255; break;
        case 4:  r = t;   g = 0;   b = 255; break;
        default: r = 255; g = 0;   b = q;   break;
    }
}


/* ─── Construction / destruction ──────────────────────────────────────────── */

SmartLightDevice::SmartLightDevice(gpio_num_t pin, uint16_t numLeds)
    : _strip(pin, numLeds, LedType::SK6812_RGBW),
      _isOn(false),
      _brightness(50),
      _hue(0),
      _whiteBright(0),
      _r(255), _g(0), _b(0)
{
}

SmartLightDevice::~SmartLightDevice() {}


/* ─── Init ────────────────────────────────────────────────────────────────── */

bool SmartLightDevice::init() {
    _strip.setBrightness(255);

    if (!_strip.init()) {
        ESP_LOGE(TAG, "Failed to init LED strip");
        return false;
    }

    ESP_LOGI(TAG, "Initialized: %d SK6812 RGBW LEDs on GPIO %d",
             _strip.getNumLeds(), (int)_strip.getNumLeds());

    _strip.clear();
    _strip.show();
    return true;
}


/* ─── State setters ───────────────────────────────────────────────────────── */

void SmartLightDevice::setOn(bool on)          { _isOn = on; }

void SmartLightDevice::setBrightness(uint8_t pct) {
    if (pct > 100) pct = 100;
    _brightness = pct;
}

void SmartLightDevice::setHue(uint16_t deg) {
    _hue = deg % 360;
    recomputeRgb();
}

void SmartLightDevice::setWhite(uint8_t pct) {
    if (pct > 100) pct = 100;
    _whiteBright = pct;
}

void SmartLightDevice::recomputeRgb() {
    hueToRgb(_hue, _r, _g, _b);
}


/* ─── Update — push state to strip ────────────────────────────────────────── */

void SmartLightDevice::update() {
    if (!_isOn) {
        _strip.clear();
        _strip.show();
        return;
    }

    // Scale RGB by brightness percentage
    uint8_t scale = (_brightness * 255) / 100;
    uint8_t sr = (_r * scale) / 255;
    uint8_t sg = (_g * scale) / 255;
    uint8_t sb = (_b * scale) / 255;

    // White channel is independent
    uint8_t w = (_whiteBright * 255) / 100;

    _strip.fill(sr, sg, sb, w);
    _strip.show();
}