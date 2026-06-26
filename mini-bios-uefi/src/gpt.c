#include "gpt.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const GTPGuid g_known_type_guids[] = {
    EFI_SYSTEM_PARTITION_GUID,
    MICROSOFT_BASIC_DATA_GUID,
    LINUX_FILESYSTEM_DATA_GUID,
    LINUX_SWAP_GUID,
    LINUX_HOME_GUID,
};

static const char *g_known_type_names[] = {
    "EFI System Partition",
    "Microsoft Basic Data",
    "Linux Filesystem",
    "Linux Swap",
    "Linux /home",
};

#define KNOWN_TYPE_COUNT (sizeof(g_known_type_guids) / sizeof(g_known_type_guids[0]))

const char *gpt_type_name(int type_id) {
    if (type_id >= 0 && (size_t)type_id < KNOWN_TYPE_COUNT) {
        return g_known_type_names[type_id];
    }
    return "Unknown";
}

const char *gpt_guid_to_string(const GTPGuid *guid, char *buf, size_t buf_size) {
    if (!guid || !buf || buf_size < 37) return NULL;
    snprintf(buf, buf_size,
             "%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X",
             guid->data1, guid->data2, guid->data3,
             guid->data4[0], guid->data4[1],
             guid->data4[2], guid->data4[3],
             guid->data4[4], guid->data4[5],
             guid->data4[6], guid->data4[7]);
    return buf;
}

static int guid_match_known(const GTPGuid *guid) {
    for (size_t i = 0; i < KNOWN_TYPE_COUNT; i++) {
        if (memcmp(guid, &g_known_type_guids[i], sizeof(GTPGuid)) == 0) {
            return (int)i;
        }
    }
    return -1;
}

bool gpt_read_header(GPTHeader *header, const void *buffer, size_t size) {
    if (!header || !buffer || size < sizeof(GPTHeader)) return false;

    memcpy(header, buffer, sizeof(GPTHeader));

    if (header->signature != GPT_SIGNATURE) {
        printf("  GPT: Invalid signature (expected 0x%llX, got 0x%llX)\n",
               (unsigned long long)GPT_SIGNATURE,
               (unsigned long long)header->signature);
        return false;
    }

    if (header->revision != GPT_HEADER_REVISION) {
        printf("  GPT: Warning — revision mismatch (got 0x%08X)\n", header->revision);
    }

    printf("  GPT: Header valid — LBA %llu, partitions=%u, first_usable=%llu, last_usable=%llu\n",
           (unsigned long long)header->my_lba, header->num_partition_entries,
           (unsigned long long)header->first_usable_lba,
           (unsigned long long)header->last_usable_lba);
    return true;
}

int gpt_read_partitions(GPTPartition *partitions, uint32_t max_parts,
                         const uint8_t *buffer, const GPTHeader *header) {
    if (!partitions || !buffer || !header) return 0;

    uint32_t count = header->num_partition_entries;
    if (count > max_parts) count = max_parts;
    if (count > GPT_MIN_PARTITIONS) count = GPT_MIN_PARTITIONS;

    const GPTPartitionEntry *entries =
        (const GPTPartitionEntry *)(buffer + header->partition_entry_lba * SECTOR_SIZE);

    int found = 0;
    for (uint32_t i = 0; i < count; i++) {
        if (entries[i].partition_type_guid.data1 == 0) continue;

        memcpy(&partitions[found].type_guid, &entries[i].partition_type_guid,
               sizeof(GTPGuid));
        memcpy(&partitions[found].unique_guid, &entries[i].unique_partition_guid,
               sizeof(GTPGuid));
        partitions[found].first_lba  = entries[i].starting_lba;
        partitions[found].last_lba   = entries[i].ending_lba;
        partitions[found].attributes = entries[i].attributes;

        /* Convert UCS-2 partition name to ASCII */
        for (int j = 0; j < 36 && (int)(sizeof(partitions[found].name) - 1) > j * 2; j++) {
            partitions[found].name[j] = (char)(entries[i].partition_name[j] & 0xFF);
        }
        partitions[found].name[71] = '\0';

        found++;
    }

    printf("  GPT: Read %d partitions from entry array\n", found);
    return found;
}

int gpt_find_efi_system_partition(const GPTPartition *partitions, uint32_t count) {
    return gpt_find_partition_by_type(partitions, count, GPT_TYPE_EFI_SYSTEM);
}

int gpt_find_partition_by_type(const GPTPartition *partitions, uint32_t count,
                                int type_id) {
    if (!partitions || type_id < 0 || (size_t)type_id >= KNOWN_TYPE_COUNT) return -1;

    for (uint32_t i = 0; i < count; i++) {
        if (memcmp(&partitions[i].type_guid,
                   &g_known_type_guids[type_id], sizeof(GTPGuid)) == 0) {
            printf("  GPT: Found %s at index %u\n", g_known_type_names[type_id], i);
            return (int)i;
        }
    }

    printf("  GPT: %s not found\n", g_known_type_names[type_id]);
    return -1;
}

