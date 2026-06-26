#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "sha256.h"
#include "event_log.h"

void event_log_init(TCGEventLog* log, uint16_t spec_version) {
    if (log == NULL) return;
    memset(log, 0, sizeof(TCGEventLog));
    log->spec_version = spec_version;
    log->event_count = 0;
    log->sha256_used = true;
}

bool event_log_add(TCGEventLog* log, uint32_t pcr_index, uint32_t event_type,
                   const uint8_t* digest, const uint8_t* event_data, uint32_t event_data_size) {
    if (log == NULL || log->event_count >= MAX_EVENTS) return false;
    if (event_data_size > MAX_EVENT_DATA_SIZE) return false;

    TCGEvent* evt = &log->events[log->event_count];
    evt->header.pcr_index = pcr_index;
    evt->header.event_type = event_type;
    memcpy(evt->header.digest, digest, SHA256_DIGEST_SIZE);
    evt->header.event_data_size = event_data_size;
    evt->event_data_size = event_data_size;

    if (event_data != NULL && event_data_size > 0) {
        memcpy(evt->event_data, event_data, event_data_size);
    }

    log->event_count++;
    return true;
}

const char* event_type_to_string(uint32_t event_type) {
    switch (event_type) {
        case EV_PREBOOT_CERT:              return "EV_PREBOOT_CERT";
        case EV_POST_CODE:                 return "EV_POST_CODE";
        case EV_NO_ACTION:                 return "EV_NO_ACTION";
        case EV_SEPARATOR:                 return "EV_SEPARATOR";
        case EV_S_CRTM_CONTENTS:           return "EV_S_CRTM_CONTENTS";
        case EV_S_CRTM_VERSION:            return "EV_S_CRTM_VERSION";
        case EV_CPU_MICROCODE:             return "EV_CPU_MICROCODE";
        case EV_EFI_BOOT_SERVICES_APP:     return "EV_EFI_BOOT_SERVICES_APP";
        case EV_EFI_BOOT_SERVICES_DRIVER:  return "EV_EFI_BOOT_SERVICES_DRIVER";
        case EV_EFI_RUNTIME_SERVICES_DRIVER: return "EV_EFI_RUNTIME_SERVICES_DRIVER";
        case EV_EFI_VARIABLE_BOOT:         return "EV_EFI_VARIABLE_BOOT";
        case EV_EFI_VARIABLE_AUTHORITY:    return "EV_EFI_VARIABLE_AUTHORITY";
        case EV_EFI_ACTION:                return "EV_EFI_ACTION";
        case EV_EFI_PLATFORM_FIRMWARE_BLOB: return "EV_EFI_PLATFORM_FIRMWARE_BLOB";
        default:                           return "UNKNOWN_EVENT";
    }
}

void event_log_print(const TCGEventLog* log) {
    uint32_t i, j;
    if (log == NULL) return;

    printf("=== TCG Event Log (Spec %d.%d) ===\n",
           log->spec_version >> 8, log->spec_version & 0xFF);
    printf("Total events: %u\n", log->event_count);

    for (i = 0; i < log->event_count; i++) {
        const TCGEvent* evt = &log->events[i];
        printf("\n  Event %u:\n", i);
        printf("    PCR Index:   %u\n", evt->header.pcr_index);
        printf("    Event Type:  %s (0x%08x)\n",
               event_type_to_string(evt->header.event_type), evt->header.event_type);
        printf("    Digest:      ");
        for (j = 0; j < 8; j++) {
            printf("%02x", evt->header.digest[j]);
        }
        printf("...\n");
        printf("    Data Size:   %u\n", evt->header.event_data_size);
        if (evt->header.event_data_size > 0) {
            printf("    Data:        ");
            for (j = 0; j < evt->event_data_size && j < 32; j++) {
                printf("%c", (evt->event_data[j] >= 0x20 && evt->event_data[j] < 0x7f)
                             ? evt->event_data[j] : '.');
            }
            printf("\n");
        }
    }
}

bool event_log_replay(const TCGEventLog* log, PCRBank* bank) {
    uint32_t i;
    if (log == NULL || bank == NULL) return false;

    pcr_bank_init(bank, TPM_ALG_SHA256);

    for (i = 0; i < log->event_count; i++) {
        const TCGEvent* evt = &log->events[i];
        pcr_extend(bank, evt->header.pcr_index,
                   evt->header.digest, SHA256_DIGEST_SIZE);
    }
    return true;
}

bool event_log_verify_pcr(const TCGEventLog* log, const PCRBank* bank,
                          uint32_t pcr_index, const uint8_t* expected_pcr,
                          TPMI_ALG_HASH hash_alg) {
    (void)bank;
    PCRBank replayed_bank;
    uint8_t replayed_digest[SHA256_DIGEST_SIZE];

    if (hash_alg != TPM_ALG_SHA256) return false;

    if (!event_log_replay(log, &replayed_bank)) return false;

    if (!pcr_read(&replayed_bank, pcr_index, replayed_digest)) return false;

    return memcmp(replayed_digest, expected_pcr, SHA256_DIGEST_SIZE) == 0;
}

uint32_t event_log_digest_size(const TCGEventLog* log) {
    if (log == NULL) return 0;
    return SHA256_DIGEST_SIZE;
}
