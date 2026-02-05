/**
 * @file xpt2046.cpp
 * @brief XPT2046 resistive touch controller implementation (ESP-IDF).
 *
 * @details
 * Implements SPI communication and touch coordinate reading.
 */

#include "xpt2046.h"
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>


static const char* TAG = "XPT2046";


/*
 * =============================================================================
 * XPT2046 COMMAND BYTES
 * =============================================================================
 * 
 * Command format: S A2 A1 A0 MODE SER/DFR PD1 PD0
 * 
 *     S = Start bit (always 1)
 *     A2-A0 = Channel select
 *     MODE = 12-bit (0) or 8-bit (1)
 *     SER/DFR = Single-ended (1) or Differential (0)
 *     PD1-PD0 = Power down mode
 */

#define XPT2046_START       0x80    // Start bit

// Channel addresses (A2-A0)
#define XPT2046_X_POS       0x50    // X position (differential)
#define XPT2046_Y_POS       0x10    // Y position (differential)
#define XPT2046_Z1_POS      0x30    // Z1 (pressure)
#define XPT2046_Z2_POS      0x40    // Z2 (pressure)

// Power down modes
#define XPT2046_PD_FULL     0x00    // Power down between conversions
#define XPT2046_PD_REF_OFF  0x01    // Reference off, ADC on
#define XPT2046_PD_ADC_OFF  0x02    // Reference on, ADC off
#define XPT2046_PD_NONE     0x03    // No power down

// Complete commands
#define CMD_READ_X      (XPT2046_START | XPT2046_X_POS | XPT2046_PD_NONE)
#define CMD_READ_Y      (XPT2046_START | XPT2046_Y_POS | XPT2046_PD_NONE)
#define CMD_READ_Z1     (XPT2046_START | XPT2046_Z1_POS | XPT2046_PD_NONE)
#define CMD_READ_Z2     (XPT2046_START | XPT2046_Z2_POS | XPT2046_PD_NONE)


/*
 * =============================================================================
 * CONSTRUCTOR
 * =============================================================================
 */
XPT2046::XPT2046(spi_host_device_t spiHost, gpio_num_t csPin, gpio_num_t irqPin)
    : spiHost(spiHost),
      csPin(csPin),
      irqPin(irqPin),
      spiDevice(nullptr),
      initialized(false),
      calXMin(200),       // Default calibration values
      calXMax(3800),
      calYMin(200),
      calYMax(3800),
      screenWidth(240),
      screenHeight(320),
      rotation(0)
{
}


/*
 * =============================================================================
 * DESTRUCTOR
 * =============================================================================
 */
XPT2046::~XPT2046() {
    if (initialized && spiDevice) {
        spi_bus_remove_device(spiDevice);
    }
}


/*
 * =============================================================================
 * INITIALIZATION
 * =============================================================================
 */
bool XPT2046::init() {
    ESP_LOGI(TAG, "Initializing XPT2046 (CS=%d, IRQ=%d)", csPin, irqPin);

    /*
     * -------------------------------------------------------------------------
     * STEP 1: Configure IRQ pin as input
     * -------------------------------------------------------------------------
     */
    gpio_config_t io_conf = {};
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.pin_bit_mask = (1ULL << irqPin);
    gpio_config(&io_conf);

    /*
     * -------------------------------------------------------------------------
     * STEP 2: Add SPI device (bus already initialized by display)
     * -------------------------------------------------------------------------
     */
    spi_device_interface_config_t devConfig = {};
    devConfig.clock_speed_hz = 2 * 1000 * 1000;     // 2 MHz (touch is slower)
    devConfig.mode = 0;                              // SPI mode 0
    devConfig.spics_io_num = csPin;
    devConfig.queue_size = 3;

    esp_err_t err = spi_bus_add_device(spiHost, &devConfig, &spiDevice);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPI device add failed: %s", esp_err_to_name(err));
        return false;
    }

    initialized = true;
    ESP_LOGI(TAG, "XPT2046 initialized successfully");
    return true;
}


/*
 * =============================================================================
 * TOUCH DETECTION
 * =============================================================================
 */
bool XPT2046::isTouched() {

    //to debug
      uint16_t z = readChannel(CMD_READ_Z1);
    return z > 100;
    // IRQ pin is active LOW when touched
    //return gpio_get_level(irqPin) == 0;
}


/*
 * =============================================================================
 * READ CHANNEL
 * =============================================================================
 */
