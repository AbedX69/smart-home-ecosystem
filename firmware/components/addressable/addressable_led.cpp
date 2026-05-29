/**
 * @file addressable_led.cpp
 * @brief Addressable LED strip driver implementation (ESP-IDF).
 *
 * @details
 * Implements RMT and SPI backends for WS2812B, SK6812 RGBW, and SK6812 RGBWW
 * addressable LED strips. Uses double buffering for flicker-free updates.
 *
 * SPI BACKEND — HOW IT WORKS
 * ==========================
 *
 * The single-wire NRZ protocol used by WS2812B/SK6812 requires:
 *     Bit "1": ~0.8µs HIGH, ~0.45µs LOW   (total ~1.25µs)
 *     Bit "0": ~0.4µs HIGH, ~0.85µs LOW   (total ~1.25µs)
 *
 * We encode each LED data bit as 8 SPI bits at ~6.4 MHz:
 *     SPI bit period = 1 / 6.4MHz ≈ 156ns
 *     8 SPI bits = 8 × 156ns = 1.25µs  ← matches one LED bit period
 *
 *     LED bit "1" → SPI byte 0xF8 (11111000):
 *         5 × 156ns = 780ns HIGH  (spec: 580-1000ns ✓)
 *         3 × 156ns = 468ns LOW   (spec: 220-420ns, slightly over but works)
 *
 *     LED bit "0" → SPI byte 0xC0 (11000000):
 *         2 × 156ns = 312ns HIGH  (spec: 220-380ns ✓)
 *         6 × 156ns = 937ns LOW   (spec: 580-1000ns ✓)
 *
 * So for each byte of LED color data (8 LED bits), we produce 8 SPI bytes.
 * The SPI peripheral + DMA blasts them out with zero CPU involvement.
 *
 * RESET PULSE:
 *     The strip needs >280µs of LOW to latch data.
 *     We append zeros to the SPI buffer: 280µs / 156ns ≈ 1795 bits ≈ 225 bytes.
 *     We use 256 bytes (rounded up) of 0x00 for a clean reset.
 */

#include "addressable_led.h"
#include <freertos/FreeRTOS.h>
#include <esp_log.h>
#include <cstring>
#include <cmath>
#include <esp_heap_caps.h>

static const char* TAG = "AddressableLED";


/*
 * =============================================================================
 * TIMING CONSTANTS
 * =============================================================================
 */

// RMT timing (nanoseconds)
static constexpr uint32_t WS2812_T0H_NS = 300;
static constexpr uint32_t WS2812_T0L_NS = 900;
static constexpr uint32_t WS2812_T1H_NS = 900;
static constexpr uint32_t WS2812_T1L_NS = 300;
static constexpr uint32_t WS2812_RESET_US = 280;

// RMT resolution (10MHz = 100ns per tick)
static constexpr uint32_t RMT_RESOLUTION_HZ = 10000000;

// SPI encoding constants
static constexpr uint8_t SPI_BIT_1 = 0xFC;         // 11111100 — 750ns HIGH, 250ns LOW
static constexpr uint8_t SPI_BIT_0 = 0xE0;         // 11100000 — 375ns HIGH, 625ns LOW
static constexpr uint32_t SPI_CLOCK_HZ = 8000000;    ///< 6.4 MHz → ~156ns per SPI bit
static constexpr size_t SPI_RESET_BYTES = 256;      ///< ~320µs of LOW for reset


/*
 * =============================================================================
 * GAMMA CORRECTION TABLE
 * =============================================================================
 */
