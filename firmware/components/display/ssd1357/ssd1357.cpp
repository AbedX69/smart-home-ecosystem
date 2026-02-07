/**
 * @file ssd1357.cpp
 * @brief SSD1357 RGB OLED display driver implementation (ESP-IDF).
 *
 * @details
 * Implements SPI communication and drawing for SSD1357 RGB OLED.
 */

#include "ssd1357.h"
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>


static const char* TAG = "SSD1357";


/*
 * =============================================================================
 * SSD1357 COMMAND DEFINITIONS
 * =============================================================================
 */

#define SSD1357_SET_COLUMN_ADDRESS      0x15
#define SSD1357_SET_ROW_ADDRESS         0x75
#define SSD1357_WRITE_RAM               0x5C
#define SSD1357_READ_RAM                0x5D
#define SSD1357_SET_REMAP               0xA0
#define SSD1357_SET_START_LINE          0xA1
#define SSD1357_SET_DISPLAY_OFFSET      0xA2
#define SSD1357_DISPLAY_ALL_OFF         0xA4
#define SSD1357_DISPLAY_ALL_ON          0xA5
#define SSD1357_NORMAL_DISPLAY          0xA6
#define SSD1357_INVERSE_DISPLAY         0xA7
#define SSD1357_FUNCTION_SELECT         0xAB
#define SSD1357_SLEEP_ON                0xAE
#define SSD1357_SLEEP_OFF               0xAF
#define SSD1357_SET_PHASE_LENGTH        0xB1
#define SSD1357_SET_CLOCK_DIV           0xB3
#define SSD1357_SET_VSL                 0xB4
#define SSD1357_SET_GPIO                0xB5
#define SSD1357_SET_PRECHARGE2          0xB6
#define SSD1357_SET_GRAY_TABLE          0xB8
#define SSD1357_SET_DEFAULT_GRAY        0xB9
#define SSD1357_SET_PRECHARGE_VOLTAGE   0xBB
#define SSD1357_SET_VCOMH               0xBE
#define SSD1357_SET_CONTRAST            0xC1
#define SSD1357_SET_MASTER_CONTRAST     0xC7
#define SSD1357_SET_MUX_RATIO           0xCA
#define SSD1357_SET_COMMAND_LOCK        0xFD


/*
 * =============================================================================
 * BUILT-IN FONT (5x7 pixels)
 * =============================================================================
 */

