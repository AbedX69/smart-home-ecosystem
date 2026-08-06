/*
 * =============================================================================
 * FILE:        ota_stage.cpp
 * AUTHOR:      AbedX69
 * CREATED:     2026-08-06
 * VERSION:     1.0.0
 * =============================================================================
 *
 * See ota_stage.h for the layout and why metadata lives in NVS rather than
 * in a header prepended to the image.
 *
 * =============================================================================
 */

#include "ota_stage.h"

#include <cstring>
#include <cstdio>
#include <esp_log.h>
#include <esp_crc.h>
#include <esp_app_format.h>

#include "config_store.h"

static const char* TAG = "OtaStage";

/* esp_app_desc_t sits after the image header (24 B) and the first segment
 * header (8 B). Hardcoded to match the hub's existing logStagedImage() rather
 * than pulling in bootloader_support for a sizeof. */
#define APP_DESC_OFFSET     32

/* Flash erase granularity. */
#define ERASE_ALIGN         4096


/* =============================================================================
 * SINGLETON
 * ========================================================================== */

OtaStage& OtaStage::instance() {
    static OtaStage inst;
    return inst;
}

OtaStage::OtaStage()
    : _part(nullptr)
    , _initialized(false)
    , _staging(false)
    , _expected(0)
    , _written(0)
    , _pending_role(DeviceRole::UNKNOWN)
    , _info{}
{}


/* =============================================================================
 * LIFECYCLE
 * ========================================================================== */

esp_err_t OtaStage::begin() {
    if (_initialized) return ESP_OK;

    /* SUBTYPE_ANY on purpose: the CSVs have carried both `spiffs` and
     * `undefined` for this partition. The label is the contract. */
    _part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                     ESP_PARTITION_SUBTYPE_ANY,
                                     OTA_STAGE_PARTITION_LABEL);
    if (!_part) {
        ESP_LOGE(TAG, "No \"%s\" partition - check the partition CSV",
                 OTA_STAGE_PARTITION_LABEL);
        return ESP_ERR_NOT_FOUND;
    }

    loadMeta();
    _initialized = true;

    ESP_LOGI(TAG, "═══════════════════════════════════════════");
    ESP_LOGI(TAG, "  Stage area: %s @ 0x%06lX (%lu KB)",
             _part->label, (unsigned long)_part->address,
             (unsigned long)_part->size / 1024);
    if (_info.valid) {
        ESP_LOGI(TAG, "  Staged: \"%s\" v%s for %s",
                 _info.name, _info.version, deviceRoleName(_info.role));
        ESP_LOGI(TAG, "  %lu B, crc %08lX",
                 (unsigned long)_info.size, (unsigned long)_info.crc32);
    } else {
        ESP_LOGI(TAG, "  Staged: none");
    }
    ESP_LOGI(TAG, "═══════════════════════════════════════════");
    return ESP_OK;
}


/* =============================================================================
 * WRITE SIDE
 * ========================================================================== */

esp_err_t OtaStage::stageBegin(size_t total_size, DeviceRole target_role) {
    if (!_initialized)  return ESP_ERR_INVALID_STATE;
    if (_staging)       return ESP_ERR_INVALID_STATE;
    if (total_size == 0) return ESP_ERR_INVALID_ARG;

    if (total_size > _part->size) {
        ESP_LOGE(TAG, "Image %lu B exceeds stage area %lu B",
                 (unsigned long)total_size, (unsigned long)_part->size);
        return ESP_ERR_INVALID_SIZE;
    }

    /* Invalidate FIRST. A crash between here and stageFinish() must leave a
     * partial image marked invalid, never a valid marker over garbage. */
    _info.valid = false;
    ConfigStore::instance().setU8(OTA_STAGE_KEY_VALID, 0);

    uint32_t erase_len = (uint32_t)((total_size + ERASE_ALIGN - 1)
                                    / ERASE_ALIGN) * ERASE_ALIGN;
    if (erase_len > _part->size) erase_len = _part->size;

    esp_err_t err = esp_partition_erase_range(_part, 0, erase_len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Erase of %lu B failed: %s",
                 (unsigned long)erase_len, esp_err_to_name(err));
        return err;
    }

    _staging      = true;
    _expected     = (uint32_t)total_size;
    _written      = 0;
    _pending_role = target_role;

    ESP_LOGI(TAG, "Staging %lu B for %s (erased %lu B)",
             (unsigned long)total_size, deviceRoleName(target_role),
             (unsigned long)erase_len);
    return ESP_OK;
}

