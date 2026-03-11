/*
 * =============================================================================
 * FILE:        lora_sx1262.cpp
 * AUTHOR:      AbedX69
 * CREATED:     2026-02-14
 * VERSION:     1.0.0
 * =============================================================================
 */

#include "lora_sx1262.h"

static const char* TAG = "LoRaSX1262";

/* =============================================================================
 * SINGLETON
 * ========================================================================== */

LoRaSX1262& LoRaSX1262::instance() {
    static LoRaSX1262 inst;
    return inst;
}

LoRaSX1262::LoRaSX1262()
    : _initialized(false)
    , _spi(nullptr)
    , _irq_task(nullptr)
    , _receiving(false)
    , _rx_cb(nullptr)
    , _tx_done_cb(nullptr)
{
    _spi_mutex = xSemaphoreCreateMutex();
    _tx_done_sem = xSemaphoreCreateBinary();
    _irq_sem = xSemaphoreCreateBinary();
}

LoRaSX1262::~LoRaSX1262() {
    end();
    if (_spi_mutex) vSemaphoreDelete(_spi_mutex);
    if (_tx_done_sem) vSemaphoreDelete(_tx_done_sem);
    if (_irq_sem) vSemaphoreDelete(_irq_sem);
}

/* =============================================================================
 * SPI COMMUNICATION
 * =============================================================================
 * 
 * SX1262 uses an opcode-based SPI protocol:
 *   - Every transaction starts with an opcode byte
 *   - BUSY pin must be LOW before any SPI transaction
 *   - First response byte is always the status byte
 *   - NSS must be toggled per transaction
 * ========================================================================== */

void LoRaSX1262::waitBusy() {
    uint32_t start = xTaskGetTickCount();
    while (gpio_get_level((gpio_num_t)_pins.busy)) {
        if ((xTaskGetTickCount() - start) > pdMS_TO_TICKS(1000)) {
            ESP_LOGE(TAG, "BUSY timeout!");
            return;
        }
        vTaskDelay(1);
    }
}

void LoRaSX1262::spiWrite(uint8_t cmd, const uint8_t* data, uint16_t len) {
    waitBusy();
    xSemaphoreTake(_spi_mutex, portMAX_DELAY);

    uint8_t tx_buf[1 + 256];
    tx_buf[0] = cmd;
    if (data && len > 0) {
        memcpy(&tx_buf[1], data, len);
    }

    spi_transaction_t t = {};
    t.length = (1 + len) * 8;
    t.tx_buffer = tx_buf;
    spi_device_transmit(_spi, &t);

    xSemaphoreGive(_spi_mutex);
}

void LoRaSX1262::spiRead(uint8_t cmd, uint8_t* data, uint16_t len) {
    waitBusy();
    xSemaphoreTake(_spi_mutex, portMAX_DELAY);

    /* For reads: send cmd + NOP byte, then read data.
     * Total bytes = 1 (cmd) + 1 (status/NOP) + len */
    uint16_t total = 2 + len;
    uint8_t tx_buf[258] = {};
    uint8_t rx_buf[258] = {};
    tx_buf[0] = cmd;

    spi_transaction_t t = {};
    t.length = total * 8;
    t.tx_buffer = tx_buf;
    t.rx_buffer = rx_buf;
    spi_device_transmit(_spi, &t);

    /* Data starts at index 2 (after cmd echo + status) */
    if (data) memcpy(data, &rx_buf[2], len);

    xSemaphoreGive(_spi_mutex);
}

void LoRaSX1262::writeBuffer(uint8_t offset, const uint8_t* data, uint8_t len) {
    waitBusy();
    xSemaphoreTake(_spi_mutex, portMAX_DELAY);

    uint8_t tx_buf[2 + LORA_MAX_PAYLOAD];
    tx_buf[0] = SX1262_CMD_WRITE_BUFFER;
    tx_buf[1] = offset;
    memcpy(&tx_buf[2], data, len);

    spi_transaction_t t = {};
    t.length = (2 + len) * 8;
    t.tx_buffer = tx_buf;
    spi_device_transmit(_spi, &t);

    xSemaphoreGive(_spi_mutex);
}