static const uint8_t font5x7[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, // Space
    0x00, 0x00, 0x5F, 0x00, 0x00, // !
    0x00, 0x07, 0x00, 0x07, 0x00, // "
    0x14, 0x7F, 0x14, 0x7F, 0x14, // #
    0x24, 0x2A, 0x7F, 0x2A, 0x12, // $
    0x23, 0x13, 0x08, 0x64, 0x62, // %
    0x36, 0x49, 0x55, 0x22, 0x50, // &
    0x00, 0x05, 0x03, 0x00, 0x00, // '
    0x00, 0x1C, 0x22, 0x41, 0x00, // (
    0x00, 0x41, 0x22, 0x1C, 0x00, // )
    0x08, 0x2A, 0x1C, 0x2A, 0x08, // *
    0x08, 0x08, 0x3E, 0x08, 0x08, // +
    0x00, 0x50, 0x30, 0x00, 0x00, // ,
    0x08, 0x08, 0x08, 0x08, 0x08, // -
    0x00, 0x60, 0x60, 0x00, 0x00, // .
    0x20, 0x10, 0x08, 0x04, 0x02, // /
    0x3E, 0x51, 0x49, 0x45, 0x3E, // 0
    0x00, 0x42, 0x7F, 0x40, 0x00, // 1
    0x42, 0x61, 0x51, 0x49, 0x46, // 2
    0x21, 0x41, 0x45, 0x4B, 0x31, // 3
    0x18, 0x14, 0x12, 0x7F, 0x10, // 4
    0x27, 0x45, 0x45, 0x45, 0x39, // 5
    0x3C, 0x4A, 0x49, 0x49, 0x30, // 6
    0x01, 0x71, 0x09, 0x05, 0x03, // 7
    0x36, 0x49, 0x49, 0x49, 0x36, // 8
    0x06, 0x49, 0x49, 0x29, 0x1E, // 9
    0x00, 0x36, 0x36, 0x00, 0x00, // :
    0x00, 0x56, 0x36, 0x00, 0x00, // ;
    0x00, 0x08, 0x14, 0x22, 0x41, // <
    0x14, 0x14, 0x14, 0x14, 0x14, // =
    0x41, 0x22, 0x14, 0x08, 0x00, // >
    0x02, 0x01, 0x51, 0x09, 0x06, // ?
    0x32, 0x49, 0x79, 0x41, 0x3E, // @
    0x7E, 0x11, 0x11, 0x11, 0x7E, // A
    0x7F, 0x49, 0x49, 0x49, 0x36, // B
    0x3E, 0x41, 0x41, 0x41, 0x22, // C
    0x7F, 0x41, 0x41, 0x22, 0x1C, // D
    0x7F, 0x49, 0x49, 0x49, 0x41, // E
    0x7F, 0x09, 0x09, 0x01, 0x01, // F
    0x3E, 0x41, 0x41, 0x51, 0x32, // G
    0x7F, 0x08, 0x08, 0x08, 0x7F, // H
    0x00, 0x41, 0x7F, 0x41, 0x00, // I
    0x20, 0x40, 0x41, 0x3F, 0x01, // J
    0x7F, 0x08, 0x14, 0x22, 0x41, // K
    0x7F, 0x40, 0x40, 0x40, 0x40, // L
    0x7F, 0x02, 0x04, 0x02, 0x7F, // M
    0x7F, 0x04, 0x08, 0x10, 0x7F, // N
    0x3E, 0x41, 0x41, 0x41, 0x3E, // O
    0x7F, 0x09, 0x09, 0x09, 0x06, // P
    0x3E, 0x41, 0x51, 0x21, 0x5E, // Q
    0x7F, 0x09, 0x19, 0x29, 0x46, // R
    0x46, 0x49, 0x49, 0x49, 0x31, // S
    0x01, 0x01, 0x7F, 0x01, 0x01, // T
    0x3F, 0x40, 0x40, 0x40, 0x3F, // U
    0x1F, 0x20, 0x40, 0x20, 0x1F, // V
    0x7F, 0x20, 0x18, 0x20, 0x7F, // W
    0x63, 0x14, 0x08, 0x14, 0x63, // X
    0x03, 0x04, 0x78, 0x04, 0x03, // Y
    0x61, 0x51, 0x49, 0x45, 0x43, // Z
    0x00, 0x00, 0x7F, 0x41, 0x41, // [
    0x02, 0x04, 0x08, 0x10, 0x20, // backslash
    0x41, 0x41, 0x7F, 0x00, 0x00, // ]
    0x04, 0x02, 0x01, 0x02, 0x04, // ^
    0x40, 0x40, 0x40, 0x40, 0x40, // _
    0x00, 0x01, 0x02, 0x04, 0x00, // `
    0x20, 0x54, 0x54, 0x54, 0x78, // a
    0x7F, 0x48, 0x44, 0x44, 0x38, // b
    0x38, 0x44, 0x44, 0x44, 0x20, // c
    0x38, 0x44, 0x44, 0x48, 0x7F, // d
    0x38, 0x54, 0x54, 0x54, 0x18, // e
    0x08, 0x7E, 0x09, 0x01, 0x02, // f
    0x08, 0x14, 0x54, 0x54, 0x3C, // g
    0x7F, 0x08, 0x04, 0x04, 0x78, // h
    0x00, 0x44, 0x7D, 0x40, 0x00, // i
    0x20, 0x40, 0x44, 0x3D, 0x00, // j
    0x00, 0x7F, 0x10, 0x28, 0x44, // k
    0x00, 0x41, 0x7F, 0x40, 0x00, // l
    0x7C, 0x04, 0x18, 0x04, 0x78, // m
    0x7C, 0x08, 0x04, 0x04, 0x78, // n
    0x38, 0x44, 0x44, 0x44, 0x38, // o
    0x7C, 0x14, 0x14, 0x14, 0x08, // p
    0x08, 0x14, 0x14, 0x18, 0x7C, // q
    0x7C, 0x08, 0x04, 0x04, 0x08, // r
    0x48, 0x54, 0x54, 0x54, 0x20, // s
    0x04, 0x3F, 0x44, 0x40, 0x20, // t
    0x3C, 0x40, 0x40, 0x20, 0x7C, // u
    0x1C, 0x20, 0x40, 0x20, 0x1C, // v
    0x3C, 0x40, 0x30, 0x40, 0x3C, // w
    0x44, 0x28, 0x10, 0x28, 0x44, // x
    0x0C, 0x50, 0x50, 0x50, 0x3C, // y
    0x44, 0x64, 0x54, 0x4C, 0x44, // z
    0x00, 0x08, 0x36, 0x41, 0x00, // {
    0x00, 0x00, 0x7F, 0x00, 0x00, // |
    0x00, 0x41, 0x36, 0x08, 0x00, // }
    0x08, 0x08, 0x2A, 0x1C, 0x08, // ~
};


