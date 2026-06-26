#ifndef FIRMWARE_LAYOUT_H
#define FIRMWARE_LAYOUT_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define SECTOR_SIZE 4096
#define PAGE_SIZE   256
#define MAX_SECTORS 128

typedef struct {
    uint32_t offset;
    uint32_t size;
} FirmwareSection;

typedef struct {
    uint32_t size;
    uint32_t sector_size;
    uint32_t sectors[MAX_SECTORS];
    uint32_t erase_count[MAX_SECTORS];
} FlashDevice;

typedef struct {
    uint32_t base_addr;
    uint32_t entry_point;
    FirmwareSection text_section;
    FirmwareSection rodata_section;
    FirmwareSection data_section;
    FirmwareSection bss_section;
} FirmwareImage;

bool flash_init(FlashDevice *dev, uint32_t total_size);
bool flash_read(const FlashDevice *dev, uint32_t offset, uint8_t *buf, uint32_t len);
bool flash_write(FlashDevice *dev, uint32_t offset, const uint8_t *buf, uint32_t len);
bool flash_erase_sector(FlashDevice *dev, uint32_t sector_index);
bool flash_program_page(FlashDevice *dev, uint32_t offset, const uint8_t *buf, uint32_t len);
bool fw_validate_header(const FirmwareImage *img);
uint32_t fw_find_entry_point(const FirmwareImage *img);

#endif
