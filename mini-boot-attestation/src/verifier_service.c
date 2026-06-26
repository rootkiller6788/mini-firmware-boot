#include "verifier_service.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>

int32_t attest_service_init(AttestDB *db, const uint8_t *verifier_id) {
    if (!db || !verifier_id) return -1;

    memset(db, 0, sizeof(*db));
    memcpy(db->verifier_id, verifier_id, ATTEST_DEVICE_ID_SIZE);
    db->device_count = 0;
    db->initialized = true;

    return 0;
}

int32_t attest_service_register_device(AttestDB *db,
                                        const uint8_t *device_id,
                                        const uint8_t *ek_pub_hash,
                                        const TPMHash *expected_pcrs,
                                        uint16_t expected_pcr_count) {
    if (!db || !device_id || !ek_pub_hash) return -1;
    if (db->device_count >= ATTEST_DB_MAX_DEVICES) return -2;

    AttestDeviceEntry *entry = &db->device_entries[db->device_count];

    memset(entry, 0, sizeof(*entry));
    memcpy(entry->device_id, device_id, ATTEST_DEVICE_ID_SIZE);
    memcpy(entry->ek_pub_hash, ek_pub_hash, EK_PUB_HASH_SIZE);

    if (expected_pcrs && expected_pcr_count > 0) {
        uint16_t count = expected_pcr_count < ATTEST_MAX_PCR_VALUES ?
                         expected_pcr_count : ATTEST_MAX_PCR_VALUES;
        uint16_t i;
        for (i = 0; i < count; i++) {
            memcpy(entry->expected_pcr_values[i], expected_pcrs[i].digest,
                   TPM_SHA256_DIGEST_SIZE);
        }
        entry->expected_pcr_count = count;
    }

    entry->last_attest_time = 0;
    entry->attest_fail_count = 0;
    entry->last_result = ATTEST_RESULT_UNKNOWN;
    entry->registered = true;
    entry->ownership_taken = false;
    entry->locked = false;

    db->device_count++;

    return 0;
}

int32_t attest_service_take_ownership(AttestDB *db,
                                       const uint8_t *device_id) {
    if (!db || !device_id) return -1;

    AttestDeviceEntry entry;
    int32_t ret = attest_service_find_device(db, device_id, &entry);
    if (ret != 0) return ret;

    uint16_t i;
    for (i = 0; i < db->device_count; i++) {
        if (memcmp(db->device_entries[i].device_id, device_id,
                   ATTEST_DEVICE_ID_SIZE) == 0) {
            db->device_entries[i].ownership_taken = true;
            db->device_entries[i].locked = false;
            return 0;
        }
    }

    return -2;
}

int32_t attest_service_clear_device(AttestDB *db,
                                     const uint8_t *device_id) {
    if (!db || !device_id) return -1;

    uint16_t i;
    for (i = 0; i < db->device_count; i++) {
        if (memcmp(db->device_entries[i].device_id, device_id,
                   ATTEST_DEVICE_ID_SIZE) == 0) {
            db->device_entries[i].attest_fail_count = 0;
            db->device_entries[i].last_result = ATTEST_RESULT_UNKNOWN;
            db->device_entries[i].last_attest_time = 0;
            db->device_entries[i].locked = false;
            return 0;
        }
    }

    return -2;
}

int32_t attest_service_lock_device(AttestDB *db,
                                    const uint8_t *device_id,
                                    bool lock_state) {
    if (!db || !device_id) return -1;

    uint16_t i;
    for (i = 0; i < db->device_count; i++) {
        if (memcmp(db->device_entries[i].device_id, device_id,
                   ATTEST_DEVICE_ID_SIZE) == 0) {
            db->device_entries[i].locked = lock_state;
            return 0;
        }
    }

    return -2;
}

int32_t attest_service_verify_quote(AttestDB *db,
                                     const uint8_t *device_id,
                                     const AttestResponse *response,
                                     const AttestChallenge *challenge,
                                     AttestResult *result,
                                     AttestVerdict *verdict) {
    if (!db || !device_id || !response || !challenge || !result || !verdict) return -1;

    AttestDeviceEntry entry;
    int32_t ret = attest_service_find_device(db, device_id, &entry);
    if (ret != 0) {
        *result = ATTEST_RESULT_UNKNOWN;
        return ret;
    }

    if (entry.locked) {
        *result = ATTEST_RESULT_UNTRUSTED;
        memset(verdict, 0, sizeof(*verdict));
        verdict->result = ATTEST_POLICY_DENY;
        snprintf((char *)verdict->detail, sizeof(verdict->detail),
                 "Device is locked");
        verdict->detail_len = (uint16_t)strlen((char *)verdict->detail);
        return 0;
    }

    AttestVerifier verifier;
    memset(&verifier, 0, sizeof(verifier));

    uint16_t i;
    for (i = 0; i < entry.expected_pcr_count && i < ATTEST_MAX_PCR_VALUES; i++) {
        memcpy(verifier.known_good_pcr_values[i].digest,
               entry.expected_pcr_values[i], TPM_SHA256_DIGEST_SIZE);
    }
    verifier.known_good_pcr_count = entry.expected_pcr_count;
    verifier.min_firmware_version = 1;
    verifier.check_firmware_version = true;

    attest_verify(response, challenge, &verifier, verdict);

    if (verdict->result == ATTEST_POLICY_ALLOW) {
        *result = ATTEST_RESULT_TRUSTED;
    } else {
        *result = ATTEST_RESULT_UNTRUSTED;
    }

    uint16_t j;
    for (j = 0; j < db->device_count; j++) {
        if (memcmp(db->device_entries[j].device_id, device_id,
                   ATTEST_DEVICE_ID_SIZE) == 0) {
            db->device_entries[j].last_attest_time = (uint64_t)time(NULL);
            db->device_entries[j].last_result = *result;
            if (*result == ATTEST_RESULT_UNTRUSTED) {
                db->device_entries[j].attest_fail_count++;
            } else {
                db->device_entries[j].attest_fail_count = 0;
            }
            break;
        }
    }

    return 0;
}