/*
 * =============================================================================
 * CONSTRUCTOR
 * =============================================================================
 */
SSD1357::SSD1357(gpio_num_t mosiPin, gpio_num_t sckPin, gpio_num_t csPin,
                 gpio_num_t dcPin, gpio_num_t rstPin, spi_host_device_t spiHost)
    : mosiPin(mosiPin),
      sckPin(sckPin),
      csPin(csPin),
      dcPin(dcPin),
      rstPin(rstPin),
      spiHost(spiHost),
      spiDevice(nullptr),
      initialized(false)
{
}


/*
 * =============================================================================
 * DESTRUCTOR
 * =============================================================================
 */
SSD1357::~SSD1357() {
    if (initialized && spiDevice) {
        spi_bus_remove_device(spiDevice);
        spi_bus_free(spiHost);
    }
}


/*
 * =============================================================================
 * INITIALIZATION
 * =============================================================================
 */
bool SSD1357::init() {
    ESP_LOGI(TAG, "Initializing SSD1357 64x64 RGB OLED (MOSI=%d, SCK=%d, CS=%d, DC=%d, RST=%d)",
             mosiPin, sckPin, csPin, dcPin, rstPin);

    /*
     * -------------------------------------------------------------------------
     * STEP 1: Configure control pins
     * -------------------------------------------------------------------------
     */
    gpio_config_t io_conf = {};
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_DISABLE;

    io_conf.pin_bit_mask = (1ULL << dcPin);
    gpio_config(&io_conf);

    io_conf.pin_bit_mask = (1ULL << rstPin);
    gpio_config(&io_conf);

    /*
     * -------------------------------------------------------------------------
     * STEP 2: Configure SPI bus
     * -------------------------------------------------------------------------
     */
    spi_bus_config_t busConfig = {};
    busConfig.mosi_io_num = mosiPin;
    busConfig.miso_io_num = -1;
    busConfig.sclk_io_num = sckPin;
    busConfig.quadwp_io_num = -1;
    busConfig.quadhd_io_num = -1;
    busConfig.max_transfer_sz = SSD1357_WIDTH * SSD1357_HEIGHT * 2 + 8;

    esp_err_t err = spi_bus_initialize(spiHost, &busConfig, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPI bus init failed: %s", esp_err_to_name(err));
        return false;
    }

    /*
     * -------------------------------------------------------------------------
     * STEP 3: Add SPI device
     * -------------------------------------------------------------------------
     */
    spi_device_interface_config_t devConfig = {};
    devConfig.clock_speed_hz = 10 * 1000 * 1000;    // 10 MHz
    devConfig.mode = 0;
    devConfig.spics_io_num = csPin;
    devConfig.queue_size = 7;

    err = spi_bus_add_device(spiHost, &devConfig, &spiDevice);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPI device add failed: %s", esp_err_to_name(err));
        spi_bus_free(spiHost);
        return false;
    }

    /*
     * -------------------------------------------------------------------------
     * STEP 4: Hardware reset
     * -------------------------------------------------------------------------
     */
    hardwareReset();
    vTaskDelay(pdMS_TO_TICKS(500));  // <-- ADD THIS LINE

    /*
     * -------------------------------------------------------------------------
     * STEP 5: Initialization sequence
     * -------------------------------------------------------------------------
     */
    
    // Unlock commands
    sendCommand(SSD1357_SET_COMMAND_LOCK);
    sendData(0x12);
    
    // Display off
    sendCommand(SSD1357_SLEEP_ON);
    
    // Set clock divider
    sendCommand(SSD1357_SET_CLOCK_DIV);
    sendData(0xF1);
    
    // Set MUX ratio
    sendCommand(SSD1357_SET_MUX_RATIO);
    sendData(0x3F);  // 64 rows
    
    // Set display offset
    sendCommand(SSD1357_SET_DISPLAY_OFFSET);
    sendData(0x00);
    
    // Set start line
    sendCommand(SSD1357_SET_START_LINE);
    sendData(0x00);
    
    // Set remap and color depth
    sendCommand(SSD1357_SET_REMAP);
    sendData(0x72); // 65K color, enable COM split, scan from COM[N-1] to COM0
    
    // Set GPIO
    sendCommand(SSD1357_SET_GPIO);
    sendData(0x00);
    
    // Function select
    sendCommand(SSD1357_FUNCTION_SELECT);
    sendData(0x01);  // Enable internal VDD regulator
    

    // Set phase length
    sendCommand(SSD1357_SET_PHASE_LENGTH);
    sendData(0x32);
    
    // Set VCOMH voltage
    sendCommand(SSD1357_SET_VCOMH);
    sendData(0x05);
    
    // Set precharge voltage
    sendCommand(SSD1357_SET_PRECHARGE_VOLTAGE);
    sendData(0x17);
    
    // Set second precharge period
    sendCommand(SSD1357_SET_PRECHARGE2);
    sendData(0x01);
    
    // Set contrast (RGB)
    sendCommand(SSD1357_SET_CONTRAST);
    sendData(0x8A);  // R
    sendData(0x51);  // G
    sendData(0x8A);  // B
    
    // Set master contrast
    sendCommand(SSD1357_SET_MASTER_CONTRAST);
    sendData(0x0F);  // Max
    
    // Use default grayscale table
    sendCommand(SSD1357_SET_DEFAULT_GRAY);
    
    // Normal display mode
    sendCommand(SSD1357_NORMAL_DISPLAY);
    
    // Display on
    sendCommand(SSD1357_SLEEP_OFF);
    vTaskDelay(pdMS_TO_TICKS(100));

    initialized = true;

    // Clear screen
    fillScreen(COLOR_BLACK);

    ESP_LOGI(TAG, "SSD1357 initialized successfully");
    return true;
}


