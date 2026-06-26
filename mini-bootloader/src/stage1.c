#include "stage1.h"
#include <stdio.h>
#include <string.h>

void mbr_init(MBR *mbr)
{
    memset(mbr, 0, sizeof(MBR));
    mbr->signature = MBR_SIGNATURE;

    const uint8_t default_bootstrap[] = {
        0x31, 0xC0, 0x8E, 0xD8, 0x8E, 0xC0, 0x8E, 0xD0,
        0xBC, 0x00, 0x7C, 0xFA, 0xFC, 0xE8, 0x08, 0x00,
        0xB4, 0x0E, 0xBB, 0x07, 0x00, 0xCD, 0x10, 0xEB,
        0xFE, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    memcpy(mbr->bootstrap_code, default_bootstrap,
           sizeof(default_bootstrap));
}

bool mbr_load_from_disk(MBR *mbr)
{
    FILE *f = fopen("disk_mbr.bin", "rb");
    if (f == NULL) {
        fprintf(stderr, "Cannot open disk image at LBA 0\n");
        return false;
    }

    size_t sz = fread(mbr, 1, MBR_SIZE, f);
    fclose(f);

    if (sz != MBR_SIZE) {
        fprintf(stderr, "Failed to read full MBR (got %zu bytes)\n", sz);
        return false;
    }

    if (!mbr_validate(mbr)) {
        fprintf(stderr, "Invalid MBR signature\n");
        return false;
    }

    return true;
}

PartitionEntry *mbr_find_active_partition(MBR *mbr)
{
    PartitionEntry *entries = (PartitionEntry *)(mbr->partition_table);

    for (int i = 0; i < PARTITION_ENTRIES; i++) {
        if (entries[i].status == BOOTABLE_MARK &&
            entries[i].first_lba > 0) {
            return &entries[i];
        }
    }
    return NULL;
}

PartitionEntry *mbr_find_bootable(MBR *mbr)
{
    PartitionEntry *active = mbr_find_active_partition(mbr);
    if (active != NULL) return active;

    PartitionEntry *entries = (PartitionEntry *)(mbr->partition_table);
    for (int i = 0; i < PARTITION_ENTRIES; i++) {
        if (entries[i].first_lba > 0 && entries[i].sectors > 0) {
            return &entries[i];
        }
    }
    return NULL;
}

void mbr_print_partitions(const MBR *mbr)
{
    const PartitionEntry *entries =
        (const PartitionEntry *)(mbr->partition_table);

    printf("=== Partition Table ===\n");
    printf("%-6s %-10s %-12s %-12s\n", "Idx", "Status", "First LBA", "Sectors");

    for (int i = 0; i < PARTITION_ENTRIES; i++) {
        const char *status_str;
        switch (entries[i].status) {
            case BOOTABLE_MARK: status_str = "BOOT"; break;
            case INACTIVE_MARK: status_str = "INACTIVE"; break;
            default:            status_str = "UNKNOWN"; break;
        }

        printf("%-6d %-10s %-12u %-12u\n",
               i, status_str, entries[i].first_lba, entries[i].sectors);
    }

    printf("Signature: 0x%04X\n", mbr->signature);
}

bool mbr_validate(const MBR *mbr)
{
    return mbr->signature == MBR_SIGNATURE;
}

void mbr_emulate_boot(const MBR *mbr)
{
    PartitionEntry *boot_part = mbr_find_bootable((MBR *)mbr);

    printf("\n=== Boot Sequence Emulation ===\n");
    printf("[BIOS] POST complete\n");
    printf("[BIOS] INT 19h: Loading MBR from LBA 0 (0x%08X)\n", LBA0_ADDR);
    printf("[BIOS] MBR loaded to 0x%04X:0x%04X (0x%05X)\n",
           0x0000, VBR_LOAD_ADDR, VBR_LOAD_ADDR);

    if (!mbr_validate(mbr)) {
        printf("[BIOS] ERROR: Invalid boot signature! Halting.\n");
        return;
    }

    printf("[MBR] Signature 0x%04X valid\n", mbr->signature);
    printf("[MBR] Relocating to 0x0600\n");
    printf("[MBR] Scanning partition table...\n");

    if (boot_part == NULL) {
        printf("[MBR] No bootable partition found. Halting.\n");
        return;
    }

    printf("[MBR] Found bootable partition:\n");
    printf("      First LBA: %u, Sectors: %u\n",
           boot_part->first_lba, boot_part->sectors);
    printf("[MBR] Loading VBR from LBA %u to 0x%04X\n",
           boot_part->first_lba, VBR_LOAD_ADDR);
    printf("[MBR] Jumping to VBR at 0x%04X\n", VBR_LOAD_ADDR);

    printf("\n[VBR] Running volume boot record...\n");
    printf("[VBR] Locating stage2 bootloader file...\n");
    printf("[VBR] Handing off to stage2...\n");
}
