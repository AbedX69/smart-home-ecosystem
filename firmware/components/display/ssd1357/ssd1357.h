/**
 * @file ssd1357.h
 * @brief SSD1357 RGB OLED display driver for ESP32 (ESP-IDF).
 *
 * @details
 * This component handles SSD1357-based RGB OLED displays over SPI.
 * Tested with 0.6" 64x64 RGB OLED modules.
 *
 * @note
 * SPI interface, RGB565 color format (16-bit per pixel).
 * Similar to TFT displays but OLED (self-emitting pixels).
 *
 * @par Supported hardware
 * - 0.6" 64x64 RGB OLED (SSD1357)
 * - Similar SSD1357-based modules
 */

/*
 * =============================================================================
 * BEGINNER'S GUIDE: SSD1357 RGB OLED
 * =============================================================================
 * 
 * =============================================================================
 * SSD1357 vs SSD1306
 * =============================================================================
 * 
 *     SSD1306:
 *         - Monochrome (white, blue, or yellow pixels)
 *         - 1-bit per pixel
 *         - I2C or SPI
 *         - Common sizes: 0.96", 1.3"
 *         - Uses frame buffer (1KB for 128x64)
 *     
 *     SSD1357:
 *         - Full RGB color (65K colors)
 *         - 16-bit per pixel (RGB565)
 *         - SPI only
 *         - Small size: 0.6" 64x64
 *         - Direct pixel streaming (no frame buffer needed)
 * 
 * =============================================================================
 * OLED vs TFT
 * =============================================================================
 * 
 *     TFT (ILI9341, ST7789):
 *         - Backlight required
 *         - Good for bright environments
 *         - Cheaper at larger sizes
 *     
 *     OLED (SSD1357):
 *         - No backlight (pixels emit light)
 *         - Perfect blacks (pixel off = true black)
 *         - Better contrast
 *         - Can burn in over time
 *         - More expensive
 * 
 * =============================================================================
 * WIRING
 * =============================================================================
 * 
 *     SSD1357         ESP32
 *     ───────         ─────
 *     VCC             3.3V
 *     GND             GND
 *     SCL             GPIO (SCK)
 *     SDA             GPIO (MOSI)
 *     RES             GPIO (Reset)
 *     DC              GPIO (Data/Command)
 *     CS              GPIO (Chip Select)
 * 
 * =============================================================================
 * USAGE EXAMPLE
 * =============================================================================
 * 
 *     #include "ssd1357.h"
 *     
 *     void app_main(void) {
 *         SSD1357 display(
 *             GPIO_NUM_23,  // MOSI (SDA)
 *             GPIO_NUM_18,  // SCK (SCL)
 *             GPIO_NUM_5,   // CS
 *             GPIO_NUM_16,  // DC
 *             GPIO_NUM_17   // RST
 *         );
 *         
 *         display.init();
 *         
 *         display.fillScreen(COLOR_BLACK);
 *         display.drawString(5, 5, "Hello!", COLOR_WHITE);
 *         display.fillCircle(32, 40, 15, COLOR_RED);
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
 * @brief Display dimensions
 */
#define SSD1357_WIDTH   64
#define SSD1357_HEIGHT  64


/**
 * @brief Common RGB565 colors
 */
#define COLOR_BLACK     0x0000
#define COLOR_WHITE     0xFFFF
#define COLOR_RED       0xF800
#define COLOR_GREEN     0x07E0
#define COLOR_BLUE      0x001F
#define COLOR_YELLOW    0xFFE0
#define COLOR_CYAN      0x07FF
#define COLOR_MAGENTA   0xF81F
#define COLOR_ORANGE    0xFD20
#define COLOR_GRAY      0x8410


/**
 * @class SSD1357
 * @brief SSD1357 RGB OLED display driver over SPI.
 *
 * @details
 * Provides:
 * - Display initialization
 * - Direct pixel drawing (no frame buffer)
 * - Drawing primitives (pixel, line, rectangle, circle)
 * - Text rendering with built-in font
 * - Color utilities
 */
class SSD1357 {

public:

