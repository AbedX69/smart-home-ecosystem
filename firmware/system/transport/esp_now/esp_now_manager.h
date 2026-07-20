/*
 * =============================================================================
 * FILE:        esp_now_manager.h
 * AUTHOR:      AbedX69
 * CREATED:     2026-02-12
 * MODIFIED:    2026-02-12
 * VERSION:     1.0.0
 * LICENSE:     MIT
 * PLATFORM:    ESP32 / ESP32-S3 / ESP32-C6 (ESP-IDF v5.x)
 * =============================================================================
 * 
 * ESP-NOW Manager - Peer-to-peer wireless communication without a router.
 * 
 * Wraps the ESP-IDF ESP-NOW API into a clean C++ class with peer management,
 * broadcast/unicast, send/receive callbacks, FreeRTOS queue-based message 
 * handling, and thread-safe operation.
 * 
 * =============================================================================
 * BEGINNER'S GUIDE: ESP-NOW
 * =============================================================================
 * 
 * WHAT IS ESP-NOW?
 * ~~~~~~~~~~~~~~~~
 * ESP-NOW is a wireless communication protocol created by Espressif (the folks
 * who make the ESP32). It lets two or more ESP32 boards talk to each other
 * DIRECTLY - no WiFi router, no internet, no server needed.
 * 
 * Think of it like walkie-talkies: you press a button, your message goes 
 * straight to the other device. That's ESP-NOW.
 * 
 * 
 * WHY USE ESP-NOW INSTEAD OF REGULAR WiFi?
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *     ┌──────────────────────────────────────────────────────────────────┐
 *     │  Feature            │  ESP-NOW          │  Regular WiFi         │
 *     ├──────────────────────────────────────────────────────────────────┤
 *     │  Needs router?      │  NO               │  YES                  │
 *     │  Latency            │  ~1-3 ms          │  ~50-200 ms           │
 *     │  Setup complexity   │  Low              │  Medium-High          │
 *     │  Max payload        │  250 bytes         │  Unlimited (TCP)     │
 *     │  Range              │  ~200m open air   │  Depends on router    │
 *     │  Max peers          │  20               │  Limited by router    │
 *     │  Power consumption  │  Very low         │  Higher               │
 *     │  Good for           │  Alerts, remotes  │  Web, video, big data │
 *     └──────────────────────────────────────────────────────────────────┘
 * 
 * BOTTOM LINE: Use ESP-NOW for fast, small messages between ESP32 boards.
 * Use WiFi when you need internet access or large data transfers.
 * 
 * 
 * =============================================================================
 * HOW ESP-NOW WORKS
 * =============================================================================
 * 
 * ESP-NOW uses the WiFi radio hardware but DOESN'T connect to a router.
 * It sends raw WiFi frames (called "vendor-specific action frames") directly
 * from one device to another using their MAC addresses.
 * 
 * 
 * UNICAST (one-to-one):
 * ~~~~~~~~~~~~~~~~~~~~~
 * You need to know the receiver's MAC address. Like texting someone - you 
 * need their phone number.
 * 
 *     ┌─────────────┐    250 bytes max     ┌─────────────┐
 *     │   ESP32 #1  │ ──────────────────►  │   ESP32 #2  │
 *     │             │    MAC: AA:BB:CC:..   │             │
 *     │  (sender)   │                       │  (receiver) │
 *     │             │  ◄── send callback    │             │
 *     │             │   (success/fail)      │             │
 *     └─────────────┘                       └─────────────┘
 * 
 * The sender gets a callback telling it if the message was received at the
 * MAC layer (like a delivery receipt). NOTE: This does NOT guarantee the 
 * application processed it - just that the radio got it.
 * 
 * 
 * BROADCAST (one-to-all):
 * ~~~~~~~~~~~~~~~~~~~~~~~
 * Send to MAC address FF:FF:FF:FF:FF:FF and ALL ESP-NOW devices in range
 * on the same channel will receive it. Like shouting in a room.
 * 
 *                                           ┌─────────────┐
 *                                     ┌────►│   ESP32 #2  │
 *     ┌─────────────┐                │     └─────────────┘
 *     │   ESP32 #1  │ ───────────────┤
 *     │  (sender)   │  FF:FF:FF:..   │     ┌─────────────┐
 *     │  broadcast  │                ├────►│   ESP32 #3  │
 *     └─────────────┘                │     └─────────────┘
 *                                     │
 *                                     │     ┌─────────────┐
 *                                     └────►│   ESP32 #4  │
 *                                           └─────────────┘
 * 
 * WARNING: Broadcast has NO delivery confirmation. You won't know if anyone 
 * received it. Use unicast if you need guaranteed delivery.
 * 
 * 
 * PEER LIST:
 * ~~~~~~~~~~
 * Before sending to a specific device, you must "add" it as a peer.
 * Think of it as your contacts list. Max 20 peers, max 7 encrypted peers 
 * (configurable up to 17).
 * 
 * For broadcast, you add FF:FF:FF:FF:FF:FF as a peer.
 * 
 * 
 * WiFi CHANNEL:
 * ~~~~~~~~~~~~~
 * ESP-NOW operates on a specific WiFi channel (1-14). ALL communicating 
 * devices MUST be on the SAME channel, or messages won't get through.
 * 
 * If you set the peer channel to 0, it uses whatever channel the device is 
 * currently on. This is simplest for most setups.
 * 
 * IMPORTANT: If your ESP32 is also connected to a WiFi router (STA mode), 
 * the channel is locked to the router's channel. ESP-NOW must use that same 
 * channel. You can't freely pick a channel while connected to WiFi.
 * 
 * 
 * =============================================================================
 * DATA FLOW IN THIS COMPONENT
 * =============================================================================
 * 
 * SENDING:
 * 
 *     Your code                  EspNowManager              ESP-IDF
 *        │                           │                         │
 *        │  send(mac, data, len)     │                         │
 *        │ ─────────────────────────►│                         │
 *        │                           │  esp_now_send()         │
 *        │                           │ ───────────────────────►│
 *        │                           │                         │──► Radio TX
 *        │                           │  send callback          │
 *        │                           │ ◄───────────────────────│
 *        │                           │                         │
 *        │  onSend callback fires    │                         │
 *        │ ◄─────────────────────────│                         │
 * 
 * 
 * RECEIVING (queue-based, NOT in ISR context):
 * 
 *     Radio RX        ESP-IDF            EspNowManager           Your code
 *        │                │                    │                      │
 *        │  raw frame     │                    │                      │
 *        │ ──────────────►│                    │                      │
 *        │                │  recv callback     │                      │
 *        │                │ (ISR-like context) │                      │
 *        │                │ ──────────────────►│                      │
 *        │                │                    │ enqueue to           │
 *        │                │                    │ FreeRTOS queue       │
 *        │                │                    │                      │
 *        │                │                    │ receive task         │
 *        │                │                    │ dequeues message     │
 *        │                │                    │                      │
 *        │                │                    │ onReceive callback   │
 *        │                │                    │ ────────────────────►│
 *        │                │                    │                      │
 * 
 * WHY THE QUEUE? The ESP-NOW receive callback runs in WiFi task context.
 * You CANNOT do heavy work there (no logging, no delays, no mutexes).
 * We copy the data into a FreeRTOS queue and process it in a separate task
 * where you can safely do anything.
 * 
 * 
 * =============================================================================
 * SMART HOME USE CASES
 * =============================================================================
 * 
 * In the smart home ecosystem, ESP-NOW is ideal for:
 * 
 *   • Doorbell press → Main controller (instant, <5ms latency)
 *   • Motion sensor → Light controller (turn on lights immediately)
 *   • Remote button → Garage door (no WiFi needed in garage)
 *   • Temperature sensor → Display unit (periodic readings)
 *   • Panic button → All devices (broadcast alert)
 * 
 * ESP-NOW can run SIMULTANEOUSLY with WiFi STA mode. So your main controller
 * can be connected to WiFi (for web interface, cloud) AND listen for ESP-NOW
 * alerts at the same time. Just remember: channel must match.
 * 
 * 
 * =============================================================================
 * WIRING
 * =============================================================================
 * 
 * None! ESP-NOW uses the built-in WiFi radio. No extra hardware needed.
 * Just upload code to two or more ESP32 boards and they can talk.
 * 
 * 
 * =============================================================================
 * FINDING YOUR MAC ADDRESS
 * =============================================================================
 * 
 * Every ESP32 has a unique MAC address burned into it at the factory.
 * This component prints it on startup, but you can also get it with:
 * 
 *     uint8_t mac[6];
 *     esp_read_mac(mac, ESP_MAC_WIFI_STA);
 *     // Prints like: AA:BB:CC:DD:EE:FF
 * 
 * You need the receiver's MAC address to send unicast messages.
 * TIP: Flash both boards, read the MACs from serial output, then hardcode 
 * them. Or use broadcast to discover peers dynamically.
 * 
 * 
 * =============================================================================
 * USAGE EXAMPLE
 * =============================================================================
 * 
 *     #include "esp_now_manager.h"
 *     
 *     // Receiver's MAC address (get this from the other board's serial output)
 *     static const uint8_t PEER_MAC[] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
 *     
 *     // Called when a message arrives
 *     void onReceive(const uint8_t* sender_mac, const uint8_t* data, int len) {
 *         ESP_LOGI("APP", "Got %d bytes from %02X:%02X:%02X:%02X:%02X:%02X",
 *                  len,
 *                  sender_mac[0], sender_mac[1], sender_mac[2],
 *                  sender_mac[3], sender_mac[4], sender_mac[5]);
 *     }
 *     
 *     // Called after send completes
 *     void onSend(const uint8_t* dest_mac, bool success) {
 *         ESP_LOGI("APP", "Send %s", success ? "OK" : "FAIL");
 *     }
 *     
 *     extern "C" void app_main(void) {
 *         EspNowManager& enm = EspNowManager::instance();
 *         
 *         enm.setReceiveCallback(onReceive);
 *         enm.setSendCallback(onSend);
 *         
 *         // Initialize (starts WiFi in STA mode internally)
 *         enm.begin();
 *         
 *         // Add the other board as a peer
 *         enm.addPeer(PEER_MAC);
 *         
 *         // Send a message
 *         const char* msg = "Hello from ESP32!";
 *         enm.send(PEER_MAC, (const uint8_t*)msg, strlen(msg));
 *         
 *         // Or broadcast to everyone
 *         enm.broadcast((const uint8_t*)msg, strlen(msg));
 *     }
 * 
 * =============================================================================
 */

