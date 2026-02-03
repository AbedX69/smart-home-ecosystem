/**
 * @file ili9341.h
 * @brief ILI9341 TFT display driver for ESP32 (ESP-IDF).
 *
 * @details
 * This component handles ILI9341-based TFT displays over SPI.
 * Standard resolution: 240x320 pixels, 65K colors (RGB565).
 * Works with 2.4", 2.8", and 3.2" displays using ILI9341.
 *
 * @note
 * SPI assumptions:
 * - Uses hardware SPI
 * - 20MHz SPI clock (safe for all boards)
 * - RGB565 color format (16-bit per pixel)
 *
 * @par Supported hardware
 * - 2.4" 240x320 TFT with touch (ILI9341 + XPT2046)
 * - 2.8" 240x320 TFT with touch (ILI9341 + XPT2046)
 * - 3.2" 240x320 TFT with touch (ILI9341 + XPT2046)
 * - Any ILI9341-based SPI display
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
 * BEGINNER'S GUIDE: ILI9341 DISPLAYS
 * =============================================================================
 * 
 * ILI9341 is one of the most popular TFT display controllers. You'll find it
 * in many cheap displays from AliExpress, Amazon, etc.
 * 
 * =============================================================================
 * DISPLAY SIZES - SAME CHIP, DIFFERENT GLASS
 * =============================================================================
 * 
 *     2.4" / 2.8" / 3.2" displays with ILI9341:
 *         - ALL use 240x320 pixels
 *         - ALL use same commands
 *         - Only physical size differs
 *         - Same code works for all!
 * 
 *     Think of it like monitors:
 *         - 24" and 27" monitors can both be 1920x1080
 *         - Same resolution, different physical size
 * 
 * =============================================================================
 * THE RED PCB MODULES
 * =============================================================================
 * 
 * The common red PCB modules from AliExpress include:
 * 
 *     1. Display (ILI9341)
 *         - 240x320 pixels
 *         - SPI interface
 *         - 65K colors
 * 
 *     2. Touch (XPT2046)
 *         - Resistive touch
 *         - Separate SPI device
 *         - Shares bus with display
 * 
 *     3. SD Card slot
 *         - For storing images/data
 *         - Also SPI (third device on bus!)
 * 
 * =============================================================================
 * WIRING OVERVIEW
 * =============================================================================
 * 
 *     Module Pin      ESP32           Notes
 *     ──────────      ─────           ─────
 *     VCC             3.3V            Power
 *     GND             GND             Ground
 *     CS              GPIO            Display chip select
 *     RESET           GPIO            Display reset
 *     DC              GPIO            Data/Command
 *     SDI(MOSI)       GPIO (MOSI)     Shared with touch
 *     SCK             GPIO (SCK)      Shared with touch
 *     LED             GPIO or 3.3V    Backlight
 *     SDO(MISO)       GPIO (MISO)     Optional, shared
 *     
 *     T_CLK           Same as SCK     Touch shares clock
 *     T_CS            GPIO            Touch chip select (different!)
 *     T_DIN           Same as MOSI    Touch shares MOSI
 *     T_DO            Same as MISO    Touch shares MISO
 *     T_IRQ           GPIO            Touch interrupt
 * 
 * =============================================================================
 * USAGE EXAMPLE
 * =============================================================================
 * 
 *     #include "ili9341.h"
 *     
 *     void app_main(void) {
 *         ILI9341 display(
 *             GPIO_NUM_23,  // MOSI
 *             GPIO_NUM_19,  // MISO (optional, can be -1)
 *             GPIO_NUM_18,  // SCK
 *             GPIO_NUM_5,   // CS
 *             GPIO_NUM_16,  // DC
 *             GPIO_NUM_17,  // RST
 *             GPIO_NUM_4    // LED (backlight)
 *         );
 *         
 *         display.init();
 *         display.fillScreen(COLOR_BLUE);
 *         display.drawString(10, 10, "Hello!", COLOR_WHITE);
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
 * @brief Display dimensions (ILI9341 standard)
 */
#define ILI9341_WIDTH   240
#define ILI9341_HEIGHT  320


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
 * @class ILI9341
 * @brief ILI9341 TFT display driver over SPI.
 *
 * @details
 * Provides:
 * - Display initialization and configuration
 * - Direct pixel drawing (no frame buffer)
 * - Basic drawing primitives (pixel, line, rectangle, circle)
 * - Text rendering with built-in font
 * - Color utilities
 * - Rotation support
 */
class ILI9341 {

public:

    /**
     * @brief Construct a new ILI9341 display instance.
     *
     * @param mosiPin GPIO for MOSI (SDI on module).
     * @param misoPin GPIO for MISO (SDO on module), -1 if not used.
     * @param sckPin GPIO for SCK.
     * @param csPin GPIO for Chip Select.
     * @param dcPin GPIO for Data/Command.
     * @param rstPin GPIO for Reset.
     * @param ledPin GPIO for Backlight (-1 to skip).
     * @param spiHost SPI host (default: SPI2_HOST).
     */
    ILI9341(gpio_num_t mosiPin, gpio_num_t misoPin, gpio_num_t sckPin,
            gpio_num_t csPin, gpio_num_t dcPin, gpio_num_t rstPin,
            gpio_num_t ledPin = GPIO_NUM_NC,
            spi_host_device_t spiHost = SPI2_HOST);


    /**
     * @brief Destroy the ILI9341 instance.
     */
    ~ILI9341();


    /**
     * @brief Initialize SPI and display.
     *
     * @return true if successful, false on error.
     */
    bool init();


    /**
     * @brief Get the SPI host (for sharing with touch controller).
     */
    spi_host_device_t getSpiHost() const { return spiHost; }


    /**
     * @brief Check if SPI bus is initialized.
     */
    bool isSpiInitialized() const { return initialized; }


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
     * @param bg Background color (RGB565).
     * @param size Font scale (1 = 5x7, 2 = 10x14, etc.)
     */
    void drawString(int16_t x, int16_t y, const char* str, uint16_t color, uint16_t bg = COLOR_BLACK, uint8_t size = 1);


    /**
     * @brief Set backlight on/off.
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
     * @brief Get current display width (changes with rotation).
     */
    uint16_t getWidth() const { return width; }


    /**
     * @brief Get current display height (changes with rotation).
     */
    uint16_t getHeight() const { return height; }


private:

    gpio_num_t mosiPin;
    gpio_num_t misoPin;
    gpio_num_t sckPin;
    gpio_num_t csPin;
    gpio_num_t dcPin;
    gpio_num_t rstPin;
    gpio_num_t ledPin;
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
