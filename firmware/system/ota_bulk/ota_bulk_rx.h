/*
 * =============================================================================
 * FILE:        ota_bulk_rx.h
 * AUTHOR:      AbedX69
 * CREATED:     2026-08-05
 * VERSION:     1.0.0
 * LICENSE:     MIT
 * PLATFORM:    ESP32-S3 / ESP32-C6 (ESP-IDF v5.x)
 * =============================================================================
 *
 * Receiver half of the ESP-NOW bulk plane. Accepts an image offered by this
 * device's own paired controller and writes it into the inactive OTA slot.
 * The sender half lives in ota_bulk_tx (hub only).
 *
 * =============================================================================
 * WIRING (strip main.cpp)
 * =============================================================================
 *
 *   OtaBulkRx& bulk = OtaBulkRx::instance();
 *   bulk.begin(DeviceRole::LIGHT);          // before the radio starts
 *
 *   enm.setReceiveCallback([](const uint8_t* sender,
 *                             const uint8_t* data, int len) {
 *       if (OtaBulkRx::instance().tryConsume(data, len)) return;
 *       MessageProtocol::instance().processMessage(data, (uint8_t)len, sender);
 *   });
 *
 *   // inside onCommand(), alongside the AutoPair::handlesCmd() branch:
 *   if (OtaBulkRx::handlesCmd(cmd))
 *       return OtaBulkRx::instance().processControl(cmd, payload, len, src_uid);
 *
 * =============================================================================
 * THREADING
 * =============================================================================
 *
 * Three contexts touch this object:
 *
 *   ESP-NOW RX task   tryConsume()      reads scalars, memcpy into queue
 *   ESP-NOW RX task   processControl()  session start/stop, takes _mtx
 *   ota_bulk task     taskLoop()        flash writes, bitmap, takes _mtx
 *
 * The bitmap is owned by the task. tryConsume() deliberately does NOT check it
 * for duplicates - that would mean reading it from a second context for what is
 * only an optimisation. The task re-checks before every write, so a duplicate
 * costs one queue slot and nothing else.
 *
 * Flash writes do not happen in the RX callback. A flash program op disables
 * the cache, which stalls anything running from flash including the WiFi task
 * itself - on a single-core C6 that means dropping the very chunks you are
 * about to ask for again. The queue is the fix, and it costs ~4 KB.
 *
 * =============================================================================
 */

#ifndef OTA_BULK_RX_H
#define OTA_BULK_RX_H

#include <cstdint>
#include <cstddef>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include "core_types.h"
#include "message_protocol.h"
#include "ota_bulk.h"

/** Queued chunks. 244 B each; depth 16 is ~3.9 KB. */
#define OTABULK_RX_QUEUE_DEPTH   16

/** Stack for the writer task. Flash writes plus a 256 B CRC buffer. */
#define OTABULK_RX_TASK_STACK    4096
#define OTABULK_RX_TASK_PRIO     5

class OtaBulkRx {
public:
    static OtaBulkRx& instance();

    /**
     * @brief Create the queue and writer task.
     * @param self_role  This device's role. Passed in rather than read from
     *                   AutoPair so the offer's role check has an explicit,
     *                   testable source. Must match OtaOfferPayload.target_role.
     */
    esp_err_t begin(DeviceRole self_role);

    /**
     * @brief Raw ESP-NOW demux. Call FIRST in the receive callback.
     * @return true if the frame was a bulk chunk and is fully handled.
     *         false means it is not ours - pass it to processMessage().
     */
    bool tryConsume(const uint8_t* data, int len);

    /** True for CmdId 0x80-0x84. */
    static bool handlesCmd(CmdId cmd);

    /**
     * @brief Control plane. The returned AckStatus IS the offer accept/reject.
     *
     * OTA_OFFER is rejected with NOT_SUPPORTED unless src_uid is this device's
     * paired controller AND target_role matches. Both checks happen before a
     * single byte of image transfers.
     */
    AckStatus processControl(CmdId cmd, const uint8_t* payload, uint8_t len,
                             DeviceUid src_uid);

    bool     isActive()       const { return _session != 0; }
    uint16_t chunksReceived() const { return _received; }
    uint16_t chunkCount()     const { return _chunk_count; }
    uint16_t chunksDropped()  const { return _dropped; }

private:
    OtaBulkRx();
    OtaBulkRx(const OtaBulkRx&)            = delete;
    OtaBulkRx& operator=(const OtaBulkRx&) = delete;

    typedef struct {
        uint16_t index;
        uint16_t len;
        uint8_t  data[OTABULK_CHUNK_SIZE];
    } BulkItem;

    static void taskEntry(void* arg);
    void taskLoop();

    bool bitTest(uint32_t i) const { return (_bitmap[i >> 3] >> (i & 7)) & 1; }
    void bitSet (uint32_t i)       { _bitmap[i >> 3] |= (uint8_t)(1u << (i & 7)); }

    void sendGapReports();
    void finishAndReport();
    void endSession(uint8_t reason, bool notify_hub);
    void clearSession();
    esp_err_t sendControl(CmdId cmd, const void* p, uint8_t len);

    void lock()   { if (_mtx) xSemaphoreTake(_mtx, portMAX_DELAY); }
    void unlock() { if (_mtx) xSemaphoreGive(_mtx); }

    bool              _initialized;
    DeviceRole        _self_role;

    QueueHandle_t     _q;
    TaskHandle_t      _task;
    SemaphoreHandle_t _mtx;

    /* Session. _session is written last on start and first on stop, so the
     * RX callback never sees a half-built session. */
    volatile uint32_t _session;
    DeviceUid         _controller;
    uint32_t          _image_size;
    uint32_t          _crc32;
    uint16_t          _chunk_size;
    uint16_t          _chunk_count;
    uint16_t          _received;
    uint16_t          _dropped;
    uint8_t*          _bitmap;
    volatile bool     _pass_end_pending;
    int64_t           _last_activity_us;
};

#endif /* OTA_BULK_RX_H */
