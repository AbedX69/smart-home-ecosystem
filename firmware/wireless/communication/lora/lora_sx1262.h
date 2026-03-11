/*
 * =============================================================================
 * FILE:        lora_sx1262.h
 * AUTHOR:      AbedX69
 * CREATED:     2026-02-14
 * VERSION:     1.0.0
 * LICENSE:     MIT
 * PLATFORM:    ESP32-S3 / ESP32-C6 (ESP-IDF v5.x) + SX1262
 * =============================================================================
 * 
 * LoRa SX1262 Driver - Direct SPI communication with Semtech SX1262.
 * 
 * Provides:
 *   - Raw SX1262 opcode-based SPI communication
 *   - LoRa TX/RX with configurable parameters
 *   - Point-to-point messaging
 *   - Gateway mode (continuous RX)
 *   - Sensor broadcast mode
 *   - RSSI/SNR reporting
 *   - DIO1 interrupt-driven RX
 * 
 * =============================================================================
 * BEGINNER'S GUIDE: LoRa
 * =============================================================================
 * 
 * WHAT IS LoRa?
 * ~~~~~~~~~~~~~
 * LoRa (Long Range) is a wireless technology using chirp spread spectrum
 * modulation. It trades data rate for extreme range and penetration.
 * 
 *     ┌─────────────────────────────────────────────────────────┐
 *     │              │  LoRa        │  WiFi        │  BLE       │
 *     ├─────────────────────────────────────────────────────────┤
 *     │  Range       │  2-15 km     │  50-100m     │  10-50m    │
 *     │  Data rate   │  0.3-50 kbps │  54+ Mbps    │  1-2 Mbps  │
 *     │  Power       │  Very low    │  High        │  Low       │
 *     │  Frequency   │  868/915 MHz │  2.4/5 GHz   │  2.4 GHz   │
 *     │  Best for    │  Sensors     │  Data/video  │  Wearables │
 *     └─────────────────────────────────────────────────────────┘
 * 
 * Perfect for: outdoor sensors, remote monitoring, farm/garden automation,
 * door/window sensors in large buildings, mailbox alerts.
 * 
 * 
 * KEY PARAMETERS:
 * ~~~~~~~~~~~~~~~
 * 
 * Spreading Factor (SF7-SF12):
 *   Higher SF = longer range but slower speed.
 *   SF7:  fastest, shortest range  (~2km)
 *   SF12: slowest, longest range   (~15km)
 * 
 * Bandwidth (BW):
 *   125 kHz: Standard, good sensitivity
 *   250 kHz: Faster, less range
 *   500 kHz: Fastest, shortest range
 * 
 * Coding Rate (CR 4/5 to 4/8):
 *   More redundancy = better noise immunity, slower
 * 
 * TX Power:
 *   SX1262 can output up to +22 dBm (about 160 mW)
 * 
 * 
 * SX1262 vs SX1276:
 * ~~~~~~~~~~~~~~~~~~
 *   SX1262: New generation. Command-based SPI (opcodes).
 *           Lower RX current (4.2mA vs 10.3mA).
 *           +22 dBm TX. Better blocking immunity.
 *   SX1276: Old generation. Register-based SPI.
 *           Still widely used. Max +17 dBm TX.
 * 
 * 
 * =============================================================================
 * HARDWARE: XIAO ESP32-S3 + Wio-SX1262 (B2B connector)
 * =============================================================================
 * 
 *     XIAO ESP32-S3              Wio-SX1262
 *     ┌─────────────┐           ┌──────────────┐
 *     │         GPIO7│── SCK  ──│SCK           │
 *     │         GPIO9│── MOSI ──│MOSI          │
 *     │         GPIO8│── MISO ──│MISO          │
 *     │        GPIO41│── NSS  ──│NSS (CS)      │
 *     │         GPIO3│── RST  ──│NRESET        │
 *     │         GPIO4│── BUSY ──│BUSY          │
 *     │         GPIO2│── DIO1 ──│DIO1 (IRQ)    │
 *     │              │          │         ANT──│── Antenna
 *     └──────────────┘          └──────────────┘
 *       (via B2B connector - no wiring needed)
 * 
 * NOTE: Pin numbers differ between B2B connector and edge pins!
 *       The above is for the B2B kit version.
 * 
 * =============================================================================
 * USAGE EXAMPLES
 * =============================================================================
 * 
 * TRANSMIT:
 *     LoRaSX1262& lora = LoRaSX1262::instance();
 *     lora.begin();
 *     lora.send((uint8_t*)"Hello LoRa!", 11);
 * 
 * RECEIVE (interrupt-driven):
 *     lora.setRxCallback(onPacketReceived);
 *     lora.startReceive();  // Continuous RX
 * 
 * GATEWAY MODE:
 *     lora.begin();
 *     lora.setRxCallback(onSensorData);
 *     lora.startReceive();  // Always listening
 * 
 * =============================================================================
 */

