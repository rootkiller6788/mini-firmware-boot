#include "gpt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define TOTAL_SECTORS (2097152ULL)
#define ESP_START_LBA  2048ULL
#define ESP_SIZE_LBA   (262144ULL)
#define LINUX_START_LBA (ESP_START_LBA + ESP_SIZE_LBA)
#define LINUX_SIZE_LBA  (1048576ULL)

static void generate_disk_guid(GTPGuid *guid) {
    srand((unsigned)time(NULL));
    guid->data1 = (uint32_t)rand();
    guid->data2 = (uint16_t)(rand() & 0xFFFF);
    guid->data3 = (uint16_t)(rand() & 0xFFFF);
    for (int i = 0; i < 8; i++) {
        guid->data4[i] = (uint8_t)(rand() & 0xFF);
    }
    guid->data3 = (uint16_t)((guid->data3 & 0x0FFF) | 0x4000);
    guid->data4[0] = (uint8_t)((guid->data4[0] & 0x3F) | 0x80);
}

static void fill_partition(GPTPartition *p, int type_id, uint64_t start, uint64_t size,
                           const char *name) {
    memset(p, 0, sizeof(GPTPartition));

    switch (type_id) {
    case GPT_TYPE_EFI_SYSTEM:
        p->type_guid.data1 = 0xC12A7328; p->type_guid.data2 = 0xF81F;
        p->type_guid.data3 = 0x11D2;
        memcpy(p->type_guid.data4, "\xBA\x4B\x00\xA0\xC9\x3E\xC9\x3B", 8);
        break;
    case GPT_TYPE_MICROSOFT_BASIC:
        p->type_guid.data1 = 0xEBD0A0A2; p->type_guid.data2 = 0xB9E5;
        p->type_guid.data3 = 0x4433;
        memcpy(p->type_guid.data4, "\x87\xC0\x68\xB6\xB7\x26\x99\xC7", 8);
        break;
    case GPT_TYPE_LINUX_FILESYSTEM:
        p->type_guid.data1 = 0x0FC63DAF; p->type_guid.data2 = 0x8483;
        p->type_guid.data3 = 0x4772;
        memcpy(p->type_guid.data4, "\x8E\x79\x3D\x69\xD8\x47\x7D\xE4", 8);
        break;
    case GPT_TYPE_LINUX_SWAP:
        p->type_guid.data1 = 0x0657FD6D; p->type_guid.data2 = 0xA4AB;
        p->type_guid.data3 = 0x43C4;
        memcpy(p->type_guid.data4, "\x84\xE5\x09\x33\xC8\x4B\x4F\x4F", 8);
        break;
    case GPT_TYPE_LINUX_HOME:
        p->type_guid.data1 = 0x933AC7E1; p->type_guid.data2 = 0x2EB4;
        p->type_guid.data3 = 0x4F13;
        memcpy(p->type_guid.data4, "\xB8\x44\x0E\x14\xE2\xAE\xF9\x15", 8);
        break;
    }

    generate_disk_guid(&p->unique_guid);
    p->first_lba = start;
    p->last_lba  = start + size - 1;
    strncpy(p->name, name, sizeof(p->name) - 1);
}