#ifndef ESP_NOW_MANAGER_H
#define ESP_NOW_MANAGER_H

/* ─── Includes ───────────────────────────────────────────────────────────── */
#include <cstdint>
#include <cstring>
#include <functional>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_now.h"
#include "esp_mac.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_rom_sys.h" 

/* ─── Constants ──────────────────────────────────────────────────────────── */

/** @brief Maximum data payload per ESP-NOW packet (set by Espressif) */
#define ESPNOW_MAX_DATA_LEN     ESP_NOW_MAX_DATA_LEN   // 250 bytes

/** @brief Maximum number of peers in the peer list */
#define ESPNOW_MAX_PEERS        20

/** @brief Broadcast MAC address - sends to all ESP-NOW devices on channel */
#define ESPNOW_BROADCAST_MAC    {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}

/** @brief Default receive queue depth (how many messages can be buffered) */
#define ESPNOW_DEFAULT_QUEUE_SIZE   16

/** @brief Default receive task stack size in bytes */
#define ESPNOW_DEFAULT_TASK_STACK   4096

/** @brief Default receive task priority */
#define ESPNOW_DEFAULT_TASK_PRIO    5

/* ─── Callback Types ─────────────────────────────────────────────────────── */

/**
 * @brief Callback type for received messages.
 * 
 * Called from the receive task (safe to log, allocate, use mutexes, etc.).
 * NOT called from ISR/WiFi context.
 * 
 * @param sender_mac  6-byte MAC address of the sender
 * @param data        Pointer to received data (valid only during callback)
 * @param data_len    Length of received data in bytes (0-250)
 */
