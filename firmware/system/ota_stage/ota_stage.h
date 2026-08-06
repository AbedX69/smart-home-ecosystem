/*
 * =============================================================================
 * FILE:        ota_stage.h
 * AUTHOR:      AbedX69
 * CREATED:     2026-08-06
 * VERSION:     1.0.0
 * LICENSE:     MIT
 * PLATFORM:    ESP32-S3 (hub) — ESP-IDF v5.x
 * =============================================================================
 *
 * Owns the hub's `storage` partition as a holding area for ANOTHER device's
 * firmware image.
 *
 * This is not OTA. Nothing here touches the hub's own app slots, otadata, or
 * boot selection. The hub is a file server: it accepts a .bin, parks it, and
 * hands the bytes to whichever transport ships them onward. If this component
 * is wrong, the worst case is a failed transfer - never a bricked hub.
 *
 * =============================================================================
 * WHY NOT ota_http's UPLOAD PATH
 * =============================================================================
 *
 * ota_http streams uploads into esp_ota_get_next_update_partition() - the
 * HUB's inactive slot. Staging a strip image there would set the hub up to
 * boot strip firmware on its next reset. Every assumption in that file is
 * "these bytes are for this device". None of them hold here.
 *
 * =============================================================================
 * LAYOUT
 * =============================================================================
 *
 * The image is written RAW at storage offset 0, with no prepended header.
 * That is deliberate: esp_app_desc_t lives 32 bytes into any ESP-IDF image
 * (24 B image header + 8 B first segment header), and the hub's existing
 * logStagedImage() reads it from exactly there. A header would break it.
 *
 * Metadata therefore lives in NVS, under the shared "smarthome" namespace:
 *
 *   stg_valid   u8    1 once a complete image has been staged and CRC'd
 *   stg_size    u32   image length in bytes
 *   stg_crc     u32   zlib CRC-32 of the whole image
 *   stg_role    u8    DeviceRole this image is FOR
 *   stg_ver     str   "major.minor.patch" from the image's app descriptor
 *   stg_name    str   project_name from the image's app descriptor
 *
 * stg_valid is cleared at stageBegin() and set only at stageFinish(), so a
 * crash or power loss mid-upload leaves a partial image marked invalid rather
 * than a valid marker over garbage.
 *
 * =============================================================================
 * TARGET ROLE IS SUPPLIED, NOT INFERRED
 * =============================================================================
 *
 * project_name in the app descriptor says "smart_light_strip", and it would be
 * tempting to route on that. Don't. Project names must stay freely changeable -
 * device identity in this ecosystem comes from the UID and the registry, never
 * from a build artefact's name. The uploader states the target role; the name
 * is recorded for humans reading a status page and for nothing else.
 *
 * =============================================================================
 * USAGE
 * =============================================================================
 *
 *   Write side (an HTTP handler, a phone app, anything):
 *
 *     OtaStage& stg = OtaStage::instance();
 *     stg.stageBegin(content_len, DeviceRole::LIGHT);
 *     while (more) stg.stageWrite(buf, n);
 *     stg.stageFinish();          // CRC + app descriptor + NVS commit
 *
 *   Read side (ota_bulk_tx):
 *
 *     if (stg.hasImage()) {
 *         const StagedImageInfo& i = stg.info();
 *         stg.readAt(chunk_index * 240, buf, 240);
 *     }
 *
 * =============================================================================
 */

#ifndef OTA_STAGE_H
#define OTA_STAGE_H

#include <cstdint>
#include <cstddef>

#include "esp_err.h"
#include "esp_partition.h"

#include "core_types.h"

/** Name of the data partition used as the holding area. */
#define OTA_STAGE_PARTITION_LABEL   "storage"

/** Block size for CRC read-back. Matches OTAManager::verifyCrc32. */
#define OTA_STAGE_CRC_BLOCK         256

/** Longest version string we keep, e.g. "255.255.255". */
#define OTA_STAGE_VER_LEN           16
#define OTA_STAGE_NAME_LEN          32

/* ─── NVS keys (15 char limit) ───────────────────────────────────────────── */
#define OTA_STAGE_KEY_VALID         "stg_valid"
#define OTA_STAGE_KEY_SIZE          "stg_size"
#define OTA_STAGE_KEY_CRC           "stg_crc"
#define OTA_STAGE_KEY_ROLE          "stg_role"
#define OTA_STAGE_KEY_VER           "stg_ver"
#define OTA_STAGE_KEY_NAME          "stg_name"

/**
 * @brief What is currently parked in storage.
 *
 * `valid` false means every other field is meaningless.
 */
struct StagedImageInfo {
    bool        valid;
    uint32_t    size;                       ///< bytes
    uint32_t    crc32;                      ///< zlib CRC-32, seed 0
    DeviceRole  role;                       ///< which role this image is FOR
    uint8_t     ver_major;
    uint8_t     ver_minor;
    uint8_t     ver_patch;
    char        version[OTA_STAGE_VER_LEN]; ///< as written in the descriptor
    char        name[OTA_STAGE_NAME_LEN];   ///< project_name, humans only
};

class OtaStage {
public:
    static OtaStage& instance();
    OtaStage(const OtaStage&)            = delete;
    OtaStage& operator=(const OtaStage&) = delete;

    /**
     * @brief Locate the storage partition and load metadata from NVS.
     *
     * ConfigStore::begin() must have run first.
     */
    esp_err_t begin();

    /* ─── Write side ───────────────────────────────────────────────────── */

    /**
     * @brief Start staging. Erases enough of the partition for `total_size`
     *        and marks any previously staged image invalid.
     *
     * @param total_size   exact image length, must be known up front
     * @param target_role  the role this image is FOR, not the hub's own
     * @return ESP_ERR_INVALID_SIZE if it will not fit the partition
     */
    esp_err_t stageBegin(size_t total_size, DeviceRole target_role);

    /** @brief Append. Writes are sequential; offset is tracked internally. */
    esp_err_t stageWrite(const void* data, size_t len);

    /**
     * @brief Seal the image: verify length, parse the app descriptor,
     *        CRC the whole thing, then commit metadata to NVS.
     *
     * Only after this returns ESP_OK does hasImage() become true.
     */
    esp_err_t stageFinish();

    /** @brief Discard a partial stage. The old image stays invalid. */
    void stageAbort();

    bool     isStaging()   const { return _staging; }
    uint32_t bytesStaged() const { return _written; }
    uint32_t expectedSize()const { return _expected; }

    /* ─── Read side ────────────────────────────────────────────────────── */

    bool hasImage() const { return _info.valid; }

    const StagedImageInfo& info() const { return _info; }

    /**
     * @brief Read `len` bytes from `offset` within the staged image.
     *
     * Bounds-checked against the staged length, not the partition size, so a
     * transport cannot walk off the end of the image into stale flash.
     */
    esp_err_t readAt(uint32_t offset, void* out, size_t len) const;

    /** @brief Mark the staged image invalid. Does not erase flash. */
    esp_err_t clear();

    /** @brief Total capacity of the storage partition, bytes. */
    uint32_t capacity() const { return _part ? _part->size : 0; }

private:
    OtaStage();

    esp_err_t loadMeta();
    esp_err_t saveMeta();
    esp_err_t computeCrc(uint32_t len, uint32_t* out_crc) const;
    esp_err_t readAppDesc();

    const esp_partition_t* _part;
    bool                   _initialized;

    bool                   _staging;
    uint32_t               _expected;
    uint32_t               _written;
    DeviceRole             _pending_role;

    StagedImageInfo        _info;
};

#endif /* OTA_STAGE_H */
