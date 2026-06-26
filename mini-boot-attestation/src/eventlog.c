#include "eventlog.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

int32_t tcg_pcr_extend(TPMHash *pcr_value,
                       const uint8_t *digest, uint32_t digest_size) {
    if (!pcr_value || !digest || digest_size == 0) return -1;
    TPMHash temp;
    tpm_hash_init(&temp);
    tpm_hash_update(&temp, pcr_value->digest, TPM_SHA256_DIGEST_SIZE);
    tpm_hash_update(&temp, digest, digest_size < TPM_SHA256_DIGEST_SIZE
                    ? digest_size : TPM_SHA256_DIGEST_SIZE);
    tpm_hash_final(&temp);
    memcpy(pcr_value->digest, temp.digest, TPM_SHA256_DIGEST_SIZE);
    return 0;
}

int32_t tcg_pcr_extend_with_data(TPMHash *pcr_value,
                                  const uint8_t *data, uint32_t data_size) {
    if (!pcr_value || !data || data_size == 0) return -1;
    TPMHash data_hash;
    tpm_hash_init(&data_hash);
    tpm_hash_update(&data_hash, data, data_size);
    tpm_hash_final(&data_hash);
    return tcg_pcr_extend(pcr_value, data_hash.digest, TPM_SHA256_DIGEST_SIZE);
}

int32_t tcg_pcr_extend_multi(TCGDigestList *pcr_values,
                              uint32_t pcr_index,
                              const TCGDigestList *digests) {
    if (!pcr_values || !digests) return -1;
    if (pcr_index >= TPM_MAX_PCRS) return -2;
    uint32_t i;
    for (i = 0; i < digests->count && i < pcr_values->count && i < TCG_MAX_DIGEST_COUNT; i++) {
        if (digests->digests[i].algorithm_id == TCG_ALG_SHA256 &&
            pcr_values->digests[i].algorithm_id == TCG_ALG_SHA256) {
            TPMHash temp;
            memcpy(temp.digest, pcr_values->digests[i].digest.sha256, TPM_SHA256_DIGEST_SIZE);
            tcg_pcr_extend(&temp, digests->digests[i].digest.sha256, TPM_SHA256_DIGEST_SIZE);
            memcpy(pcr_values->digests[i].digest.sha256, temp.digest, TPM_SHA256_DIGEST_SIZE);
        }
    }
    return 0;
}

void tcg_eventlog_init(TCGEventLog *log, uint32_t hash_algorithm) {
    if (!log) return;
    memset(log, 0, sizeof(*log));
    log->log_hash_algorithm = hash_algorithm;
}

int32_t tcg_eventlog_add(TCGEventLog *log,
                          uint32_t pcr_index, uint32_t event_type,
                          const TCGDigestList *digests,
                          const uint8_t *event_data, uint32_t data_size) {
    if (!log || !digests) return -1;
    if (log->finalized) return -4;
    if (log->entry_count >= TCG_EVENT_LOG_MAX_ENTRIES) return -2;
    if (pcr_index >= TPM_MAX_PCRS) return -3;
    TCGEvent *evt = &log->entries[log->entry_count];
    memset(evt, 0, sizeof(*evt));
    evt->pcr_index = pcr_index;
    evt->event_type = event_type;
    uint32_t i;
    for (i = 0; i < digests->count && i < TCG_MAX_DIGEST_COUNT; i++) {
        memcpy(&evt->digests.digests[i], &digests->digests[i], sizeof(TCGDigest));
    }
    evt->digests.count = digests->count;
    if (event_data && data_size > 0) {
        uint32_t copy = data_size < TCG_EVENT_DATA_MAX_SIZE ? data_size : TCG_EVENT_DATA_MAX_SIZE;
        memcpy(evt->event_data, event_data, copy);
        evt->event_data_size = copy;
    }
    log->entry_count++;
    return 0;
}

int32_t tcg_eventlog_get(const TCGEventLog *log, uint32_t index, TCGEvent *out) {
    if (!log || !out) return -1;
    if (index >= log->entry_count) return -2;
    memcpy(out, &log->entries[index], sizeof(TCGEvent));
    return 0;
}

void tcg_eventlog_finalize(TCGEventLog *log) {
    if (!log) return;
    log->finalized = true;
}

