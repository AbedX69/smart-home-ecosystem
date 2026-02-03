/**
 * @file ili9341.cpp
 * @brief ILI9341 TFT display driver implementation (ESP-IDF).
 *
 * @details
 * Implements SPI communication and drawing primitives for ILI9341.
 */

#include "ili9341.h"
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>


static const char* TAG = "ILI9341";


/*
 * =============================================================================
 * ILI9341 COMMAND DEFINITIONS
 * =============================================================================
 */

#define ILI9341_NOP         0x00
#define ILI9341_SWRESET     0x01    // Software reset
#define ILI9341_RDDID       0x04    // Read display ID
#define ILI9341_RDDST       0x09    // Read display status
#define ILI9341_SLPIN       0x10    // Sleep in
#define ILI9341_SLPOUT      0x11    // Sleep out
#define ILI9341_PTLON       0x12    // Partial mode on
#define ILI9341_NORON       0x13    // Normal display mode on
#define ILI9341_RDMODE      0x0A    // Read display power mode
#define ILI9341_RDMADCTL    0x0B    // Read display MADCTL
#define ILI9341_RDPIXFMT    0x0C    // Read display pixel format
#define ILI9341_RDIMGFMT    0x0D    // Read display image format
#define ILI9341_RDSELFDIAG  0x0F    // Read display self-diagnostic
#define ILI9341_INVOFF      0x20    // Inversion off
#define ILI9341_INVON       0x21    // Inversion on
#define ILI9341_GAMMASET    0x26    // Gamma set
#define ILI9341_DISPOFF     0x28    // Display off
#define ILI9341_DISPON      0x29    // Display on
#define ILI9341_CASET       0x2A    // Column address set
#define ILI9341_PASET       0x2B    // Page address set
#define ILI9341_RAMWR       0x2C    // Memory write
#define ILI9341_RAMRD       0x2E    // Memory read
#define ILI9341_PTLAR       0x30    // Partial area
#define ILI9341_VSCRDEF     0x33    // Vertical scrolling definition
#define ILI9341_MADCTL      0x36    // Memory access control
#define ILI9341_VSCRSADD    0x37    // Vertical scrolling start address
#define ILI9341_PIXFMT      0x3A    // Pixel format set
#define ILI9341_FRMCTR1     0xB1    // Frame rate control 1
#define ILI9341_FRMCTR2     0xB2    // Frame rate control 2
#define ILI9341_FRMCTR3     0xB3    // Frame rate control 3
#define ILI9341_INVCTR      0xB4    // Display inversion control
#define ILI9341_DFUNCTR     0xB6    // Display function control
#define ILI9341_PWCTR1      0xC0    // Power control 1
#define ILI9341_PWCTR2      0xC1    // Power control 2
#define ILI9341_PWCTR3      0xC2    // Power control 3
#define ILI9341_PWCTR4      0xC3    // Power control 4
#define ILI9341_PWCTR5      0xC4    // Power control 5
#define ILI9341_VMCTR1      0xC5    // VCOM control 1
#define ILI9341_VMCTR2      0xC7    // VCOM control 2
#define ILI9341_RDID1       0xDA    // Read ID 1
#define ILI9341_RDID2       0xDB    // Read ID 2
#define ILI9341_RDID3       0xDC    // Read ID 3
#define ILI9341_RDID4       0xDD    // Read ID 4
#define ILI9341_GMCTRP1     0xE0    // Positive gamma correction
#define ILI9341_GMCTRN1     0xE1    // Negative gamma correction


/*
 * =============================================================================
 * BUILT-IN FONT (5x7 pixels)
 * =============================================================================
 */

