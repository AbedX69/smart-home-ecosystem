/*
 * =============================================================================
 * FILE:        ota_bulk.h
 * AUTHOR:      AbedX69
 * CREATED:     2026-08-05
 * VERSION:     1.0.0
 * LICENSE:     MIT
 * PLATFORM:    ESP32-S3 / ESP32-C6 (ESP-IDF v5.x)
 * =============================================================================
 *
 * ESP-NOW bulk plane for OTA - WIRE FORMAT ONLY. No implementation, no state,
 * no includes beyond <cstdint>. Nothing here can drift when a component around
 * it changes.
 *
 * =============================================================================
 * TWO PLANES
 * =============================================================================
 *
 * CONTROL rides the ordinary 48-byte MessagePacket on CmdIds 0x80-0x84, and so
 * inherits reliable send, retry/backoff, RX dedup and ACK for free. Six
 * messages per transfer, not four thousand - the cost is irrelevant.
 *
 * BULK is raw esp_now_send(). It does not go through MessageProtocol at all:
 * no seq, no ACK, no dedup, no house/room filtering. A dropped chunk is not
 * retried by anyone - the receiver notices the hole and asks for it later.
 *
 *     hub                                    strip
 *      │  OTA_OFFER (reliable) ─────────────►│  role + controller check
 *      │◄──────────────── ACK OK/NOT_SUPP    │  beginWrite(image_size)
 *      │                                     │
 *      │  chunk 0 ─────────────────────────► │  writeChunkAt(k * 240)
 *      │  chunk 1 ─────────────────────────► │  bitmap.set(k)
 *      │  ...                     (raw, ~4k) │
 *      │  OTA_PASS_END (reliable) ─────────► │
 *      │◄───────── OTA_GAP_REPORT (reliable) │  runs of missing indices
 *      │  chunk 17, 18, 902 ───────────────► │
 *      │  OTA_PASS_END ────────────────────► │
 *      │◄───────── OTA_GAP_REPORT (0 runs)   │  bitmap full
 *      │                                     │  CRC32 slot, finishWrite()
 *      │◄───────── OTA_COMPLETE (reliable)   │
 *
 * =============================================================================
 * WHY NO WINDOW
 * =============================================================================
 *
 * esp_ota_write_with_offset() writes non-contiguously, so chunk k goes to
 * offset k * OTABULK_CHUNK_SIZE the moment it lands. Nothing is buffered and
 * arrival order does not matter.
 *
 * This works ONLY because OTAManager::beginWrite() passes a real image size to
 * esp_ota_begin(), which erases the whole range up front. If it ever passes
 * OTA_WITH_SEQUENTIAL_WRITES (0xfffffffe) the erase becomes incremental and
 * offset writes corrupt the slot silently. Do not change that call.
 *
 * =============================================================================
 * WHY GAP RUNS, NOT A BITMAP
 * =============================================================================
 *
 * A 923 KB image is 3,938 chunks - a 493-byte bitmap, or 21 packets to ship
 * back. The common case is three missing chunks. Runs cost 4 bytes each and
 * describe that in one packet. Pathological loss needs several OTA_GAP_REPORTs;
 * that case is already slow for other reasons.
 *
 * =============================================================================
 */

#ifndef OTA_BULK_H
#define OTA_BULK_H

#include <cstdint>

/* =============================================================================
 * CHUNK FRAME (raw ESP-NOW, 8 + 240 = 248 bytes, limit is 250)
 * =============================================================================
 *
 *   off  size  field
 *   ───  ────  ──────────────────────────────────────────────────────────────
 *     0     2  magic          OTABULK_MAGIC - demux tag, see tryConsume()
 *     2     2  chunk_index    0..N-1; flash offset is index * chunk_size
 *     4     4  session_id     esp_random() per transfer
 *     8   240  payload
 *   ───  ────
 *   248        total
 *
 * The header is 8 bytes rather than 6 so the payload pointer stays 8-byte
 * aligned inside the RX buffer. There is no payload_len field: it is
 * derivable as min(chunk_size, image_size - index * chunk_size), and the
 * ESP-NOW receive callback hands you the frame length anyway.
 * ========================================================================== */