/*
 * =============================================================================
 * LOW-LEVEL SPI FUNCTIONS
 * =============================================================================
 */

void SSD1357::hardwareReset() {
    gpio_set_level(rstPin, 1);
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(rstPin, 0);
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(rstPin, 1);
    vTaskDelay(pdMS_TO_TICKS(200));
}


void SSD1357::sendCommand(uint8_t cmd) {
    gpio_set_level(dcPin, 0);
    
    spi_transaction_t trans = {};
    trans.length = 8;
    trans.tx_buffer = &cmd;
    spi_device_polling_transmit(spiDevice, &trans);
}


void SSD1357::sendData(uint8_t data) {
    gpio_set_level(dcPin, 1);
    
    spi_transaction_t trans = {};
    trans.length = 8;
    trans.tx_buffer = &data;
    spi_device_polling_transmit(spiDevice, &trans);
}


void SSD1357::sendData(const uint8_t* data, size_t len) {
    if (len == 0) return;
    
    gpio_set_level(dcPin, 1);
    
    spi_transaction_t trans = {};
    trans.length = len * 8;
    trans.tx_buffer = data;
    spi_device_polling_transmit(spiDevice, &trans);
}


void SSD1357::sendData16(uint16_t data) {
    uint8_t buf[2] = {(uint8_t)(data >> 8), (uint8_t)(data & 0xFF)};
    sendData(buf, 2);
}

static constexpr uint8_t X_OFF = 30; // common for AliExpress SSD1357Z 64x64
static constexpr uint8_t Y_OFF = 0;



void SSD1357::setWindow(int16_t x0, int16_t y0, int16_t x1, int16_t y1) {
    sendCommand(SSD1357_SET_COLUMN_ADDRESS);
    sendData(x0);
    sendData(x1);
    
    sendCommand(SSD1357_SET_ROW_ADDRESS);
    sendData(y0);
    sendData(y1);
    
    sendCommand(SSD1357_WRITE_RAM);/*
    sendCommand(SSD1357_SET_COLUMN_ADDRESS);
    sendData(x0 + X_OFF);
    sendData(x1 + X_OFF);

    sendCommand(SSD1357_SET_ROW_ADDRESS);
    sendData(y0 + Y_OFF);
    sendData(y1 + Y_OFF);

    sendCommand(SSD1357_WRITE_RAM);
*/


}


