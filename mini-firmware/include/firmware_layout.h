#ifndef FIRMWARE_LAYOUT_H
#define FIRMWARE_LAYOUT_H

/*
 * firmware_layout.h — Flash Layout & Firmware Image Management
 *
 * References:
 *   - Intel Flash Descriptor (SPI Flash Layout)
 *   - UEFI PI Firmware Volume (FV) Specification
 *   - JEDEC JESD216 (SFDP)
 *
 * Knowledge coverage:
 *   L1: FlashDevice, FirmwareImage, FlashDescriptor struct/enum
 *   L2: Flash wear leveling, firmware volume concept
 *   L3: Flash descriptor parsing with CRC
 *   L4: CRC32 polynomial math (IEEE 802.3)
 *   L5: CRC32 computation, wear-leveling algorithm
 *   L7: Multi-region flash partitioning for production firmware
 */

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define SECTOR_SIZE       4096
#define PAGE_SIZE         256
#define MAX_SECTORS       128
#define FLASH_DESC_SIZE   4096
#define MAX_FLASH_REGIONS 16
#define FW_MAGIC          0x5F46574D  /* "_FWM" */
#define CRC32_POLYNOMIAL  0xEDB88320

/* ─── L1: Flash Region Types (Intel Flash Descriptor) ────────── */

typedef enum {
    FLASH_REGION_DESCRIPTOR = 0,   /* Flash Descriptor itself       */
    FLASH_REGION_BIOS       = 1,   /* UEFI/BIOS firmware            */
    FLASH_REGION_ME         = 2,   /* Intel Management Engine       */
    FLASH_REGION_GBE        = 3,   /* Gigabit Ethernet firmware     */
    FLASH_REGION_PLATFORM   = 4,   /* Platform data                 */
    FLASH_REGION_EC         = 5,   /* Embedded Controller firmware  */
    FLASH_REGION_PD         = 6,   /* Power Delivery controller     */
    FLASH_REGION_THUNDERBOLT = 7,  /* Thunderbolt firmware          */
    FLASH_REGION_ISH        = 8    /* Integrated Sensor Hub         */
} FlashRegionType;

/* ─── L1: Flash Descriptor (Intel-style) ─────────────────────── */

typedef struct {
    uint32_t offset;              /* Region base offset in flash        */
    uint32_t size;                /* Region size                       */
    FlashRegionType type;         /* What type of content              */
    uint8_t  read_permissions;    /* Master read mask                  */
    uint8_t  write_permissions;   /* Master write mask                 */
    bool     is_locked;           /* Hardware-locked?                  */
    uint32_t crc32;               /* CRC32 of region content           */
    bool     crc32_valid;         /* CRC verified?                    */
} FlashRegion;

typedef struct {
    uint32_t magic;               /* Flash descriptor signature        */
    uint16_t descriptor_version;
    uint16_t num_regions;
    uint32_t flash_size;
    uint32_t sector_size;
    FlashRegion regions[MAX_FLASH_REGIONS];
    uint8_t  master_access[3];    /* CPU, ME, GbE access permissions   */
    bool     descriptor_valid;
} FlashDescriptor;

/* ─── L1: Flash Section ──────────────────────────────────────── */

typedef struct {
    uint32_t offset;
    uint32_t size;
} FirmwareSection;

/* ─── L1: Flash Device ──────────────────────────────────────── */

typedef struct {
    uint32_t size;
    uint32_t sector_size;
    uint32_t sectors[MAX_SECTORS];
    uint32_t erase_count[MAX_SECTORS];
    uint32_t write_count[MAX_SECTORS];   /* L2: For wear leveling */
    uint32_t total_erase_count;          /* Lifetime erase count  */
    uint32_t max_erase_cycles;           /* Endurance limit       */
} FlashDevice;

/* ─── L1: Firmware Image ────────────────────────────────────── */

