/*
 * =============================================================================
 * FILE:        ota_bulk_rx.cpp
 * AUTHOR:      AbedX69
 * CREATED:     2026-08-05
 * VERSION:     1.0.0
 * =============================================================================
 *
 * See ota_bulk_rx.h for wiring and the threading model, and ota_bulk.h for the
 * wire format this implements.
 *
 * =============================================================================
 */

#include "ota_bulk_rx.h"

#include <cstring>
#include <cstdlib>
#include <esp_log.h>
#include <esp_timer.h>

#include "ota_manager.h"
#include "auto_pair.h"

static const char* TAG = "OtaBulkRx";

/* Reliable sends share MSG_PENDING_SLOTS (8). Emitting an unbounded number of
 * gap reports back to back would exhaust them and start failing. Cap per pass;
 * anything left over is picked up on the next pass. Correctness does not
 * depend on this - only convergence speed. */
#define OTABULK_MAX_REPORTS_PER_PASS   6
#define OTABULK_REPORT_GAP_MS          20

/* Task wakes this often even with no traffic, to service timeouts. */
#define OTABULK_TICK_MS                250


/* =============================================================================
 * SINGLETON
 * ========================================================================== */

OtaBulkRx& OtaBulkRx::instance() {
    static OtaBulkRx inst;
    return inst;
}

OtaBulkRx::OtaBulkRx()
    : _initialized(false)
    , _self_role(DeviceRole::UNKNOWN)
    , _q(nullptr)
    , _task(nullptr)
    , _mtx(nullptr)
    , _session(0)
    , _controller(UID_NONE)
    , _image_size(0)
    , _crc32(0)
    , _chunk_size(0)
    , _chunk_count(0)
    , _received(0)
    , _dropped(0)
    , _bitmap(nullptr)
    , _pass_end_pending(false)
    , _last_activity_us(0)
{}


/* =============================================================================
 * LIFECYCLE
 * ========================================================================== */

esp_err_t OtaBulkRx::begin(DeviceRole self_role) {
    if (_initialized) return ESP_OK;

    _self_role = self_role;

    _mtx = xSemaphoreCreateMutex();
    if (!_mtx) return ESP_ERR_NO_MEM;

    _q = xQueueCreate(OTABULK_RX_QUEUE_DEPTH, sizeof(BulkItem));
    if (!_q) {
        vSemaphoreDelete(_mtx);
        _mtx = nullptr;
        return ESP_ERR_NO_MEM;
    }

    BaseType_t ok = xTaskCreate(taskEntry, "ota_bulk", OTABULK_RX_TASK_STACK,
                                this, OTABULK_RX_TASK_PRIO, &_task);
    if (ok != pdPASS) {
        vQueueDelete(_q);
        vSemaphoreDelete(_mtx);
        _q = nullptr;
        _mtx = nullptr;
        return ESP_ERR_NO_MEM;
    }

    _initialized = true;
    ESP_LOGI(TAG, "Bulk RX ready (role=%s, queue=%d x %u B, chunk=%d B)",
             deviceRoleName(self_role), OTABULK_RX_QUEUE_DEPTH,
             (unsigned)sizeof(BulkItem), OTABULK_CHUNK_SIZE);
    return ESP_OK;
}


/* =============================================================================
 * RAW PLANE - runs in the ESP-NOW RX callback
 * ========================================================================== */

bool OtaBulkRx::tryConsume(const uint8_t* data, int len) {
    if (!_initialized || !data || len < (int)OTABULK_HDR_SIZE) return false;

    OtaChunkHeader hdr;
    memcpy(&hdr, data, sizeof(hdr));
    if (hdr.magic != OTABULK_MAGIC) return false;

    /* Past this point the frame is ours. Everything below returns true:
     * a malformed or stale chunk is dropped here, never handed to
     * MessageProtocol, which would only log it as a bad-magic packet. */

    uint32_t sess = _session;
    if (sess == 0 || hdr.session_id != sess)   return true;
    if (hdr.chunk_index >= _chunk_count)       return true;

    uint16_t plen = (uint16_t)(len - (int)OTABULK_HDR_SIZE);
    if (plen == 0 || plen > _chunk_size)       return true;

    BulkItem item;
    item.index = hdr.chunk_index;
    item.len   = plen;
    memcpy(item.data, data + OTABULK_HDR_SIZE, plen);

    /* Never block the RX callback. A full queue is exactly what the gap list
     * exists for - the chunk is simply requested again next pass. */
    if (xQueueSend(_q, &item, 0) != pdTRUE) {
        if (_dropped < 0xFFFF) _dropped++;
    }
    return true;
}