/** Demux tag. A frame is bulk iff len >= 8 AND magic matches. Everything else
 *  falls through to MessageProtocol::processMessage(). */
#define OTABULK_MAGIC           0x4F54      /* 'OT', little-endian on the wire */

/** Payload bytes per chunk. 16-byte aligned: esp_ota_write_with_offset()
 *  requires 16-byte alignment if flash encryption is ever enabled, and
 *  240 = 16 * 15 is the largest such size that fits ESP-NOW's 250. */
#define OTABULK_CHUNK_SIZE      240

#define OTABULK_HDR_SIZE        8
#define OTABULK_FRAME_MAX       (OTABULK_HDR_SIZE + OTABULK_CHUNK_SIZE)   /* 248 */

/** chunk_index is u16, so 65535 * 240 = 15.7 MB. No slot will approach this. */
#define OTABULK_MAX_CHUNKS      65535

/** Bitmap bytes for an image of n chunks. Heap-allocated on OTA_OFFER accept,
 *  freed on OTA_COMPLETE or OTA_ABORT. 493 bytes for a 923 KB image. */
#define OTABULK_BITMAP_BYTES(n) (((n) + 7) / 8)

typedef struct __attribute__((packed)) {
    uint16_t magic;             /**< OTABULK_MAGIC */
    uint16_t chunk_index;       /**< 0 .. chunk_count-1 */
    uint32_t session_id;        /**< must match the accepted OTA_OFFER */
} OtaChunkHeader;

static_assert(sizeof(OtaChunkHeader) == OTABULK_HDR_SIZE,
              "OtaChunkHeader must be exactly 8 bytes");

/* =============================================================================
 * CONTROL PLANE - payloads for CmdId 0x80-0x84
 * =============================================================================
 * Every one of these fits the 24-byte MessagePacket payload. All are sent
 * with sendCommandReliable(); the transfer never starts or ends on a packet
 * that might have been dropped.
 * ========================================================================== */

/* ─── 0x80 OTA_OFFER — hub → strip ────────────────────────────────────────────
 *
 * The receiver's ACK IS the accept/reject. There is no separate accept message:
 *
 *   AckStatus::OK              accepted, beginWrite() succeeded, send chunks
 *   AckStatus::NOT_SUPPORTED   target_role != this device's role
 *   AckStatus::BUSY            a write is already open, or pending validation
 *   AckStatus::FAIL            beginWrite() failed (oversized, no slot, erase)
 *
 * The role check happens HERE, before a single byte of image transfers, and is
 * paired with src_uid == AutoPair::getControllerUid(). An offer from anyone
 * other than this device's own paired controller is rejected outright.
 * ────────────────────────────────────────────────────────────────────────── */
typedef struct __attribute__((packed)) {
    uint32_t session_id;        /**< esp_random(), non-zero */
    uint32_t image_size;        /**< bytes; drives beginWrite() and the erase */
    uint32_t crc32;             /**< over the whole image, checked pre-finish  */
    uint16_t chunk_size;        /**< normally OTABULK_CHUNK_SIZE; reject if
                                     > OTABULK_CHUNK_SIZE or not 16-aligned    */
    uint8_t  target_role;       /**< DeviceRole; identity comes from the UID
                                     registry, never from a project name       */
    uint8_t  ver_major;
    uint8_t  ver_minor;
    uint8_t  ver_patch;
} OtaOfferPayload;              /* 18 of 24 */

/* ─── 0x81 OTA_PASS_END — hub → strip ─────────────────────────────────────────
 * "That is everything I intend to send this pass." Prompts a gap report.
 * ────────────────────────────────────────────────────────────────────────── */
typedef struct __attribute__((packed)) {
    uint32_t session_id;
    uint8_t  pass_num;          /**< 0 = initial full send, then 1, 2, ...     */
} OtaPassEndPayload;            /* 5 of 24 */

/* ─── 0x82 OTA_GAP_REPORT — strip → hub ───────────────────────────────────────
 * run_count == 0 with missing_total == 0 means the image is complete.
 * If more than OTABULK_MAX_RUNS runs are outstanding, send several reports;
 * missing_total is the true total across all of them, so the hub knows when
 * it has heard the whole picture.
 * ────────────────────────────────────────────────────────────────────────── */
