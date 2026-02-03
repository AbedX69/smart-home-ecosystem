/**
 * @file st7789.cpp
 * @brief ST7789 TFT display driver implementation (ESP-IDF).
 *
 * @details
 * Implements SPI communication and drawing primitives for ST7789.
 */

#include "st7789.h"
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>


static const char* TAG = "ST7789";


/*
 * =============================================================================
 * ST7789 COMMAND DEFINITIONS
 * =============================================================================
 */

#define ST7789_NOP          0x00
#define ST7789_SWRESET      0x01    // Software reset
#define ST7789_SLPIN        0x10    // Sleep in
#define ST7789_SLPOUT       0x11    // Sleep out
#define ST7789_PTLON        0x12    // Partial mode on
#define ST7789_NORON        0x13    // Normal display mode on
#define ST7789_INVOFF       0x20    // Inversion off
#define ST7789_INVON        0x21    // Inversion on
#define ST7789_DISPOFF      0x28    // Display off
#define ST7789_DISPON       0x29    // Display on
#define ST7789_CASET        0x2A    // Column address set
#define ST7789_RASET        0x2B    // Row address set
#define ST7789_RAMWR        0x2C    // Memory write
#define ST7789_RAMRD        0x2E    // Memory read
#define ST7789_PTLAR        0x30    // Partial area
#define ST7789_VSCRDEF      0x33    // Vertical scrolling definition
#define ST7789_MADCTL       0x36    // Memory access control
#define ST7789_VSCRSADD     0x37    // Vertical scrolling start address
#define ST7789_COLMOD       0x3A    // Interface pixel format
#define ST7789_FRMCTR1      0xB1    // Frame rate control 1
#define ST7789_FRMCTR2      0xB2    // Frame rate control 2
#define ST7789_FRMCTR3      0xB3    // Frame rate control 3
#define ST7789_INVCTR       0xB4    // Display inversion control
#define ST7789_DISSET5      0xB6    // Display function set 5
#define ST7789_PWCTR1       0xC0    // Power control 1
#define ST7789_PWCTR2       0xC1    // Power control 2
#define ST7789_PWCTR3       0xC2    // Power control 3
#define ST7789_PWCTR4       0xC3    // Power control 4
#define ST7789_PWCTR5       0xC4    // Power control 5
#define ST7789_VMCTR1       0xC5    // VCOM control 1
#define ST7789_GMCTRP1      0xE0    // Positive gamma correction
#define ST7789_GMCTRN1      0xE1    // Negative gamma correction


/*
 * =============================================================================
 * BUILT-IN FONT (5x7 pixels)
 * =============================================================================
 */

