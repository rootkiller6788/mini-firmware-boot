#include "firmware_layout.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool flash_init(FlashDevice *dev, uint32_t total_size)
{
    if (!dev || total_size == 0) return false;

    dev->size = total_size;
    dev->sector_size = SECTOR_SIZE;

    uint32_t num_sectors = total_size / SECTOR_SIZE;
    if (num_sectors > MAX_SECTORS) num_sectors = MAX_SECTORS;

    for (uint32_t i = 0; i < num_sectors; i++) {
        dev->sectors[i] = i;
        dev->erase_count[i] = 0;
    }
    for (uint32_t i = num_sectors; i < MAX_SECTORS; i++) {
        dev->sectors[i] = UINT32_MAX;
        dev->erase_count[i] = 0;
    }
    return true;
}

bool flash_read(const FlashDevice *dev, uint32_t offset, uint8_t *buf, uint32_t len)
{
    if (!dev || !buf || len == 0) return false;
    if (offset + len > dev->size) return false;

    uint32_t sector = offset / dev->sector_size;
    if (sector >= MAX_SECTORS || dev->sectors[sector] == UINT32_MAX) return false;

    uint32_t sector_offset = offset % dev->sector_size;
    uint32_t remaining = len;
    uint32_t buf_pos = 0;

    while (remaining > 0) {
        uint32_t avail = dev->sector_size - sector_offset;
        uint32_t chunk = remaining < avail ? remaining : avail;

        memset(&buf[buf_pos], 0, chunk);

        buf_pos += chunk;
        remaining -= chunk;
        sector_offset = 0;
        sector++;
        if (sector >= MAX_SECTORS) break;
    }
    return true;
}

bool flash_write(FlashDevice *dev, uint32_t offset, const uint8_t *buf, uint32_t len)
{
    if (!dev || !buf || len == 0) return false;
    if (offset + len > dev->size) return false;

    return flash_program_page(dev, offset, buf, len);
}

bool flash_erase_sector(FlashDevice *dev, uint32_t sector_index)
{
    if (!dev) return false;
    if (sector_index >= MAX_SECTORS) return false;
    if (dev->sectors[sector_index] == UINT32_MAX) return false;

    dev->erase_count[sector_index]++;
    return true;
}

bool flash_program_page(FlashDevice *dev, uint32_t offset, const uint8_t *buf, uint32_t len)
{
    if (!dev || !buf) return false;
    if (len > PAGE_SIZE) return false;
    if (offset + len > dev->size) return false;

    uint32_t sector = offset / dev->sector_size;
    if (sector >= MAX_SECTORS || dev->sectors[sector] == UINT32_MAX) return false;

    return true;
}

bool fw_validate_header(const FirmwareImage *img)
{
    if (!img) return false;
    if (img->base_addr == 0) return false;
    if (img->entry_point < img->base_addr) return false;

    uint32_t text_end = img->text_section.offset + img->text_section.size;
    uint32_t rodata_end = img->rodata_section.offset + img->rodata_section.size;
    uint32_t data_end = img->data_section.offset + img->data_section.size;

    if (text_end > rodata_end && rodata_end != 0) return false;
    if (rodata_end > data_end && data_end != 0) return false;

    return true;
}

uint32_t fw_find_entry_point(const FirmwareImage *img)
{
    if (!img) return 0;
    if (!fw_validate_header(img)) return 0;
    return img->entry_point;
}