const uint8_t AddressableLED::GAMMA_TABLE[256] = {
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,   0,   0,   1,   1,   1,   1,   1,   1,   1,   1,
      1,   1,   1,   1,   2,   2,   2,   2,   2,   2,   2,   2,   3,   3,   3,   3,
      3,   3,   4,   4,   4,   4,   5,   5,   5,   5,   5,   6,   6,   6,   6,   7,
      7,   7,   8,   8,   8,   9,   9,   9,  10,  10,  10,  11,  11,  11,  12,  12,
     13,  13,  13,  14,  14,  15,  15,  16,  16,  17,  17,  18,  18,  19,  19,  20,
     20,  21,  21,  22,  22,  23,  24,  24,  25,  25,  26,  27,  27,  28,  29,  29,
     30,  31,  31,  32,  33,  34,  34,  35,  36,  37,  37,  38,  39,  40,  40,  41,
     42,  43,  44,  45,  45,  46,  47,  48,  49,  50,  51,  52,  53,  54,  55,  55,
     56,  57,  58,  59,  60,  61,  62,  63,  65,  66,  67,  68,  69,  70,  71,  72,
     73,  74,  76,  77,  78,  79,  80,  82,  83,  84,  85,  87,  88,  89,  90,  92,
     93,  94,  96,  97,  99, 100, 101, 103, 104, 106, 107, 108, 110, 111, 113, 114,
    116, 117, 119, 121, 122, 124, 125, 127, 129, 130, 132, 134, 135, 137, 139, 140,
    142, 144, 146, 147, 149, 151, 153, 155, 156, 158, 160, 162, 164, 166, 168, 170,
    172, 174, 176, 178, 180, 182, 184, 186, 188, 190, 192, 194, 196, 199, 201, 203,
    205, 207, 210, 212, 214, 216, 219, 221, 223, 226, 228, 231, 233, 235, 238, 255
};


/*
 * =============================================================================
 * RMT ENCODER (unchanged from original)
 * =============================================================================
 */

typedef struct {
    rmt_encoder_t base;
    rmt_encoder_t* bytes_encoder;
    rmt_encoder_t* copy_encoder;
    rmt_symbol_word_t reset_code;
    int state;
} led_strip_encoder_t;


static size_t IRAM_ATTR led_strip_encode(rmt_encoder_t* encoder,
                                          rmt_channel_handle_t channel,
                                          const void* primary_data,
                                          size_t data_size,
                                          rmt_encode_state_t* ret_state)
{
    led_strip_encoder_t* led_encoder = __containerof(encoder, led_strip_encoder_t, base);
    rmt_encode_state_t session_state = RMT_ENCODING_RESET;
    size_t encoded_symbols = 0;

    switch (led_encoder->state) {
        case 0:
            encoded_symbols += led_encoder->bytes_encoder->encode(
                led_encoder->bytes_encoder, channel,
                primary_data, data_size, &session_state
            );
            if (session_state & RMT_ENCODING_COMPLETE) {
                led_encoder->state = 1;
            }
            if (session_state & RMT_ENCODING_MEM_FULL) {
                *ret_state = RMT_ENCODING_MEM_FULL;
                return encoded_symbols;
            }
            // Fall through to encode reset
            /* FALLTHROUGH */

        case 1:
            encoded_symbols += led_encoder->copy_encoder->encode(
                led_encoder->copy_encoder, channel,
                &led_encoder->reset_code,
                sizeof(led_encoder->reset_code),
                &session_state
            );
            if (session_state & RMT_ENCODING_COMPLETE) {
                led_encoder->state = 0;
                *ret_state = RMT_ENCODING_COMPLETE;
            }
            break;
    }

    return encoded_symbols;
}


static esp_err_t led_strip_encoder_reset(rmt_encoder_t* encoder)
{
    led_strip_encoder_t* led_encoder = __containerof(encoder, led_strip_encoder_t, base);
    rmt_encoder_reset(led_encoder->bytes_encoder);
    rmt_encoder_reset(led_encoder->copy_encoder);
    led_encoder->state = 0;
    return ESP_OK;
}


static esp_err_t led_strip_encoder_delete(rmt_encoder_t* encoder)
{
    led_strip_encoder_t* led_encoder = __containerof(encoder, led_strip_encoder_t, base);
    rmt_del_encoder(led_encoder->bytes_encoder);
    rmt_del_encoder(led_encoder->copy_encoder);
    delete led_encoder;
    return ESP_OK;
}


/*
 * =============================================================================
 * CONSTRUCTOR
 * =============================================================================
 */
