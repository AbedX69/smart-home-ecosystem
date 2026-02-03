/**
 * @file st7789.h
 * @brief ST7789 TFT display driver for ESP32 (ESP-IDF).
 *
 * @details
 * This component handles ST7789-based TFT displays over SPI.
 * Supports various resolutions: 240x240, 240x280, 240x320, 135x240.
 *
 * @note
 * SPI assumptions:
 * - Uses hardware SPI (VSPI or HSPI)
 * - Supports up to 80MHz SPI clock (we use 20MHz for compatibility)
 * - RGB565 color format (16-bit per pixel)
 *
 * @par Supported hardware
 * - 1.14" 135x240 TFT (ST7789)
 * - 1.3" 240x240 TFT (ST7789)
 * - 1.69" 240x280 TFT (ST7789V2)
 * - 2.0" 240x320 TFT (ST7789)
 * - Any ST7789/ST7789V/ST7789V2 based SPI display
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
 * BEGINNER'S GUIDE: ST7789 vs GC9A01
 * =============================================================================
 * 
 * Both are SPI color TFT displays with very similar interfaces:
 * 
 *     GC9A01:
 *         - Round displays (240x240)
 *         - Typically used for smart watches, gauges
 *     
 *     ST7789:
 *         - Rectangular displays (various sizes)
 *         - More common, cheaper
 *         - Used for everything: phones, IoT, games
 * 
 * =============================================================================
 * DISPLAY MEMORY OFFSET
 * =============================================================================
 * 
 * Some ST7789 displays don't start at pixel (0,0) in the chip's memory.
 * This is because the ST7789 chip supports up to 240x320, but smaller
 * panels only use part of that memory.
 * 
 *     240x320 panel: No offset (full memory used)
 *     240x280 panel: May have Y offset of 20
 *     240x240 panel: May have X or Y offset of 40 or 80
 *     135x240 panel: Has X offset of 52 or 53, Y offset of 40
 * 
 * If your display shows shifted/wrapped content, try adjusting the offset.
 * 
 * =============================================================================
 * WIRING (same as GC9A01)
 * =============================================================================
 * 
 *     ST7789          ESP32
 *     ───────         ─────
 *     GND    ──────── GND
 *     VCC    ──────── 3.3V
 *     SCL    ──────── GPIO (SCK)
 *     SDA    ──────── GPIO (MOSI)
 *     RES    ──────── GPIO (Reset)
 *     DC     ──────── GPIO (Data/Command)
 *     CS     ──────── GPIO (Chip Select)
 *     BLK    ──────── GPIO (Backlight)
 * 
 * =============================================================================
 * USAGE EXAMPLE
 * =============================================================================
 * 
 *     #include "st7789.h"
 *     
 *     void app_main(void) {
 *         // Create display (240x280 resolution)
 *         ST7789 display(
 *             240, 280,         // Width, Height
 *             GPIO_NUM_23,      // MOSI (SDA)
 *             GPIO_NUM_18,      // SCK (SCL)
 *             GPIO_NUM_5,       // CS
 *             GPIO_NUM_16,      // DC
 *             GPIO_NUM_17,      // RST
 *             GPIO_NUM_4        // BLK (backlight)
 *         );
 *         
 *         display.init();
 *         
 *         // Fill screen blue
 *         display.fillScreen(COLOR_BLUE);
 *         
 *         // Draw text
 *         display.drawString(10, 10, "ST7789 Test", COLOR_WHITE);
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
 * @class ST7789
 * @brief ST7789 TFT display driver over SPI.
 *
 * @details
 * Provides:
 * - Display initialization and configuration
 * - Direct pixel drawing (no frame buffer - too big!)
 * - Basic drawing primitives (pixel, line, rectangle, circle)
 * - Text rendering with built-in font
 * - Color utilities
 * - Configurable resolution and memory offset
 */
class ST7789 {

public:

    /**
     * @brief Construct a new ST7789 display instance.
     *
     * @param width Display width in pixels (e.g., 240).
     * @param height Display height in pixels (e.g., 280).
     * @param mosiPin GPIO for MOSI (SDA on module).
     * @param sckPin GPIO for SCK (SCL on module).
     * @param csPin GPIO for Chip Select.
     * @param dcPin GPIO for Data/Command.
     * @param rstPin GPIO for Reset.
     * @param blkPin GPIO for Backlight (-1 to skip).
     * @param spiHost SPI host (default: SPI2_HOST).
     */
    ST7789(uint16_t width, uint16_t height,
           gpio_num_t mosiPin, gpio_num_t sckPin, gpio_num_t csPin,
           gpio_num_t dcPin, gpio_num_t rstPin, gpio_num_t blkPin = GPIO_NUM_NC,
           spi_host_device_t spiHost = SPI2_HOST);


