/**
 * @file max98357.cpp
 * @brief MAX98357 I2S audio amplifier implementation (ESP-IDF).
 *
 * @details
 * Implements I2S communication for audio output.
 */

#include "max98357.h"
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <math.h>
#include <string.h>


static const char* TAG = "MAX98357";


/*
 * =============================================================================
 * CONSTRUCTOR
 * =============================================================================
 */
MAX98357::MAX98357(gpio_num_t dinPin, gpio_num_t bclkPin, gpio_num_t lrcPin,
                   gpio_num_t sdPin, i2s_port_t i2sPort)
    : dinPin(dinPin),
      bclkPin(bclkPin),
      lrcPin(lrcPin),
      sdPin(sdPin),
      i2sPort(i2sPort),
      txHandle(nullptr),
      initialized(false),
      enabled(true),
      currentSampleRate(MAX98357_DEFAULT_SAMPLE_RATE),
      currentBits(MAX98357_DEFAULT_BITS)
{
}


/*
 * =============================================================================
 * DESTRUCTOR
 * =============================================================================
 */
MAX98357::~MAX98357() {
    if (initialized && txHandle) {
        i2s_channel_disable(txHandle);
        i2s_del_channel(txHandle);
    }
}


/*
 * =============================================================================
 * INITIALIZATION
 * =============================================================================
 */
bool MAX98357::init(uint32_t sampleRate, uint8_t bitsPerSample) {
    ESP_LOGI(TAG, "Initializing MAX98357 (DIN=%d, BCLK=%d, LRC=%d, SD=%d)",
             dinPin, bclkPin, lrcPin, sdPin);
    ESP_LOGI(TAG, "Sample rate: %lu Hz, Bits: %d", sampleRate, bitsPerSample);

    currentSampleRate = sampleRate;
    currentBits = bitsPerSample;

    /*
     * -------------------------------------------------------------------------
     * STEP 1: Configure SD (shutdown) pin if specified
     * -------------------------------------------------------------------------
     */
    if (sdPin != GPIO_NUM_NC) {
        gpio_config_t io_conf = {};
        io_conf.mode = GPIO_MODE_OUTPUT;
        io_conf.pin_bit_mask = (1ULL << sdPin);
        io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
        io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
        gpio_config(&io_conf);
        gpio_set_level(sdPin, 1);  // Enable amplifier
    }

    /*
     * -------------------------------------------------------------------------
     * STEP 2: Configure I2S channel
     * -------------------------------------------------------------------------
     */
    i2s_chan_config_t chanConfig = {
        .id = i2sPort,
        .role = I2S_ROLE_MASTER,
        .dma_desc_num = 8,
        .dma_frame_num = 64,
        .auto_clear = true,
    };

    esp_err_t err = i2s_new_channel(&chanConfig, &txHandle, nullptr);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2S channel creation failed: %s", esp_err_to_name(err));
        return false;
    }

    /*
     * -------------------------------------------------------------------------
     * STEP 3: Configure I2S standard mode
     * -------------------------------------------------------------------------
     */
    i2s_std_config_t stdConfig = {
        .clk_cfg = {
            .sample_rate_hz = sampleRate,
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256,
        },
        .slot_cfg = {
            .data_bit_width = (i2s_data_bit_width_t)bitsPerSample,
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
            .slot_mode = I2S_SLOT_MODE_MONO,
            .slot_mask = I2S_STD_SLOT_LEFT,
            .ws_width = bitsPerSample,
            .ws_pol = false,
            .bit_shift = true,
            .msb_right = false,
        },
        .gpio_cfg = {
            .mclk = GPIO_NUM_NC,
            .bclk = bclkPin,
            .ws = lrcPin,
            .dout = dinPin,
            .din = GPIO_NUM_NC,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    err = i2s_channel_init_std_mode(txHandle, &stdConfig);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2S init failed: %s", esp_err_to_name(err));
        i2s_del_channel(txHandle);
        return false;
    }

    /*
     * -------------------------------------------------------------------------
     * STEP 4: Enable I2S channel
     * -------------------------------------------------------------------------
     */
    err = i2s_channel_enable(txHandle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2S enable failed: %s", esp_err_to_name(err));
        i2s_del_channel(txHandle);
        return false;
    }

    initialized = true;
    enabled = true;

    ESP_LOGI(TAG, "MAX98357 initialized successfully");
    return true;
}


