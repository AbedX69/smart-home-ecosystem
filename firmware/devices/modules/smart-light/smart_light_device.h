/*
 * =============================================================================
 * FILE:        smart_light_device.h
 * AUTHOR:      AbedX69
 * CREATED:     2026-05-05
 * VERSION:     1.0.0
 * =============================================================================
 *
 * SmartLightDevice — drives a physical SK6812 RGBW LED strip.
 *
 * Receives state (on/off, brightness, hue, white level) from the caller
 * (typically synced from a SmartLightRemote over LoRa / ESP-NOW) and maps
 * it to RGBW output on the strip.
 *
 * White channel:
 *   The SK6812 RGBW strip has a dedicated 4000K neutral white LED.
 *   The white channel is controlled independently via setWhite().
 *   RGB and white can be mixed — e.g. warm amber tint + white fill.
 *
 * =============================================================================
 * USAGE
 * =============================================================================
 *
 *     SmartLightDevice light(GPIO_NUM_1, 144);
 *     light.init();
 *
 *     light.setOn(true);
 *     light.setBrightness(75);    // 0-100 RGB brightness
 *     light.setHue(30);           // warm orange
 *     light.setWhite(50);         // 50% white channel
 *     light.update();             // push to strip
 *
 * =============================================================================
 */

#pragma once

#include <stdint.h>
#include <driver/gpio.h>
#include "addressable_led.h"


class SmartLightDevice {
public:

    /**
     * @param pin       GPIO for the SK6812 data line
     * @param numLeds   Number of LEDs on the strip
     */
    SmartLightDevice(gpio_num_t pin, uint16_t numLeds);
    ~SmartLightDevice();

    /** Initialize the LED strip hardware. Call once at boot. */
    bool init();

    /* ─── State setters ─────────────────────────────────────────────── */

    void setOn(bool on);
    void setBrightness(uint8_t pct);      ///< 0-100, scales RGB output
    void setHue(uint16_t deg);            ///< 0-359, sets RGB color
    void setWhite(uint8_t pct);           ///< 0-100, independent W channel

    /**
     * @brief Push current state to the LED strip.
     *
     * Call after changing any state. Converts hue+brightness to RGB,
     * combines with white channel, and sends to all LEDs.
     */
    void update();

    /* ─── Query ─────────────────────────────────────────────────────── */

    bool     isOn()        const { return _isOn; }
    uint8_t  brightness()  const { return _brightness; }
    uint16_t hue()         const { return _hue; }
    uint8_t  whiteBright() const { return _whiteBright; }

private:
    AddressableLED _strip;

    bool     _isOn;
    uint8_t  _brightness;     // 0-100
    uint16_t _hue;            // 0-359
    uint8_t  _whiteBright;    // 0-100
    uint8_t  _r, _g, _b;     // cached RGB from hue

    void recomputeRgb();
};