using EspNowReceiveCb = std::function<void(const uint8_t* sender_mac,
                                            const uint8_t* data,
                                            int data_len)>;

/**
 * @brief Callback type for send completion status.
 * 
 * Called from the ESP-NOW internal task. Keep it short - no blocking.
 * 
 * @param dest_mac  6-byte MAC address of the destination
 * @param success   true if MAC layer acknowledged receipt, false otherwise
 */
using EspNowSendCb = std::function<void(const uint8_t* dest_mac,
                                         bool success)>;

/* ─── Configuration ──────────────────────────────────────────────────────── */

/**
 * @brief Configuration structure for EspNowManager.
 * 
 * Pass this to begin() to customize behavior. All fields have sensible 
 * defaults so you can just use the default constructor.
 */
struct EspNowConfig {
    wifi_mode_t wifi_mode       = WIFI_MODE_STA;    ///< WiFi mode (STA recommended)
    uint8_t     channel         = 0;                ///< WiFi channel (0 = default/auto)
    bool        long_range      = false;            ///< Enable WiFi long range mode (slower but further)
    uint16_t    queue_size      = ESPNOW_DEFAULT_QUEUE_SIZE;   ///< Receive queue depth
    uint32_t    task_stack      = ESPNOW_DEFAULT_TASK_STACK;   ///< Receive task stack size
    UBaseType_t task_priority   = ESPNOW_DEFAULT_TASK_PRIO;    ///< Receive task priority
    bool        init_nvs        = true;             ///< Initialize NVS flash (needed for WiFi)
    bool        init_netif      = true;             ///< Initialize default netif (needed for WiFi)
};

