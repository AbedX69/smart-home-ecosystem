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
#include "../shared/font_5x7.h"


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
#define CMD_PARTIAL_IN                  0x91
#define CMD_PARTIAL_OUT                 0x92


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
      height(EPAPER_HEIGHT),
      xOffset(0),
      yOffset(0)
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
    // Apply offset
    x += xOffset;
    y += yOffset;
    
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


/*
 * =============================================================================
 * DISPLAY OFFSET
 * =============================================================================
 * 
 * Shifts where content appears on the display. Useful for:
 *     - Compensating for physically misaligned panels
 *     - Centering content
 *     - Adjusting for bezels or enclosures
 * 
 * COORDINATE SYSTEM:
 *     
 *     Positive xOffset → content shifts RIGHT
 *     Negative xOffset → content shifts LEFT
 *     Positive yOffset → content shifts DOWN
 *     Negative yOffset → content shifts UP
 *     
 *                        -yOffset
 *                           ▲
 *                           │
 *            -xOffset ◄─────┼─────► +xOffset
 *                           │
 *                           ▼
 *                        +yOffset
 * 
 * EXAMPLE:
 *     display.setOffset(-5, 10);  // Left 5px, down 10px
 * 
 * NOTE:
 *     - Offset is applied during drawing, not during update()
 *     - Content drawn outside visible area is clipped
 */

void EPaper::setOffset(int16_t x, int16_t y) {
    xOffset = x;
    yOffset = y;
    ESP_LOGI(TAG, "Display offset set to (%d, %d)", x, y);
}


int16_t EPaper::getOffsetX() const {
    return xOffset;
}


int16_t EPaper::getOffsetY() const {
    return yOffset;
}


/*
 * =============================================================================
 * PARTIAL REFRESH
 * =============================================================================
 * 
 * Full refresh takes ~2 seconds and flashes the screen.
 * Partial refresh updates only a region and is MUCH faster (~0.3-0.5 sec).
 * 
 * TRADEOFFS:
 *     
 *     Full Refresh:                  Partial Refresh:
 *     ┌─────────────────┐            ┌─────────────────┐
 *     │█████████████████│            │                 │
 *     │█████████████████│ flash      │    ┌─────┐      │ no flash
 *     │█████████████████│ 2 sec      │    │ NEW │      │ 0.3 sec
 *     │█████████████████│            │    └─────┘      │
 *     └─────────────────┘            └─────────────────┘
 *     
 *     - Clears ghosting            - May leave ghosting
 *     - Slow                       - Fast
 *     - Use for big changes        - Use for small updates
 * 
 * GHOSTING:
 *     After many partial refreshes, you may see "ghosts" of old content.
 *     Do a full refresh periodically to clear them (e.g., every 10 updates).
 * 
 * EXAMPLE - Clock that updates every minute:
 *     
 *     int updateCount = 0;
 *     
 *     while (true) {
 *         // Draw new time to buffer
 *         display.fillRect(10, 100, 100, 30, EPAPER_WHITE);
 *         display.drawString(10, 100, getTimeString(), EPAPER_BLACK);
 *         
 *         // Use partial refresh most of the time
 *         if (updateCount % 10 == 0) {
 *             display.update();        // Full refresh every 10 updates
 *         } else {
 *             display.partialUpdate(10, 100, 100, 30);  // Fast partial
 *         }
 *         
 *         updateCount++;
 *         vTaskDelay(pdMS_TO_TICKS(60000));  // Wait 1 minute
 *     }
 * 
 * NOTE:
 *     - Not all e-paper displays support partial refresh
 *     - Red pixels may not work well with partial refresh
 *     - Partial refresh region is rounded to byte boundaries (8 pixels)
 */

void EPaper::partialUpdate(int16_t x, int16_t y, int16_t w, int16_t h) {
    ESP_LOGI(TAG, "Partial update region (%d,%d) %dx%d", x, y, w, h);
    
    // Clamp to display bounds
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > EPAPER_WIDTH) w = EPAPER_WIDTH - x;
    if (y + h > EPAPER_HEIGHT) h = EPAPER_HEIGHT - y;
    if (w <= 0 || h <= 0) return;
    
    // Round X to byte boundaries (8 pixels)
    int16_t x0 = (x / 8) * 8;
    int16_t x1 = ((x + w + 7) / 8) * 8 - 1;
    int16_t y0 = y;
    int16_t y1 = y + h - 1;
    
    // Enter partial mode
    sendCommand(CMD_PARTIAL_IN);
    
    // Set partial RAM window
    sendCommand(CMD_SET_RAM_X_START_END);
    sendData(x0 / 8);
    sendData(x1 / 8);
    
    sendCommand(CMD_SET_RAM_Y_START_END);
    sendData(y0 & 0xFF);
    sendData((y0 >> 8) & 0xFF);
    sendData(y1 & 0xFF);
    sendData((y1 >> 8) & 0xFF);
    
    // Set start position
    sendCommand(CMD_SET_RAM_X_ADDRESS);
    sendData(x0 / 8);
    
    sendCommand(CMD_SET_RAM_Y_ADDRESS);
    sendData(y0 & 0xFF);
    sendData((y0 >> 8) & 0xFF);
    
    // Calculate buffer region to send
    uint16_t bytesPerRow = (EPAPER_WIDTH + 7) / 8;
    uint16_t partialBytesPerRow = (x1 - x0 + 1) / 8;
    
    // Write B/W data for partial region
    sendCommand(CMD_WRITE_RAM_BW);
    for (int16_t row = y0; row <= y1; row++) {
        uint16_t rowOffset = row * bytesPerRow + (x0 / 8);
        sendData(&bufferBW[rowOffset], partialBytesPerRow);
    }
    
    // Write Red data for partial region
    sendCommand(CMD_WRITE_RAM_RED);
    for (int16_t row = y0; row <= y1; row++) {
        uint16_t rowOffset = row * bytesPerRow + (x0 / 8);
        sendData(&bufferRed[rowOffset], partialBytesPerRow);
    }
    
    // Trigger partial update (faster waveform)
    sendCommand(CMD_DISPLAY_UPDATE_CONTROL_2);
    sendData(0xFF);  // Partial update mode
    
    sendCommand(CMD_MASTER_ACTIVATION);
    vTaskDelay(pdMS_TO_TICKS(10));
    waitBusy();
    
    // Exit partial mode
    sendCommand(CMD_PARTIAL_OUT);
    
    ESP_LOGI(TAG, "Partial update complete");
}
