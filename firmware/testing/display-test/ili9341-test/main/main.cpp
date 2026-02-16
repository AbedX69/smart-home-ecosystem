/**
 * @file main.cpp
 * @brief ILI9341 TFT display + XPT2046 touch test (ESP-IDF).
 *
 * @details
 * Demonstrates:
 * - Display initialization and drawing
 * - Touch detection and coordinate reading
 * - Simple drawing app (draw where you touch)
 */

#include <stdio.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "ili9341.h"
#include "xpt2046.h"


static const char* TAG = "ILI9341_TEST";


// Default pin definitions
#ifndef ILI9341_MOSI
#define ILI9341_MOSI    23
#endif
#ifndef ILI9341_MISO
#define ILI9341_MISO    19
#endif
#ifndef ILI9341_SCK
#define ILI9341_SCK     18
#endif
#ifndef ILI9341_CS
#define ILI9341_CS      5
#endif
#ifndef ILI9341_DC
#define ILI9341_DC      16
#endif
#ifndef ILI9341_RST
#define ILI9341_RST     17
#endif
#ifndef ILI9341_LED
#define ILI9341_LED     4
#endif
#ifndef XPT2046_CS
#define XPT2046_CS      15
#endif
#ifndef XPT2046_IRQ
#define XPT2046_IRQ     36
#endif


