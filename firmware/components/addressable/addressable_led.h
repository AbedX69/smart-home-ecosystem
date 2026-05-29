/**
 * @file addressable_led.h
 * @brief Addressable LED strip driver for ESP32 (ESP-IDF).
 *
 * @details
 * This component controls addressable LED strips like WS2812B, SK6812 RGBW,
 * and SK6812 RGBWW. Each LED in the strip can be set to a different color
 * independently.
 *
 * Supports two transport backends:
 *   - RMT (Remote Control peripheral) — default, uses a single data pin
 *   - SPI — uses the SPI peripheral's MOSI line for data output
 *
 * Both backends produce the same single-wire NRZ protocol. From the LED
 * strip's perspective there is no difference. SPI is useful when:
 *   - RMT channels are exhausted
 *   - Certain GPIOs don't route cleanly to RMT on a given chip variant
 *   - You want DMA-backed transmission for very long strips
 *
 * @note
 * Electrical assumptions:
 * - Data pin connected to strip's DIN (Data In)
 * - Common ground between ESP32 and LED strip
 * - External 5V power supply for strips (don't power from ESP32)
 *
 * @warning
 * Long strips draw significant current. 60 LEDs at full white = ~3.6A.
 * Always use appropriately rated power supplies.
 *
 * @par Supported LED Types
 * - WS2812B (RGB, 3 bytes per LED)
 * - SK6812 RGBW (RGB + White, 4 bytes per LED)
 * - SK6812 RGBWW (RGB + Warm White + Cool White, 5 bytes per LED)
 *
 * @par Supported Backends
 * - RMT (default) — works on any GPIO, uses RMT peripheral
 * - SPI — uses SPI MOSI pin, requires SPI-capable GPIO
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
 * BEGINNER'S GUIDE: ADDRESSABLE LED STRIPS
 * =============================================================================
 * 
 * If you're new to addressable LEDs, this section explains everything
 * from the ground up. Experienced developers can skip to the class definition.
 * 
 * =============================================================================
 * WHAT ARE ADDRESSABLE LEDS?
 * =============================================================================
 * 
 * Unlike regular LED strips where all LEDs are the same color, addressable
 * strips let you control EACH LED individually. You can make rainbows,
 * chase effects, or display patterns.
 * 
 * Physical appearance:
 * 
 *     ┌─────┐   ┌─────┐   ┌─────┐   ┌─────┐
 *     │ LED │───│ LED │───│ LED │───│ LED │───→ ...
 *     │  0  │   │  1  │   │  2  │   │  3  │
 *     └─────┘   └─────┘   └─────┘   └─────┘
 *        │         │         │         │
 *        └─────────┴─────────┴─────────┘
 *                      │
 *                  Data flows
 *                  this way →
 * 
 * Each LED has a tiny chip inside that:
 *     1. Reads incoming color data
 *     2. Keeps the first color for itself
 *     3. Passes remaining colors to the next LED
 * 
 * =============================================================================
 * HOW THE DATA PROTOCOL WORKS
 * =============================================================================
 * 
 * The ESP32 sends color data as a series of precisely-timed pulses:
 * 
 *     Bit "0":                 Bit "1":
 *     
 *     ┌──┐                     ┌────────┐
 *     │  │                     │        │
 *     │  └────────             │        └──
 *     
 *     ~0.4µs HIGH              ~0.8µs HIGH
 *     ~0.85µs LOW              ~0.45µs LOW
 * 
 * The timing must be VERY precise (within ~150 nanoseconds).
 * That's why we use hardware peripherals (RMT or SPI) — they handle
 * timing in hardware while the CPU is free for other work.
 * 
 * =============================================================================
 * RMT vs SPI BACKEND
 * =============================================================================
 * 
 * Both backends produce the same single-wire NRZ waveform. The LED strip
 * can't tell which one is driving it.
 * 
 * RMT BACKEND (default):
 *     - Uses the RMT (Remote Control) peripheral
 *     - Designed for precise pulse generation
 *     - Works on any GPIO
 *     - Limited number of channels (varies by chip)
 *     - Custom encoder converts bytes → timed pulses
 * 
 * SPI BACKEND:
 *     - Uses SPI peripheral's MOSI line
 *     - Encodes each LED data bit as multiple SPI bits
 *     - "Bit 1" → 0b11111000 (high for 5 SPI clocks, low for 3)
 *     - "Bit 0" → 0b11000000 (high for 2 SPI clocks, low for 6)
 *     - At ~6.4 MHz SPI clock, each SPI bit is ~156ns → correct LED timing
 *     - Uses DMA for zero-CPU transmission
 *     - Needs SPI-capable GPIO for MOSI
 * 
 *     SPI BIT ENCODING (visual):
 * 
 *         LED bit "1" → SPI byte 0xF8:
 *         ┌─────────────┐
 *         │ 1 1 1 1 1 │ 0 0 0
 *         └─────────────┘
 *          ~780ns HIGH    ~470ns LOW   ← matches T1H/T1L spec
 * 
 *         LED bit "0" → SPI byte 0xC0:
 *         ┌───┐
 *         │1 1│ 0 0 0 0 0 0
 *         └───┘
 *          ~312ns HIGH    ~937ns LOW   ← matches T0H/T0L spec
 * 
 * WHEN TO USE WHICH:
 *     - RMT: Default choice, works everywhere, proven reliable
 *     - SPI: When RMT doesn't work on your GPIO, or for very long strips
 *            where DMA helps, or when RMT channels are all in use
 * 
 * =============================================================================
 * USAGE EXAMPLES
 * =============================================================================
 * 
 *     #include "addressable_led.h"
 *     
 *     void app_main(void) {
 *         // RMT backend (default — backward compatible):
 *         AddressableLED strip1(GPIO_NUM_4, 60, LedType::WS2812B);
 *         strip1.init();
 *         
 *         // SPI backend (explicit):
 *         AddressableLED strip2(GPIO_NUM_11, 144, LedType::SK6812_RGBW,
 *                               ColorOrder::GRBW, TransportBackend::SPI);
 *         strip2.init();
 *         
 *         // Same API for both:
 *         strip1.setPixel(0, 255, 0, 0);
 *         strip2.setPixel(0, 0, 0, 0, 255);  // RGBW white channel
 *         strip1.show();
 *         strip2.show();
 *     }
 * 
 * =============================================================================
 */

