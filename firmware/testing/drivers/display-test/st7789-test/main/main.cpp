/**
 * @file main.cpp
 * @brief ST7789 TFT display test application (ESP-IDF).
 *
 * @details
 * Demonstrates the ST7789 component:
 * - Display initialization
 * - Color fills
 * - Drawing primitives (lines, rectangles, circles)
 * - Text rendering
 * - Animation demo
 */

#include <stdio.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "st7789.h"


static const char* TAG = "ST7789_TEST";


#ifndef ST7789_WIDTH
#define ST7789_WIDTH 240
#endif
#ifndef ST7789_HEIGHT
#define ST7789_HEIGHT 280
#endif
#ifndef ST7789_MOSI
#define ST7789_MOSI 23
#endif
#ifndef ST7789_SCK
#define ST7789_SCK 18
#endif
#ifndef ST7789_CS
#define ST7789_CS 5
#endif
#ifndef ST7789_DC
#define ST7789_DC 16
#endif
#ifndef ST7789_RST
#define ST7789_RST 17
#endif
#ifndef ST7789_BLK
#define ST7789_BLK 4
#endif


extern "C" void app_main(void) {
    ESP_LOGI(TAG, "=== ST7789 TFT Test ===");
    ESP_LOGI(TAG, "Size: %dx%d", ST7789_WIDTH, ST7789_HEIGHT);
    ESP_LOGI(TAG, "MOSI=%d, SCK=%d, CS=%d, DC=%d, RST=%d, BLK=%d",
             ST7789_MOSI, ST7789_SCK, ST7789_CS, ST7789_DC, ST7789_RST, ST7789_BLK);
    
    /*
     * -------------------------------------------------------------------------
     * CREATE AND INITIALIZE DISPLAY
     * -------------------------------------------------------------------------
     */
    ST7789 display(
        ST7789_WIDTH, ST7789_HEIGHT,
        (gpio_num_t)ST7789_MOSI,
        (gpio_num_t)ST7789_SCK,
        (gpio_num_t)ST7789_CS,
        (gpio_num_t)ST7789_DC,
        (gpio_num_t)ST7789_RST,
        (gpio_num_t)ST7789_BLK
    );
    
    if (!display.init()) {
        ESP_LOGE(TAG, "Display init failed!");
        return;
    }
    
    ESP_LOGI(TAG, "Display initialized. Running tests...");

    
    /*
     * -------------------------------------------------------------------------
     * TEST 1: Color Fills
     * -------------------------------------------------------------------------
     */
     while (1) { 
    ESP_LOGI(TAG, "Test 1: Colors");
    
    uint16_t colors[] = {COLOR_RED, COLOR_GREEN, COLOR_BLUE, COLOR_YELLOW, COLOR_CYAN, COLOR_MAGENTA};
    const char* colorNames[] = {"Red", "Green", "Blue", "Yellow", "Cyan", "Magenta"};
    
    for (int i = 0; i < 6; i++) {
        display.fillScreen(colors[i]);
        display.drawString(70, 130, colorNames[i], COLOR_WHITE, colors[i], 2);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    
    /*
     * -------------------------------------------------------------------------
     * TEST 2: Text Display
     * -------------------------------------------------------------------------
     */
    ESP_LOGI(TAG, "Test 2: Text");
    
    display.fillScreen(COLOR_BLACK);
    display.drawString(50, 30, "ST7789V2", COLOR_WHITE, COLOR_BLACK, 3);
    display.drawString(60, 70, "240 x 280", COLOR_CYAN, COLOR_BLACK, 2);
    display.drawString(40, 100, "ESP-IDF Driver", COLOR_GREEN, COLOR_BLACK, 2);
    display.drawString(60, 130, "65K Colors", COLOR_YELLOW, COLOR_BLACK, 2);
    display.drawString(30, 170, "SPI @ 20MHz", COLOR_MAGENTA, COLOR_BLACK, 2);
    
    // Show some special characters
    display.drawString(10, 210, "!@#$%^&*()_+-=[]", COLOR_ORANGE, COLOR_BLACK, 1);
    display.drawString(10, 230, "ABCDEFGHIJKLMNOP", COLOR_WHITE, COLOR_BLACK, 1);
    display.drawString(10, 250, "abcdefghijklmnop", COLOR_GRAY, COLOR_BLACK, 1);
    
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    /*
     * -------------------------------------------------------------------------
     * TEST 3: Lines
     * -------------------------------------------------------------------------
     */
    ESP_LOGI(TAG, "Test 3: Lines");
    
    display.fillScreen(COLOR_BLACK);
    display.drawString(80, 10, "Lines", COLOR_WHITE, COLOR_BLACK, 2);
    
    // Horizontal lines
    for (int y = 40; y < 140; y += 10) {
        uint16_t color = ST7789::color565(y * 2, 100, 255 - y);
        display.drawHLine(20, y, 200, color);
    }
    
    // Vertical lines
    for (int x = 20; x < 220; x += 10) {
        uint16_t color = ST7789::color565(100, x, 255 - x);
        display.drawVLine(x, 150, 100, color);
    }
    
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    // Diagonal lines
    display.fillScreen(COLOR_BLACK);
    display.drawString(50, 10, "Diagonal", COLOR_WHITE, COLOR_BLACK, 2);
    
    int cx = 120, cy = 160;
    for (int angle = 0; angle < 360; angle += 15) {
        float rad = angle * 3.14159f / 180.0f;
        int x = cx + (int)(100 * cosf(rad));
        int y = cy + (int)(100 * sinf(rad));
        uint16_t color = ST7789::color565(angle * 255 / 360, 100, 255 - angle * 255 / 360);
        display.drawLine(cx, cy, x, y, color);
    }
    
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    /*
     * -------------------------------------------------------------------------
     * TEST 4: Rectangles
     * -------------------------------------------------------------------------
     */
    ESP_LOGI(TAG, "Test 4: Rectangles");
    
    display.fillScreen(COLOR_BLACK);
    display.drawString(40, 10, "Rectangles", COLOR_WHITE, COLOR_BLACK, 2);
    
    // Nested outline rectangles
    for (int i = 0; i < 10; i++) {
        int offset = i * 10;
        uint16_t color = ST7789::color565(i * 25, 100, 255 - i * 25);
        display.drawRect(20 + offset, 40 + offset, 200 - offset * 2, 100 - offset * 2, color);
    }
    
    // Filled rectangles
    display.fillRect(30, 160, 60, 40, COLOR_RED);
    display.fillRect(100, 160, 60, 40, COLOR_GREEN);
    display.fillRect(170, 160, 60, 40, COLOR_BLUE);
    
    display.fillRect(65, 210, 60, 40, COLOR_YELLOW);
    display.fillRect(135, 210, 60, 40, COLOR_CYAN);
    
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    /*
     * -------------------------------------------------------------------------
     * TEST 5: Circles
     * -------------------------------------------------------------------------
     */
    ESP_LOGI(TAG, "Test 5: Circles");
    
    display.fillScreen(COLOR_BLACK);
    display.drawString(60, 10, "Circles", COLOR_WHITE, COLOR_BLACK, 2);
    
    // Concentric circles
    for (int r = 10; r <= 90; r += 10) {
        uint16_t color = ST7789::color565(r * 2, 50, 255 - r * 2);
        display.drawCircle(120, 150, r, color);
    }
    
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    // Filled circles
    display.fillScreen(COLOR_BLACK);
    display.drawString(20, 10, "Filled Circles", COLOR_WHITE, COLOR_BLACK, 2);
    
    display.fillCircle(60, 100, 40, COLOR_RED);
    display.fillCircle(180, 100, 40, COLOR_BLUE);
    display.fillCircle(120, 180, 40, COLOR_GREEN);
    
    // Overlapping effect
    display.fillCircle(90, 140, 25, COLOR_YELLOW);
    display.fillCircle(150, 140, 25, COLOR_CYAN);
    display.fillCircle(120, 100, 25, COLOR_MAGENTA);
    
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    /*
     * -------------------------------------------------------------------------
     * TEST 6: Gradient Demo
     * -------------------------------------------------------------------------
     */
    ESP_LOGI(TAG, "Test 6: Gradient");
    
    display.fillScreen(COLOR_BLACK);
    display.drawString(60, 10, "Gradient", COLOR_WHITE, COLOR_BLACK, 2);
    
    // Horizontal gradient
    for (int x = 0; x < 240; x++) {
        uint16_t color = ST7789::color565(x, 0, 255 - x);
        display.drawVLine(x, 40, 60, color);
    }
    
    // Vertical gradient
    for (int y = 0; y < 100; y++) {
        uint16_t color = ST7789::color565(0, y * 2 + 55, 255 - y * 2);
        display.drawHLine(0, 110 + y, 240, color);
    }
    
    // Rainbow gradient
    for (int x = 0; x < 240; x++) {
        int hue = x * 360 / 240;
        uint8_t r, g, b;
        
        // Simple HSV to RGB (hue only, full saturation/value)
        int h = hue / 60;
        int f = hue % 60;
        
        switch (h) {
            case 0: r = 255; g = f * 255 / 60; b = 0; break;
            case 1: r = 255 - f * 255 / 60; g = 255; b = 0; break;
            case 2: r = 0; g = 255; b = f * 255 / 60; break;
            case 3: r = 0; g = 255 - f * 255 / 60; b = 255; break;
            case 4: r = f * 255 / 60; g = 0; b = 255; break;
            default: r = 255; g = 0; b = 255 - f * 255 / 60; break;
        }
        
        uint16_t color = ST7789::color565(r, g, b);
        display.drawVLine(x, 220, 50, color);
    }
    
    vTaskDelay(pdMS_TO_TICKS(3000));
    
    /*
     * -------------------------------------------------------------------------
     * TEST 7: Animation - Bouncing Ball
     * -------------------------------------------------------------------------
     */
    ESP_LOGI(TAG, "Test 7: Animation");
    
    int ballX = 120;
    int ballY = 140;
    int ballDX = 4;
    int ballDY = 3;
    int ballR = 15;
    
    for (int frame = 0; frame < 200; frame++) {
        display.fillScreen(COLOR_BLACK);
        
        // Draw border
        display.drawRect(0, 0, 240, 280, COLOR_WHITE);
        
        // Draw ball
        display.fillCircle(ballX, ballY, ballR, COLOR_RED);
        display.drawCircle(ballX, ballY, ballR, COLOR_WHITE);
        
        // Update position
        ballX += ballDX;
        ballY += ballDY;
        
        // Bounce off walls
        if (ballX - ballR <= 0 || ballX + ballR >= 240) {
            ballDX = -ballDX;
            ballX += ballDX;
        }
        if (ballY - ballR <= 0 || ballY + ballR >= 280) {
            ballDY = -ballDY;
            ballY += ballDY;
        }
        
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    
    /*
     * -------------------------------------------------------------------------
     * FINAL: Complete message
     * -------------------------------------------------------------------------
     */
    ESP_LOGI(TAG, "All tests complete!");
    
    display.fillScreen(COLOR_BLACK);
    display.fillRect(20, 80, 200, 120, COLOR_GREEN);
    display.fillRect(30, 90, 180, 100, COLOR_BLACK);
    display.drawString(45, 110, "All Tests", COLOR_WHITE, COLOR_BLACK, 2);
    display.drawString(45, 140, "Complete!", COLOR_GREEN, COLOR_BLACK, 2);
    
   
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
