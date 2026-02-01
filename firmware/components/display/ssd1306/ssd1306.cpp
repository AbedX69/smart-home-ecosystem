/**
 * @file ssd1306.cpp
 * @brief SSD1306 OLED display driver implementation (ESP-IDF).
 *
 * @details
 * Implements I2C communication and drawing primitives for SSD1306.
 */

/*
 * =============================================================================
 * BEGINNER'S GUIDE: HOW THIS IMPLEMENTATION WORKS
 * =============================================================================
 * 
 * THE MAIN FLOW:
 * 
 *     1. init()       → Set up I2C, send config commands to display
 *     2. clear()      → Zero out the frame buffer (in ESP32 RAM)
 *     3. draw...()    → Modify pixels in the frame buffer
 *     4. update()     → Send entire frame buffer to display over I2C
 * 
 * The display only changes when you call update()!
 * 
 * =============================================================================
 * SSD1306 COMMAND PROTOCOL
 * =============================================================================
 * 
 * I2C messages to SSD1306 have this format:
 * 
 *     [I2C Address] [Control Byte] [Data/Command bytes...]
 *     
 *     Control Byte:
 *         0x00 = Next bytes are COMMANDS
 *         0x40 = Next bytes are DATA (pixel data for RAM)
 * 
 * SENDING A COMMAND:
 *     i2c_write: [0x3C] [0x00] [command]
 *                 addr  ctrl   cmd
 * 
 * SENDING PIXEL DATA:
 *     i2c_write: [0x3C] [0x40] [pixel bytes...]
 *                 addr  ctrl   data
 * 
 * =============================================================================
 * INITIALIZATION SEQUENCE
 * =============================================================================
 * 
 * The SSD1306 needs a specific sequence of commands to start up:
 * 
 *     1. Display OFF (while we configure)
 *     2. Set clock divider
 *     3. Set multiplex ratio (number of rows)
 *     4. Set display offset
 *     5. Set start line
 *     6. Set charge pump (MUST enable for display to work!)
 *     7. Set memory addressing mode
 *     8. Set segment remap (left/right orientation)
 *     9. Set COM output scan direction (up/down orientation)
 *     10. Set COM pins configuration
 *     11. Set contrast
 *     12. Set precharge period
 *     13. Set VCOMH deselect level
 *     14. Enable display RAM content
 *     15. Normal display (not inverted)
 *     16. Display ON
 * 
 * Don't worry about memorizing these - just copy the sequence!
 * 
 * =============================================================================
 */

#include "ssd1306.h"
#include <esp_log.h>


static const char* TAG = "SSD1306";


/*
 * =============================================================================
 * SSD1306 COMMAND DEFINITIONS
 * =============================================================================
 * 
 * These are the command bytes the SSD1306 understands.
 * Full list in datasheet, but these are the important ones.
 */

// Fundamental commands
#define SSD1306_CMD_DISPLAY_OFF         0xAE
#define SSD1306_CMD_DISPLAY_ON          0xAF
#define SSD1306_CMD_SET_CONTRAST        0x81
#define SSD1306_CMD_NORMAL_DISPLAY      0xA6
#define SSD1306_CMD_INVERT_DISPLAY      0xA7
#define SSD1306_CMD_DISPLAY_ALL_ON      0xA5
#define SSD1306_CMD_DISPLAY_ALL_ON_RESUME 0xA4

// Scrolling commands (not used here, but available)
#define SSD1306_CMD_SCROLL_RIGHT        0x26
#define SSD1306_CMD_SCROLL_LEFT         0x27
#define SSD1306_CMD_SCROLL_STOP         0x2E
#define SSD1306_CMD_SCROLL_START        0x2F

// Addressing commands
#define SSD1306_CMD_SET_MEMORY_MODE     0x20
#define SSD1306_CMD_SET_COLUMN_ADDR     0x21
#define SSD1306_CMD_SET_PAGE_ADDR       0x22
#define SSD1306_CMD_SET_START_LINE      0x40
#define SSD1306_CMD_SET_PAGE_START      0xB0

// Hardware configuration
#define SSD1306_CMD_SET_SEGMENT_REMAP   0xA0
#define SSD1306_CMD_SET_MULTIPLEX       0xA8
#define SSD1306_CMD_SET_COM_SCAN_INC    0xC0
#define SSD1306_CMD_SET_COM_SCAN_DEC    0xC8
#define SSD1306_CMD_SET_DISPLAY_OFFSET  0xD3
#define SSD1306_CMD_SET_COM_PINS        0xDA