void tcg_eventlog_sha256_init(TCGEventLogSHA256 *log) {
    if (!log) return;
    memset(log, 0, sizeof(*log));
}

int32_t tcg_eventlog_sha256_add(TCGEventLogSHA256 *log,
                                 uint8_t pcr_index, uint32_t event_type,
                                 const uint8_t *data, uint32_t data_size) {
    if (!log || !data) return -1;
    if (log->sealed) return -4;
    if (log->entry_count >= TCG_EVENT_LOG_MAX_ENTRIES) return -2;
    if (pcr_index >= TPM_MAX_PCRS) return -3;
    TCGEventSHA256 *evt = &log->entries[log->entry_count];
    evt->pcr_index = pcr_index;
    evt->event_type = event_type;
    TPMHash hash;
    tpm_hash_init(&hash);
    tpm_hash_update(&hash, data, data_size);
    tpm_hash_final(&hash);
    memcpy(evt->digest, hash.digest, TPM_SHA256_DIGEST_SIZE);
    uint32_t copy = data_size < TCG_EVENT_DATA_MAX_SIZE
                    ? data_size : TCG_EVENT_DATA_MAX_SIZE;
    memcpy(evt->data, data, copy);
    evt->data_size = copy;
    log->entry_count++;
    return 0;
}

int32_t tcg_eventlog_sha256_seal(TCGEventLogSHA256 *log) {
    if (!log) return -1;
    if (log->sealed) return -2;
    log->sealed = true;
    return 0;
}

int32_t tcg_eventlog_replay_sha256(const TCGEventLogSHA256 *log,
                                    TPMPcrComposite *recomputed_pcr,
                                    bool *match,
                                    const TPMPcrComposite *expected_pcr,
                                    uint16_t pcr_mask) {
    if (!log || !recomputed_pcr || !match || !expected_pcr) return -1;
    tpm_pcr_composite_init(recomputed_pcr);
    TPMHash current_pcr[TPM_MAX_PCRS];
    uint8_t i;
    for (i = 0; i < TPM_MAX_PCRS; i++) {
        tpm_hash_init(&current_pcr[i]);
    }
    uint32_t j;
    for (j = 0; j < log->entry_count; j++) {
        const TCGEventSHA256 *evt = &log->entries[j];
        if (evt->pcr_index < TPM_MAX_PCRS) {
            tcg_pcr_extend(&current_pcr[evt->pcr_index],
                          evt->digest, TPM_SHA256_DIGEST_SIZE);
        }
    }
    for (i = 0; i < TPM_MAX_PCRS; i++) {
        tpm_hash_final(&current_pcr[i]);
        tpm_pcr_composite_add(recomputed_pcr, i, &current_pcr[i]);
    }
    return attest_pcr_compare(recomputed_pcr, expected_pcr, pcr_mask, match);
}

int32_t tcg_eventlog_integrity_check(const TCGEventLogSHA256 *log,
                                      const TPMHash *expected_final_hash,
                                      bool *valid) {
    if (!log || !expected_final_hash || !valid) return -1;
    TPMHash accumulator;
    tpm_hash_init(&accumulator);
    uint32_t i;
    for (i = 0; i < log->entry_count; i++) {
        tpm_hash_update(&accumulator, log->entries[i].digest, TPM_SHA256_DIGEST_SIZE);
        tpm_hash_update(&accumulator, (const uint8_t *)&log->entries[i].event_type,
                       sizeof(uint32_t));
        tpm_hash_update(&accumulator, &log->entries[i].pcr_index, 1);
    }
    tpm_hash_final(&accumulator);
    *valid = (memcmp(accumulator.digest, expected_final_hash->digest,
                     TPM_SHA256_DIGEST_SIZE) == 0);
    return 0;
}

int32_t tcg_pcr_bank_compare(const TPMPcrComposite *bank_a,
                              const TPMPcrComposite *bank_b,
                              uint8_t pcr_index, bool *match) {
    if (!bank_a || !bank_b || !match) return -1;
    if (pcr_index >= TPM_MAX_PCRS) return -2;
    *match = (memcmp(bank_a->pcr_digests[pcr_index].digest,
                     bank_b->pcr_digests[pcr_index].digest,
                     TPM_SHA256_DIGEST_SIZE) == 0);
    return 0;
}

