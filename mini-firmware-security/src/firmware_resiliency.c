#include "firmware_resiliency.h"

#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#define RESILIENT_FW_HASH_SEED 0x9E3779B9

void resilient_fw_init(ResilientFW *fw) {
    uint32_t i;

    if (fw == NULL)
        return;

    memset(fw, 0, sizeof(ResilientFW));
    fw->active_fw_slot = 0;
    fw->recovery_fw_slot = RESILIENT_FW_GOLDEN_SLOT;
    fw->golden_fw_slot = RESILIENT_FW_GOLDEN_SLOT;
    fw->recovery_policy = RESILIENT_POLICY_AUTO;
    fw->auto_recovery = true;
    fw->boot_attempts = 0;
    fw->max_boot_attempts = 3;
    fw->fw_locked = false;
    fw->last_corruption_type = FW_BOOT_OK;

    fw->active_hash.valid = false;
    fw->recovery_hash.valid = false;
    fw->golden_hash.valid = false;

    for (i = 0; i < RESILIENT_FW_HASH_SIZE; i++) {
        fw->active_hash.hash[i] = 0;
        fw->recovery_hash.hash[i] = 0xFF;
        fw->golden_hash.hash[i] = 0xAA;
    }
    fw->active_hash.version = 1;
    fw->recovery_hash.version = 0;
    fw->golden_hash.version = 0;
    fw->active_hash.valid = false;
    fw->recovery_hash.valid = true;
    fw->golden_hash.valid = true;
}

bool resilient_fw_detect_corruption(ResilientFW *fw,
                                    const uint8_t *actual_hash,
                                    FWCorruptionType *corruption_type) {
    uint32_t i;

    if (fw == NULL || actual_hash == NULL || corruption_type == NULL)
        return false;

    *corruption_type = FW_BOOT_OK;

    if (!fw->active_hash.valid) {
        *corruption_type = INCOMPLETE_WRITE;
        fw->last_corruption_type = INCOMPLETE_WRITE;
        return true;
    }

    for (i = 0; i < RESILIENT_FW_HASH_SIZE; i++) {
        if (actual_hash[i] != fw->active_hash.hash[i]) {
            uint8_t diff = actual_hash[i] ^ fw->active_hash.hash[i];

            if ((diff & (diff - 1)) == 0 && diff != 0) {
                *corruption_type = RANDOM_BIT_FLIP;
            } else {
                *corruption_type = MALICIOUS_MODIFICATION;
            }

            fw->last_corruption_type = *corruption_type;
            return true;
        }
    }

    return false;
}

bool resilient_fw_recover_to_golden(ResilientFW *fw) {
    uint32_t i;

    if (fw == NULL)
        return false;

    if (!fw->golden_hash.valid)
        return false;

    fw->active_fw_slot = fw->golden_fw_slot;

    for (i = 0; i < RESILIENT_FW_HASH_SIZE; i++) {
        fw->active_hash.hash[i] = fw->golden_hash.hash[i];
    }
    fw->active_hash.version = fw->golden_hash.version;
    fw->active_hash.valid = true;
    fw->last_corruption_type = FW_BOOT_RECOVERED;

    return true;
}

bool resilient_fw_audit_log(FWAuditLog *log, uint32_t event_id,
                            const char *description) {
    size_t desc_len, copy_len;

    if (log == NULL || description == NULL)
        return false;

    if (log->entry_count >= RESILIENT_FW_MAX_LOG_SIZE)
        return false;

    log->entries[log->entry_count][0] = (uint8_t)(event_id & 0xFF);
    log->entries[log->entry_count][1] = (uint8_t)((event_id >> 8) & 0xFF);
    log->entries[log->entry_count][2] = (uint8_t)((event_id >> 16) & 0xFF);
    log->entries[log->entry_count][3] = (uint8_t)((event_id >> 24) & 0xFF);

    desc_len = strlen(description);
    copy_len = desc_len < 60 ? desc_len : 59;
    memcpy(&log->entries[log->entry_count][4], description, copy_len);
    log->entries[log->entry_count][4 + copy_len] = '\0';

    log->entry_count++;
    log->event_counter++;

    return true;
}

bool resilient_fw_verify_active(ResilientFW *fw,
                                const uint8_t *computed_hash) {
    FWCorruptionType corruption;
    bool corrupted;

    if (fw == NULL || computed_hash == NULL)
        return false;

    corrupted = resilient_fw_detect_corruption(fw, computed_hash, &corruption);

    if (corrupted) {
        if (fw->auto_recovery) {
            return resilient_fw_recover_to_golden(fw);
        }
        return false;
    }

    return true;
}

bool resilient_fw_set_recovery_policy(ResilientFW *fw, uint8_t policy) {
    if (fw == NULL)
        return false;

    if (fw->fw_locked)
        return false;

    if (policy != RESILIENT_POLICY_AUTO && policy != RESILIENT_POLICY_MANUAL)
        return false;

    fw->recovery_policy = policy;
    fw->auto_recovery = (policy == RESILIENT_POLICY_AUTO);

    return true;
}

bool resilient_fw_secure_update(ResilientFW *fw,
                                const uint8_t *new_fw_hash,
                                bool is_active_slot) {
    FWHash *target;

    if (fw == NULL || new_fw_hash == NULL)
        return false;

    if (fw->fw_locked)
        return false;

    if (is_active_slot) {
        target = &fw->active_hash;
    } else {
        target = &fw->recovery_hash;
    }

    memcpy(target->hash, new_fw_hash, RESILIENT_FW_HASH_SIZE);
    target->version++;
    target->valid = true;

    return true;
}

void resilient_fw_compute_hash(const uint8_t *data, size_t len,
                               uint8_t *hash_out) {
    uint32_t hash = RESILIENT_FW_HASH_SEED;
    size_t i;
    uint32_t pos;

    if (data == NULL || hash_out == NULL)
        return;

    for (i = 0; i < len; i++) {
        hash ^= (uint32_t)data[i];
        hash += (hash << 6) + (hash >> 2);
    }

    for (i = 0; i < RESILIENT_FW_HASH_SIZE; i++) {
        pos = (uint32_t)i;
        hash = hash * 1103515245 + 12345;
        hash_out[i] = (uint8_t)((hash >> 16) & 0xFF);
    }
}

bool resilient_fw_validate_golden(ResilientFW *fw) {
    uint32_t i;
    uint8_t sum;

    if (fw == NULL)
        return false;

    sum = 0;
    for (i = 0; i < RESILIENT_FW_HASH_SIZE; i++) {
        sum |= fw->golden_hash.hash[i];
    }

    return (sum != 0);
}

uint32_t resilient_fw_get_boot_count(ResilientFW *fw) {
    if (fw == NULL)
        return 0;

    return (uint32_t)fw->boot_attempts;
}

bool resilient_fw_rollback_protection(ResilientFW *fw, uint32_t min_version) {
    if (fw == NULL)
        return false;

    if (fw->active_hash.version < min_version)
        return false;

    if (fw->recovery_hash.version < min_version)
        return false;

    if (fw->golden_hash.version < min_version)
        return false;

    return true;
}