// Timing & driving
#define SSD1306_CMD_SET_CLOCK_DIV       0xD5
#define SSD1306_CMD_SET_PRECHARGE       0xD9
#define SSD1306_CMD_SET_VCOM_DESELECT   0xDB
#define SSD1306_CMD_CHARGE_PUMP         0x8D


/*
 * =============================================================================
 * BUILT-IN FONT (5x7 pixels)
 * =============================================================================
 * 
 * Each character is 5 columns wide, 7 rows tall.
 * Stored as 5 bytes per character, each byte is one column.
 * 
 * Character 'A' (0x41) example:
 *     
 *     Col: 0    1    2    3    4
 *          ▓    ▓    ▓    ▓    ▓
 *     0x7C 0x12 0x11 0x12 0x7C
 *     
 *     Bit layout per column:
 *     Bit 0: ○    ●    ●    ●    ○     Row 0
 *     Bit 1: ○    ○    ○    ○    ○     Row 1
 *     Bit 2: ●    ●    ●    ●    ●     Row 2
 *     Bit 3: ●    ○    ○    ○    ●     Row 3
 *     Bit 4: ●    ○    ○    ○    ●     Row 4
 *     Bit 5: ●    ●    ●    ●    ●     Row 5
 *     Bit 6: ○    ●    ○    ●    ○     Row 6
 * 
 * This creates the letter A shape!
 */

