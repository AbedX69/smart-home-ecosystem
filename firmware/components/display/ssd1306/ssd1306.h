/**
 * @file ssd1306.h
 * @brief SSD1306 OLED display driver (ESP-IDF, new I2C API only).
 */

#pragma once

#include <driver/i2c_master.h>
#include <driver/gpio.h>
#include <cstring>

// Display dimensions
#define SSD1306_WIDTH       128
#define SSD1306_HEIGHT      64
#define SSD1306_PAGES       (SSD1306_HEIGHT / 8)
#define SSD1306_BUFFER_SIZE (SSD1306_WIDTH * SSD1306_PAGES)

// Default I2C address
#define SSD1306_ADDR_DEFAULT    0x3C
#define SSD1306_ADDR_ALT        0x3D

class SSD1306 {
public:
    /**
     * @brief Construct using an existing I2C bus (from PCA9548A or direct).
     * @param busHandle I2C master bus handle
     * @param address I2C address (default 0x3C)
     */
    SSD1306(i2c_master_bus_handle_t busHandle, uint8_t address = SSD1306_ADDR_DEFAULT);
    
    ~SSD1306();

    bool init();
    void update();
    
    // Buffer operations
    void clear();
    void fill();
    
    // Drawing primitives
    void drawPixel(int16_t x, int16_t y, bool on = true);
    void drawHLine(int16_t x, int16_t y, int16_t width, bool on = true);
    void drawVLine(int16_t x, int16_t y, int16_t height, bool on = true);
    void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, bool on = true);
    void drawRect(int16_t x, int16_t y, int16_t width, int16_t height, bool on = true);
    void fillRect(int16_t x, int16_t y, int16_t width, int16_t height, bool on = true);
    void drawCircle(int16_t cx, int16_t cy, int16_t radius, bool on = true);
    void fillCircle(int16_t cx, int16_t cy, int16_t radius, bool on = true);
    
    // Text
    uint8_t drawChar(int16_t x, int16_t y, char c, bool on = true);
    void drawString(int16_t x, int16_t y, const char* str, bool on = true);
    
    // Display control
    void setContrast(uint8_t contrast);
    void setInverted(bool invert);
    void setDisplayOn(bool on);
    
private:
    void sendCommand(uint8_t cmd);
    void sendData(const uint8_t* data, size_t len);
    
    uint8_t address;
    bool initialized;
    
    i2c_master_bus_handle_t busHandle;
    i2c_master_dev_handle_t devHandle;
    
    uint8_t buffer[SSD1306_BUFFER_SIZE];
};
