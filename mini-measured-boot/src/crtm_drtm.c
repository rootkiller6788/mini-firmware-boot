#include <string.h>
#include <stdio.h>
#include "sha256.h"
#include "crtm_drtm.h"

void crtm_init(CRTM* crtm, const uint8_t* version) {
    if (crtm == NULL) return;
    memset(crtm, 0, sizeof(CRTM));
    crtm->immutable = true;
    crtm->reset_behavior = 0;
    if (version != NULL) {
        size_t len = strlen((const char*)version);
        if (len > 15) len = 15;
        memcpy(crtm->crtm_version, version, len);
        crtm->crtm_version[len] = '\0';
    }
    sha256_hash((uint8_t*)"CRTM-BOOT-BLOCK", 15, crtm->bios_measurement);
}

void srtm_flow_init(SRTMFlow* flow, PCRBank* bank, TCGEventLog* log) {
    if (flow == NULL) return;
    memset(flow, 0, sizeof(SRTMFlow));
    flow->pcr_bank = bank;
    flow->event_log = log;
    crtm_init(&flow->crtm, (uint8_t*)"CRTM v1.0.0");
}

bool srtm_measure_bios(SRTMFlow* flow, const uint8_t* measurement) {
    if (flow == NULL || measurement == NULL) return false;
    memcpy(flow->main_bios, measurement, SHA256_DIGEST_SIZE);
    pcr_extend(flow->pcr_bank, 0, measurement, SHA256_DIGEST_SIZE);
    event_log_add(flow->event_log, 0, EV_S_CRTM_CONTENTS,
                  flow->main_bios, (uint8_t*)"BIOS", 4);
    return true;
}

bool srtm_measure_option_rom(SRTMFlow* flow, const uint8_t* measurement) {
    if (flow == NULL || measurement == NULL) return false;
    memcpy(flow->option_rom, measurement, SHA256_DIGEST_SIZE);
    pcr_extend(flow->pcr_bank, 2, measurement, SHA256_DIGEST_SIZE);
    event_log_add(flow->event_log, 2, EV_EFI_BOOT_SERVICES_DRIVER,
                  flow->option_rom, (uint8_t*)"OPTION-ROM", 10);
    return true;
}

bool srtm_measure_mbr(SRTMFlow* flow, const uint8_t* measurement) {
    if (flow == NULL || measurement == NULL) return false;
    memcpy(flow->mbr, measurement, SHA256_DIGEST_SIZE);
    pcr_extend(flow->pcr_bank, 4, measurement, SHA256_DIGEST_SIZE);
    pcr_extend(flow->pcr_bank, 8, measurement, SHA256_DIGEST_SIZE);
    event_log_add(flow->event_log, 4, EV_EFI_BOOT_SERVICES_APP,
                  flow->mbr, (uint8_t*)"MBR", 3);
    return true;
}

bool srtm_measure_bootloader(SRTMFlow* flow, const uint8_t* measurement) {
    if (flow == NULL || measurement == NULL) return false;
    memcpy(flow->bootloader, measurement, SHA256_DIGEST_SIZE);
    pcr_extend(flow->pcr_bank, 4, measurement, SHA256_DIGEST_SIZE);
    pcr_extend(flow->pcr_bank, 8, measurement, SHA256_DIGEST_SIZE);
    event_log_add(flow->event_log, 4, EV_EFI_BOOT_SERVICES_APP,
                  flow->bootloader, (uint8_t*)"BOOTLOADER", 10);
    return true;
}

bool srtm_measure_os(SRTMFlow* flow, const uint8_t* measurement) {
    if (flow == NULL || measurement == NULL) return false;
    memcpy(flow->target_os_measurement, measurement, SHA256_DIGEST_SIZE);
    pcr_extend(flow->pcr_bank, 8, measurement, SHA256_DIGEST_SIZE);
    event_log_add(flow->event_log, 8, EV_EFI_ACTION,
                  flow->target_os_measurement, (uint8_t*)"OS-KERNEL", 9);
    return true;
}

void srtm_simulate(SRTMFlow* flow) {
    uint8_t mock_bios[SHA256_DIGEST_SIZE];
    uint8_t mock_option_rom[SHA256_DIGEST_SIZE];
    uint8_t mock_mbr[SHA256_DIGEST_SIZE];
    uint8_t mock_bootloader[SHA256_DIGEST_SIZE];
    uint8_t mock_os[SHA256_DIGEST_SIZE];

    if (flow == NULL) return;

    sha256_hash((uint8_t*)"BIOS_IMAGE_V1.0", 15, mock_bios);
    sha256_hash((uint8_t*)"OPTION_ROM_V2.1", 15, mock_option_rom);
    sha256_hash((uint8_t*)"MBR_SECTOR_0", 12, mock_mbr);
    sha256_hash((uint8_t*)"GRUB_BOOTLOADER", 15, mock_bootloader);
    sha256_hash((uint8_t*)"LINUX_KERNEL_5.15", 17, mock_os);

    printf("\n--- SRTM Measure Boot Flow ---\n");
    printf("  [1] CRTM (immutable boot block) initialized\n");

    srtm_measure_bios(flow, mock_bios);
    printf("  [2] BIOS measured -> PCR0: ");
    for (int i = 0; i < 8; i++) printf("%02x", flow->pcr_bank->pcr[0][i]);
    printf("...\n");

    srtm_measure_option_rom(flow, mock_option_rom);
    printf("  [3] Option ROM measured -> PCR2: ");
    for (int i = 0; i < 8; i++) printf("%02x", flow->pcr_bank->pcr[2][i]);
    printf("...\n");

    srtm_measure_mbr(flow, mock_mbr);
    printf("  [4] MBR measured -> PCR4/8\n");

    srtm_measure_bootloader(flow, mock_bootloader);
    printf("  [5] Bootloader measured -> PCR4/8\n");

    srtm_measure_os(flow, mock_os);
    printf("  [6] OS kernel measured -> PCR8\n");

    printf("  SRTM chain complete.\n");
}

