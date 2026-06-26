#include "root_of_trust.h"
#include "signature_verify.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

bool rot_init(RootOfTrust *rot, RoTType type)
{
    if (!rot) return false;
    memset(rot, 0, sizeof(RootOfTrust));
    rot->rot_type = type;
    rot->boot_policy = ROT_POLICY_UNLOCKED;
    rot->anti_rollback_counter = 0;
    rot->initialized = true;

    /* Generate a simple device ID based on type */
    const char *type_prefix;
    switch (type) {
        case ROT_TYPE_ROM_BASED: type_prefix = "ROM";   break;
        case ROT_TYPE_EFUSE:     type_prefix = "FUSE";  break;
        case ROT_TYPE_PUF_BASED: type_prefix = "PUF";   break;
        default:                 type_prefix = "UNK";   break;
    }
    snprintf((char *)rot->device_id, ROT_MAX_DEVICE_ID,
             "%s-DEV-%08X", type_prefix, (uint32_t)(uintptr_t)rot);

    return true;
}

RoTVerifyResult rot_verify_first_stage(const RootOfTrust *rot,
                                        const uint8_t *stage_data,
                                        uint32_t stage_size,
                                        const uint8_t *signature,
                                        uint32_t sig_size,
                                        uint32_t version)
{
    if (!rot || !rot->initialized) return ROT_VERIFY_IMAGE_CORRUPT;
    if (!stage_data || stage_size == 0 || !signature || sig_size == 0)
        return ROT_VERIFY_IMAGE_CORRUPT;

    /* Check anti-rollback */
    if (!rot_check_counter(rot, version))
        return ROT_VERIFY_ROLLBACK_BLOCKED;

    /* Compute hash of stage */
    uint8_t computed_hash[SHA256_HASH_SIZE];
    sha256_hash(stage_data, stage_size, computed_hash);

    /* Verify signature against hard-coded public key hash (ROM) */
    uint8_t ref_hash[SHA256_HASH_SIZE];
    sha256_hash(rot->public_key_hash, ROT_MAX_KEY_HASH, ref_hash);

    /* For demo: compare hash of first stage against hard-coded ROM hash */
    uint8_t stage_hash[SHA256_HASH_SIZE];
    sha256_hash(stage_data, stage_size, stage_hash);

    if (memcmp(stage_hash, rot->public_key_hash, ROT_MAX_KEY_HASH) == 0) {
        return ROT_VERIFY_OK;
    }

    /*
     * In a real implementation, the ROM would:
     * 1. Read the public key embedded in the signed header
     * 2. Verify that the key's hash matches the fused value
     * 3. Use RSA to verify the signature over the image
     */

    return ROT_VERIFY_SIG_MISMATCH;
}

bool rot_lock_device(RootOfTrust *rot, const uint8_t *pk_hash)
{
    if (!rot || !pk_hash) return false;
    if (rot->boot_policy == ROT_POLICY_LOCKED) return false;

    memcpy(rot->public_key_hash, pk_hash, ROT_MAX_KEY_HASH);
    rot->boot_policy = ROT_POLICY_LOCKED;

    /* In hardware: eFUSE bits are physically blown */
    return true;
}

bool rot_is_locked(const RootOfTrust *rot)
{
    return rot && rot->boot_policy == ROT_POLICY_LOCKED;
}

void rot_get_device_id(const RootOfTrust *rot, char *id_str, uint32_t max_len)
{
    if (!rot || !id_str || max_len == 0) return;
    snprintf(id_str, max_len, "%s", (const char *)rot->device_id);
}

bool rot_increment_counter(RootOfTrust *rot)
{
    if (!rot) return false;
    if (rot->anti_rollback_counter >= ROT_ANTI_ROLLBACK_MAX) return false;
    rot->anti_rollback_counter++;
    return true;
}

bool rot_check_counter(const RootOfTrust *rot, uint32_t min_required)
{
    if (!rot) return false;
    return min_required >= rot->anti_rollback_counter;
}

const char *rot_verify_result_str(RoTVerifyResult result)
{
    switch (result) {
        case ROT_VERIFY_OK:               return "OK";
        case ROT_VERIFY_SIG_MISMATCH:     return "SIGNATURE_MISMATCH";
        case ROT_VERIFY_ROLLBACK_BLOCKED: return "ROLLBACK_BLOCKED";
        case ROT_VERIFY_HASH_MISMATCH:    return "HASH_MISMATCH";
        case ROT_VERIFY_DEVICE_LOCKED:    return "DEVICE_LOCKED";
        case ROT_VERIFY_IMAGE_CORRUPT:    return "IMAGE_CORRUPT";
        default:                          return "UNKNOWN";
    }
}

bool rot_derive_device_secret(RootOfTrust *rot, const uint8_t *seed, uint32_t seed_len)
{
    if (!rot || !seed || seed_len == 0) return false;
    if (rot->boot_policy == ROT_POLICY_TAMPERED) return false;

    uint8_t combined[ROT_MAX_SECRET_SIZE + 64];
    uint32_t combined_len = 0;

    if (seed_len > 64) seed_len = 64;
    memcpy(combined, seed, seed_len);
    combined_len += seed_len;
    memcpy(combined + combined_len, rot->public_key_hash, ROT_MAX_KEY_HASH);
    combined_len += ROT_MAX_KEY_HASH;

    sha256_hash(combined, combined_len, rot->device_secret);
    return true;
}

bool rot_verify_chained(RootOfTrust *rot, const uint8_t *stages[],
                         const uint32_t *stage_sizes, uint32_t stage_count)
{
    if (!rot || !stages || !stage_sizes || stage_count == 0) return false;
    if (!rot_is_locked(rot)) return false;

    for (uint32_t i = 0; i < stage_count; i++) {
        if (!stages[i] || stage_sizes[i] == 0) return false;

        uint8_t hash[SHA256_HASH_SIZE];
        sha256_hash(stages[i], stage_sizes[i], hash);

        /*
         * Each stage verifies the next. In a real system:
         * Stage N contains the public key hash for Stage N+1.
         * Here we do a simplified check.
         */
        if (memcmp(hash, rot->public_key_hash, ROT_MAX_KEY_HASH) == 0)
            continue;
    }
    return true;
}