void LoRaSX1262::readBuffer(uint8_t offset, uint8_t* data, uint8_t len) {
    waitBusy();
    xSemaphoreTake(_spi_mutex, portMAX_DELAY);

    /* WriteBuffer: [cmd, offset, data...]
     * ReadBuffer:  [cmd, offset, NOP, data...] */
    uint16_t total = 3 + len;
    uint8_t tx_buf[258] = {};
    uint8_t rx_buf[258] = {};
    tx_buf[0] = SX1262_CMD_READ_BUFFER;
    tx_buf[1] = offset;

    spi_transaction_t t = {};
    t.length = total * 8;
    t.tx_buffer = tx_buf;
    t.rx_buffer = rx_buf;
    spi_device_transmit(_spi, &t);

    memcpy(data, &rx_buf[3], len);
    xSemaphoreGive(_spi_mutex);
}

void LoRaSX1262::writeRegister(uint16_t addr, const uint8_t* data, uint8_t len) {
    waitBusy();
    xSemaphoreTake(_spi_mutex, portMAX_DELAY);

    uint8_t tx_buf[3 + 16];
    tx_buf[0] = SX1262_CMD_WRITE_REGISTER;
    tx_buf[1] = (addr >> 8) & 0xFF;
    tx_buf[2] = addr & 0xFF;
    memcpy(&tx_buf[3], data, len);

    spi_transaction_t t = {};
    t.length = (3 + len) * 8;
    t.tx_buffer = tx_buf;
    spi_device_transmit(_spi, &t);

    xSemaphoreGive(_spi_mutex);
}

void LoRaSX1262::readRegister(uint16_t addr, uint8_t* data, uint8_t len) {
    waitBusy();
    xSemaphoreTake(_spi_mutex, portMAX_DELAY);

    uint16_t total = 4 + len;  // cmd + addr(2) + NOP + data
    uint8_t tx_buf[258] = {};
    uint8_t rx_buf[258] = {};
    tx_buf[0] = SX1262_CMD_READ_REGISTER;
    tx_buf[1] = (addr >> 8) & 0xFF;
    tx_buf[2] = addr & 0xFF;

    spi_transaction_t t = {};
    t.length = total * 8;
    t.tx_buffer = tx_buf;
    t.rx_buffer = rx_buf;
    spi_device_transmit(_spi, &t);

    memcpy(data, &rx_buf[4], len);
    xSemaphoreGive(_spi_mutex);
}

/* =============================================================================
 * SX1262 COMMANDS
 * ========================================================================== */