uint16_t XPT2046::readChannel(uint8_t command) {
    uint8_t txData[3] = {command, 0x00, 0x00};
    uint8_t rxData[3] = {0};
    
    spi_transaction_t trans = {};
    trans.length = 24;          // 3 bytes
    trans.tx_buffer = txData;
    trans.rx_buffer = rxData;
    
    spi_device_polling_transmit(spiDevice, &trans);
    
    // Result is in bits 1-12 of the 16-bit response
    // Format: 0 B11 B10 B9 B8 B7 B6 B5 | B4 B3 B2 B1 B0 x x x
    uint16_t result = ((rxData[1] << 8) | rxData[2]) >> 3;
    
    return result & 0x0FFF;  // 12-bit value
}


/*
 * =============================================================================
 * RAW POSITION
 * =============================================================================
 */
bool XPT2046::getRawPosition(int16_t* x, int16_t* y) {
    if (!isTouched()) {
        return false;
    }
    
    // Take multiple readings and average (reduces noise)
    const int samples = 4;
    int32_t sumX = 0, sumY = 0;
    int validSamples = 0;
    
    for (int i = 0; i < samples; i++) {
        if (!isTouched()) break;
        
        uint16_t rawX = readChannel(CMD_READ_X);
        uint16_t rawY = readChannel(CMD_READ_Y);
        
        // Ignore outliers (common with resistive touch)
        if (rawX > 100 && rawX < 4000 && rawY > 100 && rawY < 4000) {
            sumX += rawX;
            sumY += rawY;
            validSamples++;
        }
    }
    
    if (validSamples < 2) {
        return false;
    }
    
    *x = sumX / validSamples;
    *y = sumY / validSamples;
    
    return true;
}


/*
 * =============================================================================
 * CALIBRATED POSITION
 * =============================================================================
 */
bool XPT2046::getPosition(int16_t* x, int16_t* y) {
    int16_t rawX, rawY;
    
    if (!getRawPosition(&rawX, &rawY)) {
        return false;
    }
    
    // Map raw to screen coordinates
    int16_t screenX = mapValue(rawX, calXMin, calXMax, 0, screenWidth - 1);
    int16_t screenY = mapValue(rawY, calYMin, calYMax, 0, screenHeight - 1);
    
    // Clamp to screen bounds
    if (screenX < 0) screenX = 0;
    if (screenX >= screenWidth) screenX = screenWidth - 1;
    if (screenY < 0) screenY = 0;
    if (screenY >= screenHeight) screenY = screenHeight - 1;
    
    // Apply rotation
    switch (rotation) {
        case 0:  // No rotation
            *x = screenX;
            *y = screenY;
            break;
        case 1:  // 90° CW
            *x = screenY;
            *y = screenWidth - 1 - screenX;
            break;
        case 2:  // 180°
            *x = screenWidth - 1 - screenX;
            *y = screenHeight - 1 - screenY;
            break;
        case 3:  // 270° CW (90° CCW)
            *x = screenHeight - 1 - screenY;
            *y = screenX;
            break;
    }
    
    return true;
}


/*
 * =============================================================================
 * PRESSURE
 * =============================================================================
 */
uint16_t XPT2046::getPressure() {
    if (!isTouched()) {
        return 0;
    }
    
    uint16_t z1 = readChannel(CMD_READ_Z1);
    uint16_t z2 = readChannel(CMD_READ_Z2);
    
    // Avoid division by zero
    if (z1 == 0) return 0;
    
    // Pressure calculation (simplified)
    // Higher Z1, lower Z2 = harder press
    uint16_t pressure = z1 - z2 + 4095;
    
    return pressure;
}


/*
 * =============================================================================
 * CALIBRATION
 * =============================================================================
 */
void XPT2046::setCalibration(int16_t xMin, int16_t xMax, int16_t yMin, int16_t yMax) {
    calXMin = xMin;
    calXMax = xMax;
    calYMin = yMin;
    calYMax = yMax;
    
    ESP_LOGI(TAG, "Calibration set: X[%d-%d] Y[%d-%d]", xMin, xMax, yMin, yMax);
}


void XPT2046::setScreenSize(uint16_t width, uint16_t height) {
    screenWidth = width;
    screenHeight = height;
}


void XPT2046::setRotation(uint8_t r) {
    rotation = r & 3;
}


/*
 * =============================================================================
 * HELPER: MAP VALUE
 * =============================================================================
 */
int16_t XPT2046::mapValue(int16_t value, int16_t inMin, int16_t inMax, int16_t outMin, int16_t outMax) {
    // Handle inverted ranges
    if (inMin > inMax) {
        int16_t temp = inMin;
        inMin = inMax;
        inMax = temp;
        int16_t tempOut = outMin;
        outMin = outMax;
        outMax = tempOut;
    }
    
    return (int32_t)(value - inMin) * (outMax - outMin) / (inMax - inMin) + outMin;
}
