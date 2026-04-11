/**
 * @file main.cpp
 * @brief Demo: 2 virtual LEDs with touch, encoders, and dual display types
 *
 * HARDWARE SETUP (ESP32-S3):
 * 
 *   INPUTS:
 *     Touch 1:      GPIO 4
 *     Touch 2:      GPIO 5
 *     Encoder 1:    CLK=6, DT=7, SW=15
 *     Encoder 2:    CLK=16, DT=17, SW=18
 *
 *   I2C (PCA9548A mux → 2x SSD1306):
 *     SDA:          GPIO 8
 *     SCL:          GPIO 9
 *     Mux CH0:      SSD1306 #1
 *     Mux CH1:      SSD1306 #2
 *
 *   SPI (shared bus → 2x GC9A01):
 *     MOSI:         GPIO 11
 *     SCK:          GPIO 12
 *     GC9A01 #1:    CS=10, DC=13, RST=14
 *     GC9A01 #2:    CS=21, DC=47, RST=48
 *     Backlight:    GPIO 3 (shared)
 */

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <math.h>
#include <stdio.h>

#include "touch.h"
#include "encoder.h"
#include "pca9548a.h"
#include "ssd1306.h"
#include "gc9a01.h"

static const char* TAG = "DualLED";


// =============================================================================
// PIN DEFINITIONS
// =============================================================================

#ifndef TOUCH1_PIN
#define TOUCH1_PIN      4
#endif
#ifndef TOUCH2_PIN
#define TOUCH2_PIN      5
#endif

#ifndef ENC1_CLK
#define ENC1_CLK        6
#endif
#ifndef ENC1_DT
#define ENC1_DT         7
#endif
#ifndef ENC1_SW
#define ENC1_SW         15
#endif

#ifndef ENC2_CLK
#define ENC2_CLK        16
#endif
#ifndef ENC2_DT
#define ENC2_DT         17
#endif
#ifndef ENC2_SW
#define ENC2_SW         18
#endif

#ifndef I2C_SDA
#define I2C_SDA         8
#endif
#ifndef I2C_SCL
#define I2C_SCL         9
#endif

#ifndef SPI_MOSI
#define SPI_MOSI        11
#endif
#ifndef SPI_SCK
#define SPI_SCK         12
#endif

#ifndef GC1_CS
#define GC1_CS          38
#endif
#ifndef GC1_DC
#define GC1_DC          39
#endif
#ifndef GC1_RST
#define GC1_RST         40
#endif

#ifndef GC2_CS
#define GC2_CS          21
#endif
#ifndef GC2_DC
#define GC2_DC          47
#endif
#ifndef GC2_RST
#define GC2_RST         48
#endif

#ifndef GC_BLK
#define GC_BLK          3
#endif


// =============================================================================
// DATA STRUCTURES
// =============================================================================

enum class ControlMode {
    BRIGHTNESS,
    COLOR
};

struct VirtualLED {
    bool        isOn       = false;
    uint8_t     brightness = 50;
    uint16_t    hue        = 0;
    ControlMode mode       = ControlMode::BRIGHTNESS;
    uint8_t     r = 255;
    uint8_t     g = 0;
    uint8_t     b = 0;
    
    // Previous state for incremental updates
    bool        prevOn         = false;
    uint8_t     prevBrightness = 0;
    uint16_t    prevHue        = 0;
    bool        forceRedraw    = true;  // First draw
};


// =============================================================================
// COLOR CONVERSION
// =============================================================================

