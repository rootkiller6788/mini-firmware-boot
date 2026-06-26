#ifndef PCR_BANK_H
#define PCR_BANK_H

#include <stdbool.h>
#include <stdint.h>
#include "tpm2_structs.h"

#define PCR_COUNT            24
#define PCR_DYNAMIC_MIN      17
#define PCR_DYNAMIC_MAX      22

typedef enum {
    PCR_CRTM                 = 0,
    PCR_PLATFORM_CONFIG      = 1,
    PCR_EXTERNAL_ROM         = 2,
    PCR_ROM_CONFIG           = 3,
    PCR_BOOT_MANAGER         = 4,
    PCR_BOOT_MANAGER_CONFIG  = 5,
    PCR_SLEEP_STATE          = 6,
    PCR_SECURE_BOOT_POLICY   = 7,
    PCR_OS_LOADER_SRTM       = 8,
    PCR_OS_LOADER_DRTM       = 9,
    PCR_UNUSED_10            = 10,
    PCR_BOOT_MANAGER_DATA    = 11,
    PCR_PLATFORM_DATA        = 12,
    PCR_UNUSED_13            = 13,
    PCR_AUTHORITIES          = 14,
    PCR_UNUSED_15            = 15,
    PCR_UNUSED_16            = 16,
    PCR_DRTM_MIN             = 17,
    PCR_DRTM_18              = 18,
    PCR_DRTM_19              = 19,
    PCR_DRTM_20              = 20,
    PCR_DRTM_21              = 21,
    PCR_DRTM_MAX             = 22,
    PCR_UNUSED_23            = 23,
} PCR_INDEX;

typedef struct {
    TPMI_ALG_HASH hash_alg;
    uint8_t       pcr[PCR_COUNT][SHA256_DIGEST_SIZE];
    uint8_t       pcr_valid[PCR_COUNT];
} PCRBank;

typedef struct {
    uint32_t pcr_index;
    uint32_t event_type;
    uint8_t  digest[SHA256_DIGEST_SIZE];
    uint8_t* event_data;
    uint32_t event_size;
} PCREvent;

void     pcr_bank_init(PCRBank* bank, TPMI_ALG_HASH hash_alg);
bool     pcr_extend(PCRBank* bank, uint32_t pcr_index, const uint8_t* data, size_t data_len);
bool     pcr_read(const PCRBank* bank, uint32_t pcr_index, uint8_t* digest_out);
bool     pcr_reset(PCRBank* bank, uint32_t pcr_index);
void     pcr_print_bank(const PCRBank* bank);
const char* pcr_index_to_name(uint32_t pcr_index);
void     pcr_get_all(const PCRBank* bank, uint8_t pcrs[PCR_COUNT][SHA256_DIGEST_SIZE]);

#endif
