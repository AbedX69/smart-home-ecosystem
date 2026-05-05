/*
 * =============================================================================
 * FILE:        smart_light_remote.cpp
 * AUTHOR:      AbedX69
 * CREATED:     2026-05-05
 * VERSION:     1.0.0
 * =============================================================================
 *
 * SmartLightRemote implementation.
 *
 * Private to this translation unit:
 *   - Angle LUT (one quadrant, mirrored at runtime)
 *   - drawArcFast() — single-window arc renderer with incremental support
 *   - drawCenteredString() — text-centering helper
 *   - hueToRgb() — HSV→RGB at S=V=full
 *
 * =============================================================================
 */

#include "smart_light_remote.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <esp_log.h>

static const char* TAG = "SmartLightRemote";


/* =============================================================================
 * Color conversion (hue → RGB at full saturation/value)
 * ========================================================================== */

namespace {

void hueToRgb(uint16_t hue, uint8_t& r, uint8_t& g, uint8_t& b) {
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


/* =============================================================================
 * Angle LUT — pre-computed atan2 for one quadrant
 * ========================================================================== */

constexpr int ARC_MAX_RADIUS = 101;     // outer radius (100) + 1
static uint8_t s_angleLUT[ARC_MAX_RADIUS][ARC_MAX_RADIUS];
static bool    s_angleLUT_built = false;

inline int getAngle(int dx, int dy) {
    int adx = dx < 0 ? -dx : dx;
    int ady = dy < 0 ? -dy : dy;

    if (adx >= ARC_MAX_RADIUS) adx = ARC_MAX_RADIUS - 1;
    if (ady >= ARC_MAX_RADIUS) ady = ARC_MAX_RADIUS - 1;

    int q1angle = s_angleLUT[adx][ady];

    /* Map quadrant. Screen Y inverted: north = negative dy. */
    if (dy <= 0) {
        if (dx >= 0) return q1angle;
        return (360 - q1angle) % 360;
    } else {
        if (dx >= 0) return 180 - q1angle;
        return 180 + q1angle;
    }
}


/* =============================================================================
 * drawArcFast — annular arc with incremental support
 * ========================================================================== */

void drawArcFast(GC9A01& disp, int16_t cx, int16_t cy,
                 int16_t innerR, int16_t outerR,
                 int startDeg, int endDeg, uint16_t color)
{
    if (outerR <= 0 || endDeg <= startDeg) return;
    if (innerR < 0) innerR = 0;

    int32_t innerSq = (int32_t)innerR * innerR;
    int32_t outerSq = (int32_t)outerR * outerR;

    int16_t yStart = cy - outerR;
    int16_t yEnd   = cy + outerR;
    int16_t xStart = cx - outerR;
    int16_t xEnd   = cx + outerR;

    if (yStart < 0)              yStart = 0;
    if (yEnd >= GC9A01_HEIGHT)   yEnd   = GC9A01_HEIGHT - 1;
    if (xStart < 0)              xStart = 0;
    if (xEnd >= GC9A01_WIDTH)    xEnd   = GC9A01_WIDTH - 1;

    int16_t bboxW = xEnd - xStart + 1;
    int16_t bboxH = yEnd - yStart + 1;
    if (bboxW <= 0 || bboxH <= 0) return;

    for (int16_t offset = 0; offset <= outerR; offset++) {
        for (int pass = 0; pass < 2; pass++) {
            if (pass == 1 && offset == 0) continue;

            int16_t y = (pass == 0) ? (cy - offset) : (cy + offset);
            if (y < yStart || y > yEnd) continue;

            int16_t dy   = y - cy;
            int32_t dySq = (int32_t)dy * dy;
            if (dySq > outerSq) continue;

            int16_t dxOuter = (int16_t)sqrtf((float)(outerSq - dySq));
            int16_t dxInner = (dySq < innerSq) ? (int16_t)sqrtf((float)(innerSq - dySq)) : 0;

            int16_t rowXStart = cx - dxOuter;
            int16_t rowXEnd   = cx + dxOuter;
            if (rowXStart < 0)            rowXStart = 0;
            if (rowXEnd >= GC9A01_WIDTH)  rowXEnd   = GC9A01_WIDTH - 1;

            int16_t rw = rowXEnd - rowXStart + 1;
            if (rw <= 0) continue;

            uint16_t lineBuf[GC9A01_WIDTH];
            int16_t  runStart = -1;

            for (int16_t i = 0; i <= rw; i++) {
                bool inArc = false;

                if (i < rw) {
                    int16_t x     = rowXStart + i;
                    int16_t dx    = x - cx;
                    int16_t absDx = dx < 0 ? -dx : dx;

                    if (absDx >= dxInner) {
                        int angle = getAngle(dx, dy);
                        if (endDeg <= 360) {
                            inArc = (angle >= startDeg && angle <= endDeg);
                        } else {
                            inArc = (angle >= startDeg || angle <= (endDeg - 360));
                        }
                    }
                }

                if (inArc) {
                    if (runStart < 0) runStart = i;
                    lineBuf[i] = color;
                } else if (runStart >= 0) {
                    int16_t runLen = i - runStart;
                    int16_t wx     = rowXStart + runStart;
                    disp.beginWrite(wx, y, wx + runLen - 1, y);
                    disp.pushPixels(&lineBuf[runStart], runLen);
                    disp.endWrite();
                    runStart = -1;
                }
            }
        }
    }
}


/* =============================================================================
 * drawCenteredString
 * ========================================================================== */

void drawCenteredString(GC9A01& disp, int16_t cy, const char* str,
                        uint16_t color, uint16_t bg, uint8_t size)
{
    int16_t strWidth = (int16_t)strlen(str) * 6 * size;
    int16_t x        = (GC9A01_WIDTH - strWidth) / 2;
    disp.drawString(x, cy, str, color, bg, size);
}

}   // anonymous namespace


/* =============================================================================
 * SmartLightRemote — public API
 * ========================================================================== */

void SmartLightRemote::buildAngleLUT() {
    if (s_angleLUT_built) return;

    for (int dx = 0; dx < ARC_MAX_RADIUS; dx++) {
        for (int dy = 0; dy < ARC_MAX_RADIUS; dy++) {
            if (dx == 0 && dy == 0) {
                s_angleLUT[dx][dy] = 0;
            } else {
                float rad = atan2f((float)dx, (float)dy);
                s_angleLUT[dx][dy] = (uint8_t)(rad * 180.0f / (float)M_PI + 0.5f);
            }
        }
    }
    s_angleLUT_built = true;
    ESP_LOGI(TAG, "Angle LUT built (%d x %d)", ARC_MAX_RADIUS, ARC_MAX_RADIUS);
}


SmartLightRemote::SmartLightRemote(GC9A01& display, int index)
    : _display(display),
      _index(index),
      _isOn(false),
      _brightness(50),
      _hue(0),
      _whiteBright(0),
      _mode(SmartLightMode::BRIGHTNESS),
      _r(255), _g(0), _b(0),
      _prevOn(false),
      _prevBrightness(0),
      _prevHue(0),
      _prevWhiteBright(0),
      _prevMode(SmartLightMode::BRIGHTNESS),
      _forceRedraw(true)
{
}


void SmartLightRemote::cycleMode() {
    switch (_mode) {
        case SmartLightMode::BRIGHTNESS: _mode = SmartLightMode::COLOR;      break;
        case SmartLightMode::COLOR:      _mode = SmartLightMode::WHITE;      break;
        case SmartLightMode::WHITE:      _mode = SmartLightMode::BRIGHTNESS; break;
    }
}

void SmartLightRemote::adjustBrightness(int32_t delta) {
    int v = (int)_brightness + (int)delta;
    if (v < 0)   v = 0;
    if (v > 100) v = 100;
    _brightness = (uint8_t)v;
}

void SmartLightRemote::adjustHue(int32_t delta_degrees) {
    int v = (int)_hue + (int)delta_degrees;
    while (v < 0)    v += 360;
    while (v >= 360) v -= 360;
    _hue = (uint16_t)v;
    recomputeRgbFromHue();
}

void SmartLightRemote::adjustWhite(int32_t delta) {
    int v = (int)_whiteBright + (int)delta;
    if (v < 0)   v = 0;
    if (v > 100) v = 100;
    _whiteBright = (uint8_t)v;
}

void SmartLightRemote::recomputeRgbFromHue() {
    hueToRgb(_hue, _r, _g, _b);
}


/* =============================================================================
 * Render — full or incremental
 * ========================================================================== */

void SmartLightRemote::render() {
    const int16_t cx          = GC9A01_WIDTH  / 2;
    const int16_t cy          = GC9A01_HEIGHT / 2;
    const int16_t outerRadius = 100;
    const int16_t innerRadius = 40;

    bool toggledOnOff      = (_isOn        != _prevOn);
    bool colorChanged      = (_hue         != _prevHue);
    bool brightnessChanged = (_brightness  != _prevBrightness);
    bool whiteChanged      = (_whiteBright != _prevWhiteBright);
    bool modeChanged       = (_mode        != _prevMode);
    bool needFullRedraw    = _forceRedraw || toggledOnOff || modeChanged;

    char name[16];
    snprintf(name, sizeof(name), "LED %d", _index + 1);

    bool     inWhiteMode = (_mode == SmartLightMode::WHITE);
    uint8_t  arcLevel    = inWhiteMode ? _whiteBright : _brightness;
    uint16_t arcColor    = inWhiteMode ? COLOR_WHITE
                                       : GC9A01::color565(_r, _g, _b);

    if (needFullRedraw) {
        _display.fillScreen(COLOR_BLACK);

        if (_isOn) {
            if (arcLevel > 0) {
                int endAngle = (int)(arcLevel * 3.6f);
                drawArcFast(_display, cx, cy, innerRadius, outerRadius,
                            0, endAngle, arcColor);
            }

            drawCenteredString(_display, cy - 16, name, COLOR_WHITE, COLOR_BLACK, 2);

            char pct[16];
            snprintf(pct, sizeof(pct), "%d%%", arcLevel);
            drawCenteredString(_display, cy + 4, pct, arcColor, COLOR_BLACK, 2);

            const char* modeStr = inWhiteMode ? "WHITE" :
                                  (_mode == SmartLightMode::COLOR) ? "COLOR" : "RGB";
            drawCenteredString(_display, cy + 22, modeStr, COLOR_GRAY, COLOR_BLACK, 1);
        } else {
            drawCenteredString(_display, cy - 8,  name, COLOR_GRAY, COLOR_BLACK, 2);
            drawCenteredString(_display, cy + 16, "OFF", COLOR_GRAY, COLOR_BLACK, 2);
        }
    }
    else if (_isOn && !inWhiteMode && colorChanged) {
        int endAngle = (int)(_brightness * 3.6f);
        if (_brightness > 0) {
            drawArcFast(_display, cx, cy, innerRadius, outerRadius,
                        0, endAngle, arcColor);
        }
        _display.fillRect(cx - 30, cy + 2, 60, 16, COLOR_BLACK);

        char pct[16];
        snprintf(pct, sizeof(pct), "%d%%", _brightness);
        drawCenteredString(_display, cy + 4, pct, arcColor, COLOR_BLACK, 2);
    }
    else if (_isOn && !inWhiteMode && brightnessChanged) {
        int oldAngle = (int)(_prevBrightness * 3.6f);
        int newAngle = (int)(_brightness     * 3.6f);

        if (newAngle > oldAngle) {
            drawArcFast(_display, cx, cy, innerRadius, outerRadius,
                        oldAngle, newAngle, arcColor);
        } else {
            drawArcFast(_display, cx, cy, innerRadius, outerRadius,
                        newAngle, oldAngle, COLOR_BLACK);
        }

        _display.fillRect(cx - 30, cy + 2, 60, 16, COLOR_BLACK);
        char pct[16];
        snprintf(pct, sizeof(pct), "%d%%", _brightness);
        drawCenteredString(_display, cy + 4, pct, arcColor, COLOR_BLACK, 2);
    }
    else if (_isOn && inWhiteMode && whiteChanged) {
        int oldAngle = (int)(_prevWhiteBright * 3.6f);
        int newAngle = (int)(_whiteBright     * 3.6f);

        if (newAngle > oldAngle) {
            drawArcFast(_display, cx, cy, innerRadius, outerRadius,
                        oldAngle, newAngle, COLOR_WHITE);
        } else {
            drawArcFast(_display, cx, cy, innerRadius, outerRadius,
                        newAngle, oldAngle, COLOR_BLACK);
        }

        _display.fillRect(cx - 30, cy + 2, 60, 16, COLOR_BLACK);
        char pct[16];
        snprintf(pct, sizeof(pct), "%d%%", _whiteBright);
        drawCenteredString(_display, cy + 4, pct, COLOR_WHITE, COLOR_BLACK, 2);
    }

    _prevOn          = _isOn;
    _prevBrightness  = _brightness;
    _prevHue         = _hue;
    _prevWhiteBright = _whiteBright;
    _prevMode        = _mode;
    _forceRedraw     = false;
}
