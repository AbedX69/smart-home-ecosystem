/**
 * @file xpt2046.h
 * @brief XPT2046 resistive touch controller driver for ESP32 (ESP-IDF).
 *
 * @details
 * This component handles XPT2046-based resistive touch screens over SPI.
 * Commonly found on ILI9341 TFT display modules.
 *
 * @note
 * - SPI bus can be shared with display (different CS pins)
 * - Uses interrupt pin (T_IRQ) for touch detection
 * - Returns raw ADC values (0-4095) that need calibration
 *
 * @par Supported hardware
 * - XPT2046 touch controller
 * - ADS7843 compatible controllers
 * - Red PCB TFT modules with T_ pins
 */

/*
 * =============================================================================
 * BEGINNER'S GUIDE: RESISTIVE TOUCH
 * =============================================================================
 * 
 * =============================================================================
 * RESISTIVE vs CAPACITIVE TOUCH
 * =============================================================================
 * 
 *     Capacitive (phones, tablets):
 *         - Detects electrical charge of finger
 *         - Multi-touch capable
 *         - More expensive
 *         - Doesn't work with gloves
 *     
 *     Resistive (cheap TFT modules):
 *         - Two layers that touch when pressed
 *         - Single touch only
 *         - Cheaper
 *         - Works with any object (stylus, glove)
 *         - Needs calibration
 * 
 * =============================================================================
 * HOW XPT2046 WORKS
 * =============================================================================
 * 
 *     The touch screen has two resistive layers:
 *     
 *         ┌────────────────────┐  ← Top layer (X axis)
 *         │                    │
 *         │    Press here      │
 *         │        ↓           │
 *         └────────────────────┘
 *         ┌────────────────────┐  ← Bottom layer (Y axis)
 *         │                    │
 *         └────────────────────┘
 *     
 *     When you press, layers touch and create a voltage divider.
 *     XPT2046 reads this voltage and converts to 12-bit ADC value.
 *     
 *     Raw values: 0-4095
 *     You need to calibrate: map raw → screen pixels
 * 
 * =============================================================================
 * SHARING SPI BUS WITH DISPLAY
 * =============================================================================
 * 
 *     Display and touch can share MOSI, MISO, SCK.
 *     They MUST have different CS pins:
 *     
 *         ESP32 ─────┬──── MOSI ───┬──── Display SDI
 *                    │             └──── Touch T_DIN
 *                    │
 *                    ├──── MISO ───┬──── Display SDO
 *                    │             └──── Touch T_DO
 *                    │
 *                    ├──── SCK ────┬──── Display SCK
 *                    │             └──── Touch T_CLK
 *                    │
 *                    ├──── GPIO ──────── Display CS
 *                    │
 *                    └──── GPIO ──────── Touch T_CS (different!)
 * 
 *     When reading touch, we assert T_CS low.
 *     Display CS stays high, so display ignores the traffic.
 * 
 * =============================================================================
 * CALIBRATION
 * =============================================================================
 * 
 *     Raw touch values don't match screen pixels directly.
 *     
 *         Touch at top-left might give:     X=3800, Y=300
 *         Touch at bottom-right might give: X=200, Y=3700
 *     
 *     Calibration finds the mapping:
 *         screenX = map(rawX, X_MIN, X_MAX, 0, 239)
 *         screenY = map(rawY, Y_MIN, Y_MAX, 0, 319)
 *     
 *     You can run calibration once and save the values.
 * 
 * =============================================================================
 * USAGE EXAMPLE
 * =============================================================================
 * 
 *     #include "xpt2046.h"
 *     
 *     void app_main(void) {
 *         // Create touch controller (shares SPI with display)
 *         XPT2046 touch(
 *             SPI2_HOST,        // Same SPI host as display
 *             GPIO_NUM_33,      // T_CS
 *             GPIO_NUM_36       // T_IRQ
 *         );
 *         
 *         touch.init();
 *         
 *         // Optional: set calibration
 *         touch.setCalibration(200, 3800, 300, 3700);
 *         
 *         while (1) {
 *             if (touch.isTouched()) {
 *                 int16_t x, y;
 *                 touch.getPosition(&x, &y);
 *                 printf("Touch at: %d, %d\n", x, y);
 *             }
 *             vTaskDelay(10);
 *         }
 *     }
 * 
 * =============================================================================
 */