AddressableLED::AddressableLED(gpio_num_t pin, uint16_t numLeds,
                               LedType type, ColorOrder order,
                               TransportBackend backend)
    : pin(pin),
      numLeds(numLeds),
      ledType(type),
      colorOrder(order),
      backend(backend),
      bytesPerLed(calcBytesPerLed(type)),
      initialized(false),
      brightness(255),
      gammaEnabled(true),
      frontBuffer(nullptr),
      backBuffer(nullptr),
      bufferSize(0),
      rmtChannel(nullptr),
      rmtEncoder(nullptr),
      spiDevice(nullptr),
      spiBuffer(nullptr),
      spiBufferSize(0)
{
    if (order == ColorOrder::GRB) {
        colorOrder = getDefaultOrder(type);
    }

    bufferSize = numLeds * bytesPerLed;

    ESP_LOGI(TAG, "Created AddressableLED: %d LEDs, %d bytes/LED, buffer=%d bytes, backend=%s",
             numLeds, bytesPerLed, bufferSize,
             backend == TransportBackend::SPI ? "SPI" : "RMT");
}


/*
 * =============================================================================
 * DESTRUCTOR
 * =============================================================================
 */
AddressableLED::~AddressableLED()
{
    // RMT cleanup
    if (rmtEncoder) {
        rmt_del_encoder(rmtEncoder);
        rmtEncoder = nullptr;
    }
    if (rmtChannel) {
        rmt_disable(rmtChannel);
        rmt_del_channel(rmtChannel);
        rmtChannel = nullptr;
    }

    // SPI cleanup
    if (spiDevice) {
        spi_bus_remove_device(spiDevice);
        spiDevice = nullptr;
    }
    // Note: we don't free the SPI bus here — other devices might share it.
    // The bus is freed when the last device is removed, or at shutdown.

    if (spiBuffer) {
        heap_caps_free(spiBuffer);
        spiBuffer = nullptr;
    }

    // Buffer cleanup
    if (frontBuffer) {
        delete[] frontBuffer;
        frontBuffer = nullptr;
    }
    if (backBuffer) {
        delete[] backBuffer;
        backBuffer = nullptr;
    }

    ESP_LOGI(TAG, "AddressableLED destroyed");
}


/*
 * =============================================================================
 * INITIALIZATION — DISPATCH TO BACKEND
 * =============================================================================
 */
bool AddressableLED::init()
{
    ESP_LOGI(TAG, "Initializing AddressableLED on GPIO %d (%s backend)",
             pin, backend == TransportBackend::SPI ? "SPI" : "RMT");

    // Allocate double buffers (shared by both backends)
    frontBuffer = new uint8_t[bufferSize];
    backBuffer = new uint8_t[bufferSize];

    if (!frontBuffer || !backBuffer) {
        ESP_LOGE(TAG, "Failed to allocate buffers (%d bytes each)", bufferSize);
        return false;
    }

    memset(frontBuffer, 0, bufferSize);
    memset(backBuffer, 0, bufferSize);
    ESP_LOGI(TAG, "Allocated double buffers: %d bytes each", bufferSize);

    // Init the selected backend
    bool ok = (backend == TransportBackend::SPI) ? initSpi() : initRmt();

    if (ok) {
        initialized = true;
        ESP_LOGI(TAG, "AddressableLED initialized successfully");
    }

    return ok;
}


/*
 * =============================================================================
 * RMT BACKEND INIT
 * =============================================================================
 */
bool AddressableLED::initRmt()
{
    rmt_tx_channel_config_t tx_config = {};
    tx_config.gpio_num = pin;
    tx_config.clk_src = RMT_CLK_SRC_DEFAULT;
    tx_config.resolution_hz = RMT_RESOLUTION_HZ;
    tx_config.mem_block_symbols = 64;
    tx_config.trans_queue_depth = 1;
    tx_config.flags.invert_out = false;
    tx_config.flags.with_dma = false;

    esp_err_t err = rmt_new_tx_channel(&tx_config, &rmtChannel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create RMT channel: %s", esp_err_to_name(err));
        return false;
    }
    ESP_LOGI(TAG, "RMT channel created");

    if (!createEncoder()) {
        ESP_LOGE(TAG, "Failed to create encoder");
        rmt_del_channel(rmtChannel);
        rmtChannel = nullptr;
        return false;
    }
    ESP_LOGI(TAG, "Encoder created");

    err = rmt_enable(rmtChannel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable RMT: %s", esp_err_to_name(err));
        return false;
    }

    return true;
}


