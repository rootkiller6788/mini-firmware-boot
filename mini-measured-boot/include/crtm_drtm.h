#ifndef CRTM_DRTM_H
#define CRTM_DRTM_H

#include <stdbool.h>
#include <stdint.h>
#include "pcr_bank.h"
#include "event_log.h"

#define CRTM_MEASUREMENT_SIZE   SHA256_DIGEST_SIZE
#define MLE_HEADER_SIZE         48
#define TXT_HEAP_SIZE           4096
#define SINIT_AC_SIZE           32768

typedef struct {
    uint8_t  bios_measurement[SHA256_DIGEST_SIZE];
    uint8_t  crtm_version[16];
    bool     immutable;
    uint32_t reset_behavior;
} CRTM;

typedef struct {
    CRTM       crtm;
    uint8_t    main_bios[SHA256_DIGEST_SIZE];
    uint8_t    option_rom[SHA256_DIGEST_SIZE];
    uint8_t    mbr[SHA256_DIGEST_SIZE];
    uint8_t    bootloader[SHA256_DIGEST_SIZE];
    uint8_t    target_os_measurement[SHA256_DIGEST_SIZE];
    PCRBank*   pcr_bank;
    TCGEventLog* event_log;
} SRTMFlow;

typedef struct {
    uint8_t  ac_module_id[8];
    uint8_t  sinit_hash[SHA256_DIGEST_SIZE];
    uint8_t  mle_hash[SHA256_DIGEST_SIZE];
    uint8_t  bios_ac_base[8];
    uint32_t sinit_size;
    uint32_t mle_size;
    bool     launch_success;
} TXTInfo;

typedef struct {
    TXTInfo    txt_info;
    uint8_t    mle_measurement[SHA256_DIGEST_SIZE];
    uint8_t    sinit_measurement[SHA256_DIGEST_SIZE];
    uint8_t    os_measurement[SHA256_DIGEST_SIZE];
    PCRBank*   pcr_bank;
    TCGEventLog* event_log;
    bool       measured_launch;
} DRTMFlow;

void srtm_simulate(SRTMFlow* flow);
bool srtm_measure_bios(SRTMFlow* flow, const uint8_t* measurement);
bool srtm_measure_option_rom(SRTMFlow* flow, const uint8_t* measurement);
bool srtm_measure_mbr(SRTMFlow* flow, const uint8_t* measurement);
bool srtm_measure_bootloader(SRTMFlow* flow, const uint8_t* measurement);
bool srtm_measure_os(SRTMFlow* flow, const uint8_t* measurement);

void drtm_simulate(DRTMFlow* flow);
bool drtm_measure_sinit(DRTMFlow* flow, const uint8_t* measurement);
bool drtm_measure_mle(DRTMFlow* flow, const uint8_t* measurement);
bool drtm_measure_os(DRTMFlow* flow, const uint8_t* measurement);
bool txt_verify_launch(const DRTMFlow* flow);

void crtm_init(CRTM* crtm, const uint8_t* version);
void srtm_flow_init(SRTMFlow* flow, PCRBank* bank, TCGEventLog* log);
void drtm_flow_init(DRTMFlow* flow, PCRBank* bank, TCGEventLog* log);
void txt_info_init(TXTInfo* info);

#endif
