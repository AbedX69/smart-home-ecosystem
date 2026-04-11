/**
 * @file ssd1306.cpp
 * @brief SSD1306 OLED display driver (ESP-IDF, new I2C API only).
 */

#include "ssd1306.h"
#include <esp_log.h>

static const char* TAG = "SSD1306";

// SSD1306 commands
#define SSD1306_CMD_DISPLAY_OFF         0xAE
#define SSD1306_CMD_DISPLAY_ON          0xAF
#define SSD1306_CMD_SET_CONTRAST        0x81
#define SSD1306_CMD_NORMAL_DISPLAY      0xA6
#define SSD1306_CMD_INVERT_DISPLAY      0xA7
#define SSD1306_CMD_DISPLAY_ALL_ON_RESUME 0xA4
#define SSD1306_CMD_SET_MEMORY_MODE     0x20
#define SSD1306_CMD_SET_COLUMN_ADDR     0x21
#define SSD1306_CMD_SET_PAGE_ADDR       0x22
#define SSD1306_CMD_SET_START_LINE      0x40
#define SSD1306_CMD_SET_SEGMENT_REMAP   0xA0
#define SSD1306_CMD_SET_MULTIPLEX       0xA8
#define SSD1306_CMD_SET_COM_SCAN_DEC    0xC8
#define SSD1306_CMD_SET_DISPLAY_OFFSET  0xD3
#define SSD1306_CMD_SET_COM_PINS        0xDA
#define SSD1306_CMD_SET_CLOCK_DIV       0xD5
#define SSD1306_CMD_SET_PRECHARGE       0xD9
#define SSD1306_CMD_SET_VCOM_DESELECT   0xDB
#define SSD1306_CMD_CHARGE_PUMP         0x8D

// 5x7 font
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


SSD1306::SSD1306(i2c_master_bus_handle_t busHandle, uint8_t address)
    : address(address),
      initialized(false),
      busHandle(busHandle),
      devHandle(nullptr)
{
    memset(buffer, 0, SSD1306_BUFFER_SIZE);
}


SSD1306::~SSD1306() {
    if (initialized && devHandle) {
        i2c_master_bus_rm_device(devHandle);
    }
}


bool SSD1306::init() {
    ESP_LOGI(TAG, "Initializing SSD1306 (addr=0x%02X)", address);

    if (!busHandle) {
        ESP_LOGE(TAG, "Invalid bus handle");
        return false;
    }

    // Add device to bus
    i2c_device_config_t devConfig = {};
    devConfig.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    devConfig.device_address = address;
    devConfig.scl_speed_hz = 400000;

    esp_err_t err = i2c_master_bus_add_device(busHandle, &devConfig, &devHandle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add device: %s", esp_err_to_name(err));
        return false;
    }

    // Initialization sequence
    sendCommand(SSD1306_CMD_DISPLAY_OFF);
    sendCommand(SSD1306_CMD_SET_CLOCK_DIV);
    sendCommand(0x80);
    sendCommand(SSD1306_CMD_SET_MULTIPLEX);
    sendCommand(SSD1306_HEIGHT - 1);
    sendCommand(SSD1306_CMD_SET_DISPLAY_OFFSET);
    sendCommand(0x00);
    sendCommand(SSD1306_CMD_SET_START_LINE | 0x00);
    sendCommand(SSD1306_CMD_CHARGE_PUMP);
    sendCommand(0x14);  // Enable charge pump
    sendCommand(SSD1306_CMD_SET_MEMORY_MODE);
    sendCommand(0x00);  // Horizontal addressing
    sendCommand(SSD1306_CMD_SET_SEGMENT_REMAP | 0x01);
    sendCommand(SSD1306_CMD_SET_COM_SCAN_DEC);
    sendCommand(SSD1306_CMD_SET_COM_PINS);
    sendCommand(0x12);
    sendCommand(SSD1306_CMD_SET_CONTRAST);
    sendCommand(0xCF);
    sendCommand(SSD1306_CMD_SET_PRECHARGE);
    sendCommand(0xF1);
    sendCommand(SSD1306_CMD_SET_VCOM_DESELECT);
    sendCommand(0x40);
    sendCommand(SSD1306_CMD_DISPLAY_ALL_ON_RESUME);
    sendCommand(SSD1306_CMD_NORMAL_DISPLAY);
    sendCommand(SSD1306_CMD_DISPLAY_ON);

    initialized = true;
    ESP_LOGI(TAG, "SSD1306 initialized");
    return true;
}


void SSD1306::sendCommand(uint8_t cmd) {
    uint8_t buf[2] = {0x00, cmd};  // 0x00 = command mode
    i2c_master_transmit(devHandle, buf, 2, 100);
}


void SSD1306::sendData(const uint8_t* data, size_t len) {
    // Allocate buffer: control byte + data
    uint8_t* buf = new uint8_t[len + 1];
    buf[0] = 0x40;  // Data mode
    memcpy(buf + 1, data, len);
    i2c_master_transmit(devHandle, buf, len + 1, 100);
    delete[] buf;
}


