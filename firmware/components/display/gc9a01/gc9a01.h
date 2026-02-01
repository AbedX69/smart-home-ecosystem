/**
 * @file gc9a01.h
 * @brief GC9A01 round TFT display driver for ESP32 (ESP-IDF).
 *
 * @details
 * This component handles GC9A01-based round TFT displays over SPI.
 * Resolution: 240x240 pixels, 65K colors (RGB565).
 *
 * @note
 * SPI assumptions:
 * - Uses hardware SPI (VSPI or HSPI)
 * - Supports up to 80MHz SPI clock
 * - RGB565 color format (16-bit per pixel)
 *
 * @par Supported hardware
 * - 1.28" 240x240 round TFT (GC9A01 driver)
 * - Any GC9A01-based SPI display
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
 * BEGINNER'S GUIDE: SPI AND COLOR DISPLAYS
 * =============================================================================
 * 
 * =============================================================================
 * SPI vs I2C - WHAT'S THE DIFFERENCE?
 * =============================================================================
 * 
 * I2C (used for SSD1306 OLED):
 *     - 2 wires (SDA, SCL)
 *     - Slower (400kHz typical)
 *     - Uses addresses to select devices
 *     - Good for small, simple displays
 * 
 * SPI (used for GC9A01 TFT):
 *     - 4+ wires (MOSI, SCK, CS, DC)
 *     - MUCH faster (up to 80MHz!)
 *     - Uses CS pin to select devices
 *     - Good for color displays with lots of pixels
 * 
 * WHY SPI FOR COLOR DISPLAYS?
 *     
 *     SSD1306 OLED: 128 x 64 x 1 bit = 8,192 bits = 1KB per frame
 *     GC9A01 TFT:   240 x 240 x 16 bits = 921,600 bits = 112KB per frame!
 *     
 *     At I2C 400kHz: 112KB would take ~2.3 seconds per frame = unusable
 *     At SPI 40MHz:  112KB takes ~23ms per frame = 43 FPS = smooth!
 * 
 * =============================================================================
 * SPI PIN NAMES (ACTIVE LOW)
 * =============================================================================
 * 
 *     MOSI = Master Out, Slave In (data FROM ESP32 TO display)
 *     MISO = Master In, Slave Out (data FROM display TO ESP32) - not used here
 *     SCK  = Serial Clock (ESP32 controls timing)
 *     CS   = Chip Select (LOW = this device is selected)
 *     DC   = Data/Command (LOW = command, HIGH = pixel data)
 *     
 *     Your module labels:
 *         SDA = MOSI (they called it SDA but it's really MOSI)
 *         SCL = SCK (clock)
 * 
 * =============================================================================
 * HOW SPI COMMUNICATION WORKS
 * =============================================================================
 * 
 *     To send a COMMAND (like "turn on display"):
 *     
 *         CS ──┐          ┌── HIGH (deselect)
 *              └──────────┘   LOW (select this device)
 *         
 *         DC ──┐          ┌── 
 *              └──────────┘   LOW = this is a command
 *         
 *        SCK ──┬─┬─┬─┬─┬─┬─┬─┬── clock pulses
 *              
 *       MOSI ──╫─╫─╫─╫─╫─╫─╫─╫── command byte bits
 *     
 *     To send PIXEL DATA:
 *     
 *         CS ──┐                              ┌──
 *              └──────────────────────────────┘
 *         
 *         DC ────────┐                        ┌──
 *                    └────────────────────────┘ HIGH = this is data
 *         
 *        SCK ──┬─┬─┬─┬─┬─┬─┬─┬─┬─┬─┬─┬─┬─┬─┬─┬── many clock pulses
 *              
 *       MOSI ──╫─╫─╫─╫─╫─╫─╫─╫─╫─╫─╫─╫─╫─╫─╫─╫── pixel data stream
 * 
 * =============================================================================
 * RGB565 COLOR FORMAT
 * =============================================================================
 * 
 * Each pixel is 16 bits (2 bytes):
 * 
 *     Bit:  15 14 13 12 11 | 10 9 8 7 6 5 | 4 3 2 1 0
 *           ─────────────── ───────────── ───────────
 *               RED (5)       GREEN (6)     BLUE (5)
 *     
 *     Why more green bits? Human eyes are more sensitive to green!
 * 
 * COMMON COLORS:
 *     
 *     Black:   0x0000  (R=0,  G=0,  B=0)
 *     White:   0xFFFF  (R=31, G=63, B=31)
 *     Red:     0xF800  (R=31, G=0,  B=0)
 *     Green:   0x07E0  (R=0,  G=63, B=0)
 *     Blue:    0x001F  (R=0,  G=0,  B=31)
 *     Yellow:  0xFFE0  (R=31, G=63, B=0)
 *     Cyan:    0x07FF  (R=0,  G=63, B=31)
 *     Magenta: 0xF81F  (R=31, G=0,  B=31)
 * 
 * TO CONVERT 24-BIT RGB TO RGB565:
 *     
 *     uint16_t rgb565 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
 *     
 *     Example: Orange (255, 165, 0)
 *         R: 255 >> 3 = 31
 *         G: 165 >> 2 = 41
 *         B: 0 >> 3 = 0
 *         RGB565 = (31 << 11) | (41 << 5) | 0 = 0xFD20
 * 
 * =============================================================================
 * USAGE EXAMPLE
 * =============================================================================
 * 
 *     #include "gc9a01.h"
 *     
 *     void app_main(void) {
 *         // Create display
 *         GC9A01 display(
 *             GPIO_NUM_23,  // MOSI (SDA)
 *             GPIO_NUM_18,  // SCK (SCL)
 *             GPIO_NUM_5,   // CS
 *             GPIO_NUM_16,  // DC
 *             GPIO_NUM_17,  // RST
 *             GPIO_NUM_4    // BLK (backlight)
 *         );
 *         
 *         display.init();
 *         
 *         // Fill screen red
 *         display.fillScreen(COLOR_RED);
 *         
 *         // Draw a white circle in the center
 *         display.fillCircle(120, 120, 50, COLOR_WHITE);
 *         
 *         // Draw text
 *         display.drawString(80, 110, "Hello!", COLOR_BLACK);
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
#define GC9A01_WIDTH    240
#define GC9A01_HEIGHT   240


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
 * @class GC9A01
 * @brief GC9A01 round TFT display driver over SPI.
 *
 * @details
 * Provides:
 * - Display initialization and configuration
 * - Direct pixel drawing (no frame buffer - too big!)
 * - Basic drawing primitives (pixel, line, rectangle, circle)
 * - Text rendering with built-in font
 * - Color utilities
 */