/*
 * =============================================================================
 * SPI BACKEND INIT
 * =============================================================================
 *
 * Sets up an SPI bus with MOSI on the data pin.  CLK is not connected to
 * anything — only MOSI carries the encoded waveform.
 *
 * The SPI buffer is allocated in DMA-capable memory so the DMA controller
 * can read it directly without CPU involvement during transmission.
 *
 * BUFFER SIZING:
 *     Each LED color byte → 8 SPI bytes (one per bit)
 *     Total pixel data = bufferSize × 8
 *     Plus SPI_RESET_BYTES for the reset pulse
 *     Rounded up to 4-byte alignment for DMA
 */
bool AddressableLED::initSpi()
{
    // Calculate SPI buffer size
    spiBufferSize = ((bufferSize + bytesPerLed * 1) * 8) + SPI_RESET_BYTES;

    // Round up to 4-byte alignment for DMA
    spiBufferSize = (spiBufferSize + 3) & ~3;

    // Allocate DMA-capable memory for the SPI buffer
    spiBuffer = (uint8_t*)heap_caps_malloc(spiBufferSize, MALLOC_CAP_DMA);
    if (!spiBuffer) {
        ESP_LOGE(TAG, "Failed to allocate SPI buffer (%d bytes)", spiBufferSize);
        return false;
    }
    memset(spiBuffer, 0, spiBufferSize);
    ESP_LOGI(TAG, "Allocated SPI buffer: %d bytes (DMA-capable)", spiBufferSize);

    // Configure SPI bus — MOSI is our data line, no CLK/MISO needed externally
    spi_bus_config_t bus_cfg = {};
    bus_cfg.mosi_io_num = pin;
    bus_cfg.miso_io_num = -1;
    bus_cfg.sclk_io_num = -1;       // No clock pin exposed
    bus_cfg.quadwp_io_num = -1;
    bus_cfg.quadhd_io_num = -1;
    bus_cfg.max_transfer_sz = spiBufferSize;

    // Try SPI2_HOST first (HSPI), fall back to SPI3_HOST (VSPI)
    spi_host_device_t host = SPI2_HOST;
    esp_err_t err = spi_bus_initialize(host, &bus_cfg, SPI_DMA_CH_AUTO);

    if (err == ESP_ERR_INVALID_STATE) {
        // SPI2 already in use, try SPI3
        host = SPI3_HOST;
        err = spi_bus_initialize(host, &bus_cfg, SPI_DMA_CH_AUTO);
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init SPI bus: %s", esp_err_to_name(err));
        return false;
    }
    ESP_LOGI(TAG, "SPI bus initialized on host %d", (int)host);

    // Add SPI device
    spi_device_interface_config_t dev_cfg = {};
    dev_cfg.clock_speed_hz = SPI_CLOCK_HZ;
    dev_cfg.mode = 1;               // CPOL=0, CPHA=0
    dev_cfg.spics_io_num = -1;      // No CS — always transmitting
    dev_cfg.queue_size = 1;
    dev_cfg.flags = SPI_DEVICE_HALFDUPLEX | SPI_DEVICE_NO_DUMMY;

    err = spi_bus_add_device(host, &dev_cfg, &spiDevice);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add SPI device: %s", esp_err_to_name(err));
        spi_bus_free(host);
        return false;
    }
    ESP_LOGI(TAG, "SPI device added (clock=%d Hz)", SPI_CLOCK_HZ);

    return true;
}


/*
 * =============================================================================
 * CREATE RMT ENCODER
 * =============================================================================
 */
