/**
 * @file pca9548a.cpp
 * @brief PCA9548A I2C multiplexer implementation (ESP-IDF).
 *
 * @details
 * Implements I2C communication for PCA9548A/TCA9548A multiplexer.
 */

#include "pca9548a.h"
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>


static const char* TAG = "PCA9548A";


/*
 * =============================================================================
 * CONSTRUCTOR
 * =============================================================================
 */
PCA9548A::PCA9548A(gpio_num_t sdaPin, gpio_num_t sclPin, uint8_t address,
                   gpio_num_t rstPin, i2c_port_t i2cPort)
    : sdaPin(sdaPin),
      sclPin(sclPin),
      address(address),
      rstPin(rstPin),
      i2cPort(i2cPort),
      busHandle(nullptr),
      devHandle(nullptr),
      initialized(false),
      currentChannels(0)
{
}


/*
 * =============================================================================
 * DESTRUCTOR
 * =============================================================================
 */
PCA9548A::~PCA9548A() {
    if (initialized) {
        if (devHandle) {
            i2c_master_bus_rm_device(devHandle);
        }
        if (busHandle) {
            i2c_del_master_bus(busHandle);
        }
    }
}


/*
 * =============================================================================
 * INITIALIZATION
 * =============================================================================
 */
bool PCA9548A::init() {
    ESP_LOGI(TAG, "Initializing PCA9548A (SDA=%d, SCL=%d, Addr=0x%02X)",
             sdaPin, sclPin, address);

    /*
     * -------------------------------------------------------------------------
     * STEP 1: Configure RST pin if specified
     * -------------------------------------------------------------------------
     */
    if (rstPin != GPIO_NUM_NC) {
        gpio_config_t io_conf = {};
        io_conf.mode = GPIO_MODE_OUTPUT;
        io_conf.pin_bit_mask = (1ULL << rstPin);
        io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
        io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
        gpio_config(&io_conf);
        gpio_set_level(rstPin, 1);  // Not in reset
    }

    /*
     * -------------------------------------------------------------------------
     * STEP 2: Configure I2C master bus
     * -------------------------------------------------------------------------
     */
    i2c_master_bus_config_t busConfig = {};
    busConfig.i2c_port = i2cPort;
    busConfig.sda_io_num = sdaPin;
    busConfig.scl_io_num = sclPin;
    busConfig.clk_source = I2C_CLK_SRC_DEFAULT;
    busConfig.glitch_ignore_cnt = 7;
    busConfig.flags.enable_internal_pullup = true;

    esp_err_t err = i2c_new_master_bus(&busConfig, &busHandle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C bus init failed: %s", esp_err_to_name(err));
        return false;
    }

    /*
     * -------------------------------------------------------------------------
     * STEP 3: Add PCA9548A device
     * -------------------------------------------------------------------------
     */
    i2c_device_config_t devConfig = {};
    devConfig.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    devConfig.device_address = address;
    devConfig.scl_speed_hz = 400000;  // 400kHz

    err = i2c_master_bus_add_device(busHandle, &devConfig, &devHandle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C device add failed: %s", esp_err_to_name(err));
        i2c_del_master_bus(busHandle);
        return false;
    }

    /*
     * -------------------------------------------------------------------------
     * STEP 4: Verify communication - disable all channels
     * -------------------------------------------------------------------------
     */
    if (!disableAll()) {
        ESP_LOGE(TAG, "Failed to communicate with PCA9548A");
        i2c_master_bus_rm_device(devHandle);
        i2c_del_master_bus(busHandle);
        return false;
    }

    initialized = true;
    ESP_LOGI(TAG, "PCA9548A initialized successfully");
    return true;
}


/*
 * =============================================================================
 * CHANNEL SELECTION
 * =============================================================================
 */

bool PCA9548A::selectChannel(uint8_t channel) {
    if (channel >= PCA9548A_NUM_CHANNELS) {
        ESP_LOGE(TAG, "Invalid channel: %d (must be 0-7)", channel);
        return false;
    }

    uint8_t mask = (1 << channel);
    return enableChannels(mask);
}


bool PCA9548A::enableChannels(uint8_t channelMask) {
    if (!initialized) return false;

    if (writeRegister(channelMask)) {
        currentChannels = channelMask;
        ESP_LOGD(TAG, "Enabled channels: 0x%02X", channelMask);
        return true;
    }
    return false;
}


bool PCA9548A::disableAll() {
    if (!initialized && !devHandle) {
        // During init, devHandle exists but initialized is false
        // Allow this to proceed for init verification
    }
    
    if (writeRegister(0x00)) {
        currentChannels = 0;
        ESP_LOGD(TAG, "All channels disabled");
        return true;
    }
    return false;
}


uint8_t PCA9548A::getEnabledChannels() {
    if (!initialized) return 0xFF;
    return readRegister();
}


bool PCA9548A::isChannelEnabled(uint8_t channel) {
    if (channel >= PCA9548A_NUM_CHANNELS) return false;
    uint8_t channels = getEnabledChannels();
    return (channels & (1 << channel)) != 0;
}


/*
 * =============================================================================
 * RESET
 * =============================================================================
 */

void PCA9548A::reset() {
    if (rstPin == GPIO_NUM_NC) {
        ESP_LOGW(TAG, "Reset pin not configured");
        return;
    }

    ESP_LOGI(TAG, "Resetting PCA9548A");
    gpio_set_level(rstPin, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(rstPin, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    
    currentChannels = 0;
}


/*
 * =============================================================================
 * CHANNEL SCANNING
 * =============================================================================
 */

uint8_t PCA9548A::scanChannel(uint8_t channel, uint8_t* addresses, uint8_t maxAddresses) {
    if (!initialized || channel >= PCA9548A_NUM_CHANNELS) return 0;

    // Select the channel
    if (!selectChannel(channel)) return 0;

    uint8_t found = 0;
    ESP_LOGI(TAG, "Scanning channel %d for I2C devices...", channel);

    // Scan address range 0x08 to 0x77 (valid 7-bit addresses)
    for (uint8_t addr = 0x08; addr < 0x78; addr++) {
        // Skip our own address
        if (addr == address) continue;

        // Try to probe the address
        i2c_device_config_t probeConfig = {};
        probeConfig.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        probeConfig.device_address = addr;
        probeConfig.scl_speed_hz = 100000;

        i2c_master_dev_handle_t probeHandle;
        esp_err_t err = i2c_master_bus_add_device(busHandle, &probeConfig, &probeHandle);
        if (err != ESP_OK) continue;

        // Try a simple probe (write 1 dummy byte)
        uint8_t dummy = 0;
        err = i2c_master_transmit(probeHandle, &dummy, 1, 50);
        
        i2c_master_bus_rm_device(probeHandle);

        if (err == ESP_OK) {
            ESP_LOGI(TAG, "  Found device at 0x%02X", addr);
            if (found < maxAddresses) {
                addresses[found] = addr;
            }
            found++;
        }
    }

    ESP_LOGI(TAG, "Scan complete. Found %d device(s) on channel %d", found, channel);
    return found;
}


/*
 * =============================================================================
 * LOW-LEVEL I2C
 * =============================================================================
 */

bool PCA9548A::writeRegister(uint8_t value) {
    esp_err_t err = i2c_master_transmit(devHandle, &value, 1, 100);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C write failed: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}


uint8_t PCA9548A::readRegister() {
    uint8_t value = 0xFF;
    esp_err_t err = i2c_master_receive(devHandle, &value, 1, 100);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C read failed: %s", esp_err_to_name(err));
        return 0xFF;
    }
    return value;
}
