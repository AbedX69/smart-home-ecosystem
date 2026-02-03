/**
 * @file epaper.h
 * @brief E-Paper (E-Ink) display driver for ESP32 (ESP-IDF).
 *
 * @details
 * This component handles SPI e-paper displays.
 * Tested with 2.13" black/white/red displays (WeAct Studio).
 *
 * @note
 * E-paper is VERY different from TFT:
 * - Slow refresh (1-2 seconds)
 * - Image persists without power
 * - No backlight
 * - Partial refresh possible on some models
 *
 * @par Supported hardware
 * - 2.13" 122x250 B/W/R (SSD1680 / IL3897 driver)
 * - 2.13" 122x250 B/W (faster refresh)
 * - Similar WeAct Studio e-paper modules
 */

/*
 * =============================================================================
 * BEGINNER'S GUIDE: E-PAPER DISPLAYS
 * =============================================================================
 * 
 * =============================================================================
 * E-PAPER vs TFT vs OLED
 * =============================================================================
 * 
 *     TFT (ILI9341, ST7789):
 *         - Fast refresh (60+ FPS)
 *         - Needs constant power to show image
 *         - Backlight required
 *         - Good for animations, video
 *     
 *     OLED (SSD1306):
 *         - Fast refresh
 *         - Each pixel emits light
 *         - Great contrast
 *         - Can burn in over time
 *     
 *     E-Paper (E-Ink):
 *         - SLOW refresh (1-15 seconds!)
 *         - Image stays WITHOUT power
 *         - No backlight (reads like paper)
 *         - Perfect for: labels, signs, low-power
 *         - Bad for: animations, frequent updates
 * 
 * =============================================================================
 * HOW E-PAPER WORKS
 * =============================================================================
 * 
 *     E-paper contains tiny capsules with black and white particles:
 *     
 *         ┌─────────────────────────────┐
 *         │  ○●  ●○  ○●  ●○  ○●  ●○     │ ← Capsules
 *         │  ●○  ○●  ●○  ○●  ●○  ○●     │
 *         └─────────────────────────────┘
 *         
 *         ○ = White particle (titanium dioxide)
 *         ● = Black particle (carbon)
 *     
 *     Electric field moves particles up/down:
 *         - Positive charge → white on top (white pixel)
 *         - Negative charge → black on top (black pixel)
 *     
 *     Once positioned, particles STAY (bistable).
 *     That's why image persists without power!
 *     
 *     Red e-paper has a THIRD color of particles.
 * 
 * =============================================================================
 * THE BUSY PIN
 * =============================================================================
 * 
 *     E-paper refresh takes 1-15 seconds. The BUSY pin tells you when done:
 *     
 *         BUSY HIGH → Display is busy, don't send commands
 *         BUSY LOW  → Ready for next command
 *     
 *     You MUST wait for BUSY before sending new data!
 *     
 *         sendCommand(...);
 *         waitBusy();        // Wait for display to finish
 *         sendCommand(...);
 * 
 * =============================================================================
 * FRAME BUFFER
 * =============================================================================
 * 
 *     E-paper uses a frame buffer (like SSD1306):
 *     
 *         2.13" display: 122 x 250 pixels
 *         
 *         For B/W: 1 bit per pixel
 *             Buffer size = 122 * 250 / 8 = 3,813 bytes
 *         
 *         For B/W/R: 2 buffers (one for black, one for red)
 *             Total = 7,626 bytes
 *     
 *     Workflow:
 *         1. Draw to buffer (fast, in RAM)
 *         2. Call update() to send to display (slow, 1-2 sec)
 * 
 * =============================================================================
 * WIRING
 * =============================================================================
 * 
 *     E-Paper         ESP32
 *     ───────         ─────
 *     VCC             3.3V
 *     GND             GND
 *     SCL             GPIO (SCK)
 *     SDA             GPIO (MOSI)
 *     CS              GPIO
 *     D/C             GPIO
 *     RST             GPIO
 *     BUSY            GPIO (input!)
 * 
 * =============================================================================
 * USAGE EXAMPLE
 * =============================================================================
 * 
 *     #include "epaper.h"
 *     
 *     void app_main(void) {
 *         EPaper display(
 *             GPIO_NUM_23,  // MOSI (SDA)
 *             GPIO_NUM_18,  // SCK (SCL)
 *             GPIO_NUM_5,   // CS
 *             GPIO_NUM_16,  // DC
 *             GPIO_NUM_17,  // RST
 *             GPIO_NUM_4    // BUSY
 *         );
 *         
 *         display.init();
 *         
 *         // Draw to buffer (fast)
 *         display.clear(COLOR_WHITE);
 *         display.drawString(10, 10, "Hello E-Paper!", COLOR_BLACK);
 *         display.fillRect(50, 50, 30, 30, COLOR_RED);
 *         
 *         // Update display (slow - takes ~2 seconds)
 *         display.update();
 *         
 *         // Display will keep showing this even if powered off!
 *     }
 * 
 * =============================================================================
 */

#pragma once

#include <driver/spi_master.h>
#include <driver/gpio.h>
#include <stdint.h>
#include <string.h>


/**
 * @brief Display dimensions for 2.13" e-paper
 */
#define EPAPER_WIDTH    122
#define EPAPER_HEIGHT   250


