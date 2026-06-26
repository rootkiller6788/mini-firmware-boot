#include <stdio.h>
#include <string.h>
#include "sha256.h"
#include "pcr_bank.h"
#include "event_log.h"

int main(void) {
    PCRBank bank;
    PCRBank replayed_bank;
    TCGEventLog log;
    uint8_t crtm_hash[SHA256_DIGEST_SIZE];
    uint8_t bios_hash[SHA256_DIGEST_SIZE];
    uint8_t option_rom_hash[SHA256_DIGEST_SIZE];
    uint8_t mbr_hash[SHA256_DIGEST_SIZE];
    uint8_t bootloader_hash[SHA256_DIGEST_SIZE];
    uint8_t actual_pcr0[SHA256_DIGEST_SIZE];
    uint8_t actual_pcr8[SHA256_DIGEST_SIZE];
    uint32_t i;

    printf("=== TPM 2.0 Event Log Demo ===\n\n");

    pcr_bank_init(&bank, TPM_ALG_SHA256);
    event_log_init(&log, 0x0200);

    sha256_hash((uint8_t*)"CRTM_IMMUTABLE_CODE", 19, crtm_hash);
    sha256_hash((uint8_t*)"SYSTEM_BIOS_IMAGE", 17, bios_hash);
    sha256_hash((uint8_t*)"NIC_OPTION_ROM", 14, option_rom_hash);
    sha256_hash((uint8_t*)"MBR_CODE_SECTOR0", 16, mbr_hash);
    sha256_hash((uint8_t*)"GRUB_STAGE2", 11, bootloader_hash);

    printf("--- Step 1: Record CRTM measurement ---\n");
    event_log_add(&log, 0, EV_S_CRTM_CONTENTS, crtm_hash,
                  (uint8_t*)"CRTM", 4);
    pcr_extend(&bank, 0, crtm_hash, SHA256_DIGEST_SIZE);
    printf("  PCR0 extended with CRTM measurement\n");

    printf("--- Step 2: Record BIOS measurement ---\n");
    event_log_add(&log, 0, EV_EFI_PLATFORM_FIRMWARE_BLOB, bios_hash,
                  (uint8_t*)"BIOS", 4);
    pcr_extend(&bank, 0, bios_hash, SHA256_DIGEST_SIZE);
    printf("  PCR0 extended with BIOS measurement\n");

    printf("--- Step 3: Record Option ROM measurement ---\n");
    event_log_add(&log, 2, EV_EFI_BOOT_SERVICES_DRIVER, option_rom_hash,
                  (uint8_t*)"OptionROM", 9);
    pcr_extend(&bank, 2, option_rom_hash, SHA256_DIGEST_SIZE);
    printf("  PCR2 extended with Option ROM measurement\n");

    printf("--- Step 4: Record MBR measurement ---\n");
    event_log_add(&log, 4, EV_EFI_BOOT_SERVICES_APP, mbr_hash,
                  (uint8_t*)"MBR", 3);
    event_log_add(&log, 8, EV_EFI_BOOT_SERVICES_APP, mbr_hash,
                  (uint8_t*)"MBR->PCR8", 10);
    pcr_extend(&bank, 4, mbr_hash, SHA256_DIGEST_SIZE);
    pcr_extend(&bank, 8, mbr_hash, SHA256_DIGEST_SIZE);
    printf("  PCR4/8 extended with MBR measurement\n");

    printf("--- Step 5: Record Bootloader measurement ---\n");
    event_log_add(&log, 4, EV_EFI_BOOT_SERVICES_APP, bootloader_hash,
                  (uint8_t*)"GRUB", 4);
    event_log_add(&log, 8, EV_EFI_BOOT_SERVICES_APP, bootloader_hash,
                  (uint8_t*)"GRUB->PCR8", 11);
    pcr_extend(&bank, 4, bootloader_hash, SHA256_DIGEST_SIZE);
    pcr_extend(&bank, 8, bootloader_hash, SHA256_DIGEST_SIZE);
    printf("  PCR4/8 extended with Bootloader measurement\n\n");

    printf("--- Event Log Contents ---\n");
    event_log_print(&log);

    printf("\n--- Replay Event Log Against Fresh PCR Bank ---\n");
    event_log_replay(&log, &replayed_bank);

    pcr_read(&bank, 0, actual_pcr0);
    pcr_read(&replayed_bank, 0, crtm_hash);
    printf("  PCR0 (actual):   ");
    for (i = 0; i < 16; i++) printf("%02x", actual_pcr0[i]);
    printf("...\n");
    printf("  PCR0 (replayed): ");
    for (i = 0; i < 16; i++) printf("%02x", crtm_hash[i]);
    printf("...\n");
    printf("  PCR0 match: %s\n",
           memcmp(actual_pcr0, crtm_hash, SHA256_DIGEST_SIZE) == 0
           ? "PASS" : "FAIL");

    pcr_read(&bank, 8, actual_pcr8);
    pcr_read(&replayed_bank, 8, bios_hash);
    printf("  PCR8 (actual):   ");
    for (i = 0; i < 16; i++) printf("%02x", actual_pcr8[i]);
    printf("...\n");
    printf("  PCR8 (replayed): ");
    for (i = 0; i < 16; i++) printf("%02x", bios_hash[i]);
    printf("...\n");
    printf("  PCR8 match: %s\n",
           memcmp(actual_pcr8, bios_hash, SHA256_DIGEST_SIZE) == 0
           ? "PASS" : "FAIL");

    printf("\n--- Verify PCR0 via event log ---\n");
    {
        bool verified = event_log_verify_pcr(&log, &bank, 0,
                                             actual_pcr0, TPM_ALG_SHA256);
        printf("  PCR0 verification: %s\n", verified ? "PASS" : "FAIL");
    }

    return 0;
}