/* =============================================================================
 * CONTROL PLANE - runs in the ESP-NOW RX callback via the command handler
 * ========================================================================== */

bool OtaBulkRx::handlesCmd(CmdId cmd) {
    return cmd == CmdId::OTA_OFFER
        || cmd == CmdId::OTA_PASS_END
        || cmd == CmdId::OTA_GAP_REPORT
        || cmd == CmdId::OTA_COMPLETE
        || cmd == CmdId::OTA_ABORT;
}

AckStatus OtaBulkRx::processControl(CmdId cmd, const uint8_t* payload,
                                    uint8_t len, DeviceUid src_uid) {
    if (!_initialized) return AckStatus::FAIL;

    switch (cmd) {

    /* ── 0x80 OTA_OFFER ──────────────────────────────────────────────────── */
    case CmdId::OTA_OFFER: {
        if (!payload || len < sizeof(OtaOfferPayload)) return AckStatus::FAIL;

        OtaOfferPayload o;
        memcpy(&o, payload, sizeof(o));

        /* Authorisation first: only this device's own paired controller may
         * offer it firmware. Identity is the UID, never a project name. */
        AutoPair& pair = AutoPair::instance();
        if (!pair.isPaired() || src_uid != pair.getControllerUid()) {
            ESP_LOGW(TAG, "OTA_OFFER from %08X - not our controller, rejected",
                     (unsigned)src_uid);
            return AckStatus::NOT_SUPPORTED;
        }

        /* Role check, before any bytes move. */
        if ((DeviceRole)o.target_role != _self_role) {
            ESP_LOGW(TAG, "OTA_OFFER targets %s, we are %s - rejected",
                     deviceRoleName((DeviceRole)o.target_role),
                     deviceRoleName(_self_role));
            return AckStatus::NOT_SUPPORTED;
        }

        if (_session != 0)                                return AckStatus::BUSY;
        if (OTAManager::instance().isWriteInProgress())   return AckStatus::BUSY;

        if (o.session_id == 0 || o.image_size == 0)       return AckStatus::FAIL;
        if (o.chunk_size == 0 ||
            o.chunk_size > OTABULK_CHUNK_SIZE ||
            (o.chunk_size % 16) != 0) {
            ESP_LOGW(TAG, "Bad chunk_size %u", (unsigned)o.chunk_size);
            return AckStatus::FAIL;
        }

        uint32_t count = (o.image_size + o.chunk_size - 1) / o.chunk_size;
        if (count == 0 || count > OTABULK_MAX_CHUNKS)     return AckStatus::FAIL;

        lock();
        uint8_t* bm = (uint8_t*)calloc(1, OTABULK_BITMAP_BYTES(count));
        if (!bm) {
            unlock();
            ESP_LOGE(TAG, "Bitmap alloc failed (%u B)",
                     (unsigned)OTABULK_BITMAP_BYTES(count));
            return AckStatus::FAIL;
        }

        esp_err_t err = OTAManager::instance().beginWrite(o.image_size);
        if (err != ESP_OK) {
            free(bm);
            unlock();
            ESP_LOGE(TAG, "beginWrite failed: %s", esp_err_to_name(err));
            return AckStatus::FAIL;
        }

        _bitmap           = bm;
        _controller       = src_uid;
        _image_size       = o.image_size;
        _crc32            = o.crc32;
        _chunk_size       = o.chunk_size;
        _chunk_count      = (uint16_t)count;
        _received         = 0;
        _dropped          = 0;
        _pass_end_pending = false;
        _last_activity_us = esp_timer_get_time();
        xQueueReset(_q);
        _session          = o.session_id;      /* last: opens the raw plane */
        unlock();

        ESP_LOGI(TAG, "Accepted image v%u.%u.%u: %lu B, %u chunks, crc %08lX",
                 (unsigned)o.ver_major, (unsigned)o.ver_minor,
                 (unsigned)o.ver_patch, (unsigned long)o.image_size,
                 (unsigned)count, (unsigned long)o.crc32);
        return AckStatus::OK;
    }

    /* ── 0x81 OTA_PASS_END ───────────────────────────────────────────────── */
    case CmdId::OTA_PASS_END: {
        if (!payload || len < sizeof(OtaPassEndPayload)) return AckStatus::FAIL;
        OtaPassEndPayload pe;
        memcpy(&pe, payload, sizeof(pe));
        if (_session == 0 || pe.session_id != _session)  return AckStatus::FAIL;

        /* The task emits the report once the queue drains. Reporting from
         * here would list chunks that are queued but not yet written. */
        _last_activity_us = esp_timer_get_time();
        _pass_end_pending = true;
        return AckStatus::OK;
    }

    /* ── 0x84 OTA_ABORT ──────────────────────────────────────────────────── */
    case CmdId::OTA_ABORT: {
        if (!payload || len < sizeof(OtaAbortPayload))   return AckStatus::FAIL;
        OtaAbortPayload ab;
        memcpy(&ab, payload, sizeof(ab));
        if (_session == 0 || ab.session_id != _session)  return AckStatus::OK;

        ESP_LOGW(TAG, "Hub aborted session (reason %u)", (unsigned)ab.reason);
        lock();
        endSession(ab.reason, false);
        unlock();
        return AckStatus::OK;
    }

    /* Device -> hub messages. We never receive these. */
    case CmdId::OTA_GAP_REPORT:
    case CmdId::OTA_COMPLETE:
    default:
        return AckStatus::NOT_SUPPORTED;
    }
}