#pragma once

#include <driver/rmt_tx.h>
#include <driver/rmt_encoder.h>
#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <stdint.h>
#include <stdbool.h>


/*
 * =============================================================================
 * LED TYPE ENUMERATION
 * =============================================================================
 */

/**
 * @enum LedType
 * @brief Supported addressable LED chip types.
 */
enum class LedType {
    WS2812B,        ///< RGB (3 bytes per pixel). Default order: GRB.
    SK6812_RGBW,    ///< RGBW (4 bytes per pixel). Default order: GRBW.
    SK6812_RGBWW    ///< RGBWW (5 bytes per pixel). Default order: GRBWW.
};


/*
 * =============================================================================
 * COLOR ORDER ENUMERATION
 * =============================================================================
 */

/**
 * @enum ColorOrder
 * @brief Byte ordering for color data sent to LEDs.
 *
 * If your colors look wrong (red shows as green, etc.), try a different order.
 */
enum class ColorOrder {
    // 3-channel orders
    GRB,    ///< Green, Red, Blue (WS2812B default)
    RGB,    ///< Red, Green, Blue
    BGR,    ///< Blue, Green, Red
    BRG,    ///< Blue, Red, Green
    RBG,    ///< Red, Blue, Green
    GBR,    ///< Green, Blue, Red
    
