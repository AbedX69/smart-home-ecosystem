/**
 * @file main.cpp
 * @brief E-Paper display test application (ESP-IDF).
 *
 * @details
 * Demonstrates the E-Paper component:
 * - Display initialization
 * - Drawing primitives (all 3 colors)
 * - Text rendering
 * - Deep sleep mode
 *
 * @note E-paper refresh is SLOW (~2 seconds per update).
 *       This test shows several screens with pauses between.
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "epaper.h"


static const char* TAG = "EPAPER_TEST";


#ifndef EPAPER_MOSI
#define EPAPER_MOSI 23
#endif
#ifndef EPAPER_SCK
#define EPAPER_SCK 18
#endif
#ifndef EPAPER_CS
#define EPAPER_CS 5
#endif
#ifndef EPAPER_DC
#define EPAPER_DC 16
#endif
#ifndef EPAPER_RST
#define EPAPER_RST 17
#endif
#ifndef EPAPER_BUSY
#define EPAPER_BUSY 4
#endif


extern "C" void app_main(void) {
    ESP_LOGI(TAG, "=== E-Paper Display Test ===");
    ESP_LOGI(TAG, "MOSI=%d, SCK=%d, CS=%d, DC=%d, RST=%d, BUSY=%d",
             EPAPER_MOSI, EPAPER_SCK, EPAPER_CS, EPAPER_DC, EPAPER_RST, EPAPER_BUSY);
    
    /*
     * =========================================================================
     * CREATE AND INITIALIZE DISPLAY
     * =========================================================================
     */
    EPaper display(
        (gpio_num_t)EPAPER_MOSI,
        (gpio_num_t)EPAPER_SCK,
        (gpio_num_t)EPAPER_CS,
        (gpio_num_t)EPAPER_DC,
        (gpio_num_t)EPAPER_RST,
        (gpio_num_t)EPAPER_BUSY
    );
    
    if (!display.init()) {
        ESP_LOGE(TAG, "Display init failed!");
        return;
    }
    
    ESP_LOGI(TAG, "Display initialized. Running tests...");
    ESP_LOGI(TAG, "Note: E-paper refresh takes ~2 seconds per update");
     while (1) {
    /*
     * =========================================================================
     * TEST 1: Hello World - All 3 colors
     * =========================================================================
     */
    ESP_LOGI(TAG, "Test 1: Hello World with 3 colors");
    
    display.clear(EPAPER_WHITE);
    
    display.drawString(10, 10, "E-Paper Test", EPAPER_BLACK, 2);
    display.drawString(10, 35, "2.13 inch", EPAPER_BLACK, 1);
    display.drawString(10, 50, "122 x 250 pixels", EPAPER_BLACK, 1);
    
    display.drawString(10, 80, "BLACK text", EPAPER_BLACK, 1);
    display.drawString(10, 100, "RED text", EPAPER_RED, 1);
    
    // Color boxes
    display.fillRect(10, 130, 40, 40, EPAPER_BLACK);
    display.fillRect(60, 130, 40, 40, EPAPER_RED);
    display.drawRect(10, 130, 40, 40, EPAPER_BLACK);
    display.drawRect(60, 130, 40, 40, EPAPER_BLACK);
    
    display.drawString(15, 180, "B", EPAPER_WHITE, 2);
    display.drawString(65, 180, "R", EPAPER_WHITE, 2);
    
    display.update();
    
    ESP_LOGI(TAG, "Test 1 complete. Waiting 5 seconds...");
    vTaskDelay(pdMS_TO_TICKS(5000));
    
    /*
     * =========================================================================
     * TEST 2: Shapes
     * =========================================================================
     */
    ESP_LOGI(TAG, "Test 2: Shapes");
    
    display.clear(EPAPER_WHITE);
    
    display.drawString(30, 5, "Shapes", EPAPER_BLACK, 2);
    
    // Lines
    display.drawLine(10, 35, 110, 35, EPAPER_BLACK);
    display.drawLine(10, 35, 60, 70, EPAPER_RED);
    display.drawLine(110, 35, 60, 70, EPAPER_BLACK);
    
    // Rectangles
    display.drawRect(10, 80, 50, 30, EPAPER_BLACK);
    display.fillRect(70, 80, 50, 30, EPAPER_RED);
    
    // Circles
    display.drawCircle(35, 150, 25, EPAPER_BLACK);
    display.fillCircle(95, 150, 25, EPAPER_RED);
    
    // Nested shapes
    display.drawRect(10, 190, 100, 50, EPAPER_BLACK);
    display.drawRect(20, 200, 80, 30, EPAPER_RED);
    display.drawRect(30, 210, 60, 10, EPAPER_BLACK);
    
    display.update();
    
    ESP_LOGI(TAG, "Test 2 complete. Waiting 5 seconds...");
    vTaskDelay(pdMS_TO_TICKS(5000));
    
    /*
     * =========================================================================
     * TEST 3: Text sizes
     * =========================================================================
     */
    ESP_LOGI(TAG, "Test 3: Text sizes");
    
    display.clear(EPAPER_WHITE);
    
    display.drawString(5, 10, "Size 1", EPAPER_BLACK, 1);
    display.drawString(5, 30, "Size 2", EPAPER_BLACK, 2);
    display.drawString(5, 60, "Size 3", EPAPER_BLACK, 3);
    display.drawString(5, 100, "Sz 4", EPAPER_BLACK, 4);
    
    display.drawString(5, 150, "Red Size 2", EPAPER_RED, 2);
    
    display.drawString(5, 190, "ABCDEFGHIJK", EPAPER_BLACK, 1);
    display.drawString(5, 205, "0123456789", EPAPER_BLACK, 1);
    display.drawString(5, 220, "!@#$%^&*()", EPAPER_RED, 1);
    
    display.update();
    
    ESP_LOGI(TAG, "Test 3 complete. Waiting 5 seconds...");
    vTaskDelay(pdMS_TO_TICKS(5000));
    
    /*
     * =========================================================================
     * TEST 4: Inverted (black background)
     * =========================================================================
     */
    ESP_LOGI(TAG, "Test 4: Inverted display");
    
    display.clear(EPAPER_BLACK);
    
    display.drawString(10, 30, "Inverted!", EPAPER_WHITE, 2);
    display.drawString(10, 60, "White on Black", EPAPER_WHITE, 1);
    
    display.fillRect(10, 100, 100, 50, EPAPER_RED);
    display.drawString(20, 115, "RED box", EPAPER_WHITE, 1);
    
    display.fillCircle(60, 200, 40, EPAPER_WHITE);
    display.drawString(35, 195, "Hi!", EPAPER_BLACK, 1);
    
    display.update();
    
    ESP_LOGI(TAG, "Test 4 complete. Waiting 5 seconds...");
    vTaskDelay(pdMS_TO_TICKS(5000));
    
    /*
     * =========================================================================
     * TEST 5: Final screen with pattern
     * =========================================================================
     */
    ESP_LOGI(TAG, "Test 5: Pattern");
    
    display.clear(EPAPER_WHITE);
    
    // Checkerboard pattern
    for (int y = 0; y < 120; y += 20) {
        for (int x = 0; x < 120; x += 20) {
            uint8_t color = ((x / 20) + (y / 20)) % 2 ? EPAPER_BLACK : EPAPER_WHITE;
            display.fillRect(x, y, 20, 20, color);
        }
    }
    
    // Border
    display.drawRect(0, 0, 122, 120, EPAPER_RED);
    
    // Message
    display.drawString(5, 140, "E-Paper Test", EPAPER_BLACK, 2);
    display.drawString(5, 170, "Complete!", EPAPER_RED, 2);
    
    display.drawString(5, 210, "Display will", EPAPER_BLACK, 1);
    display.drawString(5, 225, "keep this image", EPAPER_BLACK, 1);
    display.drawString(5, 240, "even unpowered!", EPAPER_RED, 1);
    
    display.update();
    
    ESP_LOGI(TAG, "All tests complete!");
    ESP_LOGI(TAG, "The display will keep showing this image even if powered off.");
    ESP_LOGI(TAG, "Entering deep sleep in 5 seconds...");
    
    vTaskDelay(pdMS_TO_TICKS(5000));
    
    // Put display to sleep (saves power, image persists)
    display.sleep();
    
    ESP_LOGI(TAG, "Display in deep sleep. Image persists without power.");
    
   
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