/* =============================================================================
 * WRITER TASK - the only context that touches the bitmap or flash
 * ========================================================================== */

void OtaBulkRx::taskEntry(void* arg) {
    static_cast<OtaBulkRx*>(arg)->taskLoop();
}

void OtaBulkRx::taskLoop() {
    BulkItem item;

    for (;;) {
        bool got = (xQueueReceive(_q, &item, pdMS_TO_TICKS(OTABULK_TICK_MS))
                    == pdTRUE);

        lock();

        if (got && _session != 0 &&
            item.index < _chunk_count && !bitTest(item.index)) {

            uint32_t off = (uint32_t)item.index * (uint32_t)_chunk_size;
            esp_err_t err = OTAManager::instance()
                                .writeChunkAt(item.data, item.len, off);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Chunk %u write failed: %s",
                         (unsigned)item.index, esp_err_to_name(err));
                /* writeChunkAt already called abortWrite() internally. */
                OtaCompletePayload done = {};
                done.session_id = _session;
                done.result     = OTA_BULK_WRITE_FAILED;
                sendControl(CmdId::OTA_COMPLETE, &done, sizeof(done));
                clearSession();
                unlock();
                continue;
            }

            bitSet(item.index);
            _received++;
            _last_activity_us = esp_timer_get_time();
        }

        if (_session == 0) { unlock(); continue; }

        /* Pass boundary. Only once the queue has drained, so the bitmap is
         * an accurate picture of what actually reached flash. */
        if (_pass_end_pending && uxQueueMessagesWaiting(_q) == 0) {
            _pass_end_pending = false;
            if (_received >= _chunk_count) finishAndReport();
            else                           sendGapReports();
            unlock();
            continue;
        }

        if (esp_timer_get_time() - _last_activity_us
                > (int64_t)OTABULK_SESSION_TIMEOUT_MS * 1000LL) {
            ESP_LOGW(TAG, "Session timed out with %u/%u chunks",
                     (unsigned)_received, (unsigned)_chunk_count);
            endSession(OTA_ABORT_TIMEOUT, true);
        }

        unlock();
    }
}


