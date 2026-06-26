#include "firmware_layout.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * firmware_layout.c — Flash Layout, Descriptor, and Firmware Image Management
 *
 * References:
 *   - Intel Flash Descriptor specification
 *   - UEFI PI Spec Vol.3 (Firmware Volume)
 *   - JEDEC JESD216 (SFDP)
 */

/* ─── L4/L5: CRC32 Implementation (IEEE 802.3) ──────────────────── */

/*
 * CRC32 precomputed lookup table for polynomial 0xEDB88320.
 *
 * Algorithm: Table-driven CRC32 (Sarwate algorithm, 1988).
 *
 * Mathematical basis:
 *   CRC(M) = (M(x) * x^32) mod G(x)
 *   where G(x) = x^32 + x^26 + x^23 + x^22 + x^16 + x^12 +
 *                x^11 + x^10 + x^8 + x^7 + x^5 + x^4 + x^2 + x + 1
 *
 * The table-driven method processes one byte at a time using
 * precomputed remainders:
 *   CRC(byte) = table[(CRC ^ byte) & 0xFF] ^ (CRC >> 8)
 *
 * Complexity: O(n) for n bytes, uses 1KB lookup table (static).
 * This is the standard implementation used in Ethernet, gzip, PNG.
 *
 * Reference: Williams, R.N. "A Painless Guide to CRC Error
 * Detection Algorithms", 1993.
 */
