/**
 * @file main.cpp
 * @brief SSD1357 RGB OLED display test (ESP-IDF).
 *
 * @details
 * Demonstrates the SSD1357 component:
 * - Display initialization
 * - Color fills
 * - Drawing primitives
 * - Text rendering
 * - Animation on tiny 64x64 display
 */

#include <stdio.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "ssd1357.h"


static const char* TAG = "SSD1357_TEST";


#ifndef SSD1357_MOSI
#define SSD1357_MOSI 23
#endif
#ifndef SSD1357_SCK
#define SSD1357_SCK 18
#endif
#ifndef SSD1357_CS
#define SSD1357_CS 5
#endif
#ifndef SSD1357_DC
#define SSD1357_DC 16
#endif
#ifndef SSD1357_RST
#define SSD1357_RST 17
#endif


extern "C" void app_main(void) {
    ESP_LOGI(TAG, "=== SSD1357 RGB OLED Test ===");
    ESP_LOGI(TAG, "64x64 pixels, 65K colors");
    ESP_LOGI(TAG, "MOSI=%d, SCK=%d, CS=%d, DC=%d, RST=%d",
             SSD1357_MOSI, SSD1357_SCK, SSD1357_CS, SSD1357_DC, SSD1357_RST);
    
    /*
     * =========================================================================
     * CREATE AND INITIALIZE DISPLAY
     * =========================================================================
     */
    SSD1357 display(
        (gpio_num_t)SSD1357_MOSI,
        (gpio_num_t)SSD1357_SCK,
        (gpio_num_t)SSD1357_CS,
        (gpio_num_t)SSD1357_DC,
        (gpio_num_t)SSD1357_RST
    );
    
    if (!display.init()) {
        ESP_LOGE(TAG, "Display init failed!");
        return;
    }


// DEBUG: Just fill red and stop
ESP_LOGI(TAG, "Filling red...");
display.fillScreen(0xF800);  // Red
ESP_LOGI(TAG, "Done. Stopping here.");
while(1) { vTaskDelay(1000); }
    ESP_LOGI(TAG, "Display initialized. Running tests...");
     while (1) {
    /*
     * =========================================================================
     * TEST 1: Color fills
     * =========================================================================
     */
    ESP_LOGI(TAG, "Test 1: Colors");
    
    uint16_t colors[] = {COLOR_RED, COLOR_GREEN, COLOR_BLUE, COLOR_YELLOW, COLOR_CYAN, COLOR_MAGENTA, COLOR_WHITE};
    const char* names[] = {"R", "G", "B", "Y", "C", "M", "W"};
    
    for (int i = 0; i < 7; i++) {
        display.fillScreen(colors[i]);
        display.drawString(25, 28, names[i], COLOR_BLACK, colors[i], 2);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    
    /*
     * =========================================================================
     * TEST 2: Text
     * =========================================================================
     */
    ESP_LOGI(TAG, "Test 2: Text");
    
    display.fillScreen(COLOR_BLACK);
    display.drawString(5, 5, "SSD1357", COLOR_WHITE, COLOR_BLACK, 1);
    display.drawString(5, 15, "64x64", COLOR_CYAN, COLOR_BLACK, 1);
    display.drawString(5, 25, "RGB", COLOR_RED, COLOR_BLACK, 1);
    display.drawString(23, 25, "OLED", COLOR_GREEN, COLOR_BLACK, 1);
    display.drawString(5, 40, "Tiny!", COLOR_YELLOW, COLOR_BLACK, 2);
    
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    /*
     * =========================================================================
     * TEST 3: Shapes
     * =========================================================================
     */
    ESP_LOGI(TAG, "Test 3: Shapes");
    
    display.fillScreen(COLOR_BLACK);
    
    // Border
    display.drawRect(0, 0, 64, 64, COLOR_WHITE);
    
    // Filled shapes
    display.fillRect(5, 5, 20, 15, COLOR_RED);
    display.fillRect(39, 5, 20, 15, COLOR_GREEN);
    
    display.fillCircle(15, 40, 10, COLOR_BLUE);
    display.fillCircle(48, 40, 10, COLOR_YELLOW);
    
    // Outline circle in center
    display.drawCircle(32, 32, 15, COLOR_MAGENTA);
    
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    /*
     * =========================================================================
     * TEST 4: Lines
     * =========================================================================
     */
    ESP_LOGI(TAG, "Test 4: Lines");
    
    display.fillScreen(COLOR_BLACK);
    
    // Radiating lines from center
    for (int angle = 0; angle < 360; angle += 30) {
        float rad = angle * 3.14159f / 180.0f;
        int x = 32 + (int)(30 * cosf(rad));
        int y = 32 + (int)(30 * sinf(rad));
        uint16_t color = SSD1357::color565(angle * 255 / 360, 100, 255 - angle * 255 / 360);
        display.drawLine(32, 32, x, y, color);
    }
    
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    /*
     * =========================================================================
     * TEST 5: Rainbow gradient
     * =========================================================================
     */
    ESP_LOGI(TAG, "Test 5: Gradient");
    
    for (int x = 0; x < 64; x++) {
        int hue = x * 360 / 64;
        uint8_t r, g, b;
        
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
        
        uint16_t color = SSD1357::color565(r, g, b);
        display.drawVLine(x, 0, 64, color);
    }
    
    display.drawString(10, 28, "RGB!", COLOR_WHITE, COLOR_BLACK, 1);
    
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    /*
     * =========================================================================
     * TEST 6: Bouncing ball animation
     * =========================================================================
     */
    ESP_LOGI(TAG, "Test 6: Animation");
    
    int ballX = 32, ballY = 32;
    int ballDX = 2, ballDY = 1;
    int ballR = 6;
    
    for (int frame = 0; frame < 150; frame++) {
        display.fillScreen(COLOR_BLACK);
        display.drawRect(0, 0, 64, 64, COLOR_WHITE);
        display.fillCircle(ballX, ballY, ballR, COLOR_RED);
        
        ballX += ballDX;
        ballY += ballDY;
        
        if (ballX - ballR <= 1 || ballX + ballR >= 62) ballDX = -ballDX;
        if (ballY - ballR <= 1 || ballY + ballR >= 62) ballDY = -ballDY;
        
        vTaskDelay(pdMS_TO_TICKS(30));
    }
    
    /*
     * =========================================================================
     * FINAL: Complete
     * =========================================================================
     */
    ESP_LOGI(TAG, "All tests complete!");
    
    display.fillScreen(COLOR_BLACK);
    display.drawRect(2, 2, 60, 60, COLOR_GREEN);
    display.drawString(10, 20, "Done!", COLOR_GREEN, COLOR_BLACK, 1);
    display.drawString(5, 35, "0.6\" RGB", COLOR_CYAN, COLOR_BLACK, 1);
    display.drawString(10, 50, "OLED", COLOR_YELLOW, COLOR_BLACK, 1);
    
   
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
