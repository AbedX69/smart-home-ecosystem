/**
 * @file main.cpp
 * @brief GC9A01 round TFT display test application (ESP-IDF).
 *
 * @details
 * Demonstrates the GC9A01 component:
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
#include "gc9a01.h"


static const char* TAG = "GC9A01_TEST";


#ifndef GC9A01_MOSI
#define GC9A01_MOSI 23
#endif
#ifndef GC9A01_SCK
#define GC9A01_SCK 18
#endif
#ifndef GC9A01_CS
#define GC9A01_CS 5
#endif
#ifndef GC9A01_DC
#define GC9A01_DC 16
#endif
#ifndef GC9A01_RST
#define GC9A01_RST 17
#endif
#ifndef GC9A01_BLK
#define GC9A01_BLK 4
#endif


extern "C" void app_main(void) {
    ESP_LOGI(TAG, "=== GC9A01 Round TFT Test ===");
    ESP_LOGI(TAG, "MOSI=%d, SCK=%d, CS=%d, DC=%d, RST=%d, BLK=%d",
             GC9A01_MOSI, GC9A01_SCK, GC9A01_CS, GC9A01_DC, GC9A01_RST, GC9A01_BLK);
    
    /*
     * -------------------------------------------------------------------------
     * CREATE AND INITIALIZE DISPLAY
     * -------------------------------------------------------------------------
     */
    GC9A01 display(
        (gpio_num_t)GC9A01_MOSI,
        (gpio_num_t)GC9A01_SCK,
        (gpio_num_t)GC9A01_CS,
        (gpio_num_t)GC9A01_DC,
        (gpio_num_t)GC9A01_RST,
        (gpio_num_t)GC9A01_BLK
    );
    
    if (!display.init()) {
        ESP_LOGE(TAG, "Display init failed!");
        return;
    }
    
    ESP_LOGI(TAG, "Display initialized. Running tests...");
        
    while (1) {

    /*
     * -------------------------------------------------------------------------
     * TEST 1: Color Fills
     * -------------------------------------------------------------------------
     */
    ESP_LOGI(TAG, "Test 1: Colors");
    
    uint16_t colors[] = {COLOR_RED, COLOR_GREEN, COLOR_BLUE, COLOR_YELLOW, COLOR_CYAN, COLOR_MAGENTA};
    const char* colorNames[] = {"Red", "Green", "Blue", "Yellow", "Cyan", "Magenta"};
    
    for (int i = 0; i < 6; i++) {
        display.fillScreen(colors[i]);
        display.drawString(80, 116, colorNames[i], COLOR_WHITE, colors[i], 2);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    
    /*
     * -------------------------------------------------------------------------
     * TEST 2: Text Display
     * -------------------------------------------------------------------------
     */
    ESP_LOGI(TAG, "Test 2: Text");
    
    display.fillScreen(COLOR_BLACK);
    display.drawString(60, 40, "GC9A01", COLOR_WHITE, COLOR_BLACK, 3);
    display.drawString(50, 80, "Round TFT", COLOR_CYAN, COLOR_BLACK, 2);
    display.drawString(55, 110, "240 x 240", COLOR_GREEN, COLOR_BLACK, 2);
    display.drawString(45, 140, "ESP-IDF Driver", COLOR_YELLOW, COLOR_BLACK, 1);
    display.drawString(70, 160, "65K Colors", COLOR_MAGENTA, COLOR_BLACK, 1);
    
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    /*
     * -------------------------------------------------------------------------
     * TEST 3: Lines
     * -------------------------------------------------------------------------
     */
    ESP_LOGI(TAG, "Test 3: Lines");
    
    display.fillScreen(COLOR_BLACK);
    display.drawString(80, 10, "Lines", COLOR_WHITE, COLOR_BLACK, 2);
    
    // Draw lines from center outward
    int cx = 120, cy = 130;
    for (int angle = 0; angle < 360; angle += 15) {
        float rad = angle * 3.14159f / 180.0f;
        int x = cx + (int)(80 * cosf(rad));
        int y = cy + (int)(80 * sinf(rad));
        uint16_t color = GC9A01::color565(angle * 255 / 360, 100, 255 - angle * 255 / 360);
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
    display.drawString(50, 30, "Rectangles", COLOR_WHITE, COLOR_BLACK, 2);
    
    // Nested rectangles
    for (int i = 0; i < 8; i++) {
        int offset = i * 12;
        uint16_t color = GC9A01::color565(i * 30, 100, 255 - i * 30);
        display.drawRect(40 + offset, 50 + offset, 160 - offset * 2, 140 - offset * 2, color);
    }
    
    // Filled rectangles
    display.fillRect(70, 180, 40, 40, COLOR_RED);
    display.fillRect(130, 180, 40, 40, COLOR_GREEN);
    
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    /*
     * -------------------------------------------------------------------------
     * TEST 5: Circles (perfect for round display!)
     * -------------------------------------------------------------------------
     */
    ESP_LOGI(TAG, "Test 5: Circles");
    
    display.fillScreen(COLOR_BLACK);
    display.drawString(60, 10, "Circles", COLOR_WHITE, COLOR_BLACK, 2);
    
    // Concentric circles
    for (int r = 20; r <= 100; r += 10) {
        uint16_t color = GC9A01::color565(r * 2, 50, 255 - r * 2);
        display.drawCircle(120, 130, r, color);
    }
    
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    // Filled circles
    display.fillScreen(COLOR_BLACK);
    display.drawString(30, 10, "Filled Circles", COLOR_WHITE, COLOR_BLACK, 2);
    
    display.fillCircle(70, 100, 40, COLOR_RED);
    display.fillCircle(170, 100, 40, COLOR_BLUE);
    display.fillCircle(120, 170, 40, COLOR_GREEN);
    
    // Overlapping creates nice effect
    display.fillCircle(120, 120, 30, COLOR_YELLOW);
    
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    /*
     * -------------------------------------------------------------------------
     * TEST 6: Clock Face Demo (shows off round display)
     * -------------------------------------------------------------------------
     */
    ESP_LOGI(TAG, "Test 6: Clock Face");
    
    display.fillScreen(COLOR_BLACK);
    
    // Draw clock face
    display.fillCircle(120, 120, 115, COLOR_WHITE);
    display.fillCircle(120, 120, 110, COLOR_BLACK);
    
    // Hour markers
    for (int h = 0; h < 12; h++) {
        float angle = (h * 30 - 90) * 3.14159f / 180.0f;
        int x1 = 120 + (int)(95 * cosf(angle));
        int y1 = 120 + (int)(95 * sinf(angle));
        int x2 = 120 + (int)(105 * cosf(angle));
        int y2 = 120 + (int)(105 * sinf(angle));
        display.drawLine(x1, y1, x2, y2, COLOR_WHITE);
    }
    
    // Clock hands (static for demo)
    // Hour hand (pointing to 10)
    float hourAngle = (10 * 30 - 90) * 3.14159f / 180.0f;
    display.drawLine(120, 120, 120 + (int)(50 * cosf(hourAngle)), 120 + (int)(50 * sinf(hourAngle)), COLOR_WHITE);
    
    // Minute hand (pointing to 2)
    float minAngle = (10 * 6 - 90) * 3.14159f / 180.0f;
    display.drawLine(120, 120, 120 + (int)(80 * cosf(minAngle)), 120 + (int)(80 * sinf(minAngle)), COLOR_CYAN);
    
    // Center dot
    display.fillCircle(120, 120, 5, COLOR_RED);
    
    vTaskDelay(pdMS_TO_TICKS(3000));
    /*
     * -------------------------------------------------------------------------
     * TEST 7: Animation - Bouncing Ball
     * -------------------------------------------------------------------------
     */
    ESP_LOGI(TAG, "Test 7: Animation");
    
    int ballX = 120;
    int ballY = 120;
    int ballDX = 3;
    int ballDY = 2;
    int ballR = 15;

    for (int frame = 0; frame < 300; frame++) {
        display.fillScreen(COLOR_BLACK);
        display.drawCircle(120, 120, 110, COLOR_WHITE);

        
        // Draw circular boundary
        
        // Draw ball
        display.fillCircle(ballX, ballY, ballR, COLOR_RED);
        
        // Update position
        ballX += ballDX;
        ballY += ballDY;
        
        // Bounce off circular boundary
        int dx = ballX - 120;
        int dy = ballY - 120;
        float dist = sqrtf(dx * dx + dy * dy);
        
        if (dist + ballR >= 110) {
            // Reflect velocity
            float nx = dx / dist;
            float ny = dy / dist;
            float dot = ballDX * nx + ballDY * ny;
            ballDX = ballDX - 2 * dot * nx;
            ballDY = ballDY - 2 * dot * ny;
            
            // Move ball inside boundary
            ballX = 120 + (int)((110 - ballR - 1) * nx);
            ballY = 120 + (int)((110 - ballR - 1) * ny);
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
    display.fillCircle(120, 120, 100, COLOR_GREEN);
    display.fillCircle(120, 120, 80, COLOR_BLACK);
    display.drawString(55, 100, "All Tests", COLOR_WHITE, COLOR_BLACK, 2);
    display.drawString(50, 130, "Complete!", COLOR_GREEN, COLOR_BLACK, 2);
    
    
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