/* ─── Main Class ─────────────────────────────────────────────────────────── */

/**
 * @brief ESP-NOW Manager - singleton class for peer-to-peer communication.
 * 
 * Singleton because ESP-NOW has a single global state in the ESP-IDF.
 * Only one instance can exist per device.
 * 
 * Thread-safe: all public methods are protected by a mutex.
 * 
 * @note Call begin() before any other methods.
 * @note WiFi must be started for ESP-NOW to work, even though you're not 
 *       connecting to a router. begin() handles this automatically.
 */
class EspNowManager {
public:
    /* ─── Singleton Access ─────────────────────────────────────────────── */

    /**
     * @brief Get the singleton instance.
     * @return Reference to the single EspNowManager instance.
     */
    static EspNowManager& instance();

    // No copying or moving a singleton
    EspNowManager(const EspNowManager&) = delete;
    EspNowManager& operator=(const EspNowManager&) = delete;

    /* ─── Lifecycle ────────────────────────────────────────────────────── */

    /**
     * @brief Initialize ESP-NOW and start the receive task.
     * 
     * This does the following in order:
     *   1. Initialize NVS (if config.init_nvs)
     *   2. Initialize network interface (if config.init_netif)
     *   3. Create default WiFi event loop
     *   4. Start WiFi in STA mode (no connection, just radio on)
     *   5. Initialize ESP-NOW
     *   6. Register internal send/receive callbacks
     *   7. Add broadcast peer automatically
     *   8. Start the FreeRTOS receive task
     * 
     * @param config  Configuration options (default values are fine for most use)
     * @return ESP_OK on success, error code on failure
     * 
     * @note Safe to call multiple times - will return ESP_OK if already initialized.
     * @note Prints the device's MAC address to serial on startup.
     */
    esp_err_t begin(const EspNowConfig& config = EspNowConfig{});

    /**
     * @brief Deinitialize ESP-NOW and stop the receive task.
     * 
     * Cleans up all resources. All peers are removed. WiFi is NOT stopped
     * (you might be using it for other things).
     * 
     * @return ESP_OK on success
     */
    esp_err_t end();

    /**
     * @brief Check if ESP-NOW is initialized and ready.
     * @return true if begin() was called successfully
     */
    bool isReady() const;

    /* ─── Callbacks ────────────────────────────────────────────────────── */

    /**
     * @brief Set the callback for received messages.
     * 
     * @param cb  Function to call when a message arrives.
     *            Set to nullptr to clear.
     * 
     * @note Set this BEFORE calling begin() to avoid missing early messages.
     * @note The callback runs in the receive task, NOT in ISR context.
     *       You can safely log, allocate memory, use delays, etc.
     */
    void setReceiveCallback(EspNowReceiveCb cb);

    /**
     * @brief Set the callback for send completion.
     * 
     * @param cb  Function to call after each send completes.
     *            Set to nullptr to clear.
     * 
     * @note This runs in ESP-NOW's internal task context. Keep it fast.
     */
    void setSendCallback(EspNowSendCb cb);

    /* ─── Peer Management ──────────────────────────────────────────────── */

    /**
     * @brief Add a peer to the peer list.
     * 
     * You must add a device as a peer before you can send unicast messages 
     * to it. Broadcast peer (FF:FF:FF:FF:FF:FF) is added automatically 
     * during begin().
     * 
     * @param mac       6-byte MAC address of the peer
     * @param channel   WiFi channel (0 = same as current, 1-14 for specific)
     * @param encrypt   Enable encryption for this peer (requires PMK/LMK setup)
     * @param lmk       Local Master Key for encryption (16 bytes, nullptr if no encryption)
     * @return ESP_OK on success, ESP_ERR_ESPNOW_FULL if 20 peers reached,
     *         ESP_ERR_ESPNOW_EXIST if peer already added
     */
    esp_err_t addPeer(const uint8_t* mac, uint8_t channel = 0,
                      bool encrypt = false, const uint8_t* lmk = nullptr);

    /**
     * @brief Remove a peer from the peer list.
     * 
     * @param mac  6-byte MAC address of the peer to remove
     * @return ESP_OK on success, ESP_ERR_ESPNOW_NOT_FOUND if not in list
     */
    esp_err_t removePeer(const uint8_t* mac);

