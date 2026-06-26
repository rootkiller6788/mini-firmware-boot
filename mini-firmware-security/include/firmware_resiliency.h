#ifndef FIRMWARE_RESILIENCY_H
#define FIRMWARE_RESILIENCY_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define RESILIENT_FW_HASH_SIZE       32
#define RESILIENT_FW_MAX_LOG_SIZE    64
#define RESILIENT_FW_GOLDEN_SLOT     1

#define RESILIENT_POLICY_AUTO        0
#define RESILIENT_POLICY_MANUAL      1

#define FW_BOOT_OK                   0
#define FW_BOOT_FAILED               1
#define FW_BOOT_RECOVERED            2

typedef enum {
    RANDOM_BIT_FLIP = 0,
    MALICIOUS_MODIFICATION,
    INCOMPLETE_WRITE,
    AUTHENTICATION_FAILURE,
    ROLLBACK_DETECTED,
    FW_CORRUPTION_COUNT
} FWCorruptionType;

typedef struct {
    uint8_t  hash[RESILIENT_FW_HASH_SIZE];
    uint32_t version;
    bool     valid;
} FWHash;

typedef struct {
    uint8_t          active_fw_slot;
    uint8_t          recovery_fw_slot;
    uint8_t          golden_fw_slot;
    FWHash           active_hash;
    FWHash           recovery_hash;
    FWHash           golden_hash;
    uint8_t          recovery_policy;
    bool             auto_recovery;
    uint8_t          boot_attempts;
    uint8_t          max_boot_attempts;
    bool             fw_locked;
    uint32_t         last_corruption_type;
} ResilientFW;

typedef struct {
    uint8_t  entries[RESILIENT_FW_MAX_LOG_SIZE][64];
    size_t   entry_count;
    uint32_t event_counter;
} FWAuditLog;

void resilient_fw_init(ResilientFW *fw);
bool resilient_fw_detect_corruption(ResilientFW *fw,
                                    const uint8_t *actual_hash,
                                    FWCorruptionType *corruption_type);
bool resilient_fw_recover_to_golden(ResilientFW *fw);
bool resilient_fw_audit_log(FWAuditLog *log, uint32_t event_id,
                            const char *description);
bool resilient_fw_verify_active(ResilientFW *fw,
                                const uint8_t *computed_hash);
bool resilient_fw_set_recovery_policy(ResilientFW *fw, uint8_t policy);
bool resilient_fw_secure_update(ResilientFW *fw,
                                const uint8_t *new_fw_hash,
                                bool is_active_slot);
void resilient_fw_compute_hash(const uint8_t *data, size_t len,
                               uint8_t *hash_out);
bool resilient_fw_validate_golden(ResilientFW *fw);
uint32_t resilient_fw_get_boot_count(ResilientFW *fw);
bool resilient_fw_rollback_protection(ResilientFW *fw, uint32_t min_version);

#endif