/*
 * =============================================================================
 * DRAWING FUNCTIONS
 * =============================================================================
 */

void SSD1357::fillScreen(uint16_t color) {
    fillRect(0, 0, SSD1357_WIDTH, SSD1357_HEIGHT, color);
}


void SSD1357::drawPixel(int16_t x, int16_t y, uint16_t color) {
    if (x < 0 || x >= SSD1357_WIDTH || y < 0 || y >= SSD1357_HEIGHT) return;
    
    setWindow(x, y, x, y);
    sendData16(color);
}


void SSD1357::drawHLine(int16_t x, int16_t y, int16_t w, uint16_t color) {
    if (y < 0 || y >= SSD1357_HEIGHT || x >= SSD1357_WIDTH) return;
    if (x < 0) { w += x; x = 0; }
    if (x + w > SSD1357_WIDTH) w = SSD1357_WIDTH - x;
    if (w <= 0) return;
    
    setWindow(x, y, x + w - 1, y);
    
    uint8_t hi = color >> 8;
    uint8_t lo = color & 0xFF;
    
    gpio_set_level(dcPin, 1);
    
    uint8_t buf[128];
    int bufIdx = 0;
    
    for (int i = 0; i < w; i++) {
        buf[bufIdx++] = hi;
        buf[bufIdx++] = lo;
        
        if (bufIdx >= 128) {
            spi_transaction_t trans = {};
            trans.length = bufIdx * 8;
            trans.tx_buffer = buf;
            spi_device_polling_transmit(spiDevice, &trans);
            bufIdx = 0;
        }
    }
    
    if (bufIdx > 0) {
        spi_transaction_t trans = {};
        trans.length = bufIdx * 8;
        trans.tx_buffer = buf;
        spi_device_polling_transmit(spiDevice, &trans);
    }
}


void SSD1357::drawVLine(int16_t x, int16_t y, int16_t h, uint16_t color) {
    if (x < 0 || x >= SSD1357_WIDTH || y >= SSD1357_HEIGHT) return;
    if (y < 0) { h += y; y = 0; }
    if (y + h > SSD1357_HEIGHT) h = SSD1357_HEIGHT - y;
    if (h <= 0) return;
    
    setWindow(x, y, x, y + h - 1);
    
    uint8_t hi = color >> 8;
    uint8_t lo = color & 0xFF;
    
    gpio_set_level(dcPin, 1);
    
    uint8_t buf[128];
    int bufIdx = 0;
    
    for (int i = 0; i < h; i++) {
        buf[bufIdx++] = hi;
        buf[bufIdx++] = lo;
        
        if (bufIdx >= 128) {
            spi_transaction_t trans = {};
            trans.length = bufIdx * 8;
            trans.tx_buffer = buf;
            spi_device_polling_transmit(spiDevice, &trans);
            bufIdx = 0;
        }
    }
    
    if (bufIdx > 0) {
        spi_transaction_t trans = {};
        trans.length = bufIdx * 8;
        trans.tx_buffer = buf;
        spi_device_polling_transmit(spiDevice, &trans);
    }
}


void SSD1357::drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color) {
    if (y0 == y1) {
        if (x0 > x1) { int16_t t = x0; x0 = x1; x1 = t; }
        drawHLine(x0, y0, x1 - x0 + 1, color);
        return;
    }
    if (x0 == x1) {
        if (y0 > y1) { int16_t t = y0; y0 = y1; y1 = t; }
        drawVLine(x0, y0, y1 - y0 + 1, color);
        return;
    }

    int16_t dx = abs(x1 - x0);
    int16_t dy = abs(y1 - y0);
    int16_t sx = (x0 < x1) ? 1 : -1;
    int16_t sy = (y0 < y1) ? 1 : -1;
    int16_t err = dx - dy;

    while (true) {
        drawPixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int16_t e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx) { err += dx; y0 += sy; }
    }
}


void SSD1357::drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    drawHLine(x, y, w, color);
    drawHLine(x, y + h - 1, w, color);
    drawVLine(x, y, h, color);
    drawVLine(x + w - 1, y, h, color);
}


