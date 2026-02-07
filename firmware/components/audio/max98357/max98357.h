/**
 * @file max98357.h
 * @brief MAX98357 I2S audio amplifier driver for ESP32 (ESP-IDF).
 *
 * @details
 * This component handles MAX98357-based I2S audio amplifiers.
 * Outputs audio to a speaker from digital I2S data.
 *
 * @note
 * - No SPI/I2C needed - uses I2S protocol
 * - 3W output into 4Ω speaker
 * - Mono output (left channel by default)
 *
 * @par Supported hardware
 * - MAX98357A breakout boards (Adafruit, generic)
 * - Similar I2S DAC/amplifier modules
 */

/*
 * =============================================================================
 * BEGINNER'S GUIDE: I2S AUDIO
 * =============================================================================
 * 
 * =============================================================================
 * WHAT IS I2S?
 * =============================================================================
 * 
 *     I2S (Inter-IC Sound) is a digital audio protocol:
 *     
 *         ┌─────────┐                    ┌─────────────┐
 *         │  ESP32  │ ── BCLK ────────── │  MAX98357   │ ──── Speaker
 *         │         │ ── LRC  ────────── │  (I2S Amp)  │
 *         │         │ ── DIN  ────────── │             │
 *         └─────────┘                    └─────────────┘
 *     
 *     Three signals:
 *         BCLK  = Bit Clock (timing for each bit)
 *         LRC   = Left/Right Clock (which channel)
 *         DIN   = Data In (actual audio samples)
 * 
 * =============================================================================
 * MAX98357 FEATURES
 * =============================================================================
 * 
 *     - I2S input (digital) → Speaker output (analog)
 *     - Built-in Class D amplifier (efficient, cool)
 *     - 3.2W into 4Ω, 1.8W into 8Ω
 *     - No external components needed
 *     - Mono output (picks left channel by default)
 * 
 * =============================================================================
 * GAIN PIN
 * =============================================================================
 * 
 *     The GAIN pin sets output volume:
 *     
 *         GAIN connected to:    Gain
 *         ─────────────────     ────
 *         GND                   9dB
 *         Floating (NC)         12dB (default)
 *         VIN                   15dB
 *         100kΩ to GND          6dB
 *         100kΩ to VIN          3dB
 *     
 *     Leave floating for normal use.
 * 
 * =============================================================================
 * SD (SHUTDOWN) PIN
 * =============================================================================
 * 
 *     SD pin controls power:
 *     
 *         SD HIGH or floating = Amplifier ON
 *         SD LOW              = Amplifier OFF (low power)
 *     
 *     Can leave unconnected, or use GPIO for power control.
 * 
 * =============================================================================
 * WIRING
 * =============================================================================
 * 
 *     MAX98357        ESP32
 *     ────────        ─────
 *     VIN             5V (or 3.3V)
 *     GND             GND
 *     DIN             GPIO (I2S Data Out)
 *     BCLK            GPIO (I2S Bit Clock)
 *     LRC             GPIO (I2S LR Clock)
 *     SD              GPIO (optional) or leave floating
 *     GAIN            Leave floating for 12dB
 * 
 * =============================================================================
 * USAGE EXAMPLE
 * =============================================================================
 * 
 *     #include "max98357.h"
 *     
 *     void app_main(void) {
 *         MAX98357 amp(
 *             GPIO_NUM_25,   // DIN (data)
 *             GPIO_NUM_26,   // BCLK (bit clock)
 *             GPIO_NUM_27    // LRC (left/right clock)
 *         );
 *         
 *         amp.init();
 *         
 *         // Play a tone
 *         amp.playTone(440, 1000);  // 440Hz for 1 second
 *         
 *         // Play raw samples
 *         int16_t samples[1000];
 *         // ... fill samples ...
 *         amp.writeSamples(samples, 1000);
 *     }
 * 
 * =============================================================================
 */

#pragma once

#include <driver/i2s_std.h>
#include <driver/gpio.h>
#include <stdint.h>


/**
 * @brief Default I2S configuration
 */
#define MAX98357_DEFAULT_SAMPLE_RATE    44100
#define MAX98357_DEFAULT_BITS           16


/**
 * @class MAX98357
 * @brief MAX98357 I2S audio amplifier driver.
 *
 * @details
 * Provides:
 * - I2S initialization
 * - Sample playback
 * - Tone generation
 * - Volume control (via sample scaling)
 * - Shutdown control
 */
class MAX98357 {

public:

    /**
     * @brief Construct a new MAX98357 instance.
     *
     * @param dinPin GPIO for I2S Data Out (DIN on module).
     * @param bclkPin GPIO for I2S Bit Clock (BCLK on module).
     * @param lrcPin GPIO for I2S Left/Right Clock (LRC on module).
     * @param sdPin GPIO for Shutdown control (-1 to skip).
     * @param i2sPort I2S port number (default: I2S_NUM_0).
     */
    MAX98357(gpio_num_t dinPin, gpio_num_t bclkPin, gpio_num_t lrcPin,
             gpio_num_t sdPin = GPIO_NUM_NC, i2s_port_t i2sPort = I2S_NUM_0);


    /**
     * @brief Destroy the MAX98357 instance.
     */
    ~MAX98357();


    /**
     * @brief Initialize I2S for audio output.
     *
     * @param sampleRate Sample rate in Hz (default: 44100).
     * @param bitsPerSample Bits per sample (default: 16).
     * @return true if successful, false on error.
     */
    bool init(uint32_t sampleRate = MAX98357_DEFAULT_SAMPLE_RATE,
              uint8_t bitsPerSample = MAX98357_DEFAULT_BITS);


    /**
     * @brief Write audio samples to the amplifier.
     *
     * @param samples Pointer to signed 16-bit samples.
     * @param numSamples Number of samples to write.
     * @return Number of samples actually written.
     */
    size_t writeSamples(const int16_t* samples, size_t numSamples);


    /**
     * @brief Play a sine wave tone.
     *
     * @param frequency Frequency in Hz.
     * @param durationMs Duration in milliseconds.
     * @param volume Volume 0.0 to 1.0 (default: 0.5).
     */
    void playTone(uint32_t frequency, uint32_t durationMs, float volume = 0.5f);


    /**
     * @brief Play a simple beep.
     *
     * @param durationMs Duration in milliseconds.
     */
    void beep(uint32_t durationMs = 100);


    /**
     * @brief Stop audio output and clear buffer.
     */
    void stop();


    /**
     * @brief Enable or disable the amplifier.
     *
     * @param enable true = on, false = shutdown.
     */
    void setEnabled(bool enable);


    /**
     * @brief Check if amplifier is enabled.
     */
    bool isEnabled() const { return enabled; }


    /**
     * @brief Set the sample rate.
     *
     * @param sampleRate New sample rate in Hz.
     * @return true if successful.
     */
    bool setSampleRate(uint32_t sampleRate);


    /**
     * @brief Get current sample rate.
     */
    uint32_t getSampleRate() const { return currentSampleRate; }


private:

    gpio_num_t dinPin;
    gpio_num_t bclkPin;
    gpio_num_t lrcPin;
    gpio_num_t sdPin;
    i2s_port_t i2sPort;
    i2s_chan_handle_t txHandle;
    bool initialized;
    bool enabled;
    uint32_t currentSampleRate;
    uint8_t currentBits;
};
