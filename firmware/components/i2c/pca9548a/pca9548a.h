/**
 * @file pca9548a.h
 * @brief PCA9548A I2C multiplexer driver for ESP32 (ESP-IDF).
 *
 * @details
 * This component handles PCA9548A 8-channel I2C multiplexer.
 * Allows connecting multiple I2C devices with the same address.
 *
 * @note
 * - 8 channels (0-7)
 * - Each channel is independent I2C bus
 * - Can enable multiple channels at once
 *
 * @par Supported hardware
 * - PCA9548A breakout boards
 * - TCA9548A (same chip, different manufacturer)
 */

/*
 * =============================================================================
 * BEGINNER'S GUIDE: I2C MULTIPLEXER
 * =============================================================================
 * 
 * =============================================================================
 * THE PROBLEM
 * =============================================================================
 * 
 *     I2C devices have fixed addresses. What if you want 4 SSD1306 displays?
 *     They all have address 0x3C - you can't put them on the same bus!
 *     
 *         ESP32 ─── I2C Bus ─┬─ SSD1306 (0x3C)  ✓
 *                            ├─ SSD1306 (0x3C)  ✗ CONFLICT!
 *                            ├─ SSD1306 (0x3C)  ✗ CONFLICT!
 *                            └─ SSD1306 (0x3C)  ✗ CONFLICT!
 * 
 * =============================================================================
 * THE SOLUTION: I2C MULTIPLEXER
 * =============================================================================
 * 
 *     PCA9548A sits between ESP32 and devices, routing I2C to one channel:
 *     
 *         ESP32 ─── I2C ─── PCA9548A ─┬─ CH0 ─── SSD1306 (0x3C)
 *                          (0x70)     ├─ CH1 ─── SSD1306 (0x3C)
 *                                     ├─ CH2 ─── SSD1306 (0x3C)
 *                                     ├─ CH3 ─── SSD1306 (0x3C)
 *                                     ├─ CH4 ─── (empty)
 *                                     ├─ CH5 ─── (empty)
 *                                     ├─ CH6 ─── (empty)
 *                                     └─ CH7 ─── (empty)
 *     
 *     To talk to display on CH2:
 *         1. Tell PCA9548A: "enable channel 2"
 *         2. Talk to SSD1306 normally at 0x3C
 *         3. PCA9548A routes traffic to CH2 only
 * 
 * =============================================================================
 * ADDRESS PINS (A0, A1, A2)
 * =============================================================================
 * 
 *     PCA9548A address is 0x70 + (A2 A1 A0 in binary):
 *     
 *         A2  A1  A0    Address
 *         ──  ──  ──    ───────
 *         0   0   0     0x70 (default - all to GND)
 *         0   0   1     0x71
 *         0   1   0     0x72
 *         0   1   1     0x73
 *         1   0   0     0x74
 *         1   0   1     0x75
 *         1   1   0     0x76
 *         1   1   1     0x77
 *     
 *     You can have up to 8 multiplexers = 64 channels!
 * 
 * =============================================================================
 * WIRING
 * =============================================================================
 * 
 *     PCA9548A        ESP32
 *     ────────        ─────
 *     VIN             3.3V
 *     GND             GND
 *     SDA             GPIO (I2C SDA)
 *     SCL             GPIO (I2C SCL)
 *     RST             3.3V (or GPIO for reset control)
 *     A0              GND (or 3.3V to change address)
 *     A1              GND
 *     A2              GND
 *     
 *     Channel outputs (directly to I2C devices):
 *     SD0/SC0         Device on channel 0
 *     SD1/SC1         Device on channel 1
 *     ...etc
 * 
 * =============================================================================
 * USAGE EXAMPLE
 * =============================================================================
 * 
 *     #include "pca9548a.h"
 *     #include "ssd1306.h"
 *     
 *     PCA9548A mux(GPIO_NUM_21, GPIO_NUM_22);  // SDA, SCL
 *     mux.init();
 *     
 *     // Talk to display on channel 0
 *     mux.selectChannel(0);
 *     SSD1306 display0(GPIO_NUM_21, GPIO_NUM_22);
 *     display0.init();
 *     display0.drawString(0, 0, "Display 0");
 *     display0.update();
 *     
 *     // Talk to display on channel 1
 *     mux.selectChannel(1);
 *     SSD1306 display1(GPIO_NUM_21, GPIO_NUM_22);
 *     display1.init();
 *     display1.drawString(0, 0, "Display 1");
 *     display1.update();
 * 
 * =============================================================================
 */