#ifndef LORA_SX1262_H
#define LORA_SX1262_H

#include <cstdint>
#include <cstring>
#include <functional>

#include "esp_err.h"
#include "esp_log.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

/* ─── SX1262 SPI Opcodes ─────────────────────────────────────────────────── */
#define SX1262_CMD_SET_SLEEP            0x84
#define SX1262_CMD_SET_STANDBY          0x80
#define SX1262_CMD_SET_FS               0xC1
#define SX1262_CMD_SET_TX               0x83
#define SX1262_CMD_SET_RX               0x82
#define SX1262_CMD_SET_RX_DUTY_CYCLE    0x94
#define SX1262_CMD_SET_CAD              0xC5
#define SX1262_CMD_SET_TX_CONT_WAVE     0xD1
#define SX1262_CMD_SET_TX_CONT_PREAMBLE 0xD2
#define SX1262_CMD_SET_REGULATOR_MODE   0x96
#define SX1262_CMD_CALIBRATE            0x89
#define SX1262_CMD_CALIBRATE_IMAGE      0x98
#define SX1262_CMD_SET_PA_CONFIG        0x95
#define SX1262_CMD_SET_FALLBACK_MODE    0x93

#define SX1262_CMD_SET_DIO_IRQ_PARAMS   0x08
#define SX1262_CMD_GET_IRQ_STATUS       0x12
#define SX1262_CMD_CLR_IRQ_STATUS       0x02
#define SX1262_CMD_SET_DIO2_AS_RF_SW    0x9D
#define SX1262_CMD_SET_DIO3_AS_TCXO     0x97

#define SX1262_CMD_SET_RF_FREQUENCY     0x86
#define SX1262_CMD_SET_PKT_TYPE         0x8A
#define SX1262_CMD_GET_PKT_TYPE         0x11
#define SX1262_CMD_SET_TX_PARAMS        0x8E
#define SX1262_CMD_SET_MOD_PARAMS       0x8B
#define SX1262_CMD_SET_PKT_PARAMS       0x8C
#define SX1262_CMD_SET_CAD_PARAMS       0x88
#define SX1262_CMD_SET_BUFFER_BASE_ADDR 0x8F

#define SX1262_CMD_GET_STATUS           0xC0
#define SX1262_CMD_GET_RX_BUFFER_STATUS 0x13
#define SX1262_CMD_GET_PKT_STATUS       0x14
#define SX1262_CMD_GET_RSSI_INST        0x15
#define SX1262_CMD_GET_STATS            0x10
#define SX1262_CMD_RESET_STATS          0x00

#define SX1262_CMD_WRITE_REGISTER       0x0D
#define SX1262_CMD_READ_REGISTER        0x1D
#define SX1262_CMD_WRITE_BUFFER         0x0E
#define SX1262_CMD_READ_BUFFER          0x1E

#define SX1262_CMD_GET_DEVICE_ERRORS    0x17
#define SX1262_CMD_CLR_DEVICE_ERRORS    0x07

/* ─── IRQ Flags ──────────────────────────────────────────────────────────── */
#define SX1262_IRQ_TX_DONE              0x0001
#define SX1262_IRQ_RX_DONE              0x0002
#define SX1262_IRQ_PREAMBLE_DETECTED    0x0004
#define SX1262_IRQ_SYNC_WORD_VALID      0x0008
#define SX1262_IRQ_HEADER_VALID         0x0010
#define SX1262_IRQ_HEADER_ERR           0x0020
#define SX1262_IRQ_CRC_ERR              0x0040
#define SX1262_IRQ_CAD_DONE             0x0080
#define SX1262_IRQ_CAD_DETECTED         0x0100
#define SX1262_IRQ_TIMEOUT              0x0200
#define SX1262_IRQ_ALL                  0x03FF

/* ─── Constants ──────────────────────────────────────────────────────────── */
#define LORA_MAX_PAYLOAD    255

/* ─── Pin Configuration ──────────────────────────────────────────────────── */

