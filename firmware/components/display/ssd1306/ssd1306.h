/**
 * @file ssd1306.h
 * @brief SSD1306 OLED display driver for ESP32 (ESP-IDF).
 *
 * @details
 * This component handles SSD1306-based OLED displays over I2C.
 * Common sizes: 128x64, 128x32 pixels.
 *
 * @note
 * I2C assumptions:
 * - Default address: 0x3C (some modules use 0x3D)
 * - Supports 400kHz I2C clock
 *
 * @par Supported hardware
 * - 0.96" 128x64 OLED (most common)
 * - 0.91" 128x32 OLED
 * - Any SSD1306-based I2C display
 *
 * @par Tested boards
 * - ESP32D (original ESP32)
 * - ESP32-S3 WROOM
 * - ESP32-S3 Seeed XIAO
 * - ESP32-C6 WROOM
 * - ESP32-C6 Seeed XIAO
 */

/*
 * =============================================================================
 * BEGINNER'S GUIDE: OLED DISPLAYS AND I2C
 * =============================================================================
 * 
 * =============================================================================
 * WHAT IS AN OLED?
 * =============================================================================
 * 
 * OLED = Organic Light Emitting Diode
 * 
 * Unlike LCD (which needs a backlight), each OLED pixel makes its own light.
 * 
 *     LCD:                        OLED:
 *     ┌─────────────┐             ┌─────────────┐
 *     │ ░░░░░░░░░░░ │ Backlight   │             │ No backlight
 *     │ ▓▓▓▓▓▓▓▓▓▓▓ │ Layer       │ ★  ★    ★  │ Pixels glow
 *     │ ░░░░░░░░░░░ │ Pixels      │    ★  ★    │ themselves
 *     └─────────────┘             └─────────────┘
 *     
 *     Black = backlight blocked    Black = pixel OFF (true black!)
 *     White = backlight through    White = pixel ON (glowing)
 * 
 * Benefits of OLED:
 *     - True black (pixel is off, not blocking light)
 *     - High contrast
 *     - Thin and light
 *     - Low power when displaying dark content
 * 
 * =============================================================================
 * WHAT IS I2C?
 * =============================================================================
 * 
 * I2C = Inter-Integrated Circuit (pronounced "I-squared-C" or "I-two-C")
 * 
 * It's a communication protocol using just 2 wires:
 * 
 *     ESP32                    OLED Display
 *     ┌─────┐                  ┌─────┐
 *     │     │                  │     │
 *     │ SDA ├──────────────────┤ SDA │  Data line (bidirectional)
 *     │     │                  │     │
 *     │ SCL ├──────────────────┤ SCL │  Clock line (ESP32 controls)
 *     │     │                  │     │
 *     │ 3.3V├──────────────────┤ VCC │  Power
 *     │     │                  │     │
 *     │ GND ├──────────────────┤ GND │  Ground
 *     └─────┘                  └─────┘
 * 
 * HOW IT WORKS:
 *     1. ESP32 is the "master" - it controls the clock (SCL)
 *     2. Display is a "slave" - it responds to commands
 *     3. Each slave has an ADDRESS (SSD1306 is usually 0x3C)
 *     4. Master sends: [address] [command/data] [command/data] ...
 * 
 * I2C TIMING:
 *     
 *     SCL: ─┐ ┌─┐ ┌─┐ ┌─┐ ┌─┐ ┌─┐ ┌─┐ ┌─┐ ┌─    Clock pulses
 *          └─┘ └─┘ └─┘ └─┘ └─┘ └─┘ └─┘ └─┘
 *     
 *     SDA: ───┐   ┌───┐   ┌───────┐   ┌───────    Data changes between clocks
 *            └───┘   └───┘       └───┘
 *          START  bit0  bit1   bit2  ...
 * 
 * =============================================================================
 * SSD1306 DISPLAY ARCHITECTURE
 * =============================================================================
 * 
 * The SSD1306 has a built-in RAM that stores what to display.
 * We write to this RAM, and the chip handles refreshing the screen.
 * 
 *     128 pixels wide
 *     ◄─────────────────────────────────────►
 *     ┌─────────────────────────────────────┐ ▲
 *     │ Page 0 (8 rows of pixels)           │ │
 *     ├─────────────────────────────────────┤ │
 *     │ Page 1                              │ │
 *     ├─────────────────────────────────────┤ │
 *     │ Page 2                              │ 64 pixels
 *     ├─────────────────────────────────────┤ │
 *     │ Page 3                              │ │
 *     ├─────────────────────────────────────┤ │
 *     │ Page 4                              │ │
 *     ├─────────────────────────────────────┤ │
 *     │ Page 5                              │ │
 *     ├─────────────────────────────────────┤ │
 *     │ Page 6                              │ │
 *     ├─────────────────────────────────────┤ │
 *     │ Page 7                              │ ▼
 *     └─────────────────────────────────────┘
 * 
 * Each PAGE is 8 pixels tall (1 byte per column).
 * 128 columns × 8 pages = 1024 bytes total for 128x64 display.
 * 
 * BYTE LAYOUT IN A PAGE:
 *     
 *     One byte controls 8 vertical pixels:
 *     
 *     Bit 0 → ●  (top)
 *     Bit 1 → ●
 *     Bit 2 → ○
 *     Bit 3 → ●
 *     Bit 4 → ○
 *     Bit 5 → ○
 *     Bit 6 → ●
 *     Bit 7 → ●  (bottom)
 *     
 *     Example: 0b11001011 = pixels at positions 0,1,3,6,7 are ON
 * 
 * =============================================================================
 * FRAME BUFFER CONCEPT
 * =============================================================================
 * 
 * We keep a copy of the screen in ESP32 memory (frame buffer).
 * Draw everything to the buffer, then send it all at once.
 * 
 *     1. Clear buffer         2. Draw to buffer       3. Send to display
 *     ┌───────────┐          ┌───────────┐           ┌───────────┐
 *     │           │          │  Hello!   │           │  Hello!   │
 *     │           │    →     │   ┌─┐     │     →     │   ┌─┐     │
 *     │           │          │   └─┘     │           │   └─┘     │
 *     └───────────┘          └───────────┘           └───────────┘
 *      In ESP32 RAM           In ESP32 RAM            On actual OLED
 * 
 * Why buffer?
 *     - Drawing pixel-by-pixel over I2C is SLOW
 *     - Buffer lets us draw complex graphics, then send once
 *     - Avoids flickering
 * 
 * =============================================================================
 * USAGE EXAMPLE
 * =============================================================================
 * 
 *     #include "ssd1306.h"
 *     
 *     void app_main(void) {
 *         // Create display (128x64, I2C address 0x3C)
 *         SSD1306 display(GPIO_NUM_21, GPIO_NUM_22);  // SDA, SCL
 *         
 *         // Initialize
 *         display.init();
 *         
 *         // Clear screen
 *         display.clear();
 *         
 *         // Draw some pixels
 *         display.drawPixel(10, 10, true);   // x=10, y=10, white
 *         display.drawPixel(20, 20, true);
 *         
 *         // Draw text
 *         display.drawString(0, 0, "Hello!");
 *         
 *         // Send buffer to display
 *         display.update();
 *     }
 * 
 * =============================================================================
 */