static const uint8_t font5x7[] = {
    // ASCII 32-126 (printable characters)
    // Space (32)
    0x00, 0x00, 0x00, 0x00, 0x00,
    // ! (33)
    0x00, 0x00, 0x5F, 0x00, 0x00,
    // " (34)
    0x00, 0x07, 0x00, 0x07, 0x00,
    // # (35)
    0x14, 0x7F, 0x14, 0x7F, 0x14,
    // $ (36)
    0x24, 0x2A, 0x7F, 0x2A, 0x12,
    // % (37)
    0x23, 0x13, 0x08, 0x64, 0x62,
    // & (38)
    0x36, 0x49, 0x55, 0x22, 0x50,
    // ' (39)
    0x00, 0x05, 0x03, 0x00, 0x00,
    // ( (40)
    0x00, 0x1C, 0x22, 0x41, 0x00,
    // ) (41)
    0x00, 0x41, 0x22, 0x1C, 0x00,
    // * (42)
    0x08, 0x2A, 0x1C, 0x2A, 0x08,
    // + (43)
    0x08, 0x08, 0x3E, 0x08, 0x08,
    // , (44)
    0x00, 0x50, 0x30, 0x00, 0x00,
    // - (45)
    0x08, 0x08, 0x08, 0x08, 0x08,
    // . (46)
    0x00, 0x60, 0x60, 0x00, 0x00,
    // / (47)
    0x20, 0x10, 0x08, 0x04, 0x02,
    // 0 (48)
    0x3E, 0x51, 0x49, 0x45, 0x3E,
    // 1 (49)
    0x00, 0x42, 0x7F, 0x40, 0x00,
    // 2 (50)
    0x42, 0x61, 0x51, 0x49, 0x46,
    // 3 (51)
    0x21, 0x41, 0x45, 0x4B, 0x31,
    // 4 (52)
    0x18, 0x14, 0x12, 0x7F, 0x10,
    // 5 (53)
    0x27, 0x45, 0x45, 0x45, 0x39,
    // 6 (54)
    0x3C, 0x4A, 0x49, 0x49, 0x30,
    // 7 (55)
    0x01, 0x71, 0x09, 0x05, 0x03,
    // 8 (56)
    0x36, 0x49, 0x49, 0x49, 0x36,
    // 9 (57)
    0x06, 0x49, 0x49, 0x29, 0x1E,
    // : (58)
    0x00, 0x36, 0x36, 0x00, 0x00,
    // ; (59)
    0x00, 0x56, 0x36, 0x00, 0x00,
    // < (60)
    0x00, 0x08, 0x14, 0x22, 0x41,
    // = (61)
    0x14, 0x14, 0x14, 0x14, 0x14,
    // > (62)
    0x41, 0x22, 0x14, 0x08, 0x00,
    // ? (63)
    0x02, 0x01, 0x51, 0x09, 0x06,
    // @ (64)
    0x32, 0x49, 0x79, 0x41, 0x3E,
    // A (65)
    0x7E, 0x11, 0x11, 0x11, 0x7E,
    // B (66)
    0x7F, 0x49, 0x49, 0x49, 0x36,
    // C (67)
    0x3E, 0x41, 0x41, 0x41, 0x22,
    // D (68)
    0x7F, 0x41, 0x41, 0x22, 0x1C,
    // E (69)
    0x7F, 0x49, 0x49, 0x49, 0x41,
    // F (70)
    0x7F, 0x09, 0x09, 0x01, 0x01,
    // G (71)
    0x3E, 0x41, 0x41, 0x51, 0x32,
    // H (72)
    0x7F, 0x08, 0x08, 0x08, 0x7F,
    // I (73)
    0x00, 0x41, 0x7F, 0x41, 0x00,
    // J (74)
    0x20, 0x40, 0x41, 0x3F, 0x01,
    // K (75)
    0x7F, 0x08, 0x14, 0x22, 0x41,
    // L (76)
    0x7F, 0x40, 0x40, 0x40, 0x40,
    // M (77)
    0x7F, 0x02, 0x04, 0x02, 0x7F,
    // N (78)
    0x7F, 0x04, 0x08, 0x10, 0x7F,
    // O (79)
    0x3E, 0x41, 0x41, 0x41, 0x3E,
    // P (80)
    0x7F, 0x09, 0x09, 0x09, 0x06,
    // Q (81)
    0x3E, 0x41, 0x51, 0x21, 0x5E,
    // R (82)
    0x7F, 0x09, 0x19, 0x29, 0x46,
    // S (83)
    0x46, 0x49, 0x49, 0x49, 0x31,
    // T (84)
    0x01, 0x01, 0x7F, 0x01, 0x01,
    // U (85)
    0x3F, 0x40, 0x40, 0x40, 0x3F,
    // V (86)
    0x1F, 0x20, 0x40, 0x20, 0x1F,
    // W (87)
    0x7F, 0x20, 0x18, 0x20, 0x7F,
    // X (88)
    0x63, 0x14, 0x08, 0x14, 0x63,
    // Y (89)
    0x03, 0x04, 0x78, 0x04, 0x03,
    // Z (90)
    0x61, 0x51, 0x49, 0x45, 0x43,
    // [ (91)
    0x00, 0x00, 0x7F, 0x41, 0x41,
    // \ (92)
    0x02, 0x04, 0x08, 0x10, 0x20,
    // ] (93)
    0x41, 0x41, 0x7F, 0x00, 0x00,
    // ^ (94)
    0x04, 0x02, 0x01, 0x02, 0x04,
    // _ (95)
    0x40, 0x40, 0x40, 0x40, 0x40,
    // ` (96)
    0x00, 0x01, 0x02, 0x04, 0x00,
    // a (97)
    0x20, 0x54, 0x54, 0x54, 0x78,
    // b (98)
    0x7F, 0x48, 0x44, 0x44, 0x38,
    // c (99)
    0x38, 0x44, 0x44, 0x44, 0x20,
    // d (100)
    0x38, 0x44, 0x44, 0x48, 0x7F,
    // e (101)
    0x38, 0x54, 0x54, 0x54, 0x18,
    // f (102)
    0x08, 0x7E, 0x09, 0x01, 0x02,
    // g (103)
    0x08, 0x14, 0x54, 0x54, 0x3C,
    // h (104)
    0x7F, 0x08, 0x04, 0x04, 0x78,
    // i (105)
    0x00, 0x44, 0x7D, 0x40, 0x00,
    // j (106)
    0x20, 0x40, 0x44, 0x3D, 0x00,
    // k (107)
    0x00, 0x7F, 0x10, 0x28, 0x44,
    // l (108)
    0x00, 0x41, 0x7F, 0x40, 0x00,
    // m (109)
    0x7C, 0x04, 0x18, 0x04, 0x78,
    // n (110)
    0x7C, 0x08, 0x04, 0x04, 0x78,
    // o (111)
    0x38, 0x44, 0x44, 0x44, 0x38,
    // p (112)
    0x7C, 0x14, 0x14, 0x14, 0x08,
    // q (113)
    0x08, 0x14, 0x14, 0x18, 0x7C,
    // r (114)
    0x7C, 0x08, 0x04, 0x04, 0x08,
    // s (115)
    0x48, 0x54, 0x54, 0x54, 0x20,
    // t (116)
    0x04, 0x3F, 0x44, 0x40, 0x20,
    // u (117)
    0x3C, 0x40, 0x40, 0x20, 0x7C,
    // v (118)
    0x1C, 0x20, 0x40, 0x20, 0x1C,
    // w (119)
    0x3C, 0x40, 0x30, 0x40, 0x3C,
    // x (120)
    0x44, 0x28, 0x10, 0x28, 0x44,
    // y (121)
    0x0C, 0x50, 0x50, 0x50, 0x3C,
    // z (122)
    0x44, 0x64, 0x54, 0x4C, 0x44,
    // { (123)
    0x00, 0x08, 0x36, 0x41, 0x00,
    // | (124)
    0x00, 0x00, 0x7F, 0x00, 0x00,
    // } (125)
    0x00, 0x41, 0x36, 0x08, 0x00,
    // ~ (126)
    0x08, 0x08, 0x2A, 0x1C, 0x08,
};


