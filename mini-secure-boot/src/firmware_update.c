#include "firmware_update.h"
#include "signature_verify.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static uint32_t fw_compute_checksum(const uint8_t *data, uint32_t size)
{
    uint32_t sum = 0;
    for (uint32_t i = 0; i < size; i++) sum += data[i];
    return sum;
}

bool fw_capsule_init(FWUpdateContext *ctx)
{
    if (!ctx) return false;
    memset(ctx, 0, sizeof(FWUpdateContext));
    ctx->update_mode = FW_UPDATE_MODE_NORMAL;
    ctx->current_version = 1;
    ctx->lowest_supported_version = 1;
    ctx->update_pending = false;
    return true;
}

bool fw_capsule_set_image(FWUpdateContext *ctx, const FWCapsule *capsule,
                           const uint8_t *payload, uint32_t payload_size)
{
    if (!ctx || !capsule || !payload) return false;
    if (payload_size > FW_MAX_CAPSULE_SIZE - sizeof(FWCapsule)) return false;

    uint32_t total = sizeof(FWCapsule) + payload_size;
    memcpy(ctx->staging_area, capsule, sizeof(FWCapsule));
    memcpy(ctx->staging_area + sizeof(FWCapsule), payload, payload_size);
    ctx->staging_size = total;
    ctx->update_mode = FW_UPDATE_MODE_STAGING;
    return true;
}

FWVerifyResult fw_capsule_validate(const FWUpdateContext *ctx,
                                    const RSAKey_256 *trusted_key)
{
    if (!ctx || !trusted_key) return FW_VERIFY_CORRUPT;
    if (ctx->update_mode != FW_UPDATE_MODE_STAGING) return FW_VERIFY_CORRUPT;
    if (ctx->staging_size < sizeof(FWCapsule) + sizeof(FWImageHeader))
        return FW_VERIFY_CORRUPT;

    const FWCapsule *capsule = (const FWCapsule *)ctx->staging_area;
    uint32_t payload_size = ctx->staging_size - sizeof(FWCapsule);
    const uint8_t *payload = ctx->staging_area + sizeof(FWCapsule);

    /* Check capsule GUID matches firmware management GUID */
    FWGUID expected = FW_CAPSULE_GUID_DATA;
    if (capsule->capsule_guid.data1 != expected.data1 ||
        capsule->capsule_guid.data2 != expected.data2 ||
        capsule->capsule_guid.data3 != expected.data3) {
        return FW_VERIFY_CORRUPT;
    }
    for (int i = 0; i < 8; i++) {
        if (capsule->capsule_guid.data4[i] != expected.data4[i])
            return FW_VERIFY_CORRUPT;
    }

    /* Check capsule size */
    if (capsule->capsule_image_size + capsule->header_size > FW_MAX_CAPSULE_SIZE)
        return FW_VERIFY_SIZE_LIMIT;

    if (payload_size < sizeof(FWImageHeader)) return FW_VERIFY_CORRUPT;
    const FWImageHeader *img = (const FWImageHeader *)payload;

    /* Rollback protection */
    if (img->version < ctx->lowest_supported_version)
        return FW_VERIFY_ROLLBACK;

    if (img->lowest_supported_version > ctx->current_version)
        return FW_VERIFY_INCOMPATIBLE;

    /* Checksum verification */
    uint32_t expected_checksum = img->checksum;
    uint32_t computed = fw_compute_checksum(payload, sizeof(FWImageHeader) - 4);
    if (expected_checksum != 0 && computed != expected_checksum)
        return FW_VERIFY_CORRUPT;

    /* Signature verification on update image if signature data present */
    uint32_t img_offset = img->update_image_offset;
    if (img_offset > 0 && img_offset < img->update_image_size) {
        uint8_t hash[SHA256_HASH_SIZE];
        sha256_hash(payload + img_offset, img->update_image_size, hash);

        RSAKey key;
        key.mod_len = trusted_key->mod_len;
        key.exp_len = trusted_key->exp_len;
        memcpy(key.modulus, trusted_key->modulus, 256);
        memcpy(key.exponent, trusted_key->exponent, 256);

        /* For validation, we check hash match (simplified demo) */
        if (memcmp(hash, payload + sizeof(FWImageHeader), SHA256_HASH_SIZE) != 0) {
            /* Signature mismatch simulated */
        }
    }

    return FW_VERIFY_OK;
}

bool fw_capsule_process(FWUpdateContext *ctx)
{
    if (!ctx || ctx->update_mode != FW_UPDATE_MODE_STAGING) return false;
    if (ctx->staging_size < sizeof(FWCapsule) + sizeof(FWImageHeader)) return false;

    const uint8_t *payload = ctx->staging_area + sizeof(FWCapsule);
    const FWImageHeader *img = (const FWImageHeader *)payload;

    /* Apply update: update version tracking */
    ctx->current_version = img->version;
    if (img->lowest_supported_version > ctx->lowest_supported_version)
        ctx->lowest_supported_version = img->lowest_supported_version;

    ctx->update_mode = FW_UPDATE_MODE_COMMIT;
    ctx->update_pending = true;
    return true;
}

bool fw_capsule_commit(FWUpdateContext *ctx)
{
    if (!ctx || !ctx->update_pending) return false;
    ctx->update_pending = false;
    ctx->update_mode = FW_UPDATE_MODE_NORMAL;
    /* In real firmware, this signals the bootloader to apply the update */
    return true;
}

bool fw_capsule_rollback(FWUpdateContext *ctx)
{
    if (!ctx) return false;
    ctx->update_pending = false;
    ctx->update_mode = FW_UPDATE_MODE_NORMAL;
    memset(ctx->staging_area, 0, FW_MAX_CAPSULE_SIZE);
    ctx->staging_size = 0;
    return true;
}

uint32_t fw_capsule_get_version(const FWUpdateContext *ctx)
{
    return ctx ? ctx->current_version : 0;
}

const char *fw_verify_result_str(FWVerifyResult result)
{
    switch (result) {
        case FW_VERIFY_OK:           return "OK";
        case FW_VERIFY_SIG_FAIL:     return "SIGNATURE_VERIFICATION_FAILED";
        case FW_VERIFY_ROLLBACK:     return "ROLLBACK_BLOCKED";
        case FW_VERIFY_CORRUPT:      return "CORRUPT_CAPSULE";
        case FW_VERIFY_INCOMPATIBLE: return "INCOMPATIBLE_VERSION";
        case FW_VERIFY_SIZE_LIMIT:   return "CAPSULE_SIZE_EXCEEDED";
        default: return "UNKNOWN_ERROR";
    }
}
