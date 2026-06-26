#ifndef EVENT_LOG_H
#define EVENT_LOG_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "tpm2_structs.h"
#include "pcr_bank.h"

#define MAX_EVENTS          128
#define MAX_EVENT_DATA_SIZE 512

typedef enum {
    EV_PREBOOT_CERT              = 0x00000000,
    EV_POST_CODE                 = 0x00000001,
    EV_NO_ACTION                 = 0x00000003,
    EV_SEPARATOR                 = 0x00000004,
    EV_S_CRTM_CONTENTS           = 0x00000007,
    EV_S_CRTM_VERSION            = 0x00000008,
    EV_CPU_MICROCODE             = 0x00000009,
    EV_EFI_BOOT_SERVICES_APP     = 0x80000001,
    EV_EFI_BOOT_SERVICES_DRIVER  = 0x80000002,
    EV_EFI_RUNTIME_SERVICES_DRIVER = 0x80000003,
    EV_EFI_VARIABLE_BOOT         = 0x80000004,
    EV_EFI_VARIABLE_AUTHORITY    = 0x800000E0,
    EV_EFI_ACTION                = 0x800000E1,
    EV_EFI_PLATFORM_FIRMWARE_BLOB = 0x800000E2,
} TCG_EVENT_TYPE;

typedef struct {
    uint32_t pcr_index;
    uint32_t event_type;
    uint8_t  digest[SHA256_DIGEST_SIZE];
    uint32_t event_data_size;
} TCGEventHeader;

typedef struct {
    TCGEventHeader header;
    uint8_t        event_data[MAX_EVENT_DATA_SIZE];
    uint32_t       event_data_size;
} TCGEvent;

typedef struct {
    TCGEvent  events[MAX_EVENTS];
    uint32_t  event_count;
    uint16_t  spec_version;
    bool      sha256_used;
} TCGEventLog;

void     event_log_init(TCGEventLog* log, uint16_t spec_version);
bool     event_log_add(TCGEventLog* log, uint32_t pcr_index, uint32_t event_type,
                       const uint8_t* digest, const uint8_t* event_data, uint32_t event_data_size);
void     event_log_print(const TCGEventLog* log);
bool     event_log_replay(const TCGEventLog* log, PCRBank* bank);
bool     event_log_verify_pcr(const TCGEventLog* log, const PCRBank* bank, uint32_t pcr_index,
                              const uint8_t* expected_pcr, TPMI_ALG_HASH hash_alg);
const char* event_type_to_string(uint32_t event_type);
uint32_t event_log_digest_size(const TCGEventLog* log);

#endif