void SSD1357::fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    if (x >= SSD1357_WIDTH || y >= SSD1357_HEIGHT) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > SSD1357_WIDTH) w = SSD1357_WIDTH - x;
    if (y + h > SSD1357_HEIGHT) h = SSD1357_HEIGHT - y;
    if (w <= 0 || h <= 0) return;
    
    setWindow(x, y, x + w - 1, y + h - 1);
    
    uint8_t hi = color >> 8;
    uint8_t lo = color & 0xFF;
    
    gpio_set_level(dcPin, 1);
    
    uint8_t buf[256];
    int totalPixels = w * h;
    int pixelsSent = 0;
    
    while (pixelsSent < totalPixels) {
        int bufIdx = 0;
        int pixelsInBuf = 0;
        
        while (bufIdx < 254 && pixelsSent + pixelsInBuf < totalPixels) {
            buf[bufIdx++] = hi;
            buf[bufIdx++] = lo;
            pixelsInBuf++;
        }
        
        spi_transaction_t trans = {};
        trans.length = bufIdx * 8;
        trans.tx_buffer = buf;
        spi_device_polling_transmit(spiDevice, &trans);
        
        pixelsSent += pixelsInBuf;
    }
}


void SSD1357::drawCircle(int16_t cx, int16_t cy, int16_t radius, uint16_t color) {
    int16_t x = radius;
    int16_t y = 0;
    int16_t err = 0;

    while (x >= y) {
        drawPixel(cx + x, cy + y, color);
        drawPixel(cx + y, cy + x, color);
        drawPixel(cx - y, cy + x, color);
        drawPixel(cx - x, cy + y, color);
        drawPixel(cx - x, cy - y, color);
        drawPixel(cx - y, cy - x, color);
        drawPixel(cx + y, cy - x, color);
        drawPixel(cx + x, cy - y, color);

        y++;
        if (err <= 0) err += 2 * y + 1;
        if (err > 0) { x--; err -= 2 * x + 1; }
    }
}


void SSD1357::fillCircle(int16_t cx, int16_t cy, int16_t radius, uint16_t color) {
    drawVLine(cx, cy - radius, 2 * radius + 1, color);
    
    int16_t x = radius;
    int16_t y = 0;
    int16_t err = 0;

    while (x >= y) {
        drawVLine(cx + x, cy - y, 2 * y + 1, color);
        drawVLine(cx - x, cy - y, 2 * y + 1, color);
        drawVLine(cx + y, cy - x, 2 * x + 1, color);
        drawVLine(cx - y, cy - x, 2 * x + 1, color);

        y++;
        if (err <= 0) err += 2 * y + 1;
        if (err > 0) { x--; err -= 2 * x + 1; }
    }
}


/*
 * =============================================================================
 * TEXT FUNCTIONS
 * =============================================================================
 */

uint8_t SSD1357::drawChar(int16_t x, int16_t y, char c, uint16_t color, uint16_t bg, uint8_t size) {
    if (c < 32 || c > 126) c = '?';
    
    const uint8_t* charData = &font5x7[(c - 32) * 5];
    
    for (uint8_t col = 0; col < 5; col++) {
        uint8_t colData = charData[col];
        
        for (uint8_t row = 0; row < 7; row++) {
            uint16_t pixelColor = (colData & (1 << row)) ? color : bg;
            
            if (size == 1) {
                if (pixelColor != bg || color == bg) {
                    drawPixel(x + col, y + row, pixelColor);
                }
            } else {
                fillRect(x + col * size, y + row * size, size, size, pixelColor);
            }
        }
    }
    
    if (size == 1) {
        for (uint8_t row = 0; row < 7; row++) {
            drawPixel(x + 5, y + row, bg);
        }
    } else {
        fillRect(x + 5 * size, y, size, 7 * size, bg);
    }
    
    return 6 * size;
}


void SSD1357::drawString(int16_t x, int16_t y, const char* str, uint16_t color, uint16_t bg, uint8_t size) {
    int16_t cursorX = x;
    
    while (*str) {
        if (*str == '\n') {
            y += 8 * size;
            cursorX = x;
        } else {
            cursorX += drawChar(cursorX, y, *str, color, bg, size);
        }
        str++;
    }
}


/*
 * =============================================================================
 * DISPLAY CONTROL
 * =============================================================================
 */

void SSD1357::setBrightness(uint8_t brightness) {
    sendCommand(SSD1357_SET_MASTER_CONTRAST);
    sendData(brightness >> 4);  // 0-15 range
}


uint16_t SSD1357::color565(uint8_t r, uint8_t g, uint8_t b) {
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}