/*
 * =============================================================================
 * CONSTRUCTOR
 * =============================================================================
 */
ST7789::ST7789(uint16_t width, uint16_t height,
               gpio_num_t mosiPin, gpio_num_t sckPin, gpio_num_t csPin,
               gpio_num_t dcPin, gpio_num_t rstPin, gpio_num_t blkPin,
               spi_host_device_t spiHost)
    : dispWidth(width),
      dispHeight(height),
      xOffset(0),
      yOffset(0),
      mosiPin(mosiPin),
      sckPin(sckPin),
      csPin(csPin),
      dcPin(dcPin),
      rstPin(rstPin),
      blkPin(blkPin),
      spiHost(spiHost),
      spiDevice(nullptr),
      initialized(false),
      rotation(0)
{
    this->width = width;
    this->height = height;
}


/*
 * =============================================================================
 * DESTRUCTOR
 * =============================================================================
 */
ST7789::~ST7789() {
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
bool ST7789::init() {
    ESP_LOGI(TAG, "Initializing ST7789 %dx%d (MOSI=%d, SCK=%d, CS=%d, DC=%d, RST=%d, BLK=%d)",
             dispWidth, dispHeight, mosiPin, sckPin, csPin, dcPin, rstPin, blkPin);

    /*
     * -------------------------------------------------------------------------
     * STEP 1: Configure control pins (DC, RST, BLK)
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

    // BLK pin (if specified)
    if (blkPin != GPIO_NUM_NC) {
        io_conf.pin_bit_mask = (1ULL << blkPin);
        gpio_config(&io_conf);
        gpio_set_level(blkPin, 1);  // Backlight on
    }

    /*
     * -------------------------------------------------------------------------
     * STEP 2: Configure SPI bus
     * -------------------------------------------------------------------------
     */
    spi_bus_config_t busConfig = {};
    busConfig.mosi_io_num = mosiPin;
    busConfig.miso_io_num = -1;         // Not used
    busConfig.sclk_io_num = sckPin;
    busConfig.quadwp_io_num = -1;
    busConfig.quadhd_io_num = -1;
    busConfig.max_transfer_sz = dispWidth * dispHeight * 2 + 8;

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
    devConfig.clock_speed_hz = 20 * 1000 * 1000;    // 20 MHz (safe for all boards)
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
    
    sendCommand(ST7789_SWRESET);    // Software reset
    vTaskDelay(pdMS_TO_TICKS(150));
    
    sendCommand(ST7789_SLPOUT);     // Sleep out
    vTaskDelay(pdMS_TO_TICKS(120));
    
    sendCommand(ST7789_COLMOD);     // Pixel format
    sendData(0x55);                 // 16-bit RGB565
    vTaskDelay(pdMS_TO_TICKS(10));
    
    sendCommand(ST7789_MADCTL);     // Memory access control
    sendData(0x00);                 // Default orientation
    
    sendCommand(ST7789_INVON);      // Inversion on (looks better on most panels)
    vTaskDelay(pdMS_TO_TICKS(10));
    
    sendCommand(ST7789_NORON);      // Normal display mode on
    vTaskDelay(pdMS_TO_TICKS(10));
    
    sendCommand(ST7789_DISPON);     // Display on
    vTaskDelay(pdMS_TO_TICKS(120));

    initialized = true;

    // Set default offset for 240x280 display
    if (dispWidth == 240 && dispHeight == 280) {
        setOffset(0, 20);
    }

    // Clear screen
    fillScreen(COLOR_BLACK);

    ESP_LOGI(TAG, "ST7789 initialized successfully");
    return true;
}


/*
 * =============================================================================
 * LOW-LEVEL SPI FUNCTIONS
 * =============================================================================
 */

void ST7789::hardwareReset() {
    gpio_set_level(rstPin, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(rstPin, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(rstPin, 1);
    vTaskDelay(pdMS_TO_TICKS(120));
}


void ST7789::sendCommand(uint8_t cmd) {
    gpio_set_level(dcPin, 0);  // Command mode
    
    spi_transaction_t trans = {};
    trans.length = 8;
    trans.tx_buffer = &cmd;
    spi_device_polling_transmit(spiDevice, &trans);
}


void ST7789::sendData(uint8_t data) {
    gpio_set_level(dcPin, 1);  // Data mode
    
    spi_transaction_t trans = {};
    trans.length = 8;
    trans.tx_buffer = &data;
    spi_device_polling_transmit(spiDevice, &trans);
}


void ST7789::sendData(const uint8_t* data, size_t len) {
    if (len == 0) return;
    
    gpio_set_level(dcPin, 1);  // Data mode
    
    spi_transaction_t trans = {};
    trans.length = len * 8;
    trans.tx_buffer = data;
    spi_device_polling_transmit(spiDevice, &trans);
}


void ST7789::sendData16(uint16_t data) {
    uint8_t buf[2] = {(uint8_t)(data >> 8), (uint8_t)(data & 0xFF)};
    sendData(buf, 2);
}


void ST7789::setOffset(int16_t x, int16_t y) {
    xOffset = x;
    yOffset = y;
    ESP_LOGI(TAG, "Display offset set to (%d, %d)", x, y);
}


void ST7789::setWindow(int16_t x0, int16_t y0, int16_t x1, int16_t y1) {
    // Apply offset
    x0 += xOffset;
    x1 += xOffset;
    y0 += yOffset;
    y1 += yOffset;

    sendCommand(ST7789_CASET);  // Column address
    sendData16(x0);
    sendData16(x1);
    
    sendCommand(ST7789_RASET);  // Row address
    sendData16(y0);
    sendData16(y1);
    
    sendCommand(ST7789_RAMWR);  // Memory write
}


/*
 * =============================================================================
 * DRAWING FUNCTIONS
 * =============================================================================
 */

void ST7789::fillScreen(uint16_t color) {
    fillRect(0, 0, width, height, color);
}


void ST7789::drawPixel(int16_t x, int16_t y, uint16_t color) {
    if (x < 0 || x >= width || y < 0 || y >= height) return;
    
    setWindow(x, y, x, y);
    sendData16(color);
}


void ST7789::drawHLine(int16_t x, int16_t y, int16_t w, uint16_t color) {
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


void ST7789::drawVLine(int16_t x, int16_t y, int16_t h, uint16_t color) {
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


void ST7789::drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color) {
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


void ST7789::drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    drawHLine(x, y, w, color);
    drawHLine(x, y + h - 1, w, color);
    drawVLine(x, y, h, color);
    drawVLine(x + w - 1, y, h, color);
}


void ST7789::fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
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


void ST7789::drawCircle(int16_t cx, int16_t cy, int16_t radius, uint16_t color) {
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


void ST7789::fillCircle(int16_t cx, int16_t cy, int16_t radius, uint16_t color) {
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

uint8_t ST7789::drawChar(int16_t x, int16_t y, char c, uint16_t color, uint16_t bg, uint8_t size) {
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


void ST7789::drawString(int16_t x, int16_t y, const char* str, uint16_t color, uint16_t bg, uint8_t size) {
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

void ST7789::setBacklight(bool on) {
    if (blkPin != GPIO_NUM_NC) {
        gpio_set_level(blkPin, on ? 1 : 0);
    }
}


void ST7789::setRotation(uint8_t r) {
    rotation = r & 3;
    
    sendCommand(ST7789_MADCTL);
    
    switch (rotation) {
        case 0:
            sendData(0x00);
            width = dispWidth;
            height = dispHeight;
            break;
        case 1:
            sendData(0x60);
            width = dispHeight;
            height = dispWidth;
            break;
        case 2:
            sendData(0xC0);
            width = dispWidth;
            height = dispHeight;
            break;
        case 3:
            sendData(0xA0);
            width = dispHeight;
            height = dispWidth;
            break;
    }
}


void ST7789::setInverted(bool invert) {
    sendCommand(invert ? ST7789_INVON : ST7789_INVOFF);
}


uint16_t ST7789::color565(uint8_t r, uint8_t g, uint8_t b) {
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}
