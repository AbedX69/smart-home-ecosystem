/*
 * =============================================================================
 * FILE:        smart_light_remote.h
 * AUTHOR:      AbedX69
 * VERSION:     1.1.0
 * =============================================================================
 *
 * SmartLightRemote — one self-contained smart-light control unit.
 *
 * Owns its own inputs and view:
 *   - RotaryEncoder  (rotate = adjust the active mode value, press = cycle mode)
 *   - TouchSensor    (tap = toggle on/off)
 *   - GC9A01 display (by reference; drawn with the fast arc renderer)
 *   - State          (on/off, brightness, hue, white, mode)
 *
 * Does NOT own the LED strip. The strip lives in SmartLightDevice, which is
 * the device-side MCU in the wireless design. For the wired bench test the
 * caller syncs remote -> device each loop; going wireless only swaps that
 * direct sync for a transport. Keeping the strip out of here is what makes
 * that split clean.
 *
 * Display note: the GC9A01 is passed by reference and must be constructed and
 * init()'d by the caller (the caller owns SPI-host assignment, and two displays
 * may share one bus). This object must not outlive that display.
 *
 * Call SmartLightRemote::buildAngleLUT() ONCE at boot before any render().
 *
 * =============================================================================
 * USAGE
 * =============================================================================
 *
 *     SmartLightRemote::buildAngleLUT();
 *
 *     GC9A01 tft(...);  tft.init();
 *     SmartLightRemote panel(tft, 0, ENC_CLK, ENC_DT, ENC_SW, TOUCH_PIN);
 *     panel.init();        // sets up encoder + touch
 *     panel.render();      // first paint
 *
 *     while (true) {
 *         if (panel.update())            // reads inputs, updates state, redraws
 *             syncToDevice(panel, ...);  // push to the strip
 *         vTaskDelay(pdMS_TO_TICKS(10));
 *     }
 *
 * =============================================================================
 */

#pragma once

#include <stdint.h>
#include <driver/gpio.h>
#include "gc9a01.h"
#include "touch.h"
#include "encoder.h"


/* ─── Control mode ───────────────────────────────────────────────────────── */

enum class SmartLightMode : uint8_t {
    BRIGHTNESS = 0,     ///< Encoder adjusts RGB brightness (at current hue).
    COLOR      = 1,     ///< Encoder adjusts hue.
    WHITE      = 2,     ///< Encoder adjusts white-channel brightness.
};


class SmartLightRemote {
public:

    /**
     * @param display          GC9A01 to draw on (constructed + init'd by caller).
     * @param index            Zero-based; on-screen label is "LED index+1".
     * @param encClk,encDt,encSw  Rotary encoder pins.
     * @param touchPin         Touch sensor OUT pin.
     * @param touchActiveHigh  true if the module outputs HIGH on touch (default).
     */
    SmartLightRemote(GC9A01& display, int index,
                     gpio_num_t encClk, gpio_num_t encDt, gpio_num_t encSw,
                     gpio_num_t touchPin, bool touchActiveHigh = true);

    /** Initialize owned inputs (encoder + touch). Call once after construction. */
    void init();

    /**
     * @brief Poll inputs, update state, and redraw if anything changed.
     *
     *   Tap    -> toggle on/off
     *   Press  -> cycle mode (BRIGHTNESS -> COLOR -> WHITE)
     *   Rotate -> adjust the active mode value (ignored while off)
     *
     * @return true if state changed this call (caller should sync to device).
     */
    bool update();

    /** Paint to the display. Uses incremental updates when possible. */
    void render();

    /** Force a full redraw on the next render(). */
    void invalidate() { _forceRedraw = true; }

    /* ─── Manual state control (update() drives these in normal use) ─────── */
    void setOn(bool on)               { _isOn = on; }
    void toggle()                     { _isOn = !_isOn; }
    void setMode(SmartLightMode m)    { _mode = m; }
    void cycleMode();                  ///< BRIGHTNESS -> COLOR -> WHITE -> ...
    void adjustBrightness(int32_t delta);
    void adjustHue(int32_t delta_degrees);
    void adjustWhite(int32_t delta);

    /* ─── Query ─────────────────────────────────────────────────────────── */
    bool           isOn()        const { return _isOn; }
    uint8_t        brightness()  const { return _brightness; }
    uint16_t       hue()         const { return _hue; }
    uint8_t        whiteBright() const { return _whiteBright; }
    SmartLightMode mode()        const { return _mode; }
    int            index()       const { return _index; }
    uint8_t        r() const { return _r; }
    uint8_t        g() const { return _g; }
    uint8_t        b() const { return _b; }

    /** Build the arc renderer's angle LUT. Call ONCE at boot before render(). */
    static void buildAngleLUT();

private:
    GC9A01&        _display;
    int            _index;

    TouchSensor    _touch;
    RotaryEncoder  _encoder;
    int32_t        _lastEncPos;

    /* Current state */
    bool           _isOn;
    uint8_t        _brightness;     // 0..100
    uint16_t       _hue;            // 0..359
    uint8_t        _whiteBright;    // 0..100
    SmartLightMode _mode;
    uint8_t        _r, _g, _b;      // cached RGB derived from hue

    /* Previous state — incremental redraw bookkeeping */
    bool           _prevOn;
    uint8_t        _prevBrightness;
    uint16_t       _prevHue;
    uint8_t        _prevWhiteBright;
    SmartLightMode _prevMode;
    bool           _forceRedraw;

    void recomputeRgbFromHue();
};