static const uint8_t font5x7[] = {
    // ASCII 32-126 (printable characters)
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
ILI9341::ILI9341(gpio_num_t mosiPin, gpio_num_t misoPin, gpio_num_t sckPin,
                 gpio_num_t csPin, gpio_num_t dcPin, gpio_num_t rstPin,
                 gpio_num_t ledPin, spi_host_device_t spiHost)
    : mosiPin(mosiPin),
      misoPin(misoPin),
      sckPin(sckPin),
      csPin(csPin),
      dcPin(dcPin),
      rstPin(rstPin),
      ledPin(ledPin),
      spiHost(spiHost),
      spiDevice(nullptr),
      initialized(false),
      rotation(0),
      width(ILI9341_WIDTH),
      height(ILI9341_HEIGHT)
{
}


/*
 * =============================================================================
 * DESTRUCTOR
 * =============================================================================
 */
ILI9341::~ILI9341() {
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
bool ILI9341::init() {
    ESP_LOGI(TAG, "Initializing ILI9341 (MOSI=%d, MISO=%d, SCK=%d, CS=%d, DC=%d, RST=%d, LED=%d)",
             mosiPin, misoPin, sckPin, csPin, dcPin, rstPin, ledPin);

    /*
     * -------------------------------------------------------------------------
     * STEP 1: Configure control pins (DC, RST, LED)
     * -------------------------------------------------------------------------
     */
    gpio_config_t io_conf = {};
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_DISABLE;

    // DC pin
    io_conf.pin_bit_mask = (1ULL << dcPin);
    gpio_config(&io_conf);

    // RST pin
    io_conf.pin_bit_mask = (1ULL << rstPin);
    gpio_config(&io_conf);

    // LED pin (if specified)
    if (ledPin != GPIO_NUM_NC) {
        io_conf.pin_bit_mask = (1ULL << ledPin);
        gpio_config(&io_conf);
        gpio_set_level(ledPin, 1);  // Backlight on
    }

    /*
     * -------------------------------------------------------------------------
     * STEP 2: Configure SPI bus
     * -------------------------------------------------------------------------
     */
    spi_bus_config_t busConfig = {};
    busConfig.mosi_io_num = mosiPin;
    busConfig.miso_io_num = misoPin;
    busConfig.sclk_io_num = sckPin;
    busConfig.quadwp_io_num = -1;
    busConfig.quadhd_io_num = -1;
    busConfig.max_transfer_sz = ILI9341_WIDTH * ILI9341_HEIGHT * 2 + 8;

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
    devConfig.clock_speed_hz = 20 * 1000 * 1000;    // 20 MHz
    devConfig.mode = 0;                              // SPI mode 0
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

    /*
     * -------------------------------------------------------------------------
     * STEP 5: Send initialization sequence
     * -------------------------------------------------------------------------
     */
    
    sendCommand(ILI9341_SWRESET);
    vTaskDelay(pdMS_TO_TICKS(150));
    
    sendCommand(ILI9341_SLPOUT);
    vTaskDelay(pdMS_TO_TICKS(120));
    
    // Power control A
    sendCommand(0xCB);
    sendData(0x39);
    sendData(0x2C);
    sendData(0x00);
    sendData(0x34);
    sendData(0x02);
    
    // Power control B
    sendCommand(0xCF);
    sendData(0x00);
    sendData(0xC1);
    sendData(0x30);
    
    // Driver timing control A
    sendCommand(0xE8);
    sendData(0x85);
    sendData(0x00);
    sendData(0x78);
    
    // Driver timing control B
    sendCommand(0xEA);
    sendData(0x00);
    sendData(0x00);
    
    // Power on sequence control
    sendCommand(0xED);
    sendData(0x64);
    sendData(0x03);
    sendData(0x12);
    sendData(0x81);
    
    // Pump ratio control
    sendCommand(0xF7);
    sendData(0x20);
    
    // Power control 1
    sendCommand(ILI9341_PWCTR1);
    sendData(0x23);
    
    // Power control 2
    sendCommand(ILI9341_PWCTR2);
    sendData(0x10);
    
    // VCOM control 1
    sendCommand(ILI9341_VMCTR1);
    sendData(0x3E);
    sendData(0x28);
    
    // VCOM control 2
    sendCommand(ILI9341_VMCTR2);
    sendData(0x86);
    
    // Memory access control
    sendCommand(ILI9341_MADCTL);
    sendData(0x48);
    
    // Pixel format
    sendCommand(ILI9341_PIXFMT);
    sendData(0x55);  // 16-bit RGB565
    
    // Frame rate control
    sendCommand(ILI9341_FRMCTR1);
    sendData(0x00);
    sendData(0x18);
    
    // Display function control
    sendCommand(ILI9341_DFUNCTR);
    sendData(0x08);
    sendData(0x82);
    sendData(0x27);
    
    // 3Gamma function disable
    sendCommand(0xF2);
    sendData(0x00);
    
    // Gamma curve selected
    sendCommand(ILI9341_GAMMASET);
    sendData(0x01);
    
    // Positive gamma correction
    sendCommand(ILI9341_GMCTRP1);
    sendData(0x0F);
    sendData(0x31);
    sendData(0x2B);
    sendData(0x0C);
    sendData(0x0E);
    sendData(0x08);
    sendData(0x4E);
    sendData(0xF1);
    sendData(0x37);
    sendData(0x07);
    sendData(0x10);
    sendData(0x03);
    sendData(0x0E);
    sendData(0x09);
    sendData(0x00);
    
    // Negative gamma correction
    sendCommand(ILI9341_GMCTRN1);
    sendData(0x00);
    sendData(0x0E);
    sendData(0x14);
    sendData(0x03);
    sendData(0x11);
    sendData(0x07);
    sendData(0x31);
    sendData(0xC1);
    sendData(0x48);
    sendData(0x08);
    sendData(0x0F);
    sendData(0x0C);
    sendData(0x31);
    sendData(0x36);
    sendData(0x0F);
    
    sendCommand(ILI9341_SLPOUT);
    vTaskDelay(pdMS_TO_TICKS(120));
    
    sendCommand(ILI9341_DISPON);
    vTaskDelay(pdMS_TO_TICKS(50));

    initialized = true;

    // Clear screen
    fillScreen(COLOR_BLACK);

    ESP_LOGI(TAG, "ILI9341 initialized successfully");
    return true;
}


/*
 * =============================================================================
 * LOW-LEVEL SPI FUNCTIONS
 * =============================================================================
 */

void ILI9341::hardwareReset() {
    gpio_set_level(rstPin, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(rstPin, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(rstPin, 1);
    vTaskDelay(pdMS_TO_TICKS(120));
}


void ILI9341::sendCommand(uint8_t cmd) {
    gpio_set_level(dcPin, 0);  // Command mode
    
    spi_transaction_t trans = {};
    trans.length = 8;
    trans.tx_buffer = &cmd;
    spi_device_polling_transmit(spiDevice, &trans);
}


void ILI9341::sendData(uint8_t data) {
    gpio_set_level(dcPin, 1);  // Data mode
    
    spi_transaction_t trans = {};
    trans.length = 8;
    trans.tx_buffer = &data;
    spi_device_polling_transmit(spiDevice, &trans);
}


void ILI9341::sendData(const uint8_t* data, size_t len) {
    if (len == 0) return;
    
    gpio_set_level(dcPin, 1);  // Data mode
    
    spi_transaction_t trans = {};
    trans.length = len * 8;
    trans.tx_buffer = data;
    spi_device_polling_transmit(spiDevice, &trans);
}


void ILI9341::sendData16(uint16_t data) {
    uint8_t buf[2] = {(uint8_t)(data >> 8), (uint8_t)(data & 0xFF)};
    sendData(buf, 2);
}


void ILI9341::setWindow(int16_t x0, int16_t y0, int16_t x1, int16_t y1) {
    sendCommand(ILI9341_CASET);
    sendData16(x0);
    sendData16(x1);
    
    sendCommand(ILI9341_PASET);
    sendData16(y0);
    sendData16(y1);
    
    sendCommand(ILI9341_RAMWR);
}


/*
 * =============================================================================
 * DRAWING FUNCTIONS
 * =============================================================================
 */

void ILI9341::fillScreen(uint16_t color) {
    fillRect(0, 0, width, height, color);
}


void ILI9341::drawPixel(int16_t x, int16_t y, uint16_t color) {
    if (x < 0 || x >= width || y < 0 || y >= height) return;
    
    setWindow(x, y, x, y);
    sendData16(color);
}


void ILI9341::drawHLine(int16_t x, int16_t y, int16_t w, uint16_t color) {
    if (y < 0 || y >= height || x >= width) return;
    if (x < 0) { w += x; x = 0; }
    if (x + w > width) w = width - x;
    if (w <= 0) return;
    
    setWindow(x, y, x + w - 1, y);
    
    uint8_t hi = color >> 8;
    uint8_t lo = color & 0xFF;
    
    gpio_set_level(dcPin, 1);
    
    uint8_t buf[64];
    int bufIdx = 0;
    
    for (int i = 0; i < w; i++) {
        buf[bufIdx++] = hi;
        buf[bufIdx++] = lo;
        
        if (bufIdx >= 64) {
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


void ILI9341::drawVLine(int16_t x, int16_t y, int16_t h, uint16_t color) {
    if (x < 0 || x >= width || y >= height) return;
    if (y < 0) { h += y; y = 0; }
    if (y + h > height) h = height - y;
    if (h <= 0) return;
    
    setWindow(x, y, x, y + h - 1);
    
    uint8_t hi = color >> 8;
    uint8_t lo = color & 0xFF;
    
    gpio_set_level(dcPin, 1);
    
    uint8_t buf[64];
    int bufIdx = 0;
    
    for (int i = 0; i < h; i++) {
        buf[bufIdx++] = hi;
        buf[bufIdx++] = lo;
        
        if (bufIdx >= 64) {
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


void ILI9341::drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color) {
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

    // Bresenham's line algorithm
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


void ILI9341::drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    drawHLine(x, y, w, color);
    drawHLine(x, y + h - 1, w, color);
    drawVLine(x, y, h, color);
    drawVLine(x + w - 1, y, h, color);
}


void ILI9341::fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    if (x >= width || y >= height) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > width) w = width - x;
    if (y + h > height) h = height - y;
    if (w <= 0 || h <= 0) return;
    
    setWindow(x, y, x + w - 1, y + h - 1);
    
    uint8_t hi = color >> 8;
    uint8_t lo = color & 0xFF;
    
    gpio_set_level(dcPin, 1);
    
    uint8_t buf[512];
    int totalPixels = w * h;
    int pixelsSent = 0;
    
    while (pixelsSent < totalPixels) {
        int bufIdx = 0;
        int pixelsInBuf = 0;
        
        while (bufIdx < 510 && pixelsSent + pixelsInBuf < totalPixels) {
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


void ILI9341::drawCircle(int16_t cx, int16_t cy, int16_t radius, uint16_t color) {
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


void ILI9341::fillCircle(int16_t cx, int16_t cy, int16_t radius, uint16_t color) {
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

uint8_t ILI9341::drawChar(int16_t x, int16_t y, char c, uint16_t color, uint16_t bg, uint8_t size) {
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
    
    // Spacing column
    if (size == 1) {
        for (uint8_t row = 0; row < 7; row++) {
            drawPixel(x + 5, y + row, bg);
        }
    } else {
        fillRect(x + 5 * size, y, size, 7 * size, bg);
    }
    
    return 6 * size;
}


void ILI9341::drawString(int16_t x, int16_t y, const char* str, uint16_t color, uint16_t bg, uint8_t size) {
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

void ILI9341::setBacklight(bool on) {
    if (ledPin != GPIO_NUM_NC) {
        gpio_set_level(ledPin, on ? 1 : 0);
    }
}


void ILI9341::setRotation(uint8_t r) {
    rotation = r & 3;
    
    sendCommand(ILI9341_MADCTL);
    
    switch (rotation) {
        case 0:
            sendData(0x48);
            width = ILI9341_WIDTH;
            height = ILI9341_HEIGHT;
            break;
        case 1:
            sendData(0x28);
            width = ILI9341_HEIGHT;
            height = ILI9341_WIDTH;
            break;
        case 2:
            sendData(0x88);
            width = ILI9341_WIDTH;
            height = ILI9341_HEIGHT;
            break;
        case 3:
            sendData(0xE8);
            width = ILI9341_HEIGHT;
            height = ILI9341_WIDTH;
            break;
    }
}


void ILI9341::setInverted(bool invert) {
    sendCommand(invert ? ILI9341_INVON : ILI9341_INVOFF);
}


uint16_t ILI9341::color565(uint8_t r, uint8_t g, uint8_t b) {
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}
