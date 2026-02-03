/**
 * @file epaper.cpp
 * @brief E-Paper display driver implementation (ESP-IDF).
 *
 * @details
 * Implements SPI communication and drawing for e-paper displays.
 * Based on SSD1680 / IL3897 controller (common in 2.13" modules).
 */

#include "epaper.h"
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdlib.h>


static const char* TAG = "EPAPER";


/*
 * =============================================================================
 * SSD1680 COMMAND DEFINITIONS
 * =============================================================================
 */

#define CMD_DRIVER_OUTPUT_CONTROL       0x01
#define CMD_GATE_DRIVING_VOLTAGE        0x03
#define CMD_SOURCE_DRIVING_VOLTAGE      0x04
#define CMD_DEEP_SLEEP_MODE             0x10
#define CMD_DATA_ENTRY_MODE             0x11
#define CMD_SW_RESET                    0x12
#define CMD_TEMP_SENSOR_CONTROL         0x18
#define CMD_MASTER_ACTIVATION           0x20
#define CMD_DISPLAY_UPDATE_CONTROL_1    0x21
#define CMD_DISPLAY_UPDATE_CONTROL_2    0x22
#define CMD_WRITE_RAM_BW                0x24
#define CMD_WRITE_RAM_RED               0x26
#define CMD_READ_RAM                    0x27
#define CMD_VCOM_SENSE                  0x28
#define CMD_VCOM_SENSE_DURATION         0x29
#define CMD_PROGRAM_VCOM_OTP            0x2A
#define CMD_WRITE_VCOM_REGISTER         0x2C
#define CMD_OTP_READ_REGISTER           0x2D
#define CMD_STATUS_BIT_READ             0x2F
#define CMD_WRITE_LUT_REGISTER          0x32
#define CMD_SET_DUMMY_LINE_PERIOD       0x3A
#define CMD_SET_GATE_TIME               0x3B
#define CMD_BORDER_WAVEFORM_CONTROL     0x3C
#define CMD_SET_RAM_X_START_END         0x44
#define CMD_SET_RAM_Y_START_END         0x45
#define CMD_SET_RAM_X_ADDRESS           0x4E
#define CMD_SET_RAM_Y_ADDRESS           0x4F


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
EPaper::EPaper(gpio_num_t mosiPin, gpio_num_t sckPin, gpio_num_t csPin,
               gpio_num_t dcPin, gpio_num_t rstPin, gpio_num_t busyPin,
               spi_host_device_t spiHost)
    : mosiPin(mosiPin),
      sckPin(sckPin),
      csPin(csPin),
      dcPin(dcPin),
      rstPin(rstPin),
      busyPin(busyPin),
      spiHost(spiHost),
      spiDevice(nullptr),
      initialized(false),
      bufferBW(nullptr),
      bufferRed(nullptr),
      bufferSize(0),
      rotation(0),
      width(EPAPER_WIDTH),
      height(EPAPER_HEIGHT)
{
}


/*
 * =============================================================================
 * DESTRUCTOR
 * =============================================================================
 */