void SSD1306::update() {
    sendCommand(SSD1306_CMD_SET_COLUMN_ADDR);
    sendCommand(0);
    sendCommand(SSD1306_WIDTH - 1);
    sendCommand(SSD1306_CMD_SET_PAGE_ADDR);
    sendCommand(0);
    sendCommand(SSD1306_PAGES - 1);
    sendData(buffer, SSD1306_BUFFER_SIZE);
}


void SSD1306::clear() {
    memset(buffer, 0x00, SSD1306_BUFFER_SIZE);
}


void SSD1306::fill() {
    memset(buffer, 0xFF, SSD1306_BUFFER_SIZE);
}


void SSD1306::drawPixel(int16_t x, int16_t y, bool on) {
    if (x < 0 || x >= SSD1306_WIDTH || y < 0 || y >= SSD1306_HEIGHT) return;
    
    uint16_t idx = (y / 8) * SSD1306_WIDTH + x;
    uint8_t bit = 1 << (y % 8);
    
    if (on) buffer[idx] |= bit;
    else buffer[idx] &= ~bit;
}


void SSD1306::drawHLine(int16_t x, int16_t y, int16_t w, bool on) {
    for (int16_t i = 0; i < w; i++) drawPixel(x + i, y, on);
}


void SSD1306::drawVLine(int16_t x, int16_t y, int16_t h, bool on) {
    for (int16_t i = 0; i < h; i++) drawPixel(x, y + i, on);
}


void SSD1306::drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, bool on) {
    int16_t dx = abs(x1 - x0), dy = abs(y1 - y0);
    int16_t sx = (x0 < x1) ? 1 : -1, sy = (y0 < y1) ? 1 : -1;
    int16_t err = dx - dy;

    while (true) {
        drawPixel(x0, y0, on);
        if (x0 == x1 && y0 == y1) break;
        int16_t e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx) { err += dx; y0 += sy; }
    }
}


void SSD1306::drawRect(int16_t x, int16_t y, int16_t w, int16_t h, bool on) {
    drawHLine(x, y, w, on);
    drawHLine(x, y + h - 1, w, on);
    drawVLine(x, y, h, on);
    drawVLine(x + w - 1, y, h, on);
}


void SSD1306::fillRect(int16_t x, int16_t y, int16_t w, int16_t h, bool on) {
    for (int16_t i = 0; i < h; i++) drawHLine(x, y + i, w, on);
}


void SSD1306::drawCircle(int16_t cx, int16_t cy, int16_t r, bool on) {
    int16_t x = r, y = 0, err = 0;
    while (x >= y) {
        drawPixel(cx + x, cy + y, on); drawPixel(cx + y, cy + x, on);
        drawPixel(cx - y, cy + x, on); drawPixel(cx - x, cy + y, on);
        drawPixel(cx - x, cy - y, on); drawPixel(cx - y, cy - x, on);
        drawPixel(cx + y, cy - x, on); drawPixel(cx + x, cy - y, on);
        y++;
        if (err <= 0) err += 2 * y + 1;
        if (err > 0) { x--; err -= 2 * x + 1; }
    }
}


void SSD1306::fillCircle(int16_t cx, int16_t cy, int16_t r, bool on) {
    drawVLine(cx, cy - r, 2 * r + 1, on);
    int16_t x = r, y = 0, err = 0;
    while (x >= y) {
        drawVLine(cx + x, cy - y, 2 * y + 1, on);
        drawVLine(cx - x, cy - y, 2 * y + 1, on);
        drawVLine(cx + y, cy - x, 2 * x + 1, on);
        drawVLine(cx - y, cy - x, 2 * x + 1, on);
        y++;
        if (err <= 0) err += 2 * y + 1;
        if (err > 0) { x--; err -= 2 * x + 1; }
    }
}


uint8_t SSD1306::drawChar(int16_t x, int16_t y, char c, bool on) {
    if (c < 32 || c > 126) c = '?';
    const uint8_t* data = &font5x7[(c - 32) * 5];
    
    for (uint8_t col = 0; col < 5; col++) {
        for (uint8_t row = 0; row < 7; row++) {
            drawPixel(x + col, y + row, (data[col] & (1 << row)) ? on : !on);
        }
    }
    for (uint8_t row = 0; row < 7; row++) drawPixel(x + 5, y + row, !on);
    return 6;
}


void SSD1306::drawString(int16_t x, int16_t y, const char* str, bool on) {
    int16_t cx = x;
    while (*str) {
        if (*str == '\n') { y += 8; cx = x; }
        else cx += drawChar(cx, y, *str, on);
        str++;
    }
}


void SSD1306::setContrast(uint8_t contrast) {
    sendCommand(SSD1306_CMD_SET_CONTRAST);
    sendCommand(contrast);
}


void SSD1306::setInverted(bool invert) {
    sendCommand(invert ? SSD1306_CMD_INVERT_DISPLAY : SSD1306_CMD_NORMAL_DISPLAY);
}


void SSD1306::setDisplayOn(bool on) {
    sendCommand(on ? SSD1306_CMD_DISPLAY_ON : SSD1306_CMD_DISPLAY_OFF);
}
