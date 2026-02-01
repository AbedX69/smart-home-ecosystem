/**
 * @file main.cpp
 * @brief SSD1306 OLED display test application (ESP-IDF).
 *
 * @details
 * Demonstrates the SSD1306 component:
 * - Display initialization
 * - Drawing primitives (pixels, lines, rectangles, circles)
 * - Text rendering
 * - Display controls (contrast, invert)
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "ssd1306.h"


static const char* TAG = "SSD1306_TEST";


#ifndef SSD1306_SDA
#define SSD1306_SDA 21
#endif

#ifndef SSD1306_SCL
#define SSD1306_SCL 22
#endif


extern "C" void app_main(void) {
    ESP_LOGI(TAG, "=== SSD1306 OLED Test ===");
    ESP_LOGI(TAG, "SDA: GPIO %d", SSD1306_SDA);
    ESP_LOGI(TAG, "SCL: GPIO %d", SSD1306_SCL);
    
    /*
     * -------------------------------------------------------------------------
     * CREATE AND INITIALIZE DISPLAY
     * -------------------------------------------------------------------------
     */
    SSD1306 display((gpio_num_t)SSD1306_SDA, (gpio_num_t)SSD1306_SCL);
    
    if (!display.init()) {
        ESP_LOGE(TAG, "Display init failed!");
        return;
    }
    
    ESP_LOGI(TAG, "Display initialized. Running tests...");
while (1) {
    
    /*
     * -------------------------------------------------------------------------
     * TEST 1: Text Display
     * -------------------------------------------------------------------------
     */
    ESP_LOGI(TAG, "Test 1: Text");
    
    display.clear();
    display.drawString(0, 0, "Hello, World!");
    display.drawString(0, 10, "SSD1306 OLED");
    display.drawString(0, 20, "128x64 pixels");
    display.drawString(0, 30, "ESP-IDF Driver");
    display.update();
    
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    /*
     * -------------------------------------------------------------------------
     * TEST 2: Pixel Drawing
     * -------------------------------------------------------------------------
     */
    ESP_LOGI(TAG, "Test 2: Pixels");
    
    display.clear();
    display.drawString(0, 0, "Pixel Test");
    
    // Draw a diagonal line of pixels
    for (int i = 0; i < 50; i++) {
        display.drawPixel(10 + i, 20 + i/2, true);
    }
    
    display.update();
    
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    /*
     * -------------------------------------------------------------------------
     * TEST 3: Lines
     * -------------------------------------------------------------------------
     */
    ESP_LOGI(TAG, "Test 3: Lines");
    
    display.clear();
    display.drawString(0, 0, "Line Test");
    
    // Horizontal line
    display.drawHLine(0, 20, 128, true);
    
    // Vertical line
    display.drawVLine(64, 25, 30, true);
    
    // Diagonal lines
    display.drawLine(0, 30, 50, 60, true);
    display.drawLine(127, 30, 77, 60, true);
    
    display.update();
    
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    /*
     * -------------------------------------------------------------------------
     * TEST 4: Rectangles
     * -------------------------------------------------------------------------
     */
    ESP_LOGI(TAG, "Test 4: Rectangles");
    
    display.clear();
    display.drawString(0, 0, "Rectangle Test");
    
    // Outline rectangle
    display.drawRect(10, 15, 40, 30, true);
    
    // Filled rectangle
    display.fillRect(70, 15, 40, 30, true);
    
    display.update();
    
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    /*
     * -------------------------------------------------------------------------
     * TEST 5: Circles
     * -------------------------------------------------------------------------
     */
    ESP_LOGI(TAG, "Test 5: Circles");
    
    display.clear();
    display.drawString(0, 0, "Circle Test");
    
    // Outline circle
    display.drawCircle(30, 40, 15, true);
    
    // Filled circle
    display.fillCircle(90, 40, 15, true);
    
    display.update();
    
    vTaskDelay(pdMS_TO_TICKS(2000));
            

    /*
     * -------------------------------------------------------------------------
     * TEST 6: Contrast Control
     * -------------------------------------------------------------------------
     */
    ESP_LOGI(TAG, "Test 6: Contrast");
    
    display.clear();
    display.drawString(0, 0, "Contrast Test");
    display.drawString(0, 10, "Watch brightness");
    display.fillRect(20, 30, 88, 20, true);
    display.update();
    
    // Dim
    for (int c = 255; c >= 0; c -= 5) {
        display.setContrast(c);
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    
    // Brighten
    for (int c = 0; c <= 255; c += 5) {
        display.setContrast(c);
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    
    vTaskDelay(pdMS_TO_TICKS(1000));

    /*
     * -------------------------------------------------------------------------
     * TEST 7: Invert Display
     * -------------------------------------------------------------------------
     */
    ESP_LOGI(TAG, "Test 7: Invert");
    
    display.clear();
    display.drawString(0, 0, "Invert Test");
    display.drawString(0, 10, "Colors will flip");
    display.fillRect(20, 30, 88, 20, true);
    display.update();
    
    for (int i = 0; i < 6; i++) {
        display.setInverted(i % 2 == 1);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    
    display.setInverted(false);
    
    /*
     * -------------------------------------------------------------------------
     * TEST 8: Animation Demo
     * -------------------------------------------------------------------------
     */
    ESP_LOGI(TAG, "Test 8: Animation");
    
    int ballX = 10;
    int ballY = 32;
    int ballDX = 2;
    int ballDY = 1;
    
    for (int frame = 0; frame < 200; frame++) {
        display.clear();
        
        // Draw border
        display.drawRect(0, 0, 128, 64, true);
        
        // Draw ball
        display.fillCircle(ballX, ballY, 5, true);
        
        // Update position
        ballX += ballDX;
        ballY += ballDY;
        
        // Bounce off walls
        if (ballX <= 6 || ballX >= 121) ballDX = -ballDX;
        if (ballY <= 6 || ballY >= 57) ballDY = -ballDY;
        
        display.update();
        vTaskDelay(pdMS_TO_TICKS(30));
    }
    
    /*
     * -------------------------------------------------------------------------
     * FINAL: Show Complete Message
     * -------------------------------------------------------------------------
     */
    ESP_LOGI(TAG, "All tests complete!");
    
    display.clear();
    display.drawString(20, 20, "All Tests");
    display.drawString(20, 32, "Complete!");
    display.drawRect(15, 15, 98, 35, true);
    display.update();
    
    // Keep running
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