struct LoRaPins {
    int sck;
    int mosi;
    int miso;
    int nss;        ///< Chip select
    int reset;      ///< NRESET
    int busy;       ///< BUSY pin
    int dio1;       ///< DIO1 (interrupt)
};

/* Predefined pin configs for known boards */
namespace LoRaPinPresets {
    /* XIAO ESP32-S3 + Wio-SX1262 (B2B connector / kit version) */
    static constexpr LoRaPins XIAO_S3_WIO_B2B = {
        .sck   = 7,
        .mosi  = 9,
        .miso  = 8,
        .nss   = 41,
        .reset = 3,
        .busy  = 4,
        .dio1  = 2
    };

    /* XIAO ESP32-S3 + Wio-SX1262 (edge pin / non-kit version) */
    static constexpr LoRaPins XIAO_S3_WIO_EDGE = {
        .sck   = 7,
        .mosi  = 9,
        .miso  = 8,
        .nss   = 4,     // D2
        .reset = 3,     // D1
        .busy  = 2,     // D0
        .dio1  = 1      // D0 area
    };

    /* Generic: user must fill in pins */
    static constexpr LoRaPins CUSTOM = {
        .sck = -1, .mosi = -1, .miso = -1,
        .nss = -1, .reset = -1, .busy = -1, .dio1 = -1
    };
}

/* ─── LoRa Configuration ─────────────────────────────────────────────────── */

struct LoRaConfig {
    uint32_t    frequency       = 915000000;    ///< Hz (868000000 for EU, 915000000 for US)
    uint8_t     spreading_factor = 7;           ///< 7-12
    uint8_t     bandwidth       = 4;            ///< 0=7.8k 1=10.4k 2=15.6k 3=20.8k 4=31.25k
                                                ///< 5=41.7k 6=62.5k 7=125k 8=250k 9=500k
    uint8_t     coding_rate     = 1;            ///< 1=4/5 2=4/6 3=4/7 4=4/8
    int8_t      tx_power        = 22;           ///< dBm (-9 to +22)
    uint16_t    preamble_length = 8;
    bool        crc_on          = true;
    bool        implicit_header = false;        ///< false = explicit (includes length)
    uint8_t     sync_word       = 0x12;         ///< 0x12=private, 0x34=public (LoRaWAN)
    bool        use_dcdc        = true;         ///< DC-DC converter (lower power)
    bool        use_dio2_rf_sw  = true;         ///< DIO2 controls RF switch (common on modules)
};

/* ─── RX Packet Info ─────────────────────────────────────────────────────── */

struct LoRaRxPacket {
    uint8_t     data[LORA_MAX_PAYLOAD];
    uint8_t     length;
    int16_t     rssi;       ///< Received signal strength in dBm
    int8_t      snr;        ///< Signal-to-noise ratio in dB
};

/* ─── Callbacks ──────────────────────────────────────────────────────────── */

using LoRaRxCb = std::function<void(const LoRaRxPacket* packet)>;
using LoRaTxDoneCb = std::function<void()>;

/* ─── Main Class ─────────────────────────────────────────────────────────── */

class LoRaSX1262 {
public:
    static LoRaSX1262& instance();
    LoRaSX1262(const LoRaSX1262&) = delete;
    LoRaSX1262& operator=(const LoRaSX1262&) = delete;

    /* ─── Lifecycle ────────────────────────────────────────────────────── */

    /**
     * @brief Initialize SPI bus and configure the SX1262.
     * 
     * @param pins    Pin configuration (use a preset or custom)
     * @param config  LoRa radio parameters
     * @return ESP_OK on success
     */
    esp_err_t begin(const LoRaPins& pins = LoRaPinPresets::XIAO_S3_WIO_B2B,
                    const LoRaConfig& config = LoRaConfig{});

    /**
     * @brief Shut down the radio and release SPI.
     */
    esp_err_t end();

    bool isReady() const;

    /* ─── Transmit ─────────────────────────────────────────────────────── */

    /**
     * @brief Send a LoRa packet.
     * 
     * Blocking call - returns after TX complete or timeout.
     * 
     * @param data     Payload bytes
     * @param length   Payload length (max 255)
     * @param timeout_ms  TX timeout in ms (0 = no timeout)
     * @return ESP_OK on success
     */
    esp_err_t send(const uint8_t* data, uint8_t length, uint32_t timeout_ms = 5000);