void txt_info_init(TXTInfo* info) {
    if (info == NULL) return;
    memset(info, 0, sizeof(TXTInfo));
    memcpy(info->ac_module_id, "ACM_SINIT", 8);
    info->sinit_size = 0x8000;
    info->mle_size = 0x40000;
    info->launch_success = false;
}

void drtm_flow_init(DRTMFlow* flow, PCRBank* bank, TCGEventLog* log) {
    if (flow == NULL) return;
    memset(flow, 0, sizeof(DRTMFlow));
    flow->pcr_bank = bank;
    flow->event_log = log;
    flow->measured_launch = false;
    txt_info_init(&flow->txt_info);
}

bool drtm_measure_sinit(DRTMFlow* flow, const uint8_t* measurement) {
    if (flow == NULL || measurement == NULL) return false;
    memcpy(flow->sinit_measurement, measurement, SHA256_DIGEST_SIZE);
    memcpy(flow->txt_info.sinit_hash, measurement, SHA256_DIGEST_SIZE);
    pcr_extend(flow->pcr_bank, 17, measurement, SHA256_DIGEST_SIZE);
    event_log_add(flow->event_log, 17, EV_S_CRTM_CONTENTS,
                  flow->sinit_measurement, (uint8_t*)"SINIT-ACM", 9);
    return true;
}

bool drtm_measure_mle(DRTMFlow* flow, const uint8_t* measurement) {
    if (flow == NULL || measurement == NULL) return false;
    memcpy(flow->mle_measurement, measurement, SHA256_DIGEST_SIZE);
    memcpy(flow->txt_info.mle_hash, measurement, SHA256_DIGEST_SIZE);
    pcr_extend(flow->pcr_bank, 18, measurement, SHA256_DIGEST_SIZE);
    event_log_add(flow->event_log, 18, EV_EFI_ACTION,
                  flow->mle_measurement, (uint8_t*)"MLE-HEADER", 10);
    flow->measured_launch = true;
    flow->txt_info.launch_success = true;
    return true;
}

bool drtm_measure_os(DRTMFlow* flow, const uint8_t* measurement) {
    if (flow == NULL || measurement == NULL) return false;
    memcpy(flow->os_measurement, measurement, SHA256_DIGEST_SIZE);
    pcr_extend(flow->pcr_bank, 19, measurement, SHA256_DIGEST_SIZE);
    event_log_add(flow->event_log, 19, EV_EFI_ACTION,
                  flow->os_measurement, (uint8_t*)"OS-DRTM", 7);
    return true;
}

void drtm_simulate(DRTMFlow* flow) {
    uint8_t mock_sinit[SHA256_DIGEST_SIZE];
    uint8_t mock_mle[SHA256_DIGEST_SIZE];
    uint8_t mock_os[SHA256_DIGEST_SIZE];

    if (flow == NULL) return;

    sha256_hash((uint8_t*)"SINIT_ACM_MODULE", 16, mock_sinit);
    sha256_hash((uint8_t*)"MLE_MEASURED_LAUNCH", 19, mock_mle);
    sha256_hash((uint8_t*)"TRUSTED_OS_KERNEL", 17, mock_os);

    printf("\n--- DRTM Measure Boot Flow (Intel TXT) ---\n");
    printf("  [1] CPU executes GETSEC[SENTER]\n");
    printf("  [2] SINIT ACM loaded and verified\n");

    drtm_measure_sinit(flow, mock_sinit);
    printf("  [3] SINIT measured -> PCR17: ");
    for (int i = 0; i < 8; i++) printf("%02x", flow->pcr_bank->pcr[17][i]);
    printf("...\n");

    drtm_measure_mle(flow, mock_mle);
    printf("  [4] MLE measured -> PCR18: ");
    for (int i = 0; i < 8; i++) printf("%02x", flow->pcr_bank->pcr[18][i]);
    printf("...\n");

    drtm_measure_os(flow, mock_os);
    printf("  [5] Trusted OS measured -> PCR19\n");

    printf("  DRTM measured launch complete: %s\n",
           txt_verify_launch(flow) ? "VERIFIED" : "FAILED");
}

bool txt_verify_launch(const DRTMFlow* flow) {
    if (flow == NULL) return false;
    return flow->measured_launch && flow->txt_info.launch_success;
}