typedef struct {
    uint32_t base_addr;
    uint32_t entry_point;
    FirmwareSection text_section;
    FirmwareSection rodata_section;
    FirmwareSection data_section;
    FirmwareSection bss_section;
    uint32_t fw_magic;                  /*FW_MAGIC for validation*/
    uint32_t crc32;                     /* CRC32 of entire header*/
    uint32_t image_size;                /* Total FW image size   */
    uint32_t min_hardware_version;      /* Min HW rev required   */
} FirmwareImage;

/* ─── L1: Flash Descriptor API ──────────────────────────────── */

bool flash_desc_init(FlashDescriptor *desc, uint32_t flash_size);
bool flash_desc_add_region(FlashDescriptor *desc, FlashRegionType type,
                           uint32_t offset, uint32_t size);
const FlashRegion *flash_desc_find_region(const FlashDescriptor *desc,
                                          FlashRegionType type);
bool flash_desc_validate(const FlashDescriptor *desc);

/* ─── L1: Flash Device API (expanded) ────────────────────────── */

bool flash_init(FlashDevice *dev, uint32_t total_size);
bool flash_read(const FlashDevice *dev, uint32_t offset,
                uint8_t *buf, uint32_t len);
bool flash_write(FlashDevice *dev, uint32_t offset,
                 const uint8_t *buf, uint32_t len);
bool flash_erase_sector(FlashDevice *dev, uint32_t sector_index);
bool flash_program_page(FlashDevice *dev, uint32_t offset,
                        const uint8_t *buf, uint32_t len);

/* ─── L1: Wear Leveling ──────────────────────────────────────── */

/*
 * Wear leveling distributes erase cycles across sectors to avoid
 * premature failure of hot sectors. Flash cells have finite endurance
 * (typically 10K-100K P/E cycles for NOR flash).
 */
uint32_t flash_find_least_worn_sector(const FlashDevice *dev);
bool flash_should_relocate(const FlashDevice *dev, uint32_t sector);

/* ─── L1: Firmware Image API ─────────────────────────────────── */

bool fw_validate_header(const FirmwareImage *img);
uint32_t fw_find_entry_point(const FirmwareImage *img);
bool fw_verify_crc32(const FirmwareImage *img);

/* ─── L4/L5: CRC32 (IEEE 802.3 Ethernet polynomial) ──────────── */

/*
 * CRC32 polynomial: x^32 + x^26 + x^23 + x^22 + x^16 + x^12 +
 *                   x^11 + x^10 + x^8 + x^7 + x^5 + x^4 + x^2 + x + 1
 *
 * Reversed form (most common): 0xEDB88320
 *
 * Theorem (CRC Error Detection):
 *   - Detects all single-bit errors
 *   - Detects all double-bit errors
 *   - Detects all odd number of bit errors
 *   - Detects all burst errors of length ≤ 32
 *   - Detects 99.99999995% of longer burst errors
 *
 * Complexity: O(n) time, O(1) space
 * Reference: Koopman, "32-Bit Cyclic Redundancy Codes", IEEE, 2002
 */
uint32_t crc32_compute(const uint8_t *data, uint32_t len);
bool     crc32_verify(const uint8_t *data, uint32_t len, uint32_t expected);

/* ─── L1: Firmware Volume Header ─────────────────────────────── */

/*
 * UEFI PI Firmware Volume header (simplified).
 * Reference: PI Spec Vol.3 §2.2
 */
typedef struct {
    uint8_t  zero_vector[16];      /* Must be zero                    */
    uint8_t  file_system_guid[16]; /* Identifies the file system      */
    uint32_t fv_length;            /* Total length of this FV         */
    uint32_t signature;            /* "_FVH" signature                */
    uint32_t attributes;           /* Read/Write/Erase polarity       */
    uint16_t header_length;        /* Size of this header             */
    uint16_t checksum;             /* 16-bit sum of first 50 bytes=0  */
    uint8_t  reserved[20];
} FirmwareVolumeHeader;

bool fv_validate_header(const FirmwareVolumeHeader *hdr);

/* ─── L7: Diagnostics ────────────────────────────────────────── */

void flash_print_layout(const FlashDevice *dev);
void flash_desc_print(const FlashDescriptor *desc);
void fw_print_image_info(const FirmwareImage *img);

#endif /* FIRMWARE_LAYOUT_H */
