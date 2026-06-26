#ifndef BOOT_CHAIN_H
#define BOOT_CHAIN_H

#include <stdbool.h>
#include <stdint.h>

/*
 * L2: Chain loading — bootloader loads another bootloader.
 *     Common pattern in multi-boot systems (GRUB → Windows, etc.)
 * L3: CHS/LBA translation — bridging legacy and modern disk addressing.
 * L5: Sector I/O emulation — read/write 512-byte sectors.
 */

#define CHAIN_SECTOR_SIZE   512
#define CHAIN_LBA28_MAX     0x0FFFFFFF
#define CHAIN_LBA48_MAX     0x0000FFFFFFFFFFFF
#define CHAIN_MAX_CYLINDER  1024
#define CHAIN_MAX_HEAD      256
#define CHAIN_MAX_SECTOR    63

#define CHAIN_PARTITION_MAX 128
#define CHAIN_GPT_SIGNATURE 0x5452415020494645  /* "EFI PART" */

/* CHS address */
typedef struct {
    uint16_t cylinder;
    uint8_t  head;
    uint8_t  sector;
} CHSAddr;

/* GPT partition entry */
typedef struct {
    uint8_t  type_guid[16];
    uint8_t  unique_guid[16];
    uint64_t first_lba;
    uint64_t last_lba;
    uint64_t attributes;
    uint16_t name[36];
} GPTPartEntry;

/* GPT header */
typedef struct {
    uint64_t signature;
    uint32_t revision;
    uint32_t header_size;
    uint32_t header_crc32;
    uint32_t reserved;
    uint64_t my_lba;
    uint64_t alt_lba;
    uint64_t first_usable;
    uint64_t last_usable;
    uint8_t  disk_guid[16];
    uint64_t entries_lba;
    uint32_t entry_count;
    uint32_t entry_size;
    uint32_t entries_crc32;
} GPTHeader;

/* Chain load target */
typedef struct {
    uint64_t start_lba;
    uint64_t sector_count;
    uint8_t  partition_type;
    uint8_t  *boot_code;
    uint32_t  code_size;
    uint32_t  load_addr;
    uint32_t  entry_point;
} ChainTarget;

/* Sector I/O interface */
typedef struct {
    uint8_t *disk_image;
    uint32_t disk_size;
    uint32_t current_lba;
} SectorIO;

/* ── API ────────────────────────────────────────────────── */
/* CHS/LBA translation */
CHSAddr  chs_from_lba(uint32_t lba, uint8_t heads, uint8_t sectors_per_track);
uint32_t lba_from_chs(CHSAddr chs, uint8_t heads, uint8_t sectors_per_track);

/* Sector I/O */
void     sector_io_init(SectorIO *io, uint8_t *disk_image, uint32_t disk_size);
bool     sector_read(SectorIO *io, uint32_t lba, uint8_t *buffer);
bool     sector_write(SectorIO *io, uint32_t lba, const uint8_t *buffer);
bool     sector_read_multi(SectorIO *io, uint32_t lba, uint8_t count, uint8_t *buffer);

/* GPT parsing */
bool     gpt_parse_header(const uint8_t *sector, GPTHeader *header);
bool     gpt_validate_header(const GPTHeader *header);
bool     gpt_find_partition(const GPTHeader *header, const uint8_t *disk_image,
                            const uint8_t *type_guid, GPTPartEntry *entry);

/* Chain loading */
void     chain_target_init(ChainTarget *target);
bool     chain_load_boot_sector(SectorIO *io, const ChainTarget *target);
bool     chain_emulate_boot(const ChainTarget *target);
void     chain_target_print(const ChainTarget *target);

#endif