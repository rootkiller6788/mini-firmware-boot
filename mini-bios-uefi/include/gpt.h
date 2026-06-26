#ifndef GPT_H
#define GPT_H

#include <stdbool.h>
#include <stdint.h>

#define GPT_SIGNATURE       0x5452415020494645ULL
#define GPT_HEADER_REVISION 0x00010000
#define GPT_HEADER_SIZE     92
#define GPT_PARTITION_ENTRY_SIZE 128
#define GPT_MIN_PARTITIONS  128
#define SECTOR_SIZE         512
#define GPT_PROTECTIVE_MBR_SIGNATURE 0xAA55

/* Known partition type GUIDs */
#define GPT_TYPE_EFI_SYSTEM    0
#define GPT_TYPE_MICROSOFT_BASIC 1
#define GPT_TYPE_LINUX_FILESYSTEM 2
#define GPT_TYPE_LINUX_SWAP    3
#define GPT_TYPE_LINUX_HOME    4

/* EFI System Partition */
#define EFI_SYSTEM_PARTITION_GUID \
    {0xC12A7328, 0xF81F, 0x11D2, {0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B}}

/* Microsoft Basic Data Partition */
#define MICROSOFT_BASIC_DATA_GUID \
    {0xEBD0A0A2, 0xB9E5, 0x4433, {0x87, 0xC0, 0x68, 0xB6, 0xB7, 0x26, 0x99, 0xC7}}

/* Linux Filesystem Data */
#define LINUX_FILESYSTEM_DATA_GUID \
    {0x0FC63DAF, 0x8483, 0x4772, {0x8E, 0x79, 0x3D, 0x69, 0xD8, 0x47, 0x7D, 0xE4}}

/* Linux Swap */
#define LINUX_SWAP_GUID \
    {0x0657FD6D, 0xA4AB, 0x43C4, {0x84, 0xE5, 0x09, 0x33, 0xC8, 0x4B, 0x4F, 0x4F}}

/* Linux /home */
#define LINUX_HOME_GUID \
    {0x933AC7E1, 0x2EB4, 0x4F13, {0xB8, 0x44, 0x0E, 0x14, 0xE2, 0xAE, 0xF9, 0x15}}

/* Partition attributes */
#define GPT_ATTR_REQUIRED_PARTITION     0x0000000000000001ULL
#define GPT_ATTR_NO_BLOCK_IO_PROTOCOL   0x0000000000000002ULL
#define GPT_ATTR_LEGACY_BIOS_BOOTABLE   0x0000000000000004ULL

#pragma pack(push, 1)

typedef struct {
    uint32_t data1;
    uint16_t data2;
    uint16_t data3;
    uint8_t  data4[8];
} GTPGuid;

typedef struct {
    uint64_t signature;
    uint32_t revision;
    uint32_t header_size;
    uint32_t header_crc32;
    uint32_t reserved;
    uint64_t my_lba;
    uint64_t alternate_lba;
    uint64_t first_usable_lba;
    uint64_t last_usable_lba;
    GTPGuid  disk_guid;
    uint64_t partition_entry_lba;
    uint32_t num_partition_entries;
    uint32_t partition_entry_size;
    uint32_t partition_array_crc32;
    /* Remainder of sector is zero-filled */
} GPTHeader;

typedef struct {
    GTPGuid  partition_type_guid;
    GTPGuid  unique_partition_guid;
    uint64_t starting_lba;
    uint64_t ending_lba;
    uint64_t attributes;
    uint16_t partition_name[36];
} GPTPartitionEntry;

typedef struct {
    GTPGuid  type_guid;
    GTPGuid  unique_guid;
    uint64_t first_lba;
    uint64_t last_lba;
    uint64_t attributes;
    char     name[72];
} GPTPartition;

typedef struct {
    GPTHeader  header;
    GPTPartitionEntry entries[128];
    uint64_t total_sectors;
    uint32_t sector_size;
} GPTDisk;

#pragma pack(pop)

bool gpt_read_header(GPTHeader *header, const void *buffer, size_t size);
int  gpt_read_partitions(GPTPartition *partitions, uint32_t max_parts,
                         const uint8_t *buffer, const GPTHeader *header);
int  gpt_find_efi_system_partition(const GPTPartition *partitions, uint32_t count);
int  gpt_find_partition_by_type(const GPTPartition *partitions, uint32_t count,
                                int type_id);
void gpt_print_partitions(const GPTPartition *partitions, uint32_t count);
void gpt_print_header(const GPTHeader *header);
void gpt_build_protective_mbr(uint8_t *mbr, uint64_t total_sectors);
void gpt_build_disk(GPTDisk *disk, uint64_t total_sectors,
                    const GPTPartition *parts, uint32_t count);
const char* gpt_type_name(int type_id);
const char* gpt_guid_to_string(const GTPGuid *guid, char *buf, size_t buf_size);

#endif /* GPT_H */