bool AddressableLED::createEncoder()
{
    uint32_t t0h_ticks = WS2812_T0H_NS * RMT_RESOLUTION_HZ / 1000000000;
    uint32_t t0l_ticks = WS2812_T0L_NS * RMT_RESOLUTION_HZ / 1000000000;
    uint32_t t1h_ticks = WS2812_T1H_NS * RMT_RESOLUTION_HZ / 1000000000;
    uint32_t t1l_ticks = WS2812_T1L_NS * RMT_RESOLUTION_HZ / 1000000000;

    led_strip_encoder_t* led_encoder = new led_strip_encoder_t();
    if (!led_encoder) {
        ESP_LOGE(TAG, "Failed to allocate encoder");
        return false;
    }

    led_encoder->base.encode = led_strip_encode;
    led_encoder->base.reset = led_strip_encoder_reset;
    led_encoder->base.del = led_strip_encoder_delete;
    led_encoder->state = 0;

    rmt_bytes_encoder_config_t bytes_config = {};
    bytes_config.bit0.level0 = 1;
    bytes_config.bit0.duration0 = t0h_ticks;
    bytes_config.bit0.level1 = 0;
    bytes_config.bit0.duration1 = t0l_ticks;
    bytes_config.bit1.level0 = 1;
    bytes_config.bit1.duration0 = t1h_ticks;
    bytes_config.bit1.level1 = 0;
    bytes_config.bit1.duration1 = t1l_ticks;
    bytes_config.flags.msb_first = true;

    esp_err_t err = rmt_new_bytes_encoder(&bytes_config, &led_encoder->bytes_encoder);
    if (err != ESP_OK) {
        delete led_encoder;
        return false;
    }

    rmt_copy_encoder_config_t copy_config = {};
    err = rmt_new_copy_encoder(&copy_config, &led_encoder->copy_encoder);
    if (err != ESP_OK) {
        rmt_del_encoder(led_encoder->bytes_encoder);
        delete led_encoder;
        return false;
    }

    uint32_t reset_ticks = WS2812_RESET_US * RMT_RESOLUTION_HZ / 1000000;
    led_encoder->reset_code.level0 = 0;
    led_encoder->reset_code.duration0 = reset_ticks / 2;
    led_encoder->reset_code.level1 = 0;
    led_encoder->reset_code.duration1 = reset_ticks / 2;

    rmtEncoder = &led_encoder->base;
    return true;
}


/*
 * =============================================================================
 * SHOW — DISPATCH TO BACKEND
 * =============================================================================
 */
void AddressableLED::show()
{
    if (!initialized) {
        ESP_LOGW(TAG, "show() called before init()");
        return;
    }

    // Swap double buffers
    uint8_t* temp = frontBuffer;
    frontBuffer = backBuffer;
    backBuffer = temp;

    if (backend == TransportBackend::SPI) {
        showSpi();
    } else {
        showRmt();
    }
}


/*
 * =============================================================================
 * SHOW — RMT
 * =============================================================================
 */
void AddressableLED::showRmt()
{
    rmt_transmit_config_t tx_config = {};
    tx_config.loop_count = 0;

    esp_err_t err = rmt_transmit(rmtChannel, rmtEncoder, frontBuffer, bufferSize, &tx_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "RMT transmit failed: %s", esp_err_to_name(err));
        return;
    }

    err = rmt_tx_wait_all_done(rmtChannel, pdMS_TO_TICKS(1000));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "RMT wait failed: %s", esp_err_to_name(err));
    }
}


/*
 * =============================================================================
 * SHOW — SPI
 * =============================================================================
 *
 * 1. Encode the front buffer (pixel data) into the SPI buffer (bit-expanded)
 * 2. Transmit via SPI with DMA
 * 3. Block until done
 *
 * The reset pulse is just the trailing zeros in the SPI buffer.
 */
void AddressableLED::showSpi()
{
    // Encode pixel data → SPI bit patterns
    encodeSpiBuffer();

    // Set up SPI transaction
    spi_transaction_t txn = {};
    txn.length = spiBufferSize * 8;   // Length in bits
    txn.tx_buffer = spiBuffer;

    esp_err_t err = spi_device_transmit(spiDevice, &txn);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPI transmit failed: %s", esp_err_to_name(err));
    }
}


/*
 * =============================================================================
 * SPI BUFFER ENCODING
 * =============================================================================
 *
 * Converts the front buffer (raw pixel bytes) into the SPI buffer where
 * each input bit becomes one SPI byte:
 *
 *     Input bit 1 → 0xF8 (11111000)
 *     Input bit 0 → 0xC0 (11000000)
 *
 * The trailing SPI_RESET_BYTES are left as 0x00 (written once at init).
 */