/*
 * =============================================================================
 * WRITE SAMPLES
 * =============================================================================
 */
size_t MAX98357::writeSamples(const int16_t* samples, size_t numSamples) {
    if (!initialized || !enabled) return 0;

    size_t bytesWritten = 0;
    size_t bytesToWrite = numSamples * sizeof(int16_t);

    esp_err_t err = i2s_channel_write(txHandle, samples, bytesToWrite, &bytesWritten, portMAX_DELAY);
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2S write failed: %s", esp_err_to_name(err));
        return 0;
    }

    return bytesWritten / sizeof(int16_t);
}


/*
 * =============================================================================
 * PLAY TONE
 * =============================================================================
 */
void MAX98357::playTone(uint32_t frequency, uint32_t durationMs, float volume) {
    if (!initialized || !enabled) return;

    // Clamp volume
    if (volume < 0.0f) volume = 0.0f;
    if (volume > 1.0f) volume = 1.0f;

    // Calculate samples needed
    uint32_t totalSamples = (currentSampleRate * durationMs) / 1000;
    
    // Generate and play in chunks
    const size_t chunkSize = 512;
    int16_t buffer[chunkSize];
    
    float phaseDelta = 2.0f * M_PI * frequency / currentSampleRate;
    float phase = 0.0f;
    float amplitude = 32767.0f * volume;

    uint32_t samplesWritten = 0;
    
    while (samplesWritten < totalSamples) {
        size_t samplesToWrite = chunkSize;
        if (samplesWritten + samplesToWrite > totalSamples) {
            samplesToWrite = totalSamples - samplesWritten;
        }

        // Generate sine wave
        for (size_t i = 0; i < samplesToWrite; i++) {
            buffer[i] = (int16_t)(amplitude * sinf(phase));
            phase += phaseDelta;
            if (phase >= 2.0f * M_PI) {
                phase -= 2.0f * M_PI;
            }
        }

        writeSamples(buffer, samplesToWrite);
        samplesWritten += samplesToWrite;
    }
}


/*
 * =============================================================================
 * BEEP
 * =============================================================================
 */
void MAX98357::beep(uint32_t durationMs) {
    playTone(1000, durationMs, 0.3f);  // 1kHz beep at 30% volume
}


/*
 * =============================================================================
 * STOP
 * =============================================================================
 */
void MAX98357::stop() {
    if (!initialized) return;

    // Write silence to clear buffer
    int16_t silence[256] = {0};
    writeSamples(silence, 256);
}


/*
 * =============================================================================
 * ENABLE/DISABLE
 * =============================================================================
 */
void MAX98357::setEnabled(bool enable) {
    if (sdPin != GPIO_NUM_NC) {
        gpio_set_level(sdPin, enable ? 1 : 0);
    }
    enabled = enable;
    
    ESP_LOGI(TAG, "Amplifier %s", enable ? "enabled" : "disabled");
}


/*
 * =============================================================================
 * SET SAMPLE RATE
 * =============================================================================
 */
bool MAX98357::setSampleRate(uint32_t sampleRate) {
    if (!initialized) return false;

    // Disable channel
    i2s_channel_disable(txHandle);

    // Reconfigure clock
    i2s_std_clk_config_t clkConfig = {
        .sample_rate_hz = sampleRate,
        .clk_src = I2S_CLK_SRC_DEFAULT,
        .mclk_multiple = I2S_MCLK_MULTIPLE_256,
    };

    esp_err_t err = i2s_channel_reconfig_std_clock(txHandle, &clkConfig);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set sample rate: %s", esp_err_to_name(err));
        i2s_channel_enable(txHandle);
        return false;
    }

    currentSampleRate = sampleRate;

    // Re-enable channel
    i2s_channel_enable(txHandle);

    ESP_LOGI(TAG, "Sample rate set to %lu Hz", sampleRate);
    return true;
}