    // 4-channel orders
    GRBW,   ///< Green, Red, Blue, White (SK6812 RGBW default)
    RGBW,   ///< Red, Green, Blue, White
    BGRW,   ///< Blue, Green, Red, White
    WGRB,   ///< White, Green, Red, Blue
    
    // 5-channel orders
    GRBWW,  ///< Green, Red, Blue, Warm White, Cool White (default)
    RGBWW   ///< Red, Green, Blue, Warm White, Cool White
};


/*
 * =============================================================================
 * TRANSPORT BACKEND ENUMERATION
 * =============================================================================
 */

/**
 * @enum TransportBackend
 * @brief Which hardware peripheral to use for data transmission.
 *
 * @details
 * Both produce the same single-wire NRZ protocol. The LED strip
 * can't tell which one is driving it.
 */
enum class TransportBackend {
    RMT,    ///< RMT peripheral (default). Works on any GPIO.
    SPI     ///< SPI peripheral MOSI. Needs SPI-capable GPIO. Uses DMA.
};


/*
 * =============================================================================
 * ADDRESSABLE LED CLASS
 * =============================================================================
 */

/**
 * @class AddressableLED
 * @brief Driver for WS2812B, SK6812, and similar addressable LED strips.
 *
 * @details
 * Supports RMT and SPI backends. Uses double buffering for flicker-free
 * updates. Includes optional gamma correction.
 *
 * Basic usage:
 * @code
 *     AddressableLED strip(GPIO_NUM_4, 60, LedType::WS2812B);
 *     strip.init();
 *     strip.setPixel(0, 255, 0, 0);  // Red
 *     strip.show();
 * @endcode
 *
 * SPI backend:
 * @code
 *     AddressableLED strip(GPIO_NUM_11, 144, LedType::SK6812_RGBW,
 *                          ColorOrder::GRBW, TransportBackend::SPI);
 *     strip.init();
 *     strip.fill(0, 0, 0, 255);  // White channel
 *     strip.show();
 * @endcode
 */
class AddressableLED {

public:

    /**
     * @brief Construct an AddressableLED controller.
     *
     * @param pin       GPIO pin connected to strip's DIN (Data In).
     *                  For SPI backend, this must be a valid MOSI pin.
     * @param numLeds   Number of LEDs in the strip.
     * @param type      LED chip type (default: WS2812B).
     * @param order     Color byte order (default: GRB, auto-selected per type).
     * @param backend   Transport backend (default: RMT).
     *
     * @note Does not initialize hardware. Call init() after construction.
     */
    AddressableLED(gpio_num_t pin, uint16_t numLeds,
                   LedType type = LedType::WS2812B,
                   ColorOrder order = ColorOrder::GRB,
                   TransportBackend backend = TransportBackend::RMT);

    /**
     * @brief Destructor. Frees buffers and releases peripheral resources.
     */
    ~AddressableLED();


    /**
     * @brief Initialize hardware peripheral and allocate buffers.
     *
     * @return true on success, false on failure (check logs).
     *
     * @warning Must be called before using any other methods.
     */
    bool init();


    /* ═══════════════════════════════════════════════════════════════════
     * PIXEL SETTING METHODS
     * ═══════════════════════════════════════════════════════════════════ */

    /**
     * @brief Set a pixel color (RGB version for WS2812B).
     *
     * @param index LED index (0 to numLeds-1).
     * @param r     Red component (0-255).
     * @param g     Green component (0-255).
     * @param b     Blue component (0-255).
     */
    void setPixel(uint16_t index, uint8_t r, uint8_t g, uint8_t b);

    /**
     * @brief Set a pixel color (RGBW version for SK6812 RGBW).
     *
     * @param index LED index (0 to numLeds-1).
     * @param r     Red component (0-255).
     * @param g     Green component (0-255).
     * @param b     Blue component (0-255).
     * @param w     White component (0-255).
     */
    void setPixel(uint16_t index, uint8_t r, uint8_t g, uint8_t b, uint8_t w);