    /**
     * @brief Check if a MAC address is already in the peer list.
     * 
     * @param mac  6-byte MAC address to check
     * @return true if the peer exists in the list
     */
    bool hasPeer(const uint8_t* mac) const;

    /**
     * @brief Get the number of currently registered peers.
     * 
     * @param total      Output: total number of peers (including broadcast)
     * @param encrypted  Output: number of encrypted peers
     * @return ESP_OK on success
     */
    esp_err_t getPeerCount(int& total, int& encrypted) const;

    /* ─── Sending ──────────────────────────────────────────────────────── */

    /**
     * @brief Send data to a specific peer (unicast).
     * 
     * The peer must be added with addPeer() first.
     * 
     * @param dest_mac  6-byte MAC address of the destination peer
     * @param data      Pointer to data to send
     * @param len       Length of data in bytes (max 250)
     * @return ESP_OK if the send was initiated (NOT a guarantee of delivery).
     *         Check the send callback for delivery status.
     * 
     * @note This is non-blocking. The send callback fires when done.
     * @note If len > 250, returns ESP_ERR_INVALID_ARG.
     */
    esp_err_t send(const uint8_t* dest_mac, const uint8_t* data, size_t len);

    /**
     * @brief Send data to ALL peers in the peer list.
     * 
     * Equivalent to calling esp_now_send(NULL, ...) which sends to every 
     * registered peer.
     * 
     * @param data  Pointer to data to send
     * @param len   Length of data in bytes (max 250)
     * @return ESP_OK if sends were initiated
     */
    esp_err_t sendToAll(const uint8_t* data, size_t len);

    /**
     * @brief Broadcast data to all ESP-NOW devices on the same channel.
     * 
     * Uses the broadcast MAC (FF:FF:FF:FF:FF:FF) which was auto-added 
     * during begin(). Any device listening on the same channel will receive 
     * this, even if they haven't added the sender as a peer.
     * 
     * @param data  Pointer to data to send
     * @param len   Length of data in bytes (max 250)
     * @return ESP_OK if the broadcast was initiated
     * 
     * @note NO delivery confirmation for broadcast.
     */
    esp_err_t broadcast(const uint8_t* data, size_t len);

    /* ─── Utilities ────────────────────────────────────────────────────── */

    /**
     * @brief Get this device's WiFi STA MAC address.
     * 
     * @param mac  Output buffer, must be at least 6 bytes
     * @return ESP_OK on success
     */
    esp_err_t getOwnMac(uint8_t* mac) const;

    /**
     * @brief Format a MAC address as a string "AA:BB:CC:DD:EE:FF".
     * 
     * @param mac  6-byte MAC address
     * @param buf  Output buffer, must be at least 18 bytes
     */
    static void macToStr(const uint8_t* mac, char* buf);

private:
    /* ─── Singleton Constructor ────────────────────────────────────────── */
    EspNowManager();
    ~EspNowManager();

    /* ─── Internal Callbacks (static, forwarded from ESP-IDF) ──────────── */
    static void onSendStatic(const esp_now_send_info_t* tx_info,
                             esp_now_send_status_t status);
    static void onRecvStatic(const esp_now_recv_info_t* recv_info,
                             const uint8_t* data, int data_len);

    /* ─── Receive Task ─────────────────────────────────────────────────── */
    static void receiveTaskFunc(void* arg);

    /* ─── Internal State ───────────────────────────────────────────────── */

    /**
     * @brief Structure for queuing received messages from ISR to task.
     * 
     * We copy the MAC and data into this struct so we don't depend on
     * the pointers from the callback (which are only valid during the 
     * callback itself).
     */
    struct RxMessage {
        uint8_t  sender_mac[6];                     ///< Sender's MAC address
        uint8_t  data[ESP_NOW_MAX_DATA_LEN];        ///< Copy of received data
        int      data_len;                          ///< Length of received data
    };

    bool            _initialized;       ///< Has begin() been called successfully?
    QueueHandle_t   _rx_queue;          ///< FreeRTOS queue for received messages
    TaskHandle_t    _rx_task;           ///< Handle to the receive processing task
    SemaphoreHandle_t _mutex;           ///< Mutex for thread-safe access

    EspNowReceiveCb _recv_cb;           ///< User's receive callback
    EspNowSendCb    _send_cb;           ///< User's send callback
};

#endif // ESP_NOW_MANAGER_H