#pragma once

#include <driver/i2c_master.h>
#include <driver/gpio.h>
#include <stdint.h>


/**
 * @brief Default I2C address (A0=A1=A2=GND)
 */
#define PCA9548A_DEFAULT_ADDR   0x70


/**
 * @brief Number of channels
 */
#define PCA9548A_NUM_CHANNELS   8


/**
 * @class PCA9548A
 * @brief PCA9548A I2C multiplexer driver.
 *
 * @details
 * Provides:
 * - Channel selection (0-7)
 * - Multi-channel enable
 * - Channel disable
 * - Reset control
 */
class PCA9548A {

public:

    /**
     * @brief Construct a new PCA9548A instance.
     *
     * @param sdaPin GPIO for I2C SDA.
     * @param sclPin GPIO for I2C SCL.
     * @param address I2C address (0x70-0x77, default 0x70).
     * @param rstPin GPIO for reset (-1 to skip).
     * @param i2cPort I2C port number (default: I2C_NUM_0).
     */
    PCA9548A(gpio_num_t sdaPin, gpio_num_t sclPin,
             uint8_t address = PCA9548A_DEFAULT_ADDR,
             gpio_num_t rstPin = GPIO_NUM_NC,
             i2c_port_t i2cPort = I2C_NUM_0);


    /**
     * @brief Destroy the PCA9548A instance.
     */
    ~PCA9548A();


    /**
     * @brief Initialize I2C and multiplexer.
     *
     * @return true if successful, false on error.
     */
    bool init();


    /**
     * @brief Select a single channel (disables all others).
     *
     * @param channel Channel number (0-7).
     * @return true if successful.
     */
    bool selectChannel(uint8_t channel);


    /**
     * @brief Enable specific channels (bitmask).
     *
     * @param channelMask Bitmask of channels (bit 0 = CH0, bit 7 = CH7).
     * @return true if successful.
     *
     * @note Multiple channels can be enabled simultaneously.
     *       Example: enableChannels(0b00000101) enables CH0 and CH2.
     */
    bool enableChannels(uint8_t channelMask);


    /**
     * @brief Disable all channels.
     *
     * @return true if successful.
     */
    bool disableAll();


    /**
     * @brief Get currently enabled channels.
     *
     * @return Bitmask of enabled channels, or 0xFF on error.
     */
    uint8_t getEnabledChannels();


    /**
     * @brief Check if a specific channel is enabled.
     *
     * @param channel Channel number (0-7).
     * @return true if enabled.
     */
    bool isChannelEnabled(uint8_t channel);


    /**
     * @brief Hardware reset the multiplexer.
     *
     * @note Only works if RST pin was specified.
     */
    void reset();


    /**
     * @brief Scan for devices on a specific channel.
     *
     * @param channel Channel to scan (0-7).
     * @param addresses Array to store found addresses.
     * @param maxAddresses Max addresses to find.
     * @return Number of devices found.
     */
    uint8_t scanChannel(uint8_t channel, uint8_t* addresses, uint8_t maxAddresses);


    /**
     * @brief Get the I2C bus handle for direct device communication.
     */
    i2c_master_bus_handle_t getBusHandle() const { return busHandle; }


private:

    gpio_num_t sdaPin;
    gpio_num_t sclPin;
    uint8_t address;
    gpio_num_t rstPin;
    i2c_port_t i2cPort;
    i2c_master_bus_handle_t busHandle;
    i2c_master_dev_handle_t devHandle;
    bool initialized;
    uint8_t currentChannels;


    /**
     * @brief Write channel selection register.
     */
    bool writeRegister(uint8_t value);


    /**
     * @brief Read channel selection register.
     */
    uint8_t readRegister();
};