void tcg_hash_sequence_start(TCGHashSequence *seq) {
    if (!seq) return;
    memset(seq, 0, sizeof(*seq));
    tpm_hash_init(&seq->state);
    seq->started = true;
    seq->data_hashed = 0;
}

int32_t tcg_hash_sequence_update(TCGHashSequence *seq,
                                  const uint8_t *data, uint32_t len) {
    if (!seq || !data) return -1;
    if (!seq->started) return -2;
    tpm_hash_update(&seq->state, data, len);
    seq->data_hashed += len;
    return 0;
}

int32_t tcg_hash_sequence_end(TCGHashSequence *seq, TPMHash *result) {
    if (!seq || !result) return -1;
    if (!seq->started) return -2;
    tpm_hash_final(&seq->state);
    memcpy(result->digest, seq->state.digest, TPM_SHA256_DIGEST_SIZE);
    seq->started = false;
    return 0;
}

static const char *event_type_name(uint32_t type) {
    switch (type) {
        case EV_PREBOOT_CERT:          return "EV_PREBOOT_CERT";
        case EV_POST_CODE:             return "EV_POST_CODE";
        case EV_NO_ACTION:             return "EV_NO_ACTION";
        case EV_SEPARATOR:             return "EV_SEPARATOR";
        case EV_ACTION:                return "EV_ACTION";
        case EV_S_CRTM_CONTENTS:       return "EV_S_CRTM_CONTENTS";
        case EV_S_CRTM_VERSION:        return "EV_S_CRTM_VERSION";
        case EV_CPU_MICROCODE:         return "EV_CPU_MICROCODE";
        case EV_EFI_VARIABLE_DRIVER:   return "EV_EFI_VARIABLE_DRIVER";
        case EV_EFI_VARIABLE_BOOT:     return "EV_EFI_VARIABLE_BOOT";
        case EV_EFI_BOOT_SERVICES:     return "EV_EFI_BOOT_SERVICES";
        case EV_EFI_PLATFORM_FIRMWARE: return "EV_EFI_PLATFORM_FIRMWARE";
        case EV_NONHOST_CODE:          return "EV_NONHOST_CODE";
        case EV_NONHOST_CONFIG:        return "EV_NONHOST_CONFIG";
        default:                       return "EV_UNKNOWN";
    }
}

void tcg_event_dump(const TCGEvent *event) {
    if (!event) { printf("TCGEvent: (null)\n"); return; }
    printf("  TCG Event: PCR%u %s #Dig=%u DataSz=%u\n",
           event->pcr_index, event_type_name(event->event_type),
           event->digests.count, event->event_data_size);
}

void tcg_eventlog_dump(const TCGEventLog *log) {
    if (!log) { printf("TCGEventLog: (null)\n"); return; }
    printf("=== TCG Event Log ===\n");
    printf("  Algorithm: 0x%04X  Entries: %u  Finalized: %s\n",
           log->log_hash_algorithm, log->entry_count,
           log->finalized ? "YES" : "NO");
    uint32_t i;
    for (i = 0; i < log->entry_count && i < 8; i++) {
        printf("  [%u] PCR%u %s\n", i, log->entries[i].pcr_index,
               event_type_name(log->entries[i].event_type));
    }
    if (log->entry_count > 8) printf("  ... +%u more\n", log->entry_count - 8);
    printf("======================\n");
}

void tcg_eventlog_sha256_dump(const TCGEventLogSHA256 *log) {
    if (!log) { printf("TCGEventLogSHA256: (null)\n"); return; }
    printf("=== SHA-256 Event Log ===\n");
    printf("  Entries: %u  Sealed: %s\n", log->entry_count,
           log->sealed ? "YES" : "NO");
    uint32_t i;
    for (i = 0; i < log->entry_count && i < 10; i++) {
        const TCGEventSHA256 *evt = &log->entries[i];
        printf("  [%u] PCR%u %s ", i, evt->pcr_index,
               event_type_name(evt->event_type));
        uint8_t j;
        for (j = 0; j < 8; j++) printf("%02X", evt->digest[j]);
        printf("...\n");
    }
    if (log->entry_count > 10) printf("  ... +%u more\n", log->entry_count - 10);
    printf("==========================\n");
}