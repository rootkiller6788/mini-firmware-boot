#include <stdio.h>
#include <string.h>
#include "sha256.h"
#include "pcr_bank.h"

int main(void) {
    PCRBank bank;
    uint8_t data1[SHA256_DIGEST_SIZE];
    uint8_t data2[SHA256_DIGEST_SIZE];
    uint8_t data3[SHA256_DIGEST_SIZE];
    uint8_t readback[SHA256_DIGEST_SIZE];
    uint32_t i;

    printf("=== TPM 2.0 PCR Extend Demo ===\n\n");

    pcr_bank_init(&bank, TPM_ALG_SHA256);
    printf("Initialized PCR bank with SHA256 (24 PCRs)\n\n");

    sha256_hash((uint8_t*)"BIOS_FIRMWARE_V1", 17, data1);
    sha256_hash((uint8_t*)"MBR_BOOT_SECTOR", 16, data2);
    sha256_hash((uint8_t*)"GRUB_LOADER_V2", 15, data3);

    printf("--- Step 1: Extend PCR0 with BIOS measurement ---\n");
    printf("  BIOS hash: ");
    for (i = 0; i < 8; i++) printf("%02x", data1[i]);
    printf("...\n");

    pcr_extend(&bank, 0, data1, SHA256_DIGEST_SIZE);
    pcr_read(&bank, 0, readback);
    printf("  PCR0 after extend: ");
    for (i = 0; i < 32; i++) printf("%02x", readback[i]);
    printf("\n\n");

    printf("--- Step 2: Extend PCR4 with MBR measurement ---\n");
    printf("  MBR hash: ");
    for (i = 0; i < 8; i++) printf("%02x", data2[i]);
    printf("...\n");

    pcr_extend(&bank, 4, data2, SHA256_DIGEST_SIZE);
    pcr_read(&bank, 4, readback);
    printf("  PCR4 after extend: ");
    for (i = 0; i < 32; i++) printf("%02x", readback[i]);
    printf("\n\n");

    printf("--- Step 3: Extend PCR8 with Bootloader measurement ---\n");
    printf("  Bootloader hash: ");
    for (i = 0; i < 8; i++) printf("%02x", data3[i]);
    printf("...\n");

    pcr_extend(&bank, 8, data3, SHA256_DIGEST_SIZE);
    pcr_read(&bank, 8, readback);
    printf("  PCR8 after extend: ");
    for (i = 0; i < 32; i++) printf("%02x", readback[i]);
    printf("\n\n");

    printf("--- Step 4: Extend PCR0 again (chaining demo) ---\n");
    printf("  PCR_new = Hash(PCR_old || data)\n");
    printf("  This demonstrates the chained hashing property.\n");

    {
        uint8_t extra_data[SHA256_DIGEST_SIZE];
        sha256_hash((uint8_t*)"BIOS_CONFIG_CHANGE", 19, extra_data);
        pcr_extend(&bank, 0, extra_data, SHA256_DIGEST_SIZE);
        pcr_read(&bank, 0, readback);
        printf("  PCR0 after second extend: ");
        for (i = 0; i < 32; i++) printf("%02x", readback[i]);
        printf("\n\n");
    }

    printf("--- Step 5: Verify PCR reset (DRTM PCRs only) ---\n");
    pcr_extend(&bank, 17, data1, SHA256_DIGEST_SIZE);
    pcr_read(&bank, 17, readback);
    printf("  PCR17 before reset: ");
    for (i = 0; i < 8; i++) printf("%02x", readback[i]);
    printf("...\n");

    pcr_reset(&bank, 17);
    pcr_read(&bank, 17, readback);
    printf("  PCR17 after reset:  ");
    for (i = 0; i < 8; i++) printf("%02x", readback[i]);
    printf("...\n");

    {
        bool ok;
        ok = pcr_reset(&bank, 0);
        printf("  PCR0 reset attempt:  %s (static PCR, should fail)\n\n",
               ok ? "OK" : "DENIED");
    }

    printf("--- Final PCR Bank State ---\n");
    pcr_print_bank(&bank);

    return 0;
}