EPaper::~EPaper() {
    if (bufferBW) free(bufferBW);
    if (bufferRed) free(bufferRed);
    
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
bool EPaper::init() {
    ESP_LOGI(TAG, "Initializing E-Paper (MOSI=%d, SCK=%d, CS=%d, DC=%d, RST=%d, BUSY=%d)",
             mosiPin, sckPin, csPin, dcPin, rstPin, busyPin);

    /*
     * -------------------------------------------------------------------------
     * STEP 1: Allocate frame buffers
     * -------------------------------------------------------------------------
     */
    // Buffer size: (122 * 250) / 8 = 3813 bytes (rounded up to 4000)
    bufferSize = ((EPAPER_WIDTH + 7) / 8) * EPAPER_HEIGHT;
    
    bufferBW = (uint8_t*)malloc(bufferSize);
    bufferRed = (uint8_t*)malloc(bufferSize);
    
    if (!bufferBW || !bufferRed) {
        ESP_LOGE(TAG, "Failed to allocate frame buffers");
        return false;
    }
    
    // Initialize to white
    memset(bufferBW, 0xFF, bufferSize);   // 0xFF = white for BW
    memset(bufferRed, 0x00, bufferSize);  // 0x00 = no red

    /*
     * -------------------------------------------------------------------------
     * STEP 2: Configure control pins
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

    // BUSY pin (input!)
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (1ULL << busyPin);
    gpio_config(&io_conf);

    /*
     * -------------------------------------------------------------------------
     * STEP 3: Configure SPI bus
     * -------------------------------------------------------------------------
     */
    spi_bus_config_t busConfig = {};
    busConfig.mosi_io_num = mosiPin;
    busConfig.miso_io_num = -1;
    busConfig.sclk_io_num = sckPin;
    busConfig.quadwp_io_num = -1;
    busConfig.quadhd_io_num = -1;
    busConfig.max_transfer_sz = bufferSize + 100;

    esp_err_t err = spi_bus_initialize(spiHost, &busConfig, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPI bus init failed: %s", esp_err_to_name(err));
        return false;
    }

    /*
     * -------------------------------------------------------------------------
     * STEP 4: Add SPI device
     * -------------------------------------------------------------------------
     */
    spi_device_interface_config_t devConfig = {};
    devConfig.clock_speed_hz = 4 * 1000 * 1000;     // 4 MHz (e-paper is slow)
    devConfig.mode = 0;
    devConfig.spics_io_num = csPin;
    devConfig.queue_size = 3;

    err = spi_bus_add_device(spiHost, &devConfig, &spiDevice);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPI device add failed: %s", esp_err_to_name(err));
        spi_bus_free(spiHost);
        return false;
    }

    /*
     * -------------------------------------------------------------------------
     * STEP 5: Hardware reset
     * -------------------------------------------------------------------------
     */
    hardwareReset();

    /*
     * -------------------------------------------------------------------------
     * STEP 6: Initialize display
     * -------------------------------------------------------------------------
     */
    waitBusy();
    
    sendCommand(CMD_SW_RESET);
    waitBusy();
    
    // Driver output control
    sendCommand(CMD_DRIVER_OUTPUT_CONTROL);
    sendData((EPAPER_HEIGHT - 1) & 0xFF);
    sendData(((EPAPER_HEIGHT - 1) >> 8) & 0xFF);
    sendData(0x00);
    
    // Data entry mode: Y decrement, X increment
    sendCommand(CMD_DATA_ENTRY_MODE);
    sendData(0x03);
    
    // Set RAM X address
    sendCommand(CMD_SET_RAM_X_START_END);
    sendData(0x00);
    sendData((EPAPER_WIDTH - 1) / 8);
    
    // Set RAM Y address
    sendCommand(CMD_SET_RAM_Y_START_END);
    sendData(0x00);
    sendData(0x00);
    sendData((EPAPER_HEIGHT - 1) & 0xFF);
    sendData(((EPAPER_HEIGHT - 1) >> 8) & 0xFF);
    
    // Border waveform
    sendCommand(CMD_BORDER_WAVEFORM_CONTROL);
    sendData(0x05);
    
    // Temperature sensor
    sendCommand(CMD_TEMP_SENSOR_CONTROL);
    sendData(0x80);  // Internal sensor
    
    // Display update control
    sendCommand(CMD_DISPLAY_UPDATE_CONTROL_2);
    sendData(0xB1);  // Load temperature and waveform
    
    sendCommand(CMD_MASTER_ACTIVATION);
    waitBusy();

    initialized = true;
    ESP_LOGI(TAG, "E-Paper initialized successfully (buffer: %d bytes)", bufferSize);
    return true;
}


/*
 * =============================================================================
 * LOW-LEVEL SPI FUNCTIONS
 * =============================================================================
 */

