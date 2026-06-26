#ifndef STAGE1_H
#define STAGE1_H

#include <stdbool.h>
#include <stdint.h>

#define MBR_SIZE           512
#define MBR_SIGNATURE      0xAA55
#define PARTITION_OFFSET   446
#define PARTITION_ENTRIES  4
#define PARTITION_ENTRY_SIZE 16
#define BOOTABLE_MARK      0x80
#define INACTIVE_MARK      0x00

#define LBA0_ADDR          0x0000
#define VBR_LOAD_ADDR      0x7C00

typedef struct {
    uint8_t  bootstrap_code[446];
    uint8_t  partition_table[64];
    uint16_t signature;
} MBR;

typedef struct {
    uint8_t  status;
    uint8_t  chs_first[3];
    uint8_t  partition_type;
    uint8_t  chs_last[3];
    uint32_t first_lba;
    uint32_t sectors;
} PartitionEntry;

typedef struct {
    uint8_t  code[512];
    uint16_t signature;
} VBR;

void               mbr_init(MBR *mbr);
bool               mbr_load_from_disk(MBR *mbr);
PartitionEntry *   mbr_find_active_partition(MBR *mbr);
PartitionEntry *   mbr_find_bootable(MBR *mbr);
void               mbr_print_partitions(const MBR *mbr);
bool               mbr_validate(const MBR *mbr);
void               mbr_emulate_boot(const MBR *mbr);

#endif