    /**
     * @brief Set a pixel color (RGBWW version for SK6812 RGBWW).
     *
     * @param index LED index (0 to numLeds-1).
     * @param r     Red component (0-255).
     * @param g     Green component (0-255).
     * @param b     Blue component (0-255).
     * @param ww    Warm white component (0-255).
     * @param cw    Cool white component (0-255).
     */
    void setPixel(uint16_t index, uint8_t r, uint8_t g, uint8_t b,
                  uint8_t ww, uint8_t cw);


    /* ═══════════════════════════════════════════════════════════════════
     * BULK OPERATIONS
     * ═══════════════════════════════════════════════════════════════════ */

    void fill(uint8_t r, uint8_t g, uint8_t b);
    void fill(uint8_t r, uint8_t g, uint8_t b, uint8_t w);
    void fill(uint8_t r, uint8_t g, uint8_t b, uint8_t ww, uint8_t cw);
    void clear();


    /* ═══════════════════════════════════════════════════════════════════
     * BRIGHTNESS CONTROL
     * ═══════════════════════════════════════════════════════════════════ */

    void setBrightness(uint8_t brightness);
    uint8_t getBrightness() const;


    /* ═══════════════════════════════════════════════════════════════════
     * GAMMA CORRECTION CONTROL
     * ═══════════════════════════════════════════════════════════════════ */

    void setGammaCorrection(bool enable);
    bool isGammaCorrectionEnabled() const;


    /* ═══════════════════════════════════════════════════════════════════
     * SHOW - SEND DATA TO STRIP
     * ═══════════════════════════════════════════════════════════════════ */

    /**
     * @brief Send buffer data to the LED strip.
     *
     * Swaps double buffers and transmits via the configured backend.
     * Blocks until transmission completes.
     */
    void show();


    /* ═══════════════════════════════════════════════════════════════════
     * UTILITY METHODS
     * ═══════════════════════════════════════════════════════════════════ */

    uint16_t getNumLeds() const;
    LedType getLedType() const;
    uint8_t getBytesPerLed() const;
    TransportBackend getBackend() const;


private:

    /* ── Configuration ──────────────────────────────────────────────── */
    gpio_num_t pin;
    uint16_t numLeds;
    LedType ledType;
    ColorOrder colorOrder;
    TransportBackend backend;
    uint8_t bytesPerLed;

    /* ── State ──────────────────────────────────────────────────────── */
    bool initialized;
    uint8_t brightness;
    bool gammaEnabled;

    /* ── Double buffer ──────────────────────────────────────────────── */
    uint8_t* frontBuffer;
    uint8_t* backBuffer;
    size_t bufferSize;

    /* ── RMT backend resources ──────────────────────────────────────── */
    rmt_channel_handle_t rmtChannel;
    rmt_encoder_handle_t rmtEncoder;

    /* ── SPI backend resources ──────────────────────────────────────── */
    spi_device_handle_t spiDevice;
    uint8_t* spiBuffer;         ///< Expanded buffer: 8 SPI bytes per LED data byte
    size_t spiBufferSize;

    /* ── Gamma ──────────────────────────────────────────────────────── */
    static constexpr float GAMMA_VALUE = 2.2f;
    static const uint8_t GAMMA_TABLE[256];

    /* ── Helpers ────────────────────────────────────────────────────── */
    uint8_t applyCorrections(uint8_t value) const;
    void writeToBuffer(uint16_t index, uint8_t r, uint8_t g, uint8_t b,
                       uint8_t w = 0, uint8_t ww = 0, uint8_t cw = 0);
    static uint8_t calcBytesPerLed(LedType type);
    static ColorOrder getDefaultOrder(LedType type);

    /* ── Backend init/show ──────────────────────────────────────────── */
    bool initRmt();
    bool initSpi();
    void showRmt();
    void showSpi();
    bool createEncoder();

    /** @brief Expand pixel buffer into SPI bit-encoded buffer. */
    void encodeSpiBuffer();
};