void EPaper::hardwareReset() {
    gpio_set_level(rstPin, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(rstPin, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(rstPin, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
}


void EPaper::waitBusy() {
    ESP_LOGD(TAG, "Waiting for BUSY...");
    while (gpio_get_level(busyPin) == 1) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    ESP_LOGD(TAG, "BUSY released");
}


void EPaper::sendCommand(uint8_t cmd) {
    gpio_set_level(dcPin, 0);  // Command mode
    
    spi_transaction_t trans = {};
    trans.length = 8;
    trans.tx_buffer = &cmd;
    spi_device_polling_transmit(spiDevice, &trans);
}


void EPaper::sendData(uint8_t data) {
    gpio_set_level(dcPin, 1);  // Data mode
    
    spi_transaction_t trans = {};
    trans.length = 8;
    trans.tx_buffer = &data;
    spi_device_polling_transmit(spiDevice, &trans);
}


void EPaper::sendData(const uint8_t* data, size_t len) {
    gpio_set_level(dcPin, 1);  // Data mode
    
    spi_transaction_t trans = {};
    trans.length = len * 8;
    trans.tx_buffer = data;
    spi_device_polling_transmit(spiDevice, &trans);
}


/*
 * =============================================================================
 * DISPLAY UPDATE
 * =============================================================================
 */

void EPaper::clear(uint8_t color) {
    uint8_t bwValue = (color == EPAPER_BLACK) ? 0x00 : 0xFF;
    uint8_t redValue = (color == EPAPER_RED) ? 0xFF : 0x00;
    
    memset(bufferBW, bwValue, bufferSize);
    memset(bufferRed, redValue, bufferSize);
}


void EPaper::update() {
    ESP_LOGI(TAG, "Updating display (this takes ~2 seconds)...");
    
    // Set RAM position to start
    sendCommand(CMD_SET_RAM_X_ADDRESS);
    sendData(0x00);
    
    sendCommand(CMD_SET_RAM_Y_ADDRESS);
    sendData(0x00);
    sendData(0x00);
    
    // Write B/W data
    sendCommand(CMD_WRITE_RAM_BW);
    sendData(bufferBW, bufferSize);
    
    // Write Red data
    sendCommand(CMD_WRITE_RAM_RED);
    sendData(bufferRed, bufferSize);
    
    // Trigger display update
    sendCommand(CMD_DISPLAY_UPDATE_CONTROL_2);
    sendData(0xF7);  // Display mode 2 (full update)
    
    sendCommand(CMD_MASTER_ACTIVATION);
    waitBusy();
    
    ESP_LOGI(TAG, "Display update complete");
}


void EPaper::sleep() {
    sendCommand(CMD_DEEP_SLEEP_MODE);
    sendData(0x01);  // Enter deep sleep
    ESP_LOGI(TAG, "Display entering deep sleep");
}


/*
 * =============================================================================
 * DRAWING FUNCTIONS
 * =============================================================================
 */

void EPaper::getBufferPosition(int16_t x, int16_t y, int16_t* bufX, int16_t* bufY) {
    switch (rotation) {
        case 0:
            *bufX = x;
            *bufY = y;
            break;
        case 1:
            *bufX = EPAPER_WIDTH - 1 - y;
            *bufY = x;
            break;
        case 2:
            *bufX = EPAPER_WIDTH - 1 - x;
            *bufY = EPAPER_HEIGHT - 1 - y;
            break;
        case 3:
            *bufX = y;
            *bufY = EPAPER_HEIGHT - 1 - x;
            break;
        default:
            *bufX = x;
            *bufY = y;
    }
}


void EPaper::drawPixel(int16_t x, int16_t y, uint8_t color) {
    if (x < 0 || x >= width || y < 0 || y >= height) return;
    
    int16_t bufX, bufY;
    getBufferPosition(x, y, &bufX, &bufY);
    
    if (bufX < 0 || bufX >= EPAPER_WIDTH || bufY < 0 || bufY >= EPAPER_HEIGHT) return;
    
    uint16_t byteIndex = bufX / 8 + bufY * ((EPAPER_WIDTH + 7) / 8);
    uint8_t bitMask = 0x80 >> (bufX % 8);
    
    if (byteIndex >= bufferSize) return;
    
    switch (color) {
        case EPAPER_BLACK:
            bufferBW[byteIndex] &= ~bitMask;   // Clear bit = black
            bufferRed[byteIndex] &= ~bitMask;  // No red
            break;
        case EPAPER_WHITE:
            bufferBW[byteIndex] |= bitMask;    // Set bit = white
            bufferRed[byteIndex] &= ~bitMask;  // No red
            break;
        case EPAPER_RED:
            bufferBW[byteIndex] |= bitMask;    // White in BW layer
            bufferRed[byteIndex] |= bitMask;   // Red overlay
            break;
    }
}


void EPaper::drawHLine(int16_t x, int16_t y, int16_t w, uint8_t color) {
    for (int16_t i = 0; i < w; i++) {
        drawPixel(x + i, y, color);
    }
}


void EPaper::drawVLine(int16_t x, int16_t y, int16_t h, uint8_t color) {
    for (int16_t i = 0; i < h; i++) {
        drawPixel(x, y + i, color);
    }
}


void EPaper::drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t color) {
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

    // Bresenham
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


void EPaper::drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t color) {
    drawHLine(x, y, w, color);
    drawHLine(x, y + h - 1, w, color);
    drawVLine(x, y, h, color);
    drawVLine(x + w - 1, y, h, color);
}


void EPaper::fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t color) {
    for (int16_t j = 0; j < h; j++) {
        drawHLine(x, y + j, w, color);
    }
}


void EPaper::drawCircle(int16_t cx, int16_t cy, int16_t radius, uint8_t color) {
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


void EPaper::fillCircle(int16_t cx, int16_t cy, int16_t radius, uint8_t color) {
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

uint8_t EPaper::drawChar(int16_t x, int16_t y, char c, uint8_t color, uint8_t size) {
    if (c < 32 || c > 126) c = '?';
    
    const uint8_t* charData = &font5x7[(c - 32) * 5];
    
    for (uint8_t col = 0; col < 5; col++) {
        uint8_t colData = charData[col];
        
        for (uint8_t row = 0; row < 7; row++) {
            if (colData & (1 << row)) {
                if (size == 1) {
                    drawPixel(x + col, y + row, color);
                } else {
                    fillRect(x + col * size, y + row * size, size, size, color);
                }
            }
        }
    }
    
    return 6 * size;
}


void EPaper::drawString(int16_t x, int16_t y, const char* str, uint8_t color, uint8_t size) {
    int16_t cursorX = x;
    
    while (*str) {
        if (*str == '\n') {
            y += 8 * size;
            cursorX = x;
        } else {
            cursorX += drawChar(cursorX, y, *str, color, size);
        }
        str++;
    }
}


/*
 * =============================================================================
 * DISPLAY CONTROL
 * =============================================================================
 */

void EPaper::setRotation(uint8_t r) {
    rotation = r & 3;
    
    if (rotation == 0 || rotation == 2) {
        width = EPAPER_WIDTH;
        height = EPAPER_HEIGHT;
    } else {
        width = EPAPER_HEIGHT;
        height = EPAPER_WIDTH;
    }
}