/**
 * @brief E-paper colors
 */
#define EPAPER_BLACK    0
#define EPAPER_WHITE    1
#define EPAPER_RED      2


/**
 * @class EPaper
 * @brief E-Paper display driver over SPI.
 *
 * @details
 * Provides:
 * - Display initialization
 * - Frame buffer management (B/W and Red)
 * - Drawing primitives
 * - Text rendering
 * - Full and partial refresh
 */
class EPaper {

public:

    /**
     * @brief Construct a new EPaper display instance.
     *
     * @param mosiPin GPIO for MOSI (SDA on module).
     * @param sckPin GPIO for SCK (SCL on module).
     * @param csPin GPIO for Chip Select.
     * @param dcPin GPIO for Data/Command.
     * @param rstPin GPIO for Reset.
     * @param busyPin GPIO for Busy signal (input).
     * @param spiHost SPI host (default: SPI2_HOST).
     */
    EPaper(gpio_num_t mosiPin, gpio_num_t sckPin, gpio_num_t csPin,
           gpio_num_t dcPin, gpio_num_t rstPin, gpio_num_t busyPin,
           spi_host_device_t spiHost = SPI2_HOST);


    /**
     * @brief Destroy the EPaper instance and free buffers.
     */
    ~EPaper();


    /**
     * @brief Initialize SPI and display.
     *
     * @return true if successful, false on error.
     */
    bool init();


    /**
     * @brief Clear the frame buffer.
     *
     * @param color Fill color (EPAPER_WHITE, EPAPER_BLACK, or EPAPER_RED).
     */
    void clear(uint8_t color = EPAPER_WHITE);


    /**
     * @brief Update the display with buffer contents.
     *
     * @note This is SLOW (1-2 seconds). Call only when needed.
     */
    void update();


    /**
     * @brief Put display into deep sleep mode.
     *
     * @note Call init() again to wake up.
     */
    void sleep();


    /**
     * @brief Draw a single pixel.
     *
     * @param x X coordinate.
     * @param y Y coordinate.
     * @param color Pixel color (EPAPER_BLACK, EPAPER_WHITE, EPAPER_RED).
     */
    void drawPixel(int16_t x, int16_t y, uint8_t color);


    /**
     * @brief Draw a horizontal line.
     */
    void drawHLine(int16_t x, int16_t y, int16_t w, uint8_t color);


    /**
     * @brief Draw a vertical line.
     */
    void drawVLine(int16_t x, int16_t y, int16_t h, uint8_t color);


    /**
     * @brief Draw a line between two points.
     */
    void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t color);


    /**
     * @brief Draw a rectangle outline.
     */
    void drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t color);


    /**
     * @brief Draw a filled rectangle.
     */
    void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t color);


    /**
     * @brief Draw a circle outline.
     */
    void drawCircle(int16_t cx, int16_t cy, int16_t radius, uint8_t color);


    /**
     * @brief Draw a filled circle.
     */
    void fillCircle(int16_t cx, int16_t cy, int16_t radius, uint8_t color);


    /**
     * @brief Draw a single character.
     *
     * @param x Top-left X position.
     * @param y Top-left Y position.
     * @param c Character to draw.
     * @param color Text color.
     * @param size Font scale (1 = 5x7, 2 = 10x14, etc.)
     *
     * @return Width of character drawn.
     */
    uint8_t drawChar(int16_t x, int16_t y, char c, uint8_t color, uint8_t size = 1);


    /**
     * @brief Draw a string.
     *
     * @param x Starting X position.
     * @param y Starting Y position.
     * @param str Null-terminated string.
     * @param color Text color.
     * @param size Font scale.
     */
    void drawString(int16_t x, int16_t y, const char* str, uint8_t color, uint8_t size = 1);


    /**
     * @brief Set display rotation.
     *
     * @param rotation 0, 1, 2, or 3 (0° / 90° / 180° / 270°).
     */
    void setRotation(uint8_t rotation);


    /**
     * @brief Get display width (changes with rotation).
     */
    uint16_t getWidth() const { return width; }


    /**
     * @brief Get display height (changes with rotation).
     */
    uint16_t getHeight() const { return height; }


private:

    gpio_num_t mosiPin;
    gpio_num_t sckPin;
    gpio_num_t csPin;
    gpio_num_t dcPin;
    gpio_num_t rstPin;
    gpio_num_t busyPin;
    spi_host_device_t spiHost;
    spi_device_handle_t spiDevice;
    bool initialized;

    uint8_t* bufferBW;      // Black/White buffer
    uint8_t* bufferRed;     // Red buffer
    uint16_t bufferSize;

    uint8_t rotation;
    uint16_t width;
    uint16_t height;


    /**
     * @brief Send a command byte.
     */
    void sendCommand(uint8_t cmd);


    /**
     * @brief Send a data byte.
     */
    void sendData(uint8_t data);


    /**
     * @brief Send multiple data bytes.
     */
    void sendData(const uint8_t* data, size_t len);


    /**
     * @brief Wait for BUSY pin to go low.
     */
    void waitBusy();


    /**
     * @brief Hardware reset the display.
     */
    void hardwareReset();


    /**
     * @brief Convert x,y to buffer position based on rotation.
     */
    void getBufferPosition(int16_t x, int16_t y, int16_t* bufX, int16_t* bufY);
};