static const uint8_t font5x7[] = {
    // ASCII 32-126 (printable characters)
    // Each character is 5 bytes
    
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
SSD1306::SSD1306(gpio_num_t sdaPin, gpio_num_t sclPin, 
                 uint8_t address, i2c_port_t i2cPort)
    : sdaPin(sdaPin),
      sclPin(sclPin),
      address(address),
      i2cPort(i2cPort),
      initialized(false)
{
    // Clear frame buffer
    memset(buffer, 0, SSD1306_BUFFER_SIZE);
}


/*
 * =============================================================================
 * DESTRUCTOR
 * =============================================================================
 */
SSD1306::~SSD1306() {
    if (initialized) {
        i2c_driver_delete(i2cPort);
    }
}


/*
 * =============================================================================
 * INITIALIZATION
 * =============================================================================
 * 
 * This is where the magic happens!
 * We configure I2C and send the startup sequence to the display.
 */
bool SSD1306::init() {
    ESP_LOGI(TAG, "Initializing SSD1306 on I2C (SDA=%d, SCL=%d, addr=0x%02X)",
             sdaPin, sclPin, address);

    /*
     * -------------------------------------------------------------------------
     * STEP 1: Configure I2C bus
     * -------------------------------------------------------------------------
     */
    i2c_config_t conf = {};
    conf.mode = I2C_MODE_MASTER;            // ESP32 is the master
    conf.sda_io_num = sdaPin;
    conf.scl_io_num = sclPin;
    conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
    conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
    conf.master.clk_speed = 400000;         // 400kHz (fast mode)

    esp_err_t err = i2c_param_config(i2cPort, &conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C param config failed: %s", esp_err_to_name(err));
        return false;
    }

    err = i2c_driver_install(i2cPort, I2C_MODE_MASTER, 0, 0, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C driver install failed: %s", esp_err_to_name(err));
        return false;
    }

    /*
     * -------------------------------------------------------------------------
     * STEP 2: Send initialization commands to SSD1306
     * -------------------------------------------------------------------------
     * 
     * This sequence comes from the SSD1306 datasheet.
     * Order matters for some commands!
     */
    const uint8_t initCmds[] = {
        SSD1306_CMD_DISPLAY_OFF,            // Display OFF during setup
        
        SSD1306_CMD_SET_CLOCK_DIV, 0x80,    // Clock divider (default)
        
        SSD1306_CMD_SET_MULTIPLEX, 0x3F,    // Multiplex ratio: 64 rows (0x3F = 63)
        
        SSD1306_CMD_SET_DISPLAY_OFFSET, 0x00,  // No display offset
        
        SSD1306_CMD_SET_START_LINE | 0x00,  // Start line = 0
        
        SSD1306_CMD_CHARGE_PUMP, 0x14,      // Enable charge pump (REQUIRED!)
        
        SSD1306_CMD_SET_MEMORY_MODE, 0x00,  // Horizontal addressing mode
        
        SSD1306_CMD_SET_SEGMENT_REMAP | 0x01,  // Segment remap (flip horizontally)
        
        SSD1306_CMD_SET_COM_SCAN_DEC,       // COM scan direction (flip vertically)
        
        SSD1306_CMD_SET_COM_PINS, 0x12,     // COM pins configuration for 128x64
        
        SSD1306_CMD_SET_CONTRAST, 0xCF,     // Contrast (0x00-0xFF)
        
        SSD1306_CMD_SET_PRECHARGE, 0xF1,    // Pre-charge period
        
        SSD1306_CMD_SET_VCOM_DESELECT, 0x40,// VCOMH deselect level
        
        SSD1306_CMD_DISPLAY_ALL_ON_RESUME,  // Display follows RAM content
        
        SSD1306_CMD_NORMAL_DISPLAY,         // Normal display (not inverted)
        
        SSD1306_CMD_DISPLAY_ON              // Display ON!
    };

    sendCommands(initCmds, sizeof(initCmds));

    initialized = true;
    
    // Clear display
    clear();
    update();

    ESP_LOGI(TAG, "SSD1306 initialized successfully");
    return true;
}


/*
 * =============================================================================
 * SEND COMMAND / DATA
 * =============================================================================
 */

void SSD1306::sendCommand(uint8_t cmd) {
    uint8_t data[2] = {0x00, cmd};  // 0x00 = command mode
    i2c_master_write_to_device(i2cPort, address, data, 2, pdMS_TO_TICKS(100));
}


void SSD1306::sendCommands(const uint8_t* cmds, size_t len) {
    for (size_t i = 0; i < len; i++) {
        sendCommand(cmds[i]);
    }
}


void SSD1306::sendData(const uint8_t* data, size_t len) {
    /*
     * For efficiency, we send data in chunks with a single I2C transaction.
     * Format: [0x40] [data bytes...]
     * 0x40 = data mode (next bytes go to display RAM)
     */
    uint8_t* txBuffer = new uint8_t[len + 1];
    txBuffer[0] = 0x40;  // Data mode
    memcpy(txBuffer + 1, data, len);
    
    i2c_master_write_to_device(i2cPort, address, txBuffer, len + 1, pdMS_TO_TICKS(100));
    
    delete[] txBuffer;
}


/*
 * =============================================================================
 * UPDATE - SEND FRAME BUFFER TO DISPLAY
 * =============================================================================
 */
void SSD1306::update() {
    // Set column address range (0 to 127)
    sendCommand(SSD1306_CMD_SET_COLUMN_ADDR);
    sendCommand(0);                         // Start column
    sendCommand(SSD1306_WIDTH - 1);         // End column
    
    // Set page address range (0 to 7)
    sendCommand(SSD1306_CMD_SET_PAGE_ADDR);
    sendCommand(0);                         // Start page
    sendCommand(SSD1306_PAGES - 1);         // End page
    
    // Send entire buffer
    sendData(buffer, SSD1306_BUFFER_SIZE);
}


/*
 * =============================================================================
 * CLEAR / FILL
 * =============================================================================
 */

void SSD1306::clear() {
    memset(buffer, 0x00, SSD1306_BUFFER_SIZE);
}


void SSD1306::fill() {
    memset(buffer, 0xFF, SSD1306_BUFFER_SIZE);
}


/*
 * =============================================================================
 * DRAW PIXEL
 * =============================================================================
 * 
 * This is the fundamental drawing operation.
 * All other drawing functions ultimately call this.
 * 
 * COORDINATE SYSTEM:
 *     (0,0) ────────────────→ X (0-127)
 *       │
 *       │
 *       │
 *       ↓
 *       Y (0-63)
 * 
 * BUFFER LAYOUT:
 *     The buffer is organized in "pages" (horizontal strips of 8 pixels).
 *     Each byte represents a vertical column of 8 pixels.
 *     
 *     To find the byte for pixel (x, y):
 *         page = y / 8
 *         byte_index = page * 128 + x
 *         bit = y % 8
 */
void SSD1306::drawPixel(int16_t x, int16_t y, bool on) {
    // Bounds check
    if (x < 0 || x >= SSD1306_WIDTH || y < 0 || y >= SSD1306_HEIGHT) {
        return;
    }

    // Calculate buffer position
    uint16_t byteIndex = (y / 8) * SSD1306_WIDTH + x;
    uint8_t bitMask = 1 << (y % 8);

    // Set or clear the bit
    if (on) {
        buffer[byteIndex] |= bitMask;   // Set bit (pixel on)
    } else {
        buffer[byteIndex] &= ~bitMask;  // Clear bit (pixel off)
    }
}


/*
 * =============================================================================
 * DRAW LINES
 * =============================================================================
 */

void SSD1306::drawHLine(int16_t x, int16_t y, int16_t width, bool on) {
    for (int16_t i = 0; i < width; i++) {
        drawPixel(x + i, y, on);
    }
}


void SSD1306::drawVLine(int16_t x, int16_t y, int16_t height, bool on) {
    for (int16_t i = 0; i < height; i++) {
        drawPixel(x, y + i, on);
    }
}


void SSD1306::drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, bool on) {
    /*
     * Bresenham's line algorithm
     * Draws a line between any two points using only integer math.
     */
    int16_t dx = abs(x1 - x0);
    int16_t dy = abs(y1 - y0);
    int16_t sx = (x0 < x1) ? 1 : -1;
    int16_t sy = (y0 < y1) ? 1 : -1;
    int16_t err = dx - dy;

    while (true) {
        drawPixel(x0, y0, on);
        
        if (x0 == x1 && y0 == y1) break;
        
        int16_t e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
    }
}