    /**
     * @brief Construct a new SSD1357 display instance.
     *
     * @param mosiPin GPIO for MOSI (SDA on module).
     * @param sckPin GPIO for SCK (SCL on module).
     * @param csPin GPIO for Chip Select.
     * @param dcPin GPIO for Data/Command.
     * @param rstPin GPIO for Reset.
     * @param spiHost SPI host (default: SPI2_HOST).
     */
    SSD1357(gpio_num_t mosiPin, gpio_num_t sckPin, gpio_num_t csPin,
            gpio_num_t dcPin, gpio_num_t rstPin,
            spi_host_device_t spiHost = SPI2_HOST);


    /**
     * @brief Destroy the SSD1357 instance.
     */
    ~SSD1357();


    /**
     * @brief Initialize SPI and display.
     *
     * @return true if successful, false on error.
     */
    bool init();


    /**
     * @brief Fill entire screen with a color.
     *
     * @param color RGB565 color value.
     */
    void fillScreen(uint16_t color);


    /**
     * @brief Draw a single pixel.
     *
     * @param x X coordinate.
     * @param y Y coordinate.
     * @param color RGB565 color value.
     */
    void drawPixel(int16_t x, int16_t y, uint16_t color);


    /**
     * @brief Draw a horizontal line.
     */
    void drawHLine(int16_t x, int16_t y, int16_t w, uint16_t color);


    /**
     * @brief Draw a vertical line.
     */
    void drawVLine(int16_t x, int16_t y, int16_t h, uint16_t color);


    /**
     * @brief Draw a line between two points.
     */
    void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color);


    /**
     * @brief Draw a rectangle outline.
     */
    void drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);


    /**
     * @brief Draw a filled rectangle.
     */
    void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);


    /**
     * @brief Draw a circle outline.
     */
    void drawCircle(int16_t cx, int16_t cy, int16_t radius, uint16_t color);


    /**
     * @brief Draw a filled circle.
     */
    void fillCircle(int16_t cx, int16_t cy, int16_t radius, uint16_t color);


    /**
     * @brief Draw a single character.
     *
     * @param x Top-left X position.
     * @param y Top-left Y position.
     * @param c Character to draw.
     * @param color Text color (RGB565).
     * @param bg Background color (RGB565).
     * @param size Font scale (1 = 5x7, 2 = 10x14, etc.)
     *
     * @return Width of character drawn.
     */
    uint8_t drawChar(int16_t x, int16_t y, char c, uint16_t color, uint16_t bg, uint8_t size = 1);


    /**
     * @brief Draw a string.
     *
     * @param x Starting X position.
     * @param y Starting Y position.
     * @param str Null-terminated string.
     * @param color Text color (RGB565).
     * @param bg Background color (RGB565).
     * @param size Font scale.
     */
    void drawString(int16_t x, int16_t y, const char* str, uint16_t color, uint16_t bg = COLOR_BLACK, uint8_t size = 1);


    /**
     * @brief Set display brightness.
     *
     * @param brightness 0-255 (0 = off, 255 = max).
     */
    void setBrightness(uint8_t brightness);


    /**
     * @brief Convert 24-bit RGB to RGB565.
     *
     * @param r Red (0-255).
     * @param g Green (0-255).
     * @param b Blue (0-255).
     * @return RGB565 color value.
     */
    static uint16_t color565(uint8_t r, uint8_t g, uint8_t b);


    /**
     * @brief Get display width.
     */
    uint16_t getWidth() const { return SSD1357_WIDTH; }


    /**
     * @brief Get display height.
     */
    uint16_t getHeight() const { return SSD1357_HEIGHT; }


private:

    gpio_num_t mosiPin;
    gpio_num_t sckPin;
    gpio_num_t csPin;
    gpio_num_t dcPin;
    gpio_num_t rstPin;
    spi_host_device_t spiHost;
    spi_device_handle_t spiDevice;
    bool initialized;


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
     * @brief Send 16-bit data (for colors).
     */
    void sendData16(uint16_t data);


    /**
     * @brief Set the drawing window.
     */
    void setWindow(int16_t x0, int16_t y0, int16_t x1, int16_t y1);


    /**
     * @brief Hardware reset the display.
     */
    void hardwareReset();
};