esp_err_t OtaStage::stageWrite(const void* data, size_t len) {
    if (!_staging)          return ESP_ERR_INVALID_STATE;
    if (!data || len == 0)  return ESP_ERR_INVALID_ARG;

    if (_written + len > _expected) {
        ESP_LOGE(TAG, "Write of %u B at %lu overruns declared size %lu",
                 (unsigned)len, (unsigned long)_written,
                 (unsigned long)_expected);
        stageAbort();
        return ESP_ERR_INVALID_SIZE;
    }

    esp_err_t err = esp_partition_write(_part, _written, data, len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Write at %lu failed: %s",
                 (unsigned long)_written, esp_err_to_name(err));
        stageAbort();
        return err;
    }

    _written += (uint32_t)len;
    return ESP_OK;
}

esp_err_t OtaStage::stageFinish() {
    if (!_staging) return ESP_ERR_INVALID_STATE;

    if (_written != _expected) {
        ESP_LOGE(TAG, "Incomplete: %lu of %lu B",
                 (unsigned long)_written, (unsigned long)_expected);
        stageAbort();
        return ESP_ERR_INVALID_SIZE;
    }

    /* Parse the app descriptor. A file that is not an ESP-IDF image has no
     * business being shipped to a node, so this doubles as a sanity gate. */
    esp_err_t err = readAppDesc();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Not a valid ESP-IDF image - no app descriptor");
        stageAbort();
        return err;
    }

    uint32_t crc = 0;
    err = computeCrc(_written, &crc);
    if (err != ESP_OK) {
        stageAbort();
        return err;
    }

    _info.valid = true;
    _info.size  = _written;
    _info.crc32 = crc;
    _info.role  = _pending_role;

    _staging = false;
    _expected = 0;
    _written  = 0;

    err = saveMeta();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Metadata commit failed: %s", esp_err_to_name(err));
        _info.valid = false;
        return err;
    }

    ESP_LOGI(TAG, "Staged \"%s\" v%s for %s - %lu B, crc %08lX",
             _info.name, _info.version, deviceRoleName(_info.role),
             (unsigned long)_info.size, (unsigned long)_info.crc32);
    return ESP_OK;
}

void OtaStage::stageAbort() {
    if (!_staging) return;
    ESP_LOGW(TAG, "Stage aborted at %lu of %lu B",
             (unsigned long)_written, (unsigned long)_expected);
    _staging  = false;
    _expected = 0;
    _written  = 0;
    _info.valid = false;
    ConfigStore::instance().setU8(OTA_STAGE_KEY_VALID, 0);
}


/* =============================================================================
 * READ SIDE
 * ========================================================================== */

esp_err_t OtaStage::readAt(uint32_t offset, void* out, size_t len) const {
    if (!_initialized || !_info.valid) return ESP_ERR_INVALID_STATE;
    if (!out || len == 0)              return ESP_ERR_INVALID_ARG;

    /* Bounded by the IMAGE length, not the partition. A transport must not be
     * able to read past the image into whatever the last upload left behind. */
    if (offset >= _info.size || offset + len > _info.size) {
        return ESP_ERR_INVALID_SIZE;
    }
    return esp_partition_read(_part, offset, out, len);
}

esp_err_t OtaStage::clear() {
    if (!_initialized) return ESP_ERR_INVALID_STATE;
    _info.valid = false;
    ESP_LOGI(TAG, "Staged image cleared");
    return ConfigStore::instance().setU8(OTA_STAGE_KEY_VALID, 0);
}


/* =============================================================================
 * INTERNALS
 * ========================================================================== */