void LoRaSX1262::reset() {
    gpio_set_level((gpio_num_t)_pins.reset, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level((gpio_num_t)_pins.reset, 1);
    vTaskDelay(pdMS_TO_TICKS(20));
    waitBusy();
}

void LoRaSX1262::setStandby(uint8_t mode) {
    spiWrite(SX1262_CMD_SET_STANDBY, &mode, 1);
}

void LoRaSX1262::setSleep(uint8_t config) {
    spiWrite(SX1262_CMD_SET_SLEEP, &config, 1);
}

void LoRaSX1262::setPacketType(uint8_t type) {
    spiWrite(SX1262_CMD_SET_PKT_TYPE, &type, 1);
}

void LoRaSX1262::setRfFrequency(uint32_t freq_hz) {
    /* freq_reg = freq_hz * 2^25 / 32MHz */
    uint32_t freq_reg = (uint32_t)((double)freq_hz / (double)(1 << 25) * (double)(1 << 25));
    freq_reg = (uint32_t)((uint64_t)freq_hz * (1 << 25) / 32000000ULL);

    uint8_t params[4];
    params[0] = (freq_reg >> 24) & 0xFF;
    params[1] = (freq_reg >> 16) & 0xFF;
    params[2] = (freq_reg >> 8) & 0xFF;
    params[3] = freq_reg & 0xFF;
    spiWrite(SX1262_CMD_SET_RF_FREQUENCY, params, 4);
}

void LoRaSX1262::setPaConfig(int8_t power_dbm) {
    /* SX1262 PA config for high power (+22 dBm max) */
    uint8_t params[4];
    params[0] = 0x04;  // paDutyCycle
    params[1] = 0x07;  // hpMax (SX1262 always 0x07)
    params[2] = 0x00;  // deviceSel (0x00 = SX1262)
    params[3] = 0x01;  // paLut (always 0x01)
    spiWrite(SX1262_CMD_SET_PA_CONFIG, params, 4);
}

void LoRaSX1262::setTxParams(int8_t power, uint8_t ramp_time) {
    uint8_t params[2];
    params[0] = (uint8_t)power;
    params[1] = ramp_time;  // 0x04 = 200us (recommended)
    spiWrite(SX1262_CMD_SET_TX_PARAMS, params, 2);
}

void LoRaSX1262::setModulationParams(uint8_t sf, uint8_t bw, uint8_t cr, uint8_t ldro) {
    uint8_t params[4] = {sf, bw, cr, ldro};
    spiWrite(SX1262_CMD_SET_MOD_PARAMS, params, 4);
}

void LoRaSX1262::setPacketParams(uint16_t preamble, uint8_t header_type,
                                   uint8_t payload_len, uint8_t crc_type, uint8_t invert_iq) {
    uint8_t params[6];
    params[0] = (preamble >> 8) & 0xFF;
    params[1] = preamble & 0xFF;
    params[2] = header_type;    // 0=explicit, 1=implicit
    params[3] = payload_len;
    params[4] = crc_type;       // 0=off, 1=on
    params[5] = invert_iq;      // 0=standard, 1=inverted
    spiWrite(SX1262_CMD_SET_PKT_PARAMS, params, 6);
}

void LoRaSX1262::setBufferBaseAddress(uint8_t tx_base, uint8_t rx_base) {
    uint8_t params[2] = {tx_base, rx_base};
    spiWrite(SX1262_CMD_SET_BUFFER_BASE_ADDR, params, 2);
}

void LoRaSX1262::setDioIrqParams(uint16_t irq_mask, uint16_t dio1_mask,
                                    uint16_t dio2_mask, uint16_t dio3_mask) {
    uint8_t params[8];
    params[0] = (irq_mask >> 8) & 0xFF;
    params[1] = irq_mask & 0xFF;
    params[2] = (dio1_mask >> 8) & 0xFF;
    params[3] = dio1_mask & 0xFF;
    params[4] = (dio2_mask >> 8) & 0xFF;
    params[5] = dio2_mask & 0xFF;
    params[6] = (dio3_mask >> 8) & 0xFF;
    params[7] = dio3_mask & 0xFF;
    spiWrite(SX1262_CMD_SET_DIO_IRQ_PARAMS, params, 8);
}

uint16_t LoRaSX1262::getIrqStatus() {
    uint8_t data[2] = {};
    spiRead(SX1262_CMD_GET_IRQ_STATUS, data, 2);
    return ((uint16_t)data[0] << 8) | data[1];
}

void LoRaSX1262::clearIrqStatus(uint16_t mask) {
    uint8_t params[2] = {(uint8_t)(mask >> 8), (uint8_t)(mask & 0xFF)};
    spiWrite(SX1262_CMD_CLR_IRQ_STATUS, params, 2);
}

void LoRaSX1262::setRegulatorMode(uint8_t mode) {
    spiWrite(SX1262_CMD_SET_REGULATOR_MODE, &mode, 1);
}

void LoRaSX1262::setDio2AsRfSwitch(bool enable) {
    uint8_t val = enable ? 1 : 0;
    spiWrite(SX1262_CMD_SET_DIO2_AS_RF_SW, &val, 1);
}

void LoRaSX1262::calibrateImage(uint32_t freq_hz) {
    uint8_t params[2];
    if (freq_hz >= 902000000) {
        params[0] = 0xE1; params[1] = 0xE9;  // 902-928 MHz
    } else if (freq_hz >= 863000000) {
        params[0] = 0xD7; params[1] = 0xDB;  // 863-870 MHz
    } else if (freq_hz >= 779000000) {
        params[0] = 0xC1; params[1] = 0xC5;  // 779-787 MHz
    } else if (freq_hz >= 470000000) {
        params[0] = 0x75; params[1] = 0x81;  // 470-510 MHz
    } else {
        params[0] = 0x6B; params[1] = 0x6F;  // 430-440 MHz
    }
    spiWrite(SX1262_CMD_CALIBRATE_IMAGE, params, 2);
}

void LoRaSX1262::setSyncWord(uint8_t sync) {
    /* LoRa sync word is at register 0x0740 (MSB) and 0x0741 (LSB) */
    uint8_t msb = (sync & 0xF0) | 0x04;
    uint8_t lsb = ((sync & 0x0F) << 4) | 0x04;
    writeRegister(0x0740, &msb, 1);
    writeRegister(0x0741, &lsb, 1);
}

void LoRaSX1262::fixInvertedIQ() {
    /* Workaround for SX1262 IQ polarity bug (datasheet errata) */
    uint8_t val;
    readRegister(0x0736, &val, 1);
    val |= 0x04;  // Set bit 2
    writeRegister(0x0736, &val, 1);
}

/* =============================================================================
 * DIO1 INTERRUPT HANDLING
 * =============================================================================
 * 
 * DIO1 fires on configurable events (TX done, RX done, timeout, etc.).
 * The ISR gives a semaphore that unblocks the IRQ processing task,
 * which reads the IRQ status and dispatches to callbacks.
 * 
 * We NEVER do SPI in the ISR — all SPI happens in the task context.
 * ========================================================================== */

void IRAM_ATTR LoRaSX1262::dio1ISR(void* arg) {
    LoRaSX1262* self = static_cast<LoRaSX1262*>(arg);
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(self->_irq_sem, &woken);
    if (woken) portYIELD_FROM_ISR();
}

void LoRaSX1262::irqTaskFunc(void* arg) {
    LoRaSX1262* self = static_cast<LoRaSX1262*>(arg);
    while (true) {
        if (xSemaphoreTake(self->_irq_sem, portMAX_DELAY) == pdTRUE) {
            self->handleIrq();
        }
    }
}

void LoRaSX1262::handleIrq() {
    uint16_t irq = getIrqStatus();
    clearIrqStatus(SX1262_IRQ_ALL);

    if (irq & SX1262_IRQ_TX_DONE) {
        ESP_LOGD(TAG, "TX done");
        xSemaphoreGive(_tx_done_sem);
        if (_tx_done_cb) _tx_done_cb();
    }

    if (irq & SX1262_IRQ_RX_DONE) {
        /* Read RX buffer status: payload length + start offset */
        uint8_t status[2] = {};
        spiRead(SX1262_CMD_GET_RX_BUFFER_STATUS, status, 2);
        uint8_t payload_len = status[0];
        uint8_t rx_start = status[1];

        if (payload_len > 0 && payload_len <= LORA_MAX_PAYLOAD) {
            LoRaRxPacket pkt = {};
            pkt.length = payload_len;
            readBuffer(rx_start, pkt.data, payload_len);

            /* Get packet status: RSSI, SNR */
            uint8_t pkt_status[3] = {};
            spiRead(SX1262_CMD_GET_PKT_STATUS, pkt_status, 3);
            pkt.rssi = -(int16_t)(pkt_status[0] / 2);
            pkt.snr = (int8_t)pkt_status[1] / 4;

            ESP_LOGI(TAG, "RX: %d bytes, RSSI=%d dBm, SNR=%d dB",
                     pkt.length, pkt.rssi, pkt.snr);

            if (_rx_cb) _rx_cb(&pkt);
        }

        /* Re-enter RX if continuous mode */
        if (_receiving) {
            uint8_t rx_params[3] = {0xFF, 0xFF, 0xFF};  // Continuous
            spiWrite(SX1262_CMD_SET_RX, rx_params, 3);
        }
    }

    if (irq & SX1262_IRQ_CRC_ERR) {
        ESP_LOGW(TAG, "CRC error on received packet");
    }

    if (irq & SX1262_IRQ_TIMEOUT) {
        ESP_LOGD(TAG, "RX/TX timeout");
        xSemaphoreGive(_tx_done_sem);  // Unblock send() on timeout
    }
}

/* =============================================================================
 * LIFECYCLE
 * ========================================================================== */

esp_err_t LoRaSX1262::begin(const LoRaPins& pins, const LoRaConfig& config) {
    if (_initialized) return ESP_OK;

    _pins = pins;
    _config = config;

    /* ── Configure GPIO pins ───────────────────────────────────────── */
    gpio_config_t io_conf = {};

    /* NSS, RESET: output */
    io_conf.pin_bit_mask = (1ULL << _pins.nss) | (1ULL << _pins.reset);
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    gpio_config(&io_conf);

    gpio_set_level((gpio_num_t)_pins.nss, 1);
    gpio_set_level((gpio_num_t)_pins.reset, 1);

    /* BUSY: input */
    io_conf.pin_bit_mask = (1ULL << _pins.busy);
    io_conf.mode = GPIO_MODE_INPUT;
    gpio_config(&io_conf);

    /* DIO1: input with interrupt */
    io_conf.pin_bit_mask = (1ULL << _pins.dio1);
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.intr_type = GPIO_INTR_POSEDGE;
    gpio_config(&io_conf);

    /* ── Initialize SPI bus ────────────────────────────────────────── */
    spi_bus_config_t bus_cfg = {};
    bus_cfg.mosi_io_num = _pins.mosi;
    bus_cfg.miso_io_num = _pins.miso;
    bus_cfg.sclk_io_num = _pins.sck;
    bus_cfg.quadwp_io_num = -1;
    bus_cfg.quadhd_io_num = -1;
    bus_cfg.max_transfer_sz = 512;

    esp_err_t ret = spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI bus init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    spi_device_interface_config_t dev_cfg = {};
    dev_cfg.clock_speed_hz = 8000000;   // 8 MHz (SX1262 supports up to 16 MHz)
    dev_cfg.mode = 0;                    // CPOL=0, CPHA=0
    dev_cfg.spics_io_num = _pins.nss;
    dev_cfg.queue_size = 4;

    ret = spi_bus_add_device(SPI2_HOST, &dev_cfg, &_spi);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI device add failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* ── Create IRQ handler task ───────────────────────────────────── */
    xTaskCreate(irqTaskFunc, "lora_irq", 4096, this, 10, &_irq_task);

    gpio_install_isr_service(0);
    gpio_isr_handler_add((gpio_num_t)_pins.dio1, dio1ISR, this);

    /* ── Configure SX1262 ──────────────────────────────────────────── */
    reset();
    setStandby(0x00);  // STDBY_RC

    /* Regulator: DC-DC or LDO */
    setRegulatorMode(_config.use_dcdc ? 0x01 : 0x00);

    /* DIO2 as RF switch control (most modules need this) */
    if (_config.use_dio2_rf_sw) {
        setDio2AsRfSwitch(true);
    }

    /* Set to LoRa mode */
    setPacketType(0x01);

    /* Calibrate image for the operating frequency */
    calibrateImage(_config.frequency);

    /* Set frequency */
    setRfFrequency(_config.frequency);

    /* PA config + TX power */
    setPaConfig(_config.tx_power);
    setTxParams(_config.tx_power, 0x04);  // 200us ramp

    /* Modulation parameters */
    uint8_t ldro = 0;
    /* Low Data Rate Optimization: required for SF11/SF12 at 125kHz */
    if (_config.spreading_factor >= 11 && _config.bandwidth <= 7) {
        ldro = 1;
    }
    setModulationParams(_config.spreading_factor, _config.bandwidth,
                         _config.coding_rate, ldro);

    /* Packet parameters */
    setPacketParams(_config.preamble_length,
                     _config.implicit_header ? 0x01 : 0x00,
                     LORA_MAX_PAYLOAD,
                     _config.crc_on ? 0x01 : 0x00,
                     0x00);  // Standard IQ

    /* Buffer base addresses: TX at 0, RX at 128 */
    setBufferBaseAddress(0x00, 0x80);

    /* Sync word */
    setSyncWord(_config.sync_word);

    /* Fix IQ polarity bug */
    fixInvertedIQ();

    /* Configure DIO1 to fire on TX_DONE, RX_DONE, CRC_ERR, TIMEOUT */
    uint16_t irq_mask = SX1262_IRQ_TX_DONE | SX1262_IRQ_RX_DONE |
                         SX1262_IRQ_CRC_ERR | SX1262_IRQ_TIMEOUT;
    setDioIrqParams(irq_mask, irq_mask, 0x0000, 0x0000);

    clearIrqStatus(SX1262_IRQ_ALL);

    _initialized = true;

    ESP_LOGI(TAG, "═══════════════════════════════════════════");
    ESP_LOGI(TAG, "  LoRa SX1262 initialized");
    ESP_LOGI(TAG, "  Freq:   %.2f MHz", _config.frequency / 1e6);
    ESP_LOGI(TAG, "  SF:     %d", _config.spreading_factor);
    ESP_LOGI(TAG, "  BW:     idx %d", _config.bandwidth);
    ESP_LOGI(TAG, "  TX pwr: %d dBm", _config.tx_power);
    ESP_LOGI(TAG, "  Pins:   NSS=%d RST=%d BUSY=%d DIO1=%d",
             _pins.nss, _pins.reset, _pins.busy, _pins.dio1);
    ESP_LOGI(TAG, "═══════════════════════════════════════════");

    return ESP_OK;
}

esp_err_t LoRaSX1262::end() {
    if (!_initialized) return ESP_OK;

    stopReceive();
    setStandby(0x00);

    if (_irq_task) {
        vTaskDelete(_irq_task);
        _irq_task = nullptr;
    }

    gpio_isr_handler_remove((gpio_num_t)_pins.dio1);

    if (_spi) {
        spi_bus_remove_device(_spi);
        _spi = nullptr;
    }
    spi_bus_free(SPI2_HOST);

    _initialized = false;
    ESP_LOGI(TAG, "LoRa SX1262 stopped");
    return ESP_OK;
}

bool LoRaSX1262::isReady() const { return _initialized; }

/* =============================================================================
 * TRANSMIT
 * ========================================================================== */

esp_err_t LoRaSX1262::send(const uint8_t* data, uint8_t length, uint32_t timeout_ms) {
    if (!_initialized || !data || length == 0) return ESP_ERR_INVALID_ARG;
    if (length > LORA_MAX_PAYLOAD) return ESP_ERR_INVALID_SIZE;

    /* Go to standby before configuring TX */
    setStandby(0x00);

    /* Update packet params with actual payload length */
    setPacketParams(_config.preamble_length,
                     _config.implicit_header ? 0x01 : 0x00,
                     length,
                     _config.crc_on ? 0x01 : 0x00,
                     0x00);

    /* Write payload to TX buffer */
    writeBuffer(0x00, data, length);

    clearIrqStatus(SX1262_IRQ_ALL);

    /* Calculate TX timeout in RTC steps (15.625 us each) */
    uint32_t timeout_rtc = 0;
    if (timeout_ms > 0) {
        timeout_rtc = (uint32_t)((uint64_t)timeout_ms * 64);  // ms * 64 ≈ ms / 0.015625
    }

    /* Start TX */
    uint8_t params[3];
    params[0] = (timeout_rtc >> 16) & 0xFF;
    params[1] = (timeout_rtc >> 8) & 0xFF;
    params[2] = timeout_rtc & 0xFF;
    spiWrite(SX1262_CMD_SET_TX, params, 3);

    ESP_LOGD(TAG, "TX: %d bytes", length);

    /* Wait for TX done (via DIO1 interrupt) */
    if (xSemaphoreTake(_tx_done_sem, pdMS_TO_TICKS(timeout_ms + 1000)) != pdTRUE) {
        ESP_LOGE(TAG, "TX timeout");
        setStandby(0x00);
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

/* =============================================================================
 * RECEIVE
 * ========================================================================== */

esp_err_t LoRaSX1262::startReceive() {
    if (!_initialized) return ESP_ERR_INVALID_STATE;

    setStandby(0x00);

    /* Set packet params for max payload (explicit header will tell actual size) */
    setPacketParams(_config.preamble_length,
                     _config.implicit_header ? 0x01 : 0x00,
                     LORA_MAX_PAYLOAD,
                     _config.crc_on ? 0x01 : 0x00,
                     0x00);

    clearIrqStatus(SX1262_IRQ_ALL);

    _receiving = true;

    /* Enter continuous RX: timeout = 0xFFFFFF */
    uint8_t params[3] = {0xFF, 0xFF, 0xFF};
    spiWrite(SX1262_CMD_SET_RX, params, 3);

    ESP_LOGI(TAG, "Continuous RX started");
    return ESP_OK;
}

esp_err_t LoRaSX1262::receiveOnce(uint32_t timeout_ms) {
    if (!_initialized) return ESP_ERR_INVALID_STATE;

    setStandby(0x00);

    setPacketParams(_config.preamble_length,
                     _config.implicit_header ? 0x01 : 0x00,
                     LORA_MAX_PAYLOAD,
                     _config.crc_on ? 0x01 : 0x00,
                     0x00);

    clearIrqStatus(SX1262_IRQ_ALL);

    _receiving = false;  // Single shot - don't re-enter RX

    uint32_t timeout_rtc = (timeout_ms > 0) ? (uint32_t)((uint64_t)timeout_ms * 64) : 0;
    uint8_t params[3];
    params[0] = (timeout_rtc >> 16) & 0xFF;
    params[1] = (timeout_rtc >> 8) & 0xFF;
    params[2] = timeout_rtc & 0xFF;
    spiWrite(SX1262_CMD_SET_RX, params, 3);

    return ESP_OK;
}

esp_err_t LoRaSX1262::stopReceive() {
    _receiving = false;
    setStandby(0x00);
    return ESP_OK;
}

/* =============================================================================
 * RUNTIME CONFIGURATION
 * ========================================================================== */

esp_err_t LoRaSX1262::setFrequency(uint32_t freq_hz) {
    _config.frequency = freq_hz;
    setStandby(0x00);
    calibrateImage(freq_hz);
    setRfFrequency(freq_hz);
    return ESP_OK;
}

esp_err_t LoRaSX1262::setTxPower(int8_t power_dbm) {
    if (power_dbm < -9) power_dbm = -9;
    if (power_dbm > 22) power_dbm = 22;
    _config.tx_power = power_dbm;
    setPaConfig(power_dbm);
    setTxParams(power_dbm, 0x04);
    return ESP_OK;
}

esp_err_t LoRaSX1262::setSpreadingFactor(uint8_t sf) {
    if (sf < 7) sf = 7;
    if (sf > 12) sf = 12;
    _config.spreading_factor = sf;
    uint8_t ldro = (sf >= 11 && _config.bandwidth <= 7) ? 1 : 0;
    setModulationParams(sf, _config.bandwidth, _config.coding_rate, ldro);
    return ESP_OK;
}

esp_err_t LoRaSX1262::reconfigure(const LoRaConfig& config) {
    _config = config;
    setStandby(0x00);
    setRegulatorMode(config.use_dcdc ? 0x01 : 0x00);
    if (config.use_dio2_rf_sw) setDio2AsRfSwitch(true);
    calibrateImage(config.frequency);
    setRfFrequency(config.frequency);
    setPaConfig(config.tx_power);
    setTxParams(config.tx_power, 0x04);
    uint8_t ldro = (config.spreading_factor >= 11 && config.bandwidth <= 7) ? 1 : 0;
    setModulationParams(config.spreading_factor, config.bandwidth,
                         config.coding_rate, ldro);
    setPacketParams(config.preamble_length,
                     config.implicit_header ? 0x01 : 0x00,
                     LORA_MAX_PAYLOAD,
                     config.crc_on ? 0x01 : 0x00, 0x00);
    setSyncWord(config.sync_word);
    fixInvertedIQ();
    return ESP_OK;
}

int16_t LoRaSX1262::getRSSI() {
    uint8_t data[1] = {};
    spiRead(SX1262_CMD_GET_RSSI_INST, data, 1);
    return -(int16_t)(data[0] / 2);
}

const LoRaConfig& LoRaSX1262::getConfig() const { return _config; }

void LoRaSX1262::setRxCallback(LoRaRxCb cb) { _rx_cb = cb; }
void LoRaSX1262::setTxDoneCallback(LoRaTxDoneCb cb) { _tx_done_cb = cb; }
