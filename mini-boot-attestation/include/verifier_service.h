#ifndef VERIFIER_SERVICE_H
#define VERIFIER_SERVICE_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "tpm_quote.h"
#include "aik_identity.h"
#include "attest_protocol.h"

#define ATTEST_DB_MAX_DEVICES      256
#define ATTEST_DEVICE_ID_SIZE      64
#define ATTEST_FW_WHITELIST_MAX    32
#define ATTEST_FW_BINARY_SIZE      32
#define ATTEST_POLICY_MAX_RULES    16
#define ATTEST_DEVICE_HOSTNAME_SIZE 128

typedef enum {
    ATTEST_RESULT_TRUSTED   = 0,
    ATTEST_RESULT_UNTRUSTED = 1,
    ATTEST_RESULT_UNKNOWN   = 2
} AttestResult;

typedef struct {
    uint8_t  binary_hash[TPM_SHA256_DIGEST_SIZE];
    uint8_t  version[16];
    uint32_t release_date;
} AttestFirmwareRecord;

typedef struct {
    uint8_t  pcr_index;
    uint8_t  expected_value[TPM_SHA256_DIGEST_SIZE];
    uint16_t pcr_mask;
    AttestPolicyResult default_action;
    uint8_t  rule_name[ATTEST_POLICY_NAME_SIZE];
} AttestPolicyRule;

typedef struct {
    uint8_t      device_id[ATTEST_DEVICE_ID_SIZE];
    uint8_t      ek_pub_hash[EK_PUB_HASH_SIZE];
    uint8_t      expected_pcr_values[ATTEST_MAX_PCR_VALUES][TPM_SHA256_DIGEST_SIZE];
    uint16_t     expected_pcr_count;
    AttestFirmwareRecord firmware_whitelist[ATTEST_FW_WHITELIST_MAX];
    uint16_t     fw_whitelist_count;
    AttestPolicyRule policy_rules[ATTEST_POLICY_MAX_RULES];
    uint16_t     policy_rule_count;
    uint64_t     last_attest_time;
    uint32_t     attest_fail_count;
    AttestResult last_result;
    bool         registered;
    bool         ownership_taken;
    bool         locked;
} AttestDeviceEntry;

typedef struct {
    AttestDeviceEntry device_entries[ATTEST_DB_MAX_DEVICES];
    uint16_t          device_count;
    TPMKeyPublic      signing_key;
    uint8_t           verifier_id[ATTEST_DEVICE_ID_SIZE];
    bool              initialized;
} AttestDB;

int32_t  attest_service_init(AttestDB *db, const uint8_t *verifier_id);

int32_t  attest_service_register_device(AttestDB *db,
                                        const uint8_t *device_id,
                                        const uint8_t *ek_pub_hash,
                                        const TPMHash *expected_pcrs,
                                        uint16_t expected_pcr_count);

int32_t  attest_service_take_ownership(AttestDB *db,
                                       const uint8_t *device_id);

int32_t  attest_service_clear_device(AttestDB *db,
                                     const uint8_t *device_id);

int32_t  attest_service_lock_device(AttestDB *db,
                                    const uint8_t *device_id,
                                    bool lock_state);

int32_t  attest_service_verify_quote(AttestDB *db,
                                     const uint8_t *device_id,
                                     const AttestResponse *response,
                                     const AttestChallenge *challenge,
                                     AttestResult *result,
                                     AttestVerdict *verdict);

int32_t  attest_service_update_policy(AttestDB *db,
                                      const uint8_t *device_id,
                                      const AttestPolicyRule *rules,
                                      uint16_t rule_count);

int32_t  attest_service_add_firmware_whitelist(AttestDB *db,
                                               const uint8_t *device_id,
                                               const AttestFirmwareRecord *fw,
                                               uint16_t fw_count);

int32_t  attest_service_update_pcr_expected(AttestDB *db,
                                            const uint8_t *device_id,
                                            const TPMHash *expected_pcrs,
                                            uint16_t expected_pcr_count);

int32_t  attest_service_find_device(const AttestDB *db,
                                    const uint8_t *device_id,
                                    AttestDeviceEntry *entry_out);

int32_t  attest_service_get_attest_result(const AttestDB *db,
                                          const uint8_t *device_id,
                                          AttestResult *result);

void     attest_result_to_string(AttestResult result,
                                 char *buf, size_t buf_size);

void     attest_device_entry_dump(const AttestDeviceEntry *entry);

#endif