esp_err_t OtaStage::readAppDesc() {
    esp_app_desc_t d;
    esp_err_t err = esp_partition_read(_part, APP_DESC_OFFSET, &d, sizeof(d));
    if (err != ESP_OK) return err;
    if (d.magic_word != ESP_APP_DESC_MAGIC_WORD) return ESP_ERR_INVALID_ARG;

    strncpy(_info.version, d.version, OTA_STAGE_VER_LEN - 1);
    _info.version[OTA_STAGE_VER_LEN - 1] = '\0';
    strncpy(_info.name, d.project_name, OTA_STAGE_NAME_LEN - 1);
    _info.name[OTA_STAGE_NAME_LEN - 1] = '\0';

    /* Best-effort semver split. A non-numeric version is not fatal - the
     * numbers only travel in the offer for the node to log. */
    unsigned ma = 0, mi = 0, pa = 0;
    sscanf(_info.version, "%u.%u.%u", &ma, &mi, &pa);
    _info.ver_major = (uint8_t)(ma & 0xFF);
    _info.ver_minor = (uint8_t)(mi & 0xFF);
    _info.ver_patch = (uint8_t)(pa & 0xFF);
    return ESP_OK;
}

esp_err_t OtaStage::computeCrc(uint32_t len, uint32_t* out_crc) const {
    uint8_t  buf[OTA_STAGE_CRC_BLOCK];
    uint32_t crc = 0;            /* seed 0 == standard zlib CRC-32 */
    uint32_t off = 0;

    while (off < len) {
        uint32_t n = (len - off > sizeof(buf))
                     ? (uint32_t)sizeof(buf) : (len - off);
        esp_err_t err = esp_partition_read(_part, off, buf, n);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "CRC read failed at %lu: %s",
                     (unsigned long)off, esp_err_to_name(err));
            return err;
        }
        crc = esp_crc32_le(crc, buf, n);
        off += n;
    }
    *out_crc = crc;
    return ESP_OK;
}

esp_err_t OtaStage::loadMeta() {
    ConfigStore& cfg = ConfigStore::instance();

    memset(&_info, 0, sizeof(_info));
    _info.valid = (cfg.getU8(OTA_STAGE_KEY_VALID, 0) != 0);
    if (!_info.valid) return ESP_OK;

    _info.size  = cfg.getU32(OTA_STAGE_KEY_SIZE, 0);
    _info.crc32 = cfg.getU32(OTA_STAGE_KEY_CRC, 0);
    _info.role  = (DeviceRole)cfg.getU8(OTA_STAGE_KEY_ROLE,
                                        (uint8_t)DeviceRole::UNKNOWN);
    cfg.getString(OTA_STAGE_KEY_VER,  _info.version, OTA_STAGE_VER_LEN,  "?");
    cfg.getString(OTA_STAGE_KEY_NAME, _info.name,    OTA_STAGE_NAME_LEN, "?");

    unsigned ma = 0, mi = 0, pa = 0;
    sscanf(_info.version, "%u.%u.%u", &ma, &mi, &pa);
    _info.ver_major = (uint8_t)(ma & 0xFF);
    _info.ver_minor = (uint8_t)(mi & 0xFF);
    _info.ver_patch = (uint8_t)(pa & 0xFF);

    /* A size of zero or one past the partition means the metadata and the
     * flash disagree. Trust neither. */
    if (_info.size == 0 || _info.size > _part->size) {
        ESP_LOGW(TAG, "Staged size %lu is impossible - marking invalid",
                 (unsigned long)_info.size);
        _info.valid = false;
        cfg.setU8(OTA_STAGE_KEY_VALID, 0);
    }
    return ESP_OK;
}

esp_err_t OtaStage::saveMeta() {
    ConfigStore& cfg = ConfigStore::instance();

    cfg.setU32(OTA_STAGE_KEY_SIZE, _info.size);
    cfg.setU32(OTA_STAGE_KEY_CRC,  _info.crc32);
    cfg.setU8 (OTA_STAGE_KEY_ROLE, (uint8_t)_info.role);
    cfg.setString(OTA_STAGE_KEY_VER,  _info.version);
    cfg.setString(OTA_STAGE_KEY_NAME, _info.name);

    /* Valid flag LAST, so a power loss part-way through leaves the image
     * marked invalid rather than valid with half its metadata written. */
    return cfg.setU8(OTA_STAGE_KEY_VALID, 1);
}