void gpt_print_partitions(const GPTPartition *partitions, uint32_t count) {
    if (!partitions) { printf("  No partitions\n"); return; }

    printf("\n=== GPT Partition Table ===\n");
    printf("  %-3s  %-22s  %12s  %12s  %9s  %s\n",
           "#", "Type", "First LBA", "Last LBA", "Size (MB)", "Name");

    for (uint32_t i = 0; i < count; i++) {
        int type_id = guid_match_known(&partitions[i].type_guid);
        uint64_t size_mb = (partitions[i].last_lba - partitions[i].first_lba + 1) * SECTOR_SIZE;
        size_mb /= (1024 * 1024);

        char guid_str[37];
        gpt_guid_to_string(&partitions[i].type_guid, guid_str, sizeof(guid_str));

        printf("  %-3u  %-22s  %12llu  %12llu  %8llu  %s\n",
               i + 1,
               (type_id >= 0) ? g_known_type_names[type_id] : guid_str,
               (unsigned long long)partitions[i].first_lba,
               (unsigned long long)partitions[i].last_lba,
               (unsigned long long)size_mb,
               partitions[i].name);

        if (partitions[i].attributes & GPT_ATTR_REQUIRED_PARTITION) {
            printf("                                                     [REQUIRED]\n");
        }
        if (partitions[i].attributes & GPT_ATTR_LEGACY_BIOS_BOOTABLE) {
            printf("                                                     [BIOS BOOTABLE]\n");
        }
    }
}

void gpt_print_header(const GPTHeader *header) {
    if (!header) { printf("GPT header is NULL\n"); return; }

    printf("\n=== GPT Header ===\n");
    printf("  Signature:           0x%016llX\n", (unsigned long long)header->signature);
    printf("  Revision:            0x%08X\n", header->revision);
    printf("  Header Size:         %u bytes\n", header->header_size);
    printf("  Header CRC32:        0x%08X\n", header->header_crc32);
    printf("  My LBA:              %llu\n", (unsigned long long)header->my_lba);
    printf("  Alternate LBA:       %llu\n", (unsigned long long)header->alternate_lba);
    printf("  First Usable LBA:    %llu\n", (unsigned long long)header->first_usable_lba);
    printf("  Last Usable LBA:     %llu\n", (unsigned long long)header->last_usable_lba);
    printf("  Partition Entry LBA: %llu\n", (unsigned long long)header->partition_entry_lba);
    printf("  Num Partitions:      %u\n", header->num_partition_entries);
    printf("  Partition Entry Sz:  %u bytes\n", header->partition_entry_size);

    char guid_str[37];
    gpt_guid_to_string(&header->disk_guid, guid_str, sizeof(guid_str));
    printf("  Disk GUID:           %s\n", guid_str);
}

void gpt_build_protective_mbr(uint8_t *mbr, uint64_t total_sectors) {
    if (!mbr) return;
    memset(mbr, 0, SECTOR_SIZE);

    /* Protective MBR: single partition entry type 0xEE spanning entire disk */
    mbr[0x1BE + 4] = 0xEE;
    mbr[0x1BE + 5] = 0xFF;
    mbr[0x1BE + 6] = 0xFF;
    mbr[0x1BE + 7] = 0xFF;

    mbr[0x1BE + 8]  = 1;  /* Starting LBA = 1 */
    mbr[0x1BE + 9]  = 0;
    mbr[0x1BE + 10] = 0;
    mbr[0x1BE + 11] = 0;

    if (total_sectors > 0xFFFFFFFFULL) {
        mbr[0x1BE + 12] = 0xFF;
        mbr[0x1BE + 13] = 0xFF;
        mbr[0x1BE + 14] = 0xFF;
        mbr[0x1BE + 15] = 0xFF;
    } else {
        mbr[0x1BE + 12] = (uint8_t)(total_sectors >> 0);
        mbr[0x1BE + 13] = (uint8_t)(total_sectors >> 8);
        mbr[0x1BE + 14] = (uint8_t)(total_sectors >> 16);
        mbr[0x1BE + 15] = (uint8_t)(total_sectors >> 24);
    }

    mbr[0x1FE] = 0x55;
    mbr[0x1FF] = 0xAA;

    printf("  Protective MBR: built for %llu sectors\n",
           (unsigned long long)total_sectors);
}

void gpt_build_disk(GPTDisk *disk, uint64_t total_sectors,
                    const GPTPartition *parts, uint32_t count) {
    if (!disk) return;
    memset(disk, 0, sizeof(GPTDisk));

    disk->total_sectors = total_sectors;
    disk->sector_size   = SECTOR_SIZE;

    disk->header.signature            = GPT_SIGNATURE;
    disk->header.revision             = GPT_HEADER_REVISION;
    disk->header.header_size          = sizeof(GPTHeader);
    disk->header.my_lba               = 1;
    disk->header.alternate_lba        = total_sectors - 1;
    disk->header.first_usable_lba     = 34;
    disk->header.last_usable_lba      = total_sectors - 34;
    disk->header.partition_entry_lba  = 2;
    disk->header.num_partition_entries = GPT_MIN_PARTITIONS;
    disk->header.partition_entry_size  = GPT_PARTITION_ENTRY_SIZE;

    /* Copy partitions */
    for (uint32_t i = 0; i < count && i < GPT_MIN_PARTITIONS; i++) {
        memcpy(&disk->entries[i].partition_type_guid,
               &parts[i].type_guid, sizeof(GTPGuid));
        memcpy(&disk->entries[i].unique_partition_guid,
               &parts[i].unique_guid, sizeof(GTPGuid));
        disk->entries[i].starting_lba = parts[i].first_lba;
        disk->entries[i].ending_lba   = parts[i].last_lba;
        disk->entries[i].attributes   = parts[i].attributes;

        for (size_t j = 0; j < 36 && j < strlen(parts[i].name); j++) {
            disk->entries[i].partition_name[j] = (uint16_t)parts[i].name[j];
        }
    }

    printf("  GPT Disk: built with %u partitions, %llu total sectors\n",
           count, (unsigned long long)total_sectors);
}