/* =============================================================================
 * REPORTING
 * ========================================================================== */

void OtaBulkRx::sendGapReports() {
    uint16_t missing = (uint16_t)(_chunk_count - _received);

    OtaGapReportPayload rep = {};
    rep.session_id    = _session;
    rep.missing_total = missing;
    rep.run_count     = 0;

    uint8_t  reports = 0;
    uint32_t i       = 0;

    while (i < _chunk_count && reports < OTABULK_MAX_REPORTS_PER_PASS) {
        if (bitTest(i)) { i++; continue; }

        uint32_t start = i;
        while (i < _chunk_count && !bitTest(i)) i++;

        rep.runs[rep.run_count].start = (uint16_t)start;
        rep.runs[rep.run_count].count = (uint16_t)(i - start);
        rep.run_count++;

        if (rep.run_count == OTABULK_MAX_RUNS) {
            sendControl(CmdId::OTA_GAP_REPORT, &rep, sizeof(rep));
            reports++;
            rep.run_count = 0;
            memset(rep.runs, 0, sizeof(rep.runs));
            vTaskDelay(pdMS_TO_TICKS(OTABULK_REPORT_GAP_MS));
        }
    }

    if (rep.run_count > 0 && reports < OTABULK_MAX_REPORTS_PER_PASS) {
        sendControl(CmdId::OTA_GAP_REPORT, &rep, sizeof(rep));
        reports++;
    }

    ESP_LOGI(TAG, "Pass end: %u/%u chunks, %u missing, %u dropped, %u reports",
             (unsigned)_received, (unsigned)_chunk_count,
             (unsigned)missing, (unsigned)_dropped, (unsigned)reports);
}

void OtaBulkRx::finishAndReport() {
    OTAManager& ota = OTAManager::instance();
    uint8_t result  = OTA_BULK_OK;

    esp_err_t err = ota.verifyCrc32(_crc32, _image_size);
    if (err != ESP_OK) {
        result = OTA_BULK_CRC_MISMATCH;
        ota.abortWrite();
    } else {
        err = ota.finishWrite();
        if (err != ESP_OK) {
            /* esp_ota_end consumed the handle; the running image is untouched. */
            result = OTA_BULK_FINISH_FAILED;
        }
    }

    ESP_LOGI(TAG, "Image complete: result=%u", (unsigned)result);

    OtaCompletePayload done = {};
    done.session_id = _session;
    done.result     = result;
    sendControl(CmdId::OTA_COMPLETE, &done, sizeof(done));

    /* No reboot here. finishWrite() flipped the boot slot; the hub decides
     * when to restart us via CmdId::REBOOT, so OTA_COMPLETE gets out first. */
    clearSession();
}


/* =============================================================================
 * SESSION TEARDOWN
 * ========================================================================== */

void OtaBulkRx::endSession(uint8_t reason, bool notify_hub) {
    if (_session == 0) return;

    if (notify_hub) {
        OtaAbortPayload ab = {};
        ab.session_id = _session;
        ab.reason     = reason;
        sendControl(CmdId::OTA_ABORT, &ab, sizeof(ab));
    }

    if (OTAManager::instance().isWriteInProgress()) {
        OTAManager::instance().abortWrite();
    }
    clearSession();
}

void OtaBulkRx::clearSession() {
    _session          = 0;          /* first: closes the raw plane */
    _pass_end_pending = false;
    if (_bitmap) { free(_bitmap); _bitmap = nullptr; }
    _chunk_count      = 0;
    _chunk_size       = 0;
    _received         = 0;
    _image_size       = 0;
    _crc32            = 0;
    _controller       = UID_NONE;
}

esp_err_t OtaBulkRx::sendControl(CmdId cmd, const void* p, uint8_t len) {
    if (_controller == UID_NONE) return ESP_ERR_INVALID_STATE;
    return MessageProtocol::instance().sendCommandReliable(
               _controller, cmd, (const uint8_t*)p, len, nullptr);
}