#pragma once

#include <driver/i2c.h>
#include <stdint.h>
#include <string.h>


/**
 * @brief SSD1306 I2C address options
 */
#define SSD1306_ADDR_DEFAULT    0x3C    // Most common
#define SSD1306_ADDR_ALTERNATE  0x3D    // Some modules


/**
 * @brief Display dimensions
 */
#define SSD1306_WIDTH           128
#define SSD1306_HEIGHT          64
#define SSD1306_PAGES           (SSD1306_HEIGHT / 8)    // 8 pages for 64px height
#define SSD1306_BUFFER_SIZE     (SSD1306_WIDTH * SSD1306_PAGES)  // 1024 bytes


/**
 * @class SSD1306
 * @brief SSD1306 OLED display driver over I2C.
 *
 * @details
 * Provides:
 * - Display initialization and configuration
 * - Frame buffer management
 * - Basic drawing primitives (pixel, line, rectangle)
 * - Text rendering with built-in font
 */
class SSD1306 {

public:

    /**
     * @brief Construct a new SSD1306 display instance.
     *
     * @param sdaPin GPIO pin for I2C data (SDA).
     * @param sclPin GPIO pin for I2C clock (SCL).
     * @param address I2C address (default: 0x3C).
     * @param i2cPort I2C port number (default: I2C_NUM_0).
     *
     * @note
     * Does not initialize hardware. Call init() to set up I2C and display.
     */
    SSD1306(gpio_num_t sdaPin, gpio_num_t sclPin, 
            uint8_t address = SSD1306_ADDR_DEFAULT,
            i2c_port_t i2cPort = I2C_NUM_0);


    /**
     * @brief Destroy the SSD1306 instance.
     *
     * @details
     * Releases I2C driver resources.
     */
    ~SSD1306();


    /**
     * @brief Initialize I2C and display.
     *
     * @return true if successful, false on error.
     *
     * @details
     * - Configures I2C bus
     * - Sends initialization commands to SSD1306
     * - Clears display
     */
    bool init();