int32_t attest_service_update_policy(AttestDB *db,
                                      const uint8_t *device_id,
                                      const AttestPolicyRule *rules,
                                      uint16_t rule_count) {
    if (!db || !device_id || !rules) return -1;

    AttestDeviceEntry entry;
    int32_t ret = attest_service_find_device(db, device_id, &entry);
    if (ret != 0) return ret;

    uint16_t i;
    for (i = 0; i < db->device_count; i++) {
        if (memcmp(db->device_entries[i].device_id, device_id,
                   ATTEST_DEVICE_ID_SIZE) == 0) {
            uint16_t count = rule_count < ATTEST_POLICY_MAX_RULES ?
                             rule_count : ATTEST_POLICY_MAX_RULES;
            memcpy(db->device_entries[i].policy_rules, rules,
                   count * sizeof(AttestPolicyRule));
            db->device_entries[i].policy_rule_count = count;
            return 0;
        }
    }

    return -2;
}

int32_t attest_service_add_firmware_whitelist(AttestDB *db,
                                               const uint8_t *device_id,
                                               const AttestFirmwareRecord *fw,
                                               uint16_t fw_count) {
    if (!db || !device_id || !fw) return -1;

    uint16_t i;
    for (i = 0; i < db->device_count; i++) {
        if (memcmp(db->device_entries[i].device_id, device_id,
                   ATTEST_DEVICE_ID_SIZE) == 0) {
            uint16_t count = fw_count < ATTEST_FW_WHITELIST_MAX ?
                             fw_count : ATTEST_FW_WHITELIST_MAX;
            memcpy(db->device_entries[i].firmware_whitelist, fw,
                   count * sizeof(AttestFirmwareRecord));
            db->device_entries[i].fw_whitelist_count = count;
            return 0;
        }
    }

    return -2;
}

int32_t attest_service_update_pcr_expected(AttestDB *db,
                                            const uint8_t *device_id,
                                            const TPMHash *expected_pcrs,
                                            uint16_t expected_pcr_count) {
    if (!db || !device_id || !expected_pcrs) return -1;

    uint16_t i;
    for (i = 0; i < db->device_count; i++) {
        if (memcmp(db->device_entries[i].device_id, device_id,
                   ATTEST_DEVICE_ID_SIZE) == 0) {
            uint16_t count = expected_pcr_count < ATTEST_MAX_PCR_VALUES ?
                             expected_pcr_count : ATTEST_MAX_PCR_VALUES;
            uint16_t j;
            for (j = 0; j < count; j++) {
                memcpy(db->device_entries[i].expected_pcr_values[j],
                       expected_pcrs[j].digest,
                       TPM_SHA256_DIGEST_SIZE);
            }
            db->device_entries[i].expected_pcr_count = count;
            return 0;
        }
    }

    return -2;
}

int32_t attest_service_find_device(const AttestDB *db,
                                    const uint8_t *device_id,
                                    AttestDeviceEntry *entry_out) {
    if (!db || !device_id || !entry_out) return -1;
    if (!db->initialized) return -4;

    uint16_t i;
    for (i = 0; i < db->device_count; i++) {
        if (db->device_entries[i].registered &&
            memcmp(db->device_entries[i].device_id, device_id,
                   ATTEST_DEVICE_ID_SIZE) == 0) {
            memcpy(entry_out, &db->device_entries[i], sizeof(AttestDeviceEntry));
            return 0;
        }
    }

    return -3;
}

int32_t attest_service_get_attest_result(const AttestDB *db,
                                          const uint8_t *device_id,
                                          AttestResult *result) {
    if (!db || !device_id || !result) return -1;

    AttestDeviceEntry entry;
    int32_t ret = attest_service_find_device(db, device_id, &entry);
    if (ret != 0) {
        *result = ATTEST_RESULT_UNKNOWN;
        return ret;
    }

    *result = entry.last_result;
    return 0;
}

void attest_result_to_string(AttestResult result,
                              char *buf, size_t buf_size) {
    if (!buf || buf_size == 0) return;

    switch (result) {
        case ATTEST_RESULT_TRUSTED:
            snprintf(buf, buf_size, "TRUSTED");
            break;
        case ATTEST_RESULT_UNTRUSTED:
            snprintf(buf, buf_size, "UNTRUSTED");
            break;
        case ATTEST_RESULT_UNKNOWN:
        default:
            snprintf(buf, buf_size, "UNKNOWN");
            break;
    }
}

void attest_device_entry_dump(const AttestDeviceEntry *entry) {
    if (!entry) {
        printf("AttestDeviceEntry: (null)\n");
        return;
    }
    printf("=== Device Entry ===\n");
    printf("  Device ID:    %.16s\n", entry->device_id);
    printf("  Registered:   %s\n", entry->registered ? "YES" : "NO");
    printf("  Ownership:    %s\n", entry->ownership_taken ? "TAKEN" : "NONE");
    printf("  Locked:       %s\n", entry->locked ? "YES" : "NO");
    printf("  PCR Count:    %u\n", entry->expected_pcr_count);
    printf("  FW Whitelist: %u\n", entry->fw_whitelist_count);
    printf("  Policy Rules: %u\n", entry->policy_rule_count);
    printf("  Fail Count:   %u\n", entry->attest_fail_count);
    char buf[32];
    attest_result_to_string(entry->last_result, buf, sizeof(buf));
    printf("  Last Result:  %s\n", buf);
    printf("====================\n");
}