void hueToRgb(uint16_t hue, uint8_t& r, uint8_t& g, uint8_t& b) {
    hue = hue % 360;
    
    uint8_t region = hue / 60;
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


// =============================================================================
// GC9A01 DRAWING HELPERS
// =============================================================================

// Check if angle (in degrees, 0=top, clockwise) is within arc range
static inline bool angleInArc(int angleDeg, int startDeg, int endDeg) {
    // Normalize to 0-359
    while (angleDeg < 0) angleDeg += 360;
    while (angleDeg >= 360) angleDeg -= 360;
    
    if (endDeg <= 360) {
        return angleDeg >= startDeg && angleDeg <= endDeg;
    } else {
        // Arc wraps around 360
        return angleDeg >= startDeg || angleDeg <= (endDeg - 360);
    }
}

// Helper: draw one scanline of the arc
static void drawArcScanline(GC9A01& disp, int16_t cx, int16_t y,
                            int16_t innerRadius, int16_t outerRadius,
                            int startDeg, int endDeg, uint16_t color)
{
    int16_t dy = y - (GC9A01_HEIGHT / 2);
    int32_t dySq = dy * dy;
    int32_t innerSq = innerRadius * innerRadius;
    int32_t outerSq = outerRadius * outerRadius;
    
    if (dySq > outerSq) return;
    int16_t dxOuter = (int16_t)sqrtf(outerSq - dySq);
    int16_t dxInner = (dySq < innerSq) ? (int16_t)sqrtf(innerSq - dySq) : 0;
    
    int16_t runStart = -1;
    
    for (int16_t x = cx - dxOuter; x <= cx + dxOuter; x++) {
        int16_t dx = x - cx;
        int16_t absDx = dx < 0 ? -dx : dx;
        
        if (absDx < dxInner) {
            if (runStart >= 0) {
                disp.drawHLine(runStart, y, x - runStart, color);
                runStart = -1;
            }
            continue;
        }
        
        int angle = (int)(atan2f((float)dx, (float)(-dy)) * 180.0f / M_PI);
        if (angle < 0) angle += 360;
        
        if (angleInArc(angle, startDeg, endDeg)) {
            if (runStart < 0) runStart = x;
        } else {
            if (runStart >= 0) {
                disp.drawHLine(runStart, y, x - runStart, color);
                runStart = -1;
            }
        }
    }
    
    if (runStart >= 0) {
        disp.drawHLine(runStart, y, (cx + dxOuter + 1) - runStart, color);
    }
}

// Arc drawing - expands from center outward
void drawArcFast(GC9A01& disp, int16_t cx, int16_t cy, 
                 int16_t innerRadius, int16_t outerRadius,
                 int startDeg, int endDeg, uint16_t color)
{
    if (outerRadius <= 0 || endDeg <= startDeg) return;
    if (innerRadius < 0) innerRadius = 0;
    
    // Draw from center outward (expanding effect)
    for (int16_t offset = 0; offset <= outerRadius; offset++) {
        // Draw line above center
        if (cy - offset >= cy - outerRadius) {
            drawArcScanline(disp, cx, cy - offset, innerRadius, outerRadius, startDeg, endDeg, color);
        }
        // Draw line below center (skip offset=0, already drawn)
        if (offset > 0 && cy + offset <= cy + outerRadius) {
            drawArcScanline(disp, cx, cy + offset, innerRadius, outerRadius, startDeg, endDeg, color);
        }
    }
}

// Wrapper for compatibility
void drawPieSliceFast(GC9A01& disp, int16_t cx, int16_t cy, int16_t radius,
                      int startDeg, int endDeg, uint16_t color)
{
    drawArcFast(disp, cx, cy, 0, radius, startDeg, endDeg, color);
}

void drawCenteredString(GC9A01& disp, int16_t cy, const char* str, 
                        uint16_t color, uint16_t bg, uint8_t size = 2)
{
    int16_t strWidth = strlen(str) * 6 * size;
    int16_t x = (GC9A01_WIDTH - strWidth) / 2;
    disp.drawString(x, cy, str, color, bg, size);
}


// =============================================================================
// DISPLAY UPDATE FUNCTIONS
// =============================================================================

void updateSSD1306(PCA9548A& mux, SSD1306& display, int ledIndex, const VirtualLED& led) {
    mux.selectChannel(ledIndex);
    display.setInverted(led.isOn);
    display.clear();
    
    char line1[24];
    snprintf(line1, sizeof(line1), "LED %d: %s", ledIndex + 1, led.isOn ? "ON" : "OFF");
    display.drawString(0, 0, line1);
    
    char line2[24];
    snprintf(line2, sizeof(line2), "Brightness: %d%%", led.brightness);
    display.drawString(0, 24, line2);
    
    char line3[24];
    snprintf(line3, sizeof(line3), "(%d,%d,%d)", led.r, led.g, led.b);
    display.drawString(0, 48, line3);
    
    const char* modeStr = (led.mode == ControlMode::BRIGHTNESS) ? "[B]" : "[C]";
    display.drawString(104, 0, modeStr);
    
    display.update();
}


void updateGC9A01(GC9A01& display, int ledIndex, VirtualLED& led) {
    const int16_t cx = GC9A01_WIDTH / 2;
    const int16_t cy = GC9A01_HEIGHT / 2;
    const int16_t outerRadius = 100;
    const int16_t innerRadius = 40;  // Protects center text area
    
    // Detect what changed
    bool toggledOnOff = (led.isOn != led.prevOn);
    bool colorChanged = (led.hue != led.prevHue);
    bool brightnessChanged = (led.brightness != led.prevBrightness);
    bool needFullRedraw = led.forceRedraw || toggledOnOff;
    
    char name[16];
    snprintf(name, sizeof(name), "LED %d", ledIndex + 1);
    
    if (needFullRedraw) {
        // Full screen clear only on toggle or first draw
        display.fillScreen(COLOR_BLACK);
        
        if (led.isOn) {
            uint16_t arcColor = GC9A01::color565(led.r, led.g, led.b);
            
            if (led.brightness > 0) {
                int endAngle = (int)(led.brightness * 3.6f);
                drawArcFast(display, cx, cy, innerRadius, outerRadius, 0, endAngle, arcColor);
            }
            
            drawCenteredString(display, cy - 16, name, COLOR_WHITE, COLOR_BLACK, 2);
            
            char pct[16];
            snprintf(pct, sizeof(pct), "%d%%", led.brightness);
            drawCenteredString(display, cy + 8, pct, arcColor, COLOR_BLACK, 2);
        } else {
            drawCenteredString(display, cy - 8, name, COLOR_GRAY, COLOR_BLACK, 2);
            drawCenteredString(display, cy + 16, "OFF", COLOR_GRAY, COLOR_BLACK, 2);
        }
    } 
    else if (led.isOn && colorChanged) {
        // Color change - redraw arc in new color (no center redraw needed!)
        uint16_t arcColor = GC9A01::color565(led.r, led.g, led.b);
        int endAngle = (int)(led.brightness * 3.6f);
        
        if (led.brightness > 0) {
            drawArcFast(display, cx, cy, innerRadius, outerRadius, 0, endAngle, arcColor);
        }
        
        // Clear and redraw percentage text (fit within inner circle)
        display.fillRect(cx - 24, cy + 6, 48, 16, COLOR_BLACK);
        char pct[16];
        snprintf(pct, sizeof(pct), "%d%%", led.brightness);
        drawCenteredString(display, cy + 8, pct, arcColor, COLOR_BLACK, 2);
    }
    else if (led.isOn && brightnessChanged) {
        // Brightness change - incremental update
        uint16_t arcColor = GC9A01::color565(led.r, led.g, led.b);
        int oldAngle = (int)(led.prevBrightness * 3.6f);
        int newAngle = (int)(led.brightness * 3.6f);
        
        if (newAngle > oldAngle) {
            drawArcFast(display, cx, cy, innerRadius, outerRadius, oldAngle, newAngle, arcColor);
        } else {
            drawArcFast(display, cx, cy, innerRadius, outerRadius, newAngle, oldAngle, COLOR_BLACK);
        }
        
        // Clear and redraw percentage text (fit within inner circle)
        display.fillRect(cx - 24, cy + 6, 48, 16, COLOR_BLACK);
        char pct[16];
        snprintf(pct, sizeof(pct), "%d%%", led.brightness);
        drawCenteredString(display, cy + 8, pct, arcColor, COLOR_BLACK, 2);
    }
    
    // Update previous state
    led.prevOn = led.isOn;
    led.prevBrightness = led.brightness;
    led.prevHue = led.hue;
    led.forceRedraw = false;
}


// =============================================================================
// MAIN APPLICATION
// =============================================================================

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "Dual LED Controller starting...");
    
    // -------------------------------------------------------------------------
    // Initialize touch sensors
    // -------------------------------------------------------------------------
    TouchSensor touch[2] = {
        TouchSensor(static_cast<gpio_num_t>(TOUCH1_PIN), true),
        TouchSensor(static_cast<gpio_num_t>(TOUCH2_PIN), true)
    };
    touch[0].init();
    touch[1].init();
    ESP_LOGI(TAG, "Touch sensors initialized");
    
    // -------------------------------------------------------------------------
    // Initialize encoders
    // -------------------------------------------------------------------------
    RotaryEncoder encoder[2] = {
        RotaryEncoder(static_cast<gpio_num_t>(ENC1_CLK), 
                      static_cast<gpio_num_t>(ENC1_DT), 
                      static_cast<gpio_num_t>(ENC1_SW)),
        RotaryEncoder(static_cast<gpio_num_t>(ENC2_CLK), 
                      static_cast<gpio_num_t>(ENC2_DT), 
                      static_cast<gpio_num_t>(ENC2_SW))
    };
    encoder[0].init();
    encoder[1].init();
    int32_t lastEncoderPos[2] = {0, 0};
    ESP_LOGI(TAG, "Encoders initialized");
    
    // -------------------------------------------------------------------------
    // Initialize I2C mux and SSD1306 displays
    // -------------------------------------------------------------------------
    PCA9548A mux(static_cast<gpio_num_t>(I2C_SDA), 
                 static_cast<gpio_num_t>(I2C_SCL));
    if (!mux.init()) {
        ESP_LOGE(TAG, "Failed to init I2C mux!");
        return;
    }
    ESP_LOGI(TAG, "I2C mux initialized");
    
    SSD1306 oled[2] = {
        SSD1306(mux.getBusHandle(), SSD1306_ADDR_DEFAULT),
        SSD1306(mux.getBusHandle(), SSD1306_ADDR_DEFAULT)
    };
    
    mux.selectChannel(0);
    if (!oled[0].init()) {
        ESP_LOGE(TAG, "Failed to init OLED 0!");
    }
    
    mux.selectChannel(1);
    if (!oled[1].init()) {
        ESP_LOGE(TAG, "Failed to init OLED 1!");
    }
    ESP_LOGI(TAG, "SSD1306 displays initialized");
    
    // -------------------------------------------------------------------------
    // Initialize GC9A01 displays
    // -------------------------------------------------------------------------
    GC9A01 tft[2] = {
        GC9A01(static_cast<gpio_num_t>(SPI_MOSI), 
               static_cast<gpio_num_t>(SPI_SCK), 
               static_cast<gpio_num_t>(GC1_CS), 
               static_cast<gpio_num_t>(GC1_DC), 
               static_cast<gpio_num_t>(GC1_RST), 
               static_cast<gpio_num_t>(GC_BLK), 
               SPI2_HOST),
        GC9A01(static_cast<gpio_num_t>(SPI_MOSI), 
               static_cast<gpio_num_t>(SPI_SCK), 
               static_cast<gpio_num_t>(GC2_CS), 
               static_cast<gpio_num_t>(GC2_DC), 
               static_cast<gpio_num_t>(GC2_RST), 
               GPIO_NUM_NC, 
               SPI2_HOST)  // Same host, different CS
    };
    
    if (!tft[0].init()) {
        ESP_LOGE(TAG, "Failed to init TFT 0!");
    }
    if (!tft[1].init()) {
        ESP_LOGE(TAG, "Failed to init TFT 1!");
    }
    ESP_LOGI(TAG, "GC9A01 displays initialized");
    
    // -------------------------------------------------------------------------
    // Virtual LED states
    // -------------------------------------------------------------------------
    VirtualLED led[2];
    
    // Initial draw
    for (int i = 0; i < 2; i++) {
        updateSSD1306(mux, oled[i], i, led[i]);
        updateGC9A01(tft[i], i, led[i]);
    }
    
    ESP_LOGI(TAG, "Entering main loop...");
    
    // -------------------------------------------------------------------------
    // Main loop
    // -------------------------------------------------------------------------
    while (true) {
        bool needsUpdate[2] = {false, false};
        
        touch[0].update();
        touch[1].update();
        
        for (int i = 0; i < 2; i++) {
            // Touch toggle
            if (touch[i].wasTouched()) {
                led[i].isOn = !led[i].isOn;
                needsUpdate[i] = true;
                ESP_LOGI(TAG, "LED %d toggled: %s", i + 1, led[i].isOn ? "ON" : "OFF");
            }
            
            // Encoder button - mode switch
            if (encoder[i].wasButtonPressed()) {
                if (led[i].mode == ControlMode::BRIGHTNESS) {
                    led[i].mode = ControlMode::COLOR;
                } else {
                    led[i].mode = ControlMode::BRIGHTNESS;
                }
                needsUpdate[i] = true;
                ESP_LOGI(TAG, "LED %d mode: %s", i + 1, 
                         led[i].mode == ControlMode::BRIGHTNESS ? "BRIGHTNESS" : "COLOR");
            }
            
            // Encoder rotation
            int32_t currentPos = encoder[i].getPosition();
            int32_t delta = currentPos - lastEncoderPos[i];
            
            if (delta != 0) {
                lastEncoderPos[i] = currentPos;
                
                if (led[i].mode == ControlMode::BRIGHTNESS) {
                    int newBright = led[i].brightness + delta;
                    if (newBright < 0) newBright = 0;
                    if (newBright > 100) newBright = 100;
                    led[i].brightness = (uint8_t)newBright;
                } else {
                    int newHue = led[i].hue + (delta * 5);
                    while (newHue < 0) newHue += 360;
                    while (newHue >= 360) newHue -= 360;
                    led[i].hue = (uint16_t)newHue;
                    hueToRgb(led[i].hue, led[i].r, led[i].g, led[i].b);
                }
                
                needsUpdate[i] = true;
            }
        }
        
        // Update displays
        for (int i = 0; i < 2; i++) {
            if (needsUpdate[i]) {
                updateSSD1306(mux, oled[i], i, led[i]);
                
                // Skip TFT update if LED is off and wasn't just toggled
                if (led[i].isOn || led[i].isOn != led[i].prevOn) {
                    updateGC9A01(tft[i], i, led[i]);
                }
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}