extern "C" void app_main(void) {
    ESP_LOGI(TAG, "=== ILI9341 + XPT2046 Touch Test ===");
    ESP_LOGI(TAG, "Display: MOSI=%d, MISO=%d, SCK=%d, CS=%d, DC=%d, RST=%d, LED=%d",
             ILI9341_MOSI, ILI9341_MISO, ILI9341_SCK, ILI9341_CS, ILI9341_DC, ILI9341_RST, ILI9341_LED);
    ESP_LOGI(TAG, "Touch: CS=%d, IRQ=%d", XPT2046_CS, XPT2046_IRQ);
    
    /*
     * =========================================================================
     * STEP 1: Initialize display
     * =========================================================================
     */
    ILI9341 display(
        (gpio_num_t)ILI9341_MOSI,
        (gpio_num_t)ILI9341_MISO,
        (gpio_num_t)ILI9341_SCK,
        (gpio_num_t)ILI9341_CS,
        (gpio_num_t)ILI9341_DC,
        (gpio_num_t)ILI9341_RST,
        (gpio_num_t)ILI9341_LED
    );
    
    if (!display.init()) {
        ESP_LOGE(TAG, "Display init failed!");
        return;
    }
    ESP_LOGI(TAG, "Display initialized");
    display.setRotation(2);
    ESP_LOGI(TAG, "Display size: %dx%d", display.getWidth(), display.getHeight());

    /*
     * =========================================================================
     * STEP 2: Initialize touch (shares SPI bus with display)
     * =========================================================================
     */
    XPT2046 touch(
        display.getSpiHost(),       // Same SPI host as display
        (gpio_num_t)XPT2046_CS,
        (gpio_num_t)XPT2046_IRQ
    );
    
    if (!touch.init()) {
        ESP_LOGE(TAG, "Touch init failed!");
        return;
    }
    ESP_LOGI(TAG, "Touch initialized");
    
    // Set calibration (you may need to adjust these for your display)
    touch.setCalibration(200,3800, 3700,  300);
    
    /*ponh,. mn6
     * =========================================================================
     * STEP 3: Display test - show colors
     * =========================================================================
     */
    ESP_LOGI(TAG, "Test 1: Colors");
    
    uint16_t colors[] = {COLOR_RED, COLOR_GREEN, COLOR_BLUE, COLOR_YELLOW, COLOR_CYAN, COLOR_MAGENTA};
    const char* names[] = {"Red", "Green", "Blue", "Yellow", "Cyan", "Magenta"};
    
    for (int i = 0; i < 6; i++) {
        display.fillScreen(colors[i]);
        display.drawString(80, 150, names[i], COLOR_WHITE, colors[i], 2);
        vTaskDelay(pdMS_TO_TICKS(400));
    }
    
    /*
     * =========================================================================
     * STEP 4: Touch coordinate test screen
     * =========================================================================
     */
    ESP_LOGI(TAG, "Test 2: Touch coordinates");
    
    display.fillScreen(COLOR_BLACK);
    display.drawString(30, 10, "Touch Coordinate Test", COLOR_WHITE, COLOR_BLACK, 1);
    display.drawRect(0, 0, 240, 320, COLOR_WHITE);
    
    // Touch coordinate area
    display.fillRect(10, 40, 220, 80, COLOR_BLUE);
    display.drawString(50, 55, "Touch Position:", COLOR_WHITE, COLOR_BLUE, 1);
    display.drawString(60, 80, "Not touched", COLOR_WHITE, COLOR_BLUE, 1);
    
    // Raw value area
    display.fillRect(10, 130, 220, 60, COLOR_GREEN);
    display.drawString(60, 140, "Raw Values:", COLOR_BLACK, COLOR_GREEN, 1);
    display.drawString(80, 165, "---", COLOR_BLACK, COLOR_GREEN, 1);
    
    // Instructions
    display.drawString(20, 210, "Touch screen to test", COLOR_GRAY, COLOR_BLACK, 1);
    display.drawString(20, 230, "Hold 2s for draw mode", COLOR_GRAY, COLOR_BLACK, 1);
    
    // Color buttons for draw mode
    display.fillRect(10, 270, 40, 40, COLOR_RED);
    display.fillRect(60, 270, 40, 40, COLOR_GREEN);
    display.fillRect(110, 270, 40, 40, COLOR_BLUE);
    display.fillRect(160, 270, 40, 40, COLOR_YELLOW);
    display.fillRect(210, 270, 20, 40, COLOR_WHITE);  // Clear button
    
    int touchCount = 0;
    int64_t touchStart = 0;
    bool inDrawMode = false;
    uint16_t drawColor = COLOR_BLUE;
    
while (1) {
 
    if (touch.isTouched()) {
            int16_t x, y;
            int16_t rawX, rawY;
            
            if (touch.getPosition(&x, &y) && touch.getRawPosition(&rawX, &rawY)) {
                
                // Check for long press to enter draw mode
                if (touchStart == 0) {
                    touchStart = esp_timer_get_time();
                } else if (!inDrawMode && (esp_timer_get_time() - touchStart) > 2000000) {
                    // 2 second hold = enter draw mode
                    inDrawMode = true;
                    display.fillScreen(COLOR_WHITE);
                    display.drawString(70, 5, "Draw Mode", COLOR_BLACK, COLOR_WHITE, 2);
                    
                    // Color palette at bottom
                    display.fillRect(0, 290, 48, 30, COLOR_RED);
                    display.fillRect(48, 290, 48, 30, COLOR_GREEN);
                    display.fillRect(96, 290, 48, 30, COLOR_BLUE);
                    display.fillRect(144, 290, 48, 30, COLOR_YELLOW);
                    display.fillRect(192, 290, 48, 30, COLOR_BLACK);
                    display.drawString(200, 300, "CLR", COLOR_WHITE, COLOR_BLACK, 1);
                    
                    ESP_LOGI(TAG, "Entered draw mode!");
                }
                
                if (inDrawMode) {
                    // Check if touching color palette
                    if (y >= 290) {
                        if (x < 48) drawColor = COLOR_RED;
                        else if (x < 96) drawColor = COLOR_GREEN;
                        else if (x < 144) drawColor = COLOR_BLUE;
                        else if (x < 192) drawColor = COLOR_YELLOW;
                        else {
                            // Clear screen
                            display.fillRect(0, 30, 240, 260, COLOR_WHITE);
                        }
                    } else if (y > 30) {
                        // Draw where touched
                        display.fillCircle(x, y, 4, drawColor);
                    }
                } else {
                    // Update coordinate display
                    char buf[32];
                    
                    // Clear old values
                    display.fillRect(30, 70, 180, 30, COLOR_BLUE);
                    display.fillRect(50, 155, 150, 25, COLOR_GREEN);
                    
                    // Show calibrated position
                    snprintf(buf, sizeof(buf), "X:%3d  Y:%3d", x, y);
                    display.drawString(50, 80, buf, COLOR_WHITE, COLOR_BLUE, 2);
                    
                    // Show raw values
                    snprintf(buf, sizeof(buf), "X:%4d  Y:%4d", rawX, rawY);
                    display.drawString(55, 165, buf, COLOR_BLACK, COLOR_GREEN, 1);
                    
                    touchCount++;
                    ESP_LOGI(TAG, "Touch #%d: pos(%d,%d) raw(%d,%d)", touchCount, x, y, rawX, rawY);
                }
            }
        } else {
            touchStart = 0;  // Reset long press timer
        }
        
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