#pragma once

#include <driver/spi_master.h>
#include <driver/gpio.h>
#include <stdint.h>


/**
 * @class XPT2046
 * @brief XPT2046 resistive touch controller driver over SPI.
 *
 * @details
 * Provides:
 * - Touch detection via IRQ pin
 * - Raw coordinate reading (12-bit ADC)
 * - Calibrated coordinate reading
 * - Pressure detection
 */
class XPT2046 {

public:

    /**
     * @brief Construct a new XPT2046 touch instance.
     *
     * @param spiHost SPI host to use (should match display).
     * @param csPin GPIO for Touch Chip Select (T_CS).
     * @param irqPin GPIO for Touch Interrupt (T_IRQ).
     */
    XPT2046(spi_host_device_t spiHost, gpio_num_t csPin, gpio_num_t irqPin);


    /**
     * @brief Destroy the XPT2046 instance.
     */
    ~XPT2046();


    /**
     * @brief Initialize SPI device for touch.
     *
     * @note Call this AFTER display init (SPI bus already configured).
     * @return true if successful, false on error.
     */
    bool init();


    /**
     * @brief Check if screen is being touched.
     *
     * @return true if touched (IRQ pin low).
     */
    bool isTouched();


    /**
     * @brief Get raw touch coordinates (0-4095).
     *
     * @param x Pointer to store raw X value.
     * @param y Pointer to store raw Y value.
     * @return true if valid reading, false if not touched.
     */
    bool getRawPosition(int16_t* x, int16_t* y);


    /**
     * @brief Get calibrated touch coordinates (screen pixels).
     *
     * @param x Pointer to store X pixel position.
     * @param y Pointer to store Y pixel position.
     * @return true if valid reading, false if not touched.
     */
    bool getPosition(int16_t* x, int16_t* y);


    /**
     * @brief Get touch pressure (Z axis).
     *
     * @return Pressure value (higher = harder press), 0 if not touched.
     */
    uint16_t getPressure();


    /**
     * @brief Set calibration values.
     *
     * @param xMin Raw X value at left edge.
     * @param xMax Raw X value at right edge.
     * @param yMin Raw Y value at top edge.
     * @param yMax Raw Y value at bottom edge.
     */
    void setCalibration(int16_t xMin, int16_t xMax, int16_t yMin, int16_t yMax);


    /**
     * @brief Set screen dimensions for calibration mapping.
     *
     * @param width Screen width in pixels (default: 240).
     * @param height Screen height in pixels (default: 320).
     */
    void setScreenSize(uint16_t width, uint16_t height);


    /**
     * @brief Set display rotation for coordinate mapping.
     *
     * @param rotation 0, 1, 2, or 3 (matches display rotation).
     */
    void setRotation(uint8_t rotation);


private:

    spi_host_device_t spiHost;
    gpio_num_t csPin;
    gpio_num_t irqPin;
    spi_device_handle_t spiDevice;
    bool initialized;

    // Calibration values
    int16_t calXMin;
    int16_t calXMax;
    int16_t calYMin;
    int16_t calYMax;

    // Screen dimensions
    uint16_t screenWidth;
    uint16_t screenHeight;
    uint8_t rotation;


    /**
     * @brief Read a value from XPT2046.
     *
     * @param command Command byte (channel select).
     * @return 12-bit ADC value.
     */
    uint16_t readChannel(uint8_t command);


    /**
     * @brief Map raw value to screen coordinate.
     */
    int16_t mapValue(int16_t value, int16_t inMin, int16_t inMax, int16_t outMin, int16_t outMax);
};