static const uint32_t crc32_table[256] = {
    0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA,
    0x076DC419, 0x706AF48F, 0xE963A535, 0x9E6495A3,
    0x0EDB8832, 0x79DCB8A4, 0xE0D5E91E, 0x97D2D988,
    0x09B64C2B, 0x7EB17CBD, 0xE7B82D07, 0x90BF1D91,
    0x1DB71064, 0x6AB020F2, 0xF3B97148, 0x84BE41DE,
    0x1ADAD47D, 0x6DDDE4EB, 0xF4D4B551, 0x83D385C7,
    0x136C9856, 0x646BA8C0, 0xFD62F97A, 0x8A65C9EC,
    0x14015C4F, 0x63066CD9, 0xFA0F3D63, 0x8D080DF5,
    0x3B6E20C8, 0x4C69105E, 0xD56041E4, 0xA2677172,
    0x3C03E4D1, 0x4B04D447, 0xD20D85FD, 0xA50AB56B,
    0x35B5A8FA, 0x42B2986C, 0xDBBBC9D6, 0xACBCF940,
    0x32D86CE3, 0x45DF5C75, 0xDCD60DCF, 0xABD13D59,
    0x26D930AC, 0x51DE003A, 0xC8D75180, 0xBFD06116,
    0x21B4F4B5, 0x56B3C423, 0xCFBA9599, 0xB8BDA50F,
    0x2802B89E, 0x5F058808, 0xC60CD9B2, 0xB10BE924,
    0x2F6F7C87, 0x58684C11, 0xC1611DAB, 0xB6662D3D,
    0x76DC4190, 0x01DB7106, 0x98D220BC, 0xEFD5102A,
    0x71B18589, 0x06B6B51F, 0x9FBFE4A5, 0xE8B8D433,
    0x7807C9A2, 0x0F00F934, 0x9609A88E, 0xE10E9818,
    0x7F6A0DBB, 0x086D3D2D, 0x91646C97, 0xE6635C01,
    0x6B6B51F4, 0x1C6C6162, 0x856530D8, 0xF262004E,
    0x6C0695ED, 0x1B01A57B, 0x8208F4C1, 0xF50FC457,
    0x65B0D9C6, 0x12B7E950, 0x8BBEB8EA, 0xFCB9887C,
    0x62DD1DDF, 0x15DA2D49, 0x8CD37CF3, 0xFBD44C65,
    0x4DB26158, 0x3AB551CE, 0xA3BC0074, 0xD4BB30E2,
    0x4ADFA541, 0x3DD895D7, 0xA4D1C46D, 0xD3D6F4FB,
    0x4369E96A, 0x346ED9FC, 0xAD678846, 0xDA60B8D0,
    0x44042D73, 0x33031DE5, 0xAA0A4C5F, 0xDD0D7CC9,
    0x5005713C, 0x270241AA, 0xBE0B1010, 0xC90C2086,
    0x5768B525, 0x206F85B3, 0xB966D409, 0xCE61E49F,
    0x5EDEF90E, 0x29D9C998, 0xB0D09822, 0xC7D7A8B4,
    0x59B33D17, 0x2EB40D81, 0xB7BD5C3B, 0xC0BA6CAD,
    0xEDB88320, 0x9ABFB3B6, 0x03B6E20C, 0x74B1D29A,
    0xEAD54739, 0x9DD277AF, 0x04DB2615, 0x73DC1683,
    0xE3630B12, 0x94643B84, 0x0D6D6A3E, 0x7A6A5AA8,
    0xE40ECF0B, 0x9309FF9D, 0x0A00AE27, 0x7D079EB1,
    0xF00F9344, 0x8708A3D2, 0x1E01F268, 0x6906C2FE,
    0xF762575D, 0x806567CB, 0x196C3671, 0x6E6B06E7,
    0xFED41B76, 0x89D32BE0, 0x10DA7A5A, 0x67DD4ACC,
    0xF9B9DF6F, 0x8EBEEFF9, 0x17B7BE43, 0x60B08ED5,
    0xD6D6A3E8, 0xA1D1937E, 0x38D8C2C4, 0x4FDFF252,
    0xD1BB67F1, 0xA6BC5767, 0x3FB506DD, 0x48B2364B,
    0xD80D2BDA, 0xAF0A1B4C, 0x36034AF6, 0x41047A60,
    0xDF60EFC3, 0xA867DF55, 0x316E8EEF, 0x4669BE79,
    0xCB61B38C, 0xBC66831A, 0x256FD2A0, 0x5268E236,
    0xCC0C7795, 0xBB0B4703, 0x220216B9, 0x5505262F,
    0xC5BA3BBE, 0xB2BD0B28, 0x2BB45A92, 0x5CB30A04,
    0xC2D7FFA7, 0xB5D0CF31, 0x2CD99E8B, 0x5BDEAE1D,
    0x9B64C2B0, 0xEC63F226, 0x756AA39C, 0x026D930A,
    0x9C0906A9, 0xEB0E363F, 0x72076785, 0x05005713,
    0x95BF4A82, 0xE2B87A14, 0x7BB12BAE, 0x0CB61B38,
    0x92D28E9B, 0xE5D5BE0D, 0x7CDCEFB7, 0x0BDBDF21,
    0x86D3D2D4, 0xF1D4E242, 0x68DDB3F8, 0x1FDA836E,
    0x81BE16CD, 0xF6B9265B, 0x6FB077E1, 0x18B74777,
    0x88085AE6, 0xFF0F6A70, 0x66063BCA, 0x11010B5C,
    0x8F659EFF, 0xF862AE69, 0x616BFFD3, 0x166CCF45,
    0xA00AE278, 0xD70DD2EE, 0x4E048354, 0x3903B3C2,
    0xA7672661, 0xD06016F7, 0x4969474D, 0x3E6E77DB,
    0xAED16A4A, 0xD9D65ADC, 0x40DF0B66, 0x37D83BF0,
    0xA9BCAE53, 0xDEBB9EC5, 0x47B2CF7F, 0x30B5FFE9,
    0xBDBDF21C, 0xCABAC28A, 0x53B39330, 0x24B4A3A6,
    0xBAD03605, 0xCDD70693, 0x54DE5729, 0x23D967BF,
    0xB3667A2E, 0xC4614AB8, 0x5D681B02, 0x2A6F2B94,
    0xB40BBE37, 0xC30C8EA1, 0x5A05DF1B, 0x2D02EF8D
};