    /* ─── Receive ──────────────────────────────────────────────────────── */

    /**
     * @brief Start continuous receive mode.
     * 
     * Packets are reported via the RX callback. The radio stays in
     * RX mode until stopReceive() is called.
     * 
     * @return ESP_OK on success
     */
    esp_err_t startReceive();

    /**
     * @brief Start single-shot receive with timeout.
     * 
     * @param timeout_ms  RX timeout in ms (0 = no timeout / single)
     */
    esp_err_t receiveOnce(uint32_t timeout_ms = 10000);

    /**
     * @brief Stop receiving and go to standby.
     */
    esp_err_t stopReceive();

    /* ─── Configuration ────────────────────────────────────────────────── */

    /**
     * @brief Change frequency without full re-init.
     */
    esp_err_t setFrequency(uint32_t freq_hz);

    /**
     * @brief Change TX power.
     * @param power_dbm  -9 to +22 dBm
     */
    esp_err_t setTxPower(int8_t power_dbm);

    /**
     * @brief Change spreading factor.
     * @param sf  7-12
     */
    esp_err_t setSpreadingFactor(uint8_t sf);

    /**
     * @brief Reconfigure all radio parameters.
     */
    esp_err_t reconfigure(const LoRaConfig& config);

    /* ─── Status ───────────────────────────────────────────────────────── */

    /** @brief Get current RSSI (while receiving) */
    int16_t getRSSI();

    /** @brief Get the config currently in use */
    const LoRaConfig& getConfig() const;

    /* ─── Callbacks ────────────────────────────────────────────────────── */

    void setRxCallback(LoRaRxCb cb);
    void setTxDoneCallback(LoRaTxDoneCb cb);

private:
    LoRaSX1262();
    ~LoRaSX1262();

    /* ─── SPI Communication ────────────────────────────────────────────── */
    void spiWrite(uint8_t cmd, const uint8_t* data, uint16_t len);
    void spiRead(uint8_t cmd, uint8_t* data, uint16_t len);
    void writeBuffer(uint8_t offset, const uint8_t* data, uint8_t len);
    void readBuffer(uint8_t offset, uint8_t* data, uint8_t len);
    void writeRegister(uint16_t addr, const uint8_t* data, uint8_t len);
    void readRegister(uint16_t addr, uint8_t* data, uint8_t len);

    /* ─── SX1262 Commands ──────────────────────────────────────────────── */
    void waitBusy();
    void reset();
    void setStandby(uint8_t mode = 0x00);  // 0=STDBY_RC, 1=STDBY_XOSC
    void setSleep(uint8_t config = 0x00);
    void setPacketType(uint8_t type);       // 0=FSK, 1=LoRa
    void setRfFrequency(uint32_t freq_hz);
    void setPaConfig(int8_t power_dbm);
    void setTxParams(int8_t power, uint8_t ramp_time);
    void setModulationParams(uint8_t sf, uint8_t bw, uint8_t cr, uint8_t ldro);
    void setPacketParams(uint16_t preamble, uint8_t header_type, uint8_t payload_len,
                          uint8_t crc_type, uint8_t invert_iq);
    void setBufferBaseAddress(uint8_t tx_base, uint8_t rx_base);
    void setDioIrqParams(uint16_t irq_mask, uint16_t dio1_mask,
                          uint16_t dio2_mask, uint16_t dio3_mask);
    uint16_t getIrqStatus();
    void clearIrqStatus(uint16_t mask);
    void setRegulatorMode(uint8_t mode);    // 0=LDO, 1=DC-DC
    void setDio2AsRfSwitch(bool enable);
    void calibrateImage(uint32_t freq_hz);
    void setSyncWord(uint8_t sync);
    void fixInvertedIQ();

    /* ─── DIO1 Interrupt ───────────────────────────────────────────────── */
    static void IRAM_ATTR dio1ISR(void* arg);
    static void irqTaskFunc(void* arg);
    void handleIrq();

    /* State */
    bool                _initialized;
    LoRaPins            _pins;
    LoRaConfig          _config;
    spi_device_handle_t _spi;
    SemaphoreHandle_t   _spi_mutex;
    SemaphoreHandle_t   _tx_done_sem;
    TaskHandle_t        _irq_task;
    SemaphoreHandle_t   _irq_sem;
    bool                _receiving;

    LoRaRxCb            _rx_cb;
    LoRaTxDoneCb        _tx_done_cb;
};

#endif // LORA_SX1262_H