    /**
     * @brief Destroy the ST7789 instance.
     */
    ~ST7789();


    /**
     * @brief Initialize SPI and display.
     *
     * @return true if successful, false on error.
     */
    bool init();


    /**
     * @brief Set memory offset for displays that don't start at (0,0).
     *
     * @param xOffset X offset in pixels.
     * @param yOffset Y offset in pixels.
     */
    void setOffset(int16_t xOffset, int16_t yOffset);


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
     *
     * @param x Starting X position.
     * @param y Y position.
     * @param w Line width in pixels.
     * @param color RGB565 color value.
     */
    void drawHLine(int16_t x, int16_t y, int16_t w, uint16_t color);


    /**
     * @brief Draw a vertical line.
     *
     * @param x X position.
     * @param y Starting Y position.
     * @param h Line height in pixels.
     * @param color RGB565 color value.
     */
    void drawVLine(int16_t x, int16_t y, int16_t h, uint16_t color);


    /**
     * @brief Draw a line between two points.
     *
     * @param x0 Start X.
     * @param y0 Start Y.
     * @param x1 End X.
     * @param y1 End Y.
     * @param color RGB565 color value.
     */
    void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color);


    /**
     * @brief Draw a rectangle outline.
     *
     * @param x Top-left X.
     * @param y Top-left Y.
     * @param w Rectangle width.
     * @param h Rectangle height.
     * @param color RGB565 color value.
     */
    void drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);


    /**
     * @brief Draw a filled rectangle.
     *
     * @param x Top-left X.
     * @param y Top-left Y.
     * @param w Rectangle width.
     * @param h Rectangle height.
     * @param color RGB565 color value.
     */
    void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);


    /**
     * @brief Draw a circle outline.
     *
     * @param cx Center X.
     * @param cy Center Y.
     * @param radius Circle radius.
     * @param color RGB565 color value.
     */
    void drawCircle(int16_t cx, int16_t cy, int16_t radius, uint16_t color);


    /**
     * @brief Draw a filled circle.
     *
     * @param cx Center X.
     * @param cy Center Y.
     * @param radius Circle radius.
     * @param color RGB565 color value.
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
     * @param bg Background color (RGB565), use same as color for transparent.
     * @param size Font scale (1 = 5x7, 2 = 10x14, etc.)
     */
    void drawString(int16_t x, int16_t y, const char* str, uint16_t color, uint16_t bg = COLOR_BLACK, uint8_t size = 1);


    /**
     * @brief Set backlight brightness.
     *
     * @param on true = backlight on, false = off.
     */
    void setBacklight(bool on);


    /**
     * @brief Set display rotation.
     *
     * @param rotation 0, 1, 2, or 3 (0° / 90° / 180° / 270°).
     */
    void setRotation(uint8_t rotation);


    /**
     * @brief Invert display colors.
     *
     * @param invert true = inverted, false = normal.
     */
    void setInverted(bool invert);


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
    uint16_t getWidth() const { return dispWidth; }


    /**
     * @brief Get display height.
     */
    uint16_t getHeight() const { return dispHeight; }


private:

    uint16_t dispWidth;
    uint16_t dispHeight;
    int16_t xOffset;
    int16_t yOffset;

    gpio_num_t mosiPin;
    gpio_num_t sckPin;
    gpio_num_t csPin;
    gpio_num_t dcPin;
    gpio_num_t rstPin;
    gpio_num_t blkPin;
    spi_host_device_t spiHost;
    spi_device_handle_t spiDevice;
    bool initialized;

    uint8_t rotation;
    uint16_t width;     // Current width (may change with rotation)
    uint16_t height;    // Current height (may change with rotation)


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
     *
     * @param x0 Start X.
     * @param y0 Start Y.
     * @param x1 End X.
     * @param y1 End Y.
     */
    void setWindow(int16_t x0, int16_t y0, int16_t x1, int16_t y1);


    /**
     * @brief Hardware reset the display.
     */
    void hardwareReset();
};