void AddressableLED::encodeSpiBuffer()
{
    // Skip 2 LEDs worth of SPI bytes (padding to absorb leading junk)
    size_t padBytes = bytesPerLed * 1 * 8;  // 1 LED × 4 bytes × 8 SPI bytes per bit
    size_t outIdx = padBytes;

    for (size_t i = 0; i < bufferSize; i++) {
        uint8_t byte = frontBuffer[i];
        for (int bit = 7; bit >= 0; bit--) {
            spiBuffer[outIdx++] = (byte & (1 << bit)) ? SPI_BIT_1 : SPI_BIT_0;
        }
    }
}


/*
 * =============================================================================
 * STATIC HELPERS
 * =============================================================================
 */
uint8_t AddressableLED::calcBytesPerLed(LedType type)
{
    switch (type) {
        case LedType::WS2812B:      return 3;
        case LedType::SK6812_RGBW:  return 4;
        case LedType::SK6812_RGBWW: return 5;
        default:                    return 3;
    }
}


ColorOrder AddressableLED::getDefaultOrder(LedType type)
{
    switch (type) {
        case LedType::WS2812B:      return ColorOrder::GRB;
        case LedType::SK6812_RGBW:  return ColorOrder::GRBW;
        case LedType::SK6812_RGBWW: return ColorOrder::GRBWW;
        default:                    return ColorOrder::GRB;
    }
}


uint8_t AddressableLED::applyCorrections(uint8_t value) const
{
    uint8_t corrected = gammaEnabled ? GAMMA_TABLE[value] : value;
    return (uint8_t)(((uint16_t)corrected * brightness) / 255);
}


/*
 * =============================================================================
 * WRITE TO BUFFER
 * =============================================================================
 */
void AddressableLED::writeToBuffer(uint16_t index, uint8_t r, uint8_t g, uint8_t b,
                                    uint8_t w, uint8_t ww, uint8_t cw)
{
    if (index >= numLeds) return;

    size_t offset = index * bytesPerLed;

    uint8_t cr  = applyCorrections(r);
    uint8_t cg  = applyCorrections(g);
    uint8_t cb  = applyCorrections(b);
    uint8_t cw1 = applyCorrections(w);
    uint8_t cww = applyCorrections(ww);
    uint8_t ccw = applyCorrections(cw);

    switch (colorOrder) {
        case ColorOrder::GRB:
            backBuffer[offset + 0] = cg;
            backBuffer[offset + 1] = cr;
            backBuffer[offset + 2] = cb;
            break;
        case ColorOrder::RGB:
            backBuffer[offset + 0] = cr;
            backBuffer[offset + 1] = cg;
            backBuffer[offset + 2] = cb;
            break;
        case ColorOrder::BGR:
            backBuffer[offset + 0] = cb;
            backBuffer[offset + 1] = cg;
            backBuffer[offset + 2] = cr;
            break;
        case ColorOrder::BRG:
            backBuffer[offset + 0] = cb;
            backBuffer[offset + 1] = cr;
            backBuffer[offset + 2] = cg;
            break;
        case ColorOrder::RBG:
            backBuffer[offset + 0] = cr;
            backBuffer[offset + 1] = cb;
            backBuffer[offset + 2] = cg;
            break;
        case ColorOrder::GBR:
            backBuffer[offset + 0] = cg;
            backBuffer[offset + 1] = cb;
            backBuffer[offset + 2] = cr;
            break;

        case ColorOrder::GRBW:
            backBuffer[offset + 0] = cg;
            backBuffer[offset + 1] = cr;
            backBuffer[offset + 2] = cb;
            backBuffer[offset + 3] = cw1;
            break;
        case ColorOrder::RGBW:
            backBuffer[offset + 0] = cr;
            backBuffer[offset + 1] = cg;
            backBuffer[offset + 2] = cb;
            backBuffer[offset + 3] = cw1;
            break;
        case ColorOrder::BGRW:
            backBuffer[offset + 0] = cb;
            backBuffer[offset + 1] = cg;
            backBuffer[offset + 2] = cr;
            backBuffer[offset + 3] = cw1;
            break;
        case ColorOrder::WGRB:
            backBuffer[offset + 0] = cw1;
            backBuffer[offset + 1] = cg;
            backBuffer[offset + 2] = cr;
            backBuffer[offset + 3] = cb;
            break;

        case ColorOrder::GRBWW:
            backBuffer[offset + 0] = cg;
            backBuffer[offset + 1] = cr;
            backBuffer[offset + 2] = cb;
            backBuffer[offset + 3] = cww;
            backBuffer[offset + 4] = ccw;
            break;
        case ColorOrder::RGBWW:
            backBuffer[offset + 0] = cr;
            backBuffer[offset + 1] = cg;
            backBuffer[offset + 2] = cb;
            backBuffer[offset + 3] = cww;
            backBuffer[offset + 4] = ccw;
            break;

        default:
            backBuffer[offset + 0] = cg;
            backBuffer[offset + 1] = cr;
            backBuffer[offset + 2] = cb;
            break;
    }
}