#define OTABULK_MAX_RUNS        4

typedef struct __attribute__((packed)) {
    uint16_t start;             /**< first missing chunk_index in this run     */
    uint16_t count;             /**< how many consecutive, >= 1                */
} OtaGapRun;

typedef struct __attribute__((packed)) {
    uint32_t  session_id;
    uint16_t  missing_total;    /**< across ALL reports for this pass          */
    uint8_t   run_count;        /**< 0 .. OTABULK_MAX_RUNS, runs in this packet*/
    OtaGapRun runs[OTABULK_MAX_RUNS];
} OtaGapReportPayload;          /* 23 of 24 */

/* ─── 0x83 OTA_COMPLETE — strip → hub ─────────────────────────────────────────
 * Sent after: bitmap full, CRC32 matched, finishWrite() returned. The strip
 * does NOT reboot on its own — finishWrite() deliberately does not reboot, so
 * this ACK gets out first. Reboot is the hub's call via CmdId::REBOOT.
 * ────────────────────────────────────────────────────────────────────────── */
typedef enum : uint8_t {
    OTA_BULK_OK             = 0x00,     /**< flashed, boot flipped, ready      */
    OTA_BULK_CRC_MISMATCH   = 0x01,     /**< bitmap full but CRC32 wrong       */
    OTA_BULK_FINISH_FAILED  = 0x02,     /**< esp_ota_end rejected the image    */
    OTA_BULK_WRITE_FAILED   = 0x03,     /**< a writeChunkAt() failed mid-pass  */
} OtaBulkResult;

typedef struct __attribute__((packed)) {
    uint32_t session_id;
    uint8_t  result;            /**< OtaBulkResult */
} OtaCompletePayload;           /* 5 of 24 */

/* ─── 0x84 OTA_ABORT — either direction ───────────────────────────────────────
 * Receiver calls abortWrite() and frees the bitmap. The running image is
 * untouched either way; abort is always safe.
 * ────────────────────────────────────────────────────────────────────────── */
typedef enum : uint8_t {
    OTA_ABORT_USER          = 0x00,     /**< operator cancelled                */
    OTA_ABORT_TIMEOUT       = 0x01,     /**< no traffic for the session window */
    OTA_ABORT_SESSION       = 0x02,     /**< session_id mismatch               */
    OTA_ABORT_TOO_MANY_PASS = 0x03,     /**< loss not converging               */
    OTA_ABORT_SINK_ERROR    = 0x04,     /**< OTAManager returned an error      */
} OtaBulkAbort;

typedef struct __attribute__((packed)) {
    uint32_t session_id;
    uint8_t  reason;            /**< OtaBulkAbort */
} OtaAbortPayload;              /* 5 of 24 */

static_assert(sizeof(OtaOfferPayload)     <= 24, "offer exceeds MSG_PAYLOAD_MAX");
static_assert(sizeof(OtaPassEndPayload)   <= 24, "pass_end exceeds MSG_PAYLOAD_MAX");
static_assert(sizeof(OtaGapReportPayload) <= 24, "gap_report exceeds MSG_PAYLOAD_MAX");
static_assert(sizeof(OtaCompletePayload)  <= 24, "complete exceeds MSG_PAYLOAD_MAX");
static_assert(sizeof(OtaAbortPayload)     <= 24, "abort exceeds MSG_PAYLOAD_MAX");

/* =============================================================================
 * POLICY — tunable, NOT part of the wire format
 * ========================================================================== */

/** Gap between raw chunk sends. ESP-NOW's TX queue is shallow; blasting 3,938
 *  frames without pacing drops most of them. Tune on hardware. */
#define OTABULK_TX_PACING_MS    2

/** Passes before the hub gives up and sends OTA_ABORT. */
#define OTABULK_MAX_PASSES      8

/** Receiver aborts if no chunk and no control message for this long. */
#define OTABULK_SESSION_TIMEOUT_MS  30000

#endif /* OTA_BULK_H */