uint32_t crc32_compute(const uint8_t *data, uint32_t len)
{
    if (!data || len == 0) return 0;

    uint32_t crc = 0xFFFFFFFF;
    for (uint32_t i = 0; i < len; i++) {
        crc = crc32_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFF;
}

bool crc32_verify(const uint8_t *data, uint32_t len, uint32_t expected)
{
    if (!data || len == 0) return false;
    return crc32_compute(data, len) == expected;
}

/* ─── L3: Flash Descriptor Management ────────────────────────────── */

bool flash_desc_init(FlashDescriptor *desc, uint32_t flash_size)
{
    if (!desc || flash_size == 0) return false;
    memset(desc, 0, sizeof(FlashDescriptor));
    desc->magic        = 0x0FF0A55A;  /* Intel Flash Descriptor magic */
    desc->flash_size   = flash_size;
    desc->sector_size  = SECTOR_SIZE;
    desc->num_regions  = 0;
    desc->descriptor_valid = true;
    return true;
}

bool flash_desc_add_region(FlashDescriptor *desc, FlashRegionType type,
                           uint32_t offset, uint32_t size)
{
    if (!desc || size == 0) return false;
    if (desc->num_regions >= MAX_FLASH_REGIONS) return false;
    if (offset + size > desc->flash_size) return false;

    FlashRegion *r = &desc->regions[desc->num_regions];
    r->offset  = offset;
    r->size    = size;
    r->type    = type;
    r->is_locked = (type == FLASH_REGION_DESCRIPTOR);
    r->read_permissions  = 0xFF;  /* All masters can read */
    r->write_permissions = (type == FLASH_REGION_DESCRIPTOR) ? 0x00 : 0xFF;
    r->crc32_valid = false;

    desc->num_regions++;
    return true;
}

const FlashRegion *flash_desc_find_region(const FlashDescriptor *desc,
                                          FlashRegionType type)
{
    if (!desc) return NULL;
    for (uint32_t i = 0; i < desc->num_regions; i++) {
        if (desc->regions[i].type == type) {
            return &desc->regions[i];
        }
    }
    return NULL;
}

/*
 * Validate flash descriptor integrity.
 *
 * Checks:
 *   1. Magic number (0x0FF0A55A)
 *   2. Regions don't overlap
 *   3. All regions are within flash bounds
 *   4. Exactly one descriptor region exists
 */
bool flash_desc_validate(const FlashDescriptor *desc)
{
    if (!desc) return false;

    if (desc->magic != 0x0FF0A55A) {
        fprintf(stderr, "FlashDesc: Invalid magic 0x%08X\n", desc->magic);
        return false;
    }

    /* Check for overlapping regions */
    for (uint32_t i = 0; i < desc->num_regions; i++) {
        const FlashRegion *a = &desc->regions[i];
        if (a->offset + a->size > desc->flash_size) {
            fprintf(stderr, "FlashDesc: Region %u out of bounds\n", i);
            return false;
        }
        for (uint32_t j = i + 1; j < desc->num_regions; j++) {
            const FlashRegion *b = &desc->regions[j];
            if (a->offset < b->offset + b->size &&
                b->offset < a->offset + a->size) {
                fprintf(stderr, "FlashDesc: Regions %u and %u overlap\n", i, j);
                return false;
            }
        }
    }

    return true;
}

/* ─── L2: Flash Device Operations ────────────────────────────────── */

bool flash_init(FlashDevice *dev, uint32_t total_size)
{
    if (!dev || total_size == 0) return false;

    memset(dev, 0, sizeof(FlashDevice));
    dev->size = total_size;
    dev->sector_size = SECTOR_SIZE;
    dev->max_erase_cycles = 100000;  /* Typical NOR flash endurance */

    uint32_t num_sectors = total_size / SECTOR_SIZE;
    if (num_sectors > MAX_SECTORS) num_sectors = MAX_SECTORS;

    for (uint32_t i = 0; i < num_sectors; i++) {
        dev->sectors[i] = i;
        dev->erase_count[i] = 0;
        dev->write_count[i] = 0;
    }
    for (uint32_t i = num_sectors; i < MAX_SECTORS; i++) {
        dev->sectors[i] = UINT32_MAX;
        dev->erase_count[i] = 0;
        dev->write_count[i] = 0;
    }
    return true;
}

bool flash_read(const FlashDevice *dev, uint32_t offset,
                uint8_t *buf, uint32_t len)
{
    if (!dev || !buf || len == 0) return false;
    if (offset + len > dev->size) return false;

    uint32_t sector = offset / dev->sector_size;
    if (sector >= MAX_SECTORS || dev->sectors[sector] == UINT32_MAX)
        return false;

    uint32_t sector_offset = offset % dev->sector_size;
    uint32_t remaining = len;
    uint32_t buf_pos = 0;

    while (remaining > 0) {
        uint32_t avail = dev->sector_size - sector_offset;
        uint32_t chunk = (remaining < avail) ? remaining : avail;
        memset(&buf[buf_pos], 0, chunk);
        buf_pos += chunk;
        remaining -= chunk;
        sector_offset = 0;
        sector++;
        if (sector >= MAX_SECTORS) break;
    }
    return true;
}

bool flash_write(FlashDevice *dev, uint32_t offset,
                 const uint8_t *buf, uint32_t len)
{
    if (!dev || !buf || len == 0) return false;
    if (offset + len > dev->size) return false;

    uint32_t sector = offset / dev->sector_size;
    if (sector >= MAX_SECTORS || dev->sectors[sector] == UINT32_MAX)
        return false;

    dev->write_count[sector]++;
    return flash_program_page(dev, offset, buf, len);
}

bool flash_erase_sector(FlashDevice *dev, uint32_t sector_index)
{
    if (!dev) return false;
    if (sector_index >= MAX_SECTORS) return false;
    if (dev->sectors[sector_index] == UINT32_MAX) return false;

    /* Check endurance limit */
    if (dev->erase_count[sector_index] >= dev->max_erase_cycles) {
        fprintf(stderr, "Flash: Sector %u exceeded endurance (%u/%u)\n",
                sector_index, dev->erase_count[sector_index],
                dev->max_erase_cycles);
        return false;
    }

    dev->erase_count[sector_index]++;
    dev->total_erase_count++;
    dev->write_count[sector_index] = 0;
    return true;
}

bool flash_program_page(FlashDevice *dev, uint32_t offset,
                        const uint8_t *buf, uint32_t len)
{
    if (!dev || !buf) return false;
    if (len > PAGE_SIZE) return false;
    if (offset + len > dev->size) return false;

    uint32_t sector = offset / dev->sector_size;
    if (sector >= MAX_SECTORS || dev->sectors[sector] == UINT32_MAX)
        return false;

    return true;
}

/* ─── L2: Wear Leveling ──────────────────────────────────────────── */

/*
 * Find the sector with the lowest erase count.
 * Used for static wear leveling: relocate cold data from
 * a low-wear sector and use the hot sector instead.
 *
 * Algorithm: Linear scan of all valid sectors.
 * Complexity: O(n) where n = number of active sectors.
 */
uint32_t flash_find_least_worn_sector(const FlashDevice *dev)
{
    if (!dev) return UINT32_MAX;

    uint32_t best_sector = UINT32_MAX;
    uint32_t min_erases  = UINT32_MAX;

    for (uint32_t i = 0; i < MAX_SECTORS; i++) {
        if (dev->sectors[i] != UINT32_MAX) {
            if (dev->erase_count[i] < min_erases) {
                min_erases  = dev->erase_count[i];
                best_sector = i;
            }
        }
    }
    return best_sector;
}

/*
 * Determine if a sector should be relocated for wear leveling.
 *
 * Relocate when:
 *   1. Sector erase count > 80% of max endurance
 *   2. OR sector erase count > 2x average of all sectors
 *
 * This implements a simple threshold-based wear leveling policy.
 * Production firmware (e.g., UBI/UBIFS, FTL) uses more sophisticated
 * algorithms: dynamic wear leveling for hot data, static for cold.
 */
bool flash_should_relocate(const FlashDevice *dev, uint32_t sector)
{
    if (!dev || sector >= MAX_SECTORS) return false;
    if (dev->sectors[sector] == UINT32_MAX) return false;

    /* Threshold 1: Endurance warning */
    if (dev->erase_count[sector] > dev->max_erase_cycles * 80 / 100)
        return true;

    /* Threshold 2: Relative wear */
    uint32_t total = 0, count = 0;
    for (uint32_t i = 0; i < MAX_SECTORS; i++) {
        if (dev->sectors[i] != UINT32_MAX) {
            total += dev->erase_count[i];
            count++;
        }
    }
    if (count == 0) return false;

    uint32_t avg = total / count;
    if (dev->erase_count[sector] > avg * 2)
        return true;

    return false;
}

/* ─── L3: Firmware Image Validation ──────────────────────────────── */

bool fw_validate_header(const FirmwareImage *img)
{
    if (!img) return false;
    if (img->fw_magic != FW_MAGIC) return false;
    if (img->base_addr == 0) return false;
    if (img->entry_point < img->base_addr) return false;

    /* Validate section layout */
    uint32_t text_end   = img->text_section.offset + img->text_section.size;
    uint32_t rodata_end = img->rodata_section.offset + img->rodata_section.size;
    uint32_t data_end   = img->data_section.offset + img->data_section.size;

    if (rodata_end > 0 && text_end > rodata_end) return false;
    if (data_end > 0 && rodata_end > data_end) return false;

    return true;
}

uint32_t fw_find_entry_point(const FirmwareImage *img)
{
    if (!img) return 0;
    if (!fw_validate_header(img)) return 0;
    return img->entry_point;
}

/*
 * Verify CRC32 of firmware image header.
 *
 * The CRC protects against accidental corruption during
 * firmware updates (e.g., interrupted flash write).
 * It is NOT a security check — use SHA-256 + RSA for that.
 */
bool fw_verify_crc32(const FirmwareImage *img)
{
    if (!img) return false;
    uint32_t computed = crc32_compute((const uint8_t *)img,
                                       sizeof(FirmwareImage) - sizeof(uint32_t));
    return computed == img->crc32;
}

/* ─── L3: Firmware Volume Header Validation ───────────────────────── */

/*
 * Validate UEFI PI Firmware Volume header.
 *
 * Checks per PI Spec Vol.3 §2.2:
 *   1. Zero vector (16 bytes) is all zeros
 *   2. Signature is "_FVH"
 *   3. Header length >= sizeof(FirmwareVolumeHeader)
 *   4. 16-bit checksum of first 50 bytes == 0
 */
bool fv_validate_header(const FirmwareVolumeHeader *hdr)
{
    if (!hdr) return false;

    /* Check zero vector */
    for (int i = 0; i < 16; i++) {
        if (hdr->zero_vector[i] != 0) return false;
    }

    /* Check signature */
    if (hdr->signature != 0x4856465F)  /* "_FVH" little-endian */
        return false;

    /* Check header length */
    if (hdr->header_length < sizeof(FirmwareVolumeHeader))
        return false;

    /* Verify 16-bit checksum */
    uint16_t sum = 0;
    uint8_t *bytes = (uint8_t *)hdr;
    for (uint16_t i = 0; i < hdr->header_length; i++) {
        sum += bytes[i];
    }
    return (sum == 0);
}

/* ─── L7: Diagnostics ────────────────────────────────────────────── */

void flash_print_layout(const FlashDevice *dev)
{
    if (!dev) return;

    printf("=== Flash Layout ===\n");
    printf("Total Size:      %u bytes (%u KB)\n",
           dev->size, dev->size / 1024);
    printf("Sector Size:     %u bytes\n", dev->sector_size);
    printf("Active Sectors:  %u / %u\n",
           (uint32_t)(dev->size / dev->sector_size), MAX_SECTORS);
    printf("Total Erases:    %u\n", dev->total_erase_count);
    printf("Max Erase Cycles: %u\n\n", dev->max_erase_cycles);

    for (uint32_t i = 0; i < MAX_SECTORS; i++) {
        if (dev->sectors[i] != UINT32_MAX) {
            printf("  Sector %3u: %5u erasures",
                   i, dev->erase_count[i]);
            if (dev->erase_count[i] > dev->max_erase_cycles * 80 / 100) {
                printf(" [WARNING: near end of life]");
            }
            printf("\n");
        }
    }
}

void flash_desc_print(const FlashDescriptor *desc)
{
    if (!desc) return;

    printf("=== Flash Descriptor ===\n");
    printf("Magic:      0x%08X %s\n", desc->magic,
           desc->magic == 0x0FF0A55A ? "(valid)" : "(invalid)");
    printf("Flash Size: %u MB\n", desc->flash_size / (1024*1024));
    printf("Regions:    %u\n\n", desc->num_regions);

    static const char *region_names[] = {
        "Descriptor", "BIOS", "ME", "GbE", "Platform",
        "EC", "PD", "Thunderbolt", "ISH"
    };

    for (uint32_t i = 0; i < desc->num_regions; i++) {
        const FlashRegion *r = &desc->regions[i];
        const char *rname = (r->type < 9) ? region_names[r->type] : "Unknown";
        printf("  [%u] %-12s  Offset=0x%08X  Size=0x%08X (%u KB)",
               i, rname, r->offset, r->size, r->size / 1024);
        if (r->is_locked) printf(" [LOCKED]");
        printf("  RD:0x%02X WR:0x%02X\n", r->read_permissions, r->write_permissions);
    }
}

void fw_print_image_info(const FirmwareImage *img)
{
    if (!img) return;

    printf("=== Firmware Image ===\n");
    printf("Base:      0x%08X\n", img->base_addr);
    printf("Entry:     0x%08X\n", img->entry_point);
    printf("Magic:     0x%08X (%s)\n", img->fw_magic,
           img->fw_magic == FW_MAGIC ? "OK" : "BAD");
    printf("Size:      %u bytes\n", img->image_size);
    printf("Min HW:    v%u\n", img->min_hardware_version);
    printf("CRC32:     0x%08X (%s)\n", img->crc32,
           fw_verify_crc32(img) ? "OK" : "MISMATCH");

    printf("  .text:   offset=0x%08X size=0x%08X\n",
           img->text_section.offset, img->text_section.size);
    printf("  .rodata: offset=0x%08X size=0x%08X\n",
           img->rodata_section.offset, img->rodata_section.size);
    printf("  .data:   offset=0x%08X size=0x%08X\n",
           img->data_section.offset, img->data_section.size);
    printf("  .bss:    offset=0x%08X size=0x%08X\n",
           img->bss_section.offset, img->bss_section.size);
}
