#include "stage1.h"
#include <stdio.h>
#include <string.h>

int main(void)
{
    printf("=== MBR Boot Demo ===\n\n");

    MBR mbr;
    mbr_init(&mbr);

    PartitionEntry *entries = (PartitionEntry *)(mbr.partition_table);

    entries[0].status    = BOOTABLE_MARK;
    entries[0].first_lba = 63;
    entries[0].sectors   = 2097152;
    entries[0].partition_type = 0x83;

    entries[1].status    = INACTIVE_MARK;
    entries[1].first_lba = 2097215;
    entries[1].sectors   = 4194304;
    entries[1].partition_type = 0x83;

    printf("MBR created with 2 partitions\n");

    {
        FILE *f = fopen("disk_mbr.bin", "wb");
        if (f != NULL) {
            fwrite(&mbr, 1, MBR_SIZE, f);
            fclose(f);
            printf("Disk image 'disk_mbr.bin' written\n");
        }
    }

    mbr_print_partitions(&mbr);

    PartitionEntry *active = mbr_find_active_partition(&mbr);
    if (active != NULL) {
        printf("\nActive partition found:\n");
        printf("  First LBA: %u\n", active->first_lba);
        printf("  Sectors:   %u\n", active->sectors);
    } else {
        printf("\nNo active partition found\n");
    }

    PartitionEntry *bootable = mbr_find_bootable(&mbr);
    if (bootable != NULL) {
        printf("Bootable partition: LBA %u, %u sectors\n",
               bootable->first_lba, bootable->sectors);
    }

    printf("\nValidating MBR: %s\n", mbr_validate(&mbr) ? "PASS" : "FAIL");

    mbr_emulate_boot(&mbr);

    return 0;
}
