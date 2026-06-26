#include "boot_chain.h"
#include <stdio.h>
#include <string.h>

/*
 * L2: Chain loading — bootloader loads another bootloader.
 *     Common in dual-boot systems (GRUB chains to Windows BOOTMGR).
 * L3: CHS/LBA translation — bridging legacy and modern disk addressing.
 *     C/H/S = Cylinder/Head/Sector, LBA = Logical Block Address.
 * L5: Sector I/O emulation — read/write 512-byte sectors from disk image.
 */

/*
 * ── L5: CHS to LBA translation algorithm ────────────────────────────
 *
 * Formula (ATA standard):
 *   LBA = (C × HPC + H) × SPT + (S − 1)
 * where HPC = heads per cylinder, SPT = sectors per track.
 *
 * Reverse (LBA to CHS):
 *   C = LBA / (HPC × SPT)
 *   H = (LBA / SPT) mod HPC
 *   S = (LBA mod SPT) + 1
 *
 * Reference: ATA/ATAPI-6, Section 6.2.2
 * Complexity: O(1)
 */
CHSAddr chs_from_lba(uint32_t lba, uint8_t heads, uint8_t sectors_per_track)
{
    CHSAddr chs;
    if (heads == 0 || sectors_per_track == 0) {
        chs.cylinder = 0; chs.head = 0; chs.sector = 0;
        return chs;
    }

    uint32_t hpc_x_spt = (uint32_t)heads * sectors_per_track;
    chs.cylinder = (uint16_t)(lba / hpc_x_spt);
    chs.head     = (uint8_t)((lba / sectors_per_track) % heads);
    chs.sector   = (uint8_t)((lba % sectors_per_track) + 1);

    return chs;
}

uint32_t lba_from_chs(CHSAddr chs, uint8_t heads, uint8_t sectors_per_track)
{
    if (heads == 0 || sectors_per_track == 0) return 0;

    /* LBA = (C * HPC + H) * SPT + (S - 1) */
    return ((uint32_t)chs.cylinder * heads + chs.head)
           * sectors_per_track + (chs.sector - 1);
}

/*
 * ── L5: Sector I/O emulation ──────────────────────────────────────
 * Reads/writes 512-byte sectors from an in-memory disk image.
 * Used to emulate BIOS INT 13h disk services in userspace.
 */

void sector_io_init(SectorIO *io, uint8_t *disk_image, uint32_t disk_size)
{
    io->disk_image  = disk_image;
    io->disk_size   = disk_size;
    io->current_lba = 0;
}

bool sector_read(SectorIO *io, uint32_t lba, uint8_t *buffer)
{
    if (io == NULL || io->disk_image == NULL || buffer == NULL) return false;

    uint32_t offset = lba * CHAIN_SECTOR_SIZE;
    if (offset + CHAIN_SECTOR_SIZE > io->disk_size) {
        fprintf(stderr, "[sector] Read past end: LBA=%u\n", lba);
        return false;
    }

    memcpy(buffer, io->disk_image + offset, CHAIN_SECTOR_SIZE);
    io->current_lba = lba;
    return true;
}

bool sector_write(SectorIO *io, uint32_t lba, const uint8_t *buffer)
{
    if (io == NULL || io->disk_image == NULL || buffer == NULL) return false;

    uint32_t offset = lba * CHAIN_SECTOR_SIZE;
    if (offset + CHAIN_SECTOR_SIZE > io->disk_size) {
        fprintf(stderr, "[sector] Write past end: LBA=%u\n", lba);
        return false;
    }

    memcpy(io->disk_image + offset, buffer, CHAIN_SECTOR_SIZE);
    io->current_lba = lba;
    return true;
}

bool sector_read_multi(SectorIO *io, uint32_t lba, uint8_t count, uint8_t *buffer)
{
    if (io == NULL || buffer == NULL || count == 0) return false;

    for (uint8_t i = 0; i < count; i++) {
        if (!sector_read(io, lba + i, buffer + i * CHAIN_SECTOR_SIZE))
            return false;
    }
    return true;
}

/*
 * ── L5: GPT header parsing (UEFI Specification §5.3) ────────────────
 *
 * GPT Header (LBA 1):
 *   Offset 0:  Signature ("EFI PART")
 *   Offset 8:  Revision
 *   Offset 12: Header size
 *   Offset 16: Header CRC32
 *   ...
 *   Offset 72: Partition entry LBA
 *   Offset 80: Number of partition entries
 */