class GC9A01 {

public:

    /**
     * @brief Construct a new GC9A01 display instance.
     *
     * @param mosiPin GPIO for MOSI (SDA on module).
     * @param sckPin GPIO for SCK (SCL on module).
     * @param csPin GPIO for Chip Select.
     * @param dcPin GPIO for Data/Command.
     * @param rstPin GPIO for Reset.
     * @param blkPin GPIO for Backlight (-1 to skip).
     * @param spiHost SPI host (default: SPI2_HOST).
     */
    GC9A01(gpio_num_t mosiPin, gpio_num_t sckPin, gpio_num_t csPin,
           gpio_num_t dcPin, gpio_num_t rstPin, gpio_num_t blkPin = GPIO_NUM_NC,
           spi_host_device_t spiHost = SPI2_HOST);


    /**
     * @brief Destroy the GC9A01 instance.
     */
    ~GC9A01();


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
     * @param x X coordinate (0-239).
     * @param y Y coordinate (0-239).
     * @param color RGB565 color value.
     */
    void drawPixel(int16_t x, int16_t y, uint16_t color);


    /**
     * @brief Draw a horizontal line.
     *
     * @param x Starting X position.
     * @param y Y position.
     * @param width Line width in pixels.
     * @param color RGB565 color value.
     */
    void drawHLine(int16_t x, int16_t y, int16_t width, uint16_t color);


    /**
     * @brief Draw a vertical line.
     *
     * @param x X position.
     * @param y Starting Y position.
     * @param height Line height in pixels.
     * @param color RGB565 color value.
     */
    void drawVLine(int16_t x, int16_t y, int16_t height, uint16_t color);


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
     * @param width Rectangle width.
     * @param height Rectangle height.
     * @param color RGB565 color value.
     */
    void drawRect(int16_t x, int16_t y, int16_t width, int16_t height, uint16_t color);


    /**
     * @brief Draw a filled rectangle.
     *
     * @param x Top-left X.
     * @param y Top-left Y.
     * @param width Rectangle width.
     * @param height Rectangle height.
     * @param color RGB565 color value.
     */
    void fillRect(int16_t x, int16_t y, int16_t width, int16_t height, uint16_t color);


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


private:

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
