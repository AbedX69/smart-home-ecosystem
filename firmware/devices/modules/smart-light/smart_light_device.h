/*
 * =============================================================================
 * FILE:        smart_light_device.h
 * AUTHOR:      AbedX69
 * CREATED:     2026-05-05
 * VERSION:     0.0.1 (STUB)
 * =============================================================================
 *
 * SmartLightDevice — the actual physical light hardware.
 *
 * This is the device side: it owns the LED hardware (PWM channels for warm/
 * cool white, addressable LED strip, RGB MOSFET driver — TBD). It receives
 * state from a SmartLightRemote (eventually over LoRa / ESP-NOW / mesh) and
 * drives the LEDs to match.
 *
 * TODO — NOT YET IMPLEMENTED
 * ==========================
 *   - Decide LED hardware: addressable_led? pwm_dimmer + mosfet_driver?
 *     Combination?
 *   - Mirror the SmartLightRemote state surface:
 *       setOn(bool), setBrightness(0..100), setHue(0..359),
 *       setWhite(0..100)
 *   - Map state → hardware output (gamma curve, max current limiting,
 *     cool/warm white blending if applicable).
 *   - Add network receiver (separate component) that calls the setters.
 *
 * =============================================================================
 */

#pragma once

#include <stdint.h>

class SmartLightDevice {
public:
    SmartLightDevice();
    ~SmartLightDevice();

    bool init();        // TODO
    void update();      // TODO

    /* TODO: state setters mirroring SmartLightRemote */
    // void setOn(bool on);
    // void setBrightness(uint8_t pct);
    // void setHue(uint16_t deg);
    // void setWhite(uint8_t pct);

private:
    // TODO: pick hardware drivers
};
