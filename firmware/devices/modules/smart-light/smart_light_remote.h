/*
 * =============================================================================
 * FILE:        smart_light_remote.h
 * AUTHOR:      AbedX69
 * CREATED:     2026-05-05
 * VERSION:     1.0.0
 * LICENSE:     MIT
 * PLATFORM:    All ESP32 variants (ESP-IDF v5.x)
 * =============================================================================
 *
 * SmartLightRemote — A round on-screen panel for one smart-light channel,
 * rendered on a GC9A01 display. This is the REMOTE-SIDE view that the user
 * interacts with; it does NOT drive any physical LED hardware.
 *
 * Owns:
 *   - LED-style state (on/off, brightness, hue, white channel, control mode)
 *   - A reference to the GC9A01 it draws on
 *   - Incremental redraw bookkeeping (prev* fields)
 *
 * Does NOT own:
 *   - Input devices (touch / encoder). Caller drives state via setters.
 *   - Physical LEDs. (See smart_light_device — separate component.)
 *   - Networking. Sync to the device over LoRa/ESP-NOW happens elsewhere.
 *
 * The arc-drawing helpers and the angle LUT live in smart_light_remote.cpp.
 * Call SmartLightRemote::buildAngleLUT() ONCE at boot before any render().
 *
 * =============================================================================
 * USAGE
 * =============================================================================
 *
 *     SmartLightRemote::buildAngleLUT();   // once, before any render
 *
 *     GC9A01 tft(...);
 *     tft.init();
 *
 *     SmartLightRemote panel(tft, 0);      // index 0 → "LED 1" label
 *     panel.render();                       // first paint
 *
 *     panel.setOn(true);
 *     panel.adjustBrightness(+25);
 *     panel.render();                       // incremental redraw
 *
 * =============================================================================
 */

#pragma once

#include <stdint.h>
#include "gc9a01.h"


/* ─── Control Mode ───────────────────────────────────────────────────────── */

enum class SmartLightMode : uint8_t {
    BRIGHTNESS = 0,     ///< Encoder adjusts RGB brightness (with current hue).
    COLOR      = 1,     ///< Encoder adjusts hue.
    WHITE      = 2,     ///< Encoder adjusts white-channel brightness.
};


class SmartLightRemote {
public:

    /**
     * @param display   Reference to the GC9A01 to draw on (must outlive this).
     * @param index     Zero-based index, used for the on-screen label ("LED N+1").
     */
    SmartLightRemote(GC9A01& display, int index);

    /* ─── Setters (idempotent, mark dirty internally) ──────────────── */

    void setOn(bool on)                  { _isOn = on; }
    void toggle()                        { _isOn = !_isOn; }
    void setMode(SmartLightMode mode)    { _mode = mode; }
    void cycleMode();   ///< BRIGHTNESS → COLOR → WHITE → BRIGHTNESS

    /** Adjust brightness by delta, clamped to 0..100. */
    void adjustBrightness(int32_t delta);

    /** Adjust hue by delta degrees, wrapped to 0..359. */
    void adjustHue(int32_t delta_degrees);

    /** Adjust white-channel brightness by delta, clamped to 0..100. */
    void adjustWhite(int32_t delta);

    /** Force a full redraw on next render() (e.g. after blanking the screen). */
    void invalidate() { _forceRedraw = true; }

    /* ─── Render (call after any state change) ─────────────────────── */

    /** Paint to the display. Uses incremental updates when possible. */
    void render();

    /* ─── Query ────────────────────────────────────────────────────── */

    bool             isOn()        const { return _isOn; }
    uint8_t          brightness()  const { return _brightness; }
    uint16_t         hue()         const { return _hue; }
    uint8_t          whiteBright() const { return _whiteBright; }
    SmartLightMode   mode()        const { return _mode; }
    int              index()       const { return _index; }

    /** RGB derived from current hue (cached). */
    uint8_t r() const { return _r; }
    uint8_t g() const { return _g; }
    uint8_t b() const { return _b; }

    /* ─── One-time setup ───────────────────────────────────────────── */

    /** Build the angle lookup table used by the arc renderer.
     *  Must be called ONCE at program start, before any render(). */
    static void buildAngleLUT();

private:

    GC9A01&        _display;
    int            _index;

    /* Current state */
    bool           _isOn;
    uint8_t        _brightness;     // 0..100
    uint16_t       _hue;            // 0..359
    uint8_t        _whiteBright;    // 0..100
    SmartLightMode _mode;
    uint8_t        _r, _g, _b;      // Cached RGB derived from hue

    /* Previous state — for incremental redraw decisions */
    bool           _prevOn;
    uint8_t        _prevBrightness;
    uint16_t       _prevHue;
    uint8_t        _prevWhiteBright;
    SmartLightMode _prevMode;
    bool           _forceRedraw;

    /* Helpers */
    void recomputeRgbFromHue();
};