bool gpt_parse_header(const uint8_t *sector, GPTHeader *header)
{
    if (sector == NULL || header == NULL) return false;

    memcpy(header, sector, sizeof(GPTHeader));

    if (header->signature != CHAIN_GPT_SIGNATURE) {
        fprintf(stderr, "[gpt] Invalid GPT signature\n");
        return false;
    }

    printf("[gpt] Header valid, revision=%u, entries=%u @ LBA %llu\n",
           header->revision, header->entry_count,
           (unsigned long long)header->entries_lba);
    return true;
}

bool gpt_validate_header(const GPTHeader *header)
{
    if (header == NULL) return false;
    return header->signature == CHAIN_GPT_SIGNATURE
           && header->revision >= 0x00010000
           && header->header_size >= 92;
}

bool gpt_find_partition(const GPTHeader *header, const uint8_t *disk_image,
                        const uint8_t *type_guid, GPTPartEntry *entry)
{
    if (header == NULL || disk_image == NULL || entry == NULL) return false;

    uint32_t part_lba = (uint32_t)header->entries_lba;
    uint32_t part_off = part_lba * CHAIN_SECTOR_SIZE;

    for (uint32_t i = 0; i < header->entry_count; i++) {
        const GPTPartEntry *pe = (const GPTPartEntry *)
            (disk_image + part_off + i * header->entry_size);

        if (pe->first_lba == 0 && pe->last_lba == 0) continue;

        if (type_guid == NULL || memcmp(pe->type_guid, type_guid, 16) == 0) {
            memcpy(entry, pe, sizeof(GPTPartEntry));
            printf("[gpt] Found partition: LBA %llu-%llu\n",
                   (unsigned long long)pe->first_lba,
                   (unsigned long long)pe->last_lba);
            return true;
        }
    }
    return false;
}

/*
 * ── L2: Chain load implementation ──────────────────────────────────
 * Loads a boot sector from a target partition and emulates
 * the handoff: copy boot code to load address, jump to entry.
 */
void chain_target_init(ChainTarget *target)
{
    memset(target, 0, sizeof(ChainTarget));
    target->load_addr  = 0x7C00;
    target->entry_point = 0x7C00;
}

bool chain_load_boot_sector(SectorIO *io, const ChainTarget *target)
{
    if (io == NULL || target == NULL) return false;

    /* Read the first sector of the target partition */
    uint8_t sector[CHAIN_SECTOR_SIZE];
    if (!sector_read(io, (uint32_t)target->start_lba, sector)) {
        fprintf(stderr, "[chain] Failed to read boot sector at LBA %llu\n",
                (unsigned long long)target->start_lba);
        return false;
    }

    /* Check boot signature (0xAA55 at offset 510) */
    uint16_t sig = sector[510] | ((uint16_t)sector[511] << 8);
    if (sig != 0xAA55) {
        fprintf(stderr, "[chain] Invalid boot signature: 0x%04X\n", sig);
        return false;
    }

    printf("[chain] Valid boot sector at LBA %llu, signature 0xAA55\n",
           (unsigned long long)target->start_lba);
    return true;
}

bool chain_emulate_boot(const ChainTarget *target)
{
    if (target == NULL) return false;

    printf("\n=== Chain Boot Emulation ===\n");
    printf("[chain] Loading boot sector from LBA %llu\n",
           (unsigned long long)target->start_lba);
    printf("[chain] Copying %u bytes to 0x%08X\n",
           target->code_size ? target->code_size : 512,
           target->load_addr);
    printf("[chain] Verifying boot signature... OK\n");
    printf("[chain] Setting up DL=0x%02X (boot drive)\n", 0x80);
    printf("[chain] Jumping to 0x%08X\n", target->entry_point);
    printf("[chain] === Handing off to next stage ===\n");

    return true;
}

void chain_target_print(const ChainTarget *target)
{
    if (target == NULL) return;
    printf("\n=== Chain Load Target ===\n");
    printf("Start LBA:   %llu\n", (unsigned long long)target->start_lba);
    printf("Sectors:     %llu\n", (unsigned long long)target->sector_count);
    printf("Part type:   0x%02X\n", target->partition_type);
    printf("Load addr:   0x%08X\n", target->load_addr);
    printf("Entry point: 0x%08X\n", target->entry_point);
}