int main(void) {
    printf("========================================\n");
    printf("  mini-gpt: GPT Partition Table Demo\n");
    printf("========================================\n\n");

    /* --- Step 1: Create Disk Layout --- */
    printf("--- Step 1: Creating Disk Layout ---\n");
    printf("  Disk size: %llu sectors (%llu MB)\n",
           (unsigned long long)TOTAL_SECTORS,
           (unsigned long long)(TOTAL_SECTORS * SECTOR_SIZE / (1024 * 1024)));

    GPTPartition partitions[4];
    memset(partitions, 0, sizeof(partitions));

    fill_partition(&partitions[0], GPT_TYPE_EFI_SYSTEM,
                   ESP_START_LBA, ESP_SIZE_LBA,
                   "EFI System Partition");
    partitions[0].attributes = GPT_ATTR_REQUIRED_PARTITION;

    fill_partition(&partitions[1], GPT_TYPE_LINUX_FILESYSTEM,
                   LINUX_START_LBA, LINUX_SIZE_LBA,
                   "Linux Root");

    uint64_t swap_start = LINUX_START_LBA + LINUX_SIZE_LBA;
    uint64_t swap_size  = TOTAL_SECTORS - swap_start - 34;
    fill_partition(&partitions[2], GPT_TYPE_LINUX_SWAP,
                   swap_start, swap_size,
                   "Linux Swap");

    /* --- Step 2: Build GPT Disk --- */
    printf("\n--- Step 2: Building GPT Disk Structure ---\n");

    GPTDisk disk;
    gpt_build_disk(&disk, TOTAL_SECTORS, partitions, 3);

    generate_disk_guid(&disk.header.disk_guid);

    gpt_print_header(&disk.header);

    /* --- Step 3: Create Protective MBR --- */
    printf("\n--- Step 3: Creating Protective MBR ---\n");
    uint8_t mbr[SECTOR_SIZE];
    gpt_build_protective_mbr(mbr, TOTAL_SECTORS);
    printf("  Protective MBR at LBA 0: byte[0x1BE]=0x%02X (type 0xEE)\n", mbr[0x1BE + 4]);
    printf("  MBR signature: 0x%02X%02X\n", mbr[0x1FE], mbr[0x1FF]);

    /* --- Step 4: Read Partitions --- */
    printf("\n--- Step 4: Reading Partition Table ---\n");

    /* Build a full sector array for partition entry region */
    size_t entry_region_size = (size_t)disk.header.num_partition_entries *
                               disk.header.partition_entry_size;
    uint8_t *entry_region = calloc(1, entry_region_size);
    if (entry_region) {
        memcpy(entry_region, disk.entries, entry_region_size);

        /* Build a buffer that simulates disk read at partition_entry_lba */
        uint8_t *disk_buf = calloc(1,
            (size_t)(disk.header.partition_entry_lba + 2) * SECTOR_SIZE + entry_region_size);
        if (disk_buf) {
            /* Place header at LBA 1 */
            memcpy(disk_buf + SECTOR_SIZE, &disk.header, sizeof(GPTHeader));
            /* Place entries at partition_entry_lba */
            memcpy(disk_buf + disk.header.partition_entry_lba * SECTOR_SIZE,
                   entry_region, entry_region_size);

            GPTHeader read_header;
            if (gpt_read_header(&read_header, disk_buf + SECTOR_SIZE, sizeof(GPTHeader))) {
                GPTPartition read_parts[128];
                int count = gpt_read_partitions(read_parts, 128, disk_buf, &read_header);
                gpt_print_partitions(read_parts, (uint32_t)count);

                /* --- Step 5: Find ESP --- */
                printf("\n--- Step 5: Find EFI System Partition ---\n");
                int esp_idx = gpt_find_efi_system_partition(read_parts, (uint32_t)count);
                if (esp_idx >= 0) {
                    printf("  ESP found at partition %d: LBA %llu–%llu (%llu MB)\n",
                           esp_idx + 1,
                           (unsigned long long)read_parts[esp_idx].first_lba,
                           (unsigned long long)read_parts[esp_idx].last_lba,
                           (unsigned long long)((read_parts[esp_idx].last_lba -
                                                 read_parts[esp_idx].first_lba + 1)
                                                * SECTOR_SIZE / (1024 * 1024)));
                }

                /* --- Step 6: Find Linux partitions --- */
                printf("\n--- Step 6: Find Linux Partitions ---\n");
                int lin_idx = gpt_find_partition_by_type(read_parts, (uint32_t)count,
                                                         GPT_TYPE_LINUX_FILESYSTEM);
                int swp_idx = gpt_find_partition_by_type(read_parts, (uint32_t)count,
                                                         GPT_TYPE_LINUX_SWAP);
                if (lin_idx >= 0) printf("  Linux root partition: index %d\n", lin_idx + 1);
                if (swp_idx >= 0) printf("  Linux swap partition: index %d\n", swp_idx + 1);
            }
            free(disk_buf);
        }
        free(entry_region);
    }

    /* --- Step 7: Summary --- */
    printf("\n--- Step 7: GPT Layout Summary ---\n");
    printf("  LBA 0:       Protective MBR\n");
    printf("  LBA 1:       GPT Header (primary)\n");
    printf("  LBA 2–33:    Partition Entry Array (128 x 128 bytes)\n");
    printf("  LBA 34–2047: Reserved\n");
    printf("  LBA 2048–%llu:  %s\n",
           (unsigned long long)(ESP_START_LBA + ESP_SIZE_LBA - 1),
           partitions[0].name);
    printf("  LBA %llu–%llu: %s\n",
           (unsigned long long)partitions[1].first_lba,
           (unsigned long long)partitions[1].last_lba,
           partitions[1].name);
    printf("  LBA %llu–%llu: %s\n",
           (unsigned long long)partitions[2].first_lba,
           (unsigned long long)partitions[2].last_lba,
           partitions[2].name);
    printf("  LBA %llu:     GPT Header (alternate/backup)\n",
           (unsigned long long)(TOTAL_SECTORS - 1));

    printf("\n=== mini-gpt: Demo Complete ===\n");
    return 0;
}