    /**
     * @brief Send frame buffer to display.
     *
     * @details
     * Call this after drawing to make changes visible.
     * Transfers entire 1024-byte buffer over I2C.
     */
    void update();


    /**
     * @brief Clear the frame buffer (all pixels off).
     *
     * @note Call update() to see the change on display.
     */
    void clear();


    /**
     * @brief Fill the frame buffer (all pixels on).
     *
     * @note Call update() to see the change on display.
     */
    void fill();


    /**
     * @brief Set or clear a single pixel.
     *
     * @param x Horizontal position (0-127).
     * @param y Vertical position (0-63).
     * @param on true = pixel on (white), false = pixel off (black).
     *
     * @note Call update() to see the change on display.
     */
    void drawPixel(int16_t x, int16_t y, bool on);


    /**
     * @brief Draw a horizontal line.
     *
     * @param x Starting X position.
     * @param y Y position.
     * @param width Line width in pixels.
     * @param on true = white, false = black.
     */
    void drawHLine(int16_t x, int16_t y, int16_t width, bool on);


    /**
     * @brief Draw a vertical line.
     *
     * @param x X position.
     * @param y Starting Y position.
     * @param height Line height in pixels.
     * @param on true = white, false = black.
     */
    void drawVLine(int16_t x, int16_t y, int16_t height, bool on);


    /**
     * @brief Draw a line between two points.
     *
     * @param x0 Start X.
     * @param y0 Start Y.
     * @param x1 End X.
     * @param y1 End Y.
     * @param on true = white, false = black.
     */
    void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, bool on);


    /**
     * @brief Draw a rectangle outline.
     *
     * @param x Top-left X.
     * @param y Top-left Y.
     * @param width Rectangle width.
     * @param height Rectangle height.
     * @param on true = white, false = black.
     */
    void drawRect(int16_t x, int16_t y, int16_t width, int16_t height, bool on);


    /**
     * @brief Draw a filled rectangle.
     *
     * @param x Top-left X.
     * @param y Top-left Y.
     * @param width Rectangle width.
     * @param height Rectangle height.
     * @param on true = white, false = black.
     */
    void fillRect(int16_t x, int16_t y, int16_t width, int16_t height, bool on);


    /**
     * @brief Draw a circle outline.
     *
     * @param cx Center X.
     * @param cy Center Y.
     * @param radius Circle radius.
     * @param on true = white, false = black.
     */
    void drawCircle(int16_t cx, int16_t cy, int16_t radius, bool on);


    /**
     * @brief Draw a filled circle.
     *
     * @param cx Center X.
     * @param cy Center Y.
     * @param radius Circle radius.
     * @param on true = white, false = black.
     */
    void fillCircle(int16_t cx, int16_t cy, int16_t radius, bool on);


    /**
     * @brief Draw a single character.
     *
     * @param x Top-left X position.
     * @param y Top-left Y position.
     * @param c Character to draw.
     * @param on true = white text, false = black text.
     *
     * @return Width of character drawn (for cursor advancement).
     *
     * @note Uses built-in 5x7 font. Character cell is 6x8 (1px spacing).
     */
    uint8_t drawChar(int16_t x, int16_t y, char c, bool on);


    /**
     * @brief Draw a string.
     *
     * @param x Starting X position.
     * @param y Starting Y position.
     * @param str Null-terminated string.
     * @param on true = white text, false = black text.
     */
    void drawString(int16_t x, int16_t y, const char* str, bool on = true);


    /**
     * @brief Set display contrast.
     *
     * @param contrast Value 0-255 (higher = brighter).
     */
    void setContrast(uint8_t contrast);


    /**
     * @brief Invert display colors.
     *
     * @param invert true = inverted (white becomes black), false = normal.
     */
    void setInverted(bool invert);


    /**
     * @brief Turn display on or off.
     *
     * @param on true = display on, false = display off (sleep mode).
     */
    void setDisplayOn(bool on);


private:

    gpio_num_t sdaPin;          // I2C data pin
    gpio_num_t sclPin;          // I2C clock pin
    uint8_t address;            // I2C address
    i2c_port_t i2cPort;         // I2C port (0 or 1)
    bool initialized;           // Track if init() was called

    uint8_t buffer[SSD1306_BUFFER_SIZE];    // Frame buffer


    /**
     * @brief Send a command byte to the display.
     */
    void sendCommand(uint8_t cmd);


    /**
     * @brief Send multiple command bytes.
     */
    void sendCommands(const uint8_t* cmds, size_t len);


    /**
     * @brief Send data bytes to display RAM.
     */
    void sendData(const uint8_t* data, size_t len);
};