/*
 * =============================================================================
 * DRAW RECTANGLES
 * =============================================================================
 */

void SSD1306::drawRect(int16_t x, int16_t y, int16_t width, int16_t height, bool on) {
    drawHLine(x, y, width, on);                     // Top
    drawHLine(x, y + height - 1, width, on);        // Bottom
    drawVLine(x, y, height, on);                    // Left
    drawVLine(x + width - 1, y, height, on);        // Right
}


void SSD1306::fillRect(int16_t x, int16_t y, int16_t width, int16_t height, bool on) {
    for (int16_t i = 0; i < height; i++) {
        drawHLine(x, y + i, width, on);
    }
}


/*
 * =============================================================================
 * DRAW CIRCLES
 * =============================================================================
 */

void SSD1306::drawCircle(int16_t cx, int16_t cy, int16_t radius, bool on) {
    /*
     * Midpoint circle algorithm
     * Draws a circle using 8-way symmetry.
     */
    int16_t x = radius;
    int16_t y = 0;
    int16_t err = 0;

    while (x >= y) {
        drawPixel(cx + x, cy + y, on);
        drawPixel(cx + y, cy + x, on);
        drawPixel(cx - y, cy + x, on);
        drawPixel(cx - x, cy + y, on);
        drawPixel(cx - x, cy - y, on);
        drawPixel(cx - y, cy - x, on);
        drawPixel(cx + y, cy - x, on);
        drawPixel(cx + x, cy - y, on);

        y++;
        if (err <= 0) {
            err += 2 * y + 1;
        }
        if (err > 0) {
            x--;
            err -= 2 * x + 1;
        }
    }
}


void SSD1306::fillCircle(int16_t cx, int16_t cy, int16_t radius, bool on) {
    drawVLine(cx, cy - radius, 2 * radius + 1, on);
    
    int16_t x = radius;
    int16_t y = 0;
    int16_t err = 0;

    while (x >= y) {
        drawVLine(cx + x, cy - y, 2 * y + 1, on);
        drawVLine(cx - x, cy - y, 2 * y + 1, on);
        drawVLine(cx + y, cy - x, 2 * x + 1, on);
        drawVLine(cx - y, cy - x, 2 * x + 1, on);

        y++;
        if (err <= 0) {
            err += 2 * y + 1;
        }
        if (err > 0) {
            x--;
            err -= 2 * x + 1;
        }
    }
}


/*
 * =============================================================================
 * DRAW TEXT
 * =============================================================================
 */

uint8_t SSD1306::drawChar(int16_t x, int16_t y, char c, bool on) {
    // Check printable range
    if (c < 32 || c > 126) {
        c = '?';  // Replace unprintable with ?
    }

    // Get font data for this character
    const uint8_t* charData = &font5x7[(c - 32) * 5];

    // Draw 5 columns
    for (uint8_t col = 0; col < 5; col++) {
        uint8_t colData = charData[col];
        
        for (uint8_t row = 0; row < 7; row++) {
            if (colData & (1 << row)) {
                drawPixel(x + col, y + row, on);
            } else {
                drawPixel(x + col, y + row, !on);
            }
        }
    }

    // Clear the spacing column
    for (uint8_t row = 0; row < 7; row++) {
        drawPixel(x + 5, y + row, !on);
    }

    return 6;  // Character width including spacing
}


void SSD1306::drawString(int16_t x, int16_t y, const char* str, bool on) {
    int16_t cursorX = x;
    
    while (*str) {
        if (*str == '\n') {
            // Newline: move down 8 pixels, reset X
            y += 8;
            cursorX = x;
        } else {
            cursorX += drawChar(cursorX, y, *str, on);
        }
        str++;
    }
}


/*
 * =============================================================================
 * DISPLAY CONTROL
 * =============================================================================
 */

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