/*
 * =============================================================================
 * SET PIXEL — ALL OVERLOADS
 * =============================================================================
 */
void AddressableLED::setPixel(uint16_t index, uint8_t r, uint8_t g, uint8_t b)
{
    if (!initialized) { ESP_LOGW(TAG, "setPixel called before init()"); return; }
    writeToBuffer(index, r, g, b, 0, 0, 0);
}

void AddressableLED::setPixel(uint16_t index, uint8_t r, uint8_t g, uint8_t b, uint8_t w)
{
    if (!initialized) { ESP_LOGW(TAG, "setPixel called before init()"); return; }
    if (ledType == LedType::WS2812B) {
        ESP_LOGW(TAG, "setPixel(RGBW) called on WS2812B strip - W ignored");
        writeToBuffer(index, r, g, b, 0, 0, 0);
        return;
    }
    writeToBuffer(index, r, g, b, w, 0, 0);
}

void AddressableLED::setPixel(uint16_t index, uint8_t r, uint8_t g, uint8_t b,
                               uint8_t ww, uint8_t cw)
{
    if (!initialized) { ESP_LOGW(TAG, "setPixel called before init()"); return; }
    if (ledType == LedType::WS2812B) {
        ESP_LOGW(TAG, "setPixel(RGBWW) called on WS2812B - WW/CW ignored");
        writeToBuffer(index, r, g, b, 0, 0, 0);
        return;
    }
    if (ledType == LedType::SK6812_RGBW) {
        ESP_LOGW(TAG, "setPixel(RGBWW) called on RGBW strip - CW ignored, WW used as W");
        writeToBuffer(index, r, g, b, ww, 0, 0);
        return;
    }
    writeToBuffer(index, r, g, b, 0, ww, cw);
}


/*
 * =============================================================================
 * FILL / CLEAR
 * =============================================================================
 */
void AddressableLED::fill(uint8_t r, uint8_t g, uint8_t b)
{
    for (uint16_t i = 0; i < numLeds; i++) setPixel(i, r, g, b);
}

void AddressableLED::fill(uint8_t r, uint8_t g, uint8_t b, uint8_t w)
{
    for (uint16_t i = 0; i < numLeds; i++) setPixel(i, r, g, b, w);
}

void AddressableLED::fill(uint8_t r, uint8_t g, uint8_t b, uint8_t ww, uint8_t cw)
{
    for (uint16_t i = 0; i < numLeds; i++) setPixel(i, r, g, b, ww, cw);
}

void AddressableLED::clear()
{
    if (!initialized) return;
    memset(backBuffer, 0, bufferSize);
}


/*
 * =============================================================================
 * BRIGHTNESS / GAMMA
 * =============================================================================
 */
void AddressableLED::setBrightness(uint8_t newBrightness) { brightness = newBrightness; }
uint8_t AddressableLED::getBrightness() const { return brightness; }
void AddressableLED::setGammaCorrection(bool enable) { gammaEnabled = enable; }
bool AddressableLED::isGammaCorrectionEnabled() const { return gammaEnabled; }


/*
 * =============================================================================
 * UTILITY
 * =============================================================================
 */
uint16_t AddressableLED::getNumLeds() const { return numLeds; }
LedType AddressableLED::getLedType() const { return ledType; }
uint8_t AddressableLED::getBytesPerLed() const { return bytesPerLed; }
TransportBackend AddressableLED::getBackend() const { return backend; }