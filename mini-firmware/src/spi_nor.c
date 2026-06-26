#include "spi_nor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool spi_init(SPIFlash *flash, uint32_t jedec_id, uint32_t capacity)
{
    if (!flash || capacity == 0) return false;

    flash->jedec_id = jedec_id;
    flash->capacity = capacity;
    flash->page_size = 256;
    flash->status_reg = 0;

    flash->data = (uint8_t *)calloc(capacity, 1);
    if (!flash->data) return false;

    return true;
}

uint32_t spi_read_jedec_id(const SPIFlash *flash)
{
    if (!flash) return 0;
    return flash->jedec_id;
}

bool spi_read(const SPIFlash *flash, uint32_t addr, uint8_t *buf, uint32_t len)
{
    if (!flash || !buf || len == 0) return false;
    if (addr + len > flash->capacity) return false;

    memcpy(buf, &flash->data[addr], len);
    return true;
}

bool spi_write_enable(SPIFlash *flash)
{
    if (!flash) return false;

    flash->status_reg |= SR_BIT_WEL;
    return true;
}

bool spi_sector_erase(SPIFlash *flash, uint32_t addr)
{
    if (!flash) return false;
    if (addr >= flash->capacity) return false;

    uint32_t sector_addr = (addr / flash->page_size) * flash->page_size;
    uint32_t erase_size = flash->page_size;

    if (sector_addr + erase_size > flash->capacity) {
        erase_size = flash->capacity - sector_addr;
    }

    memset(&flash->data[sector_addr], 0xFF, erase_size);
    printf("[SPI NOR] Sector erased at 0x%08X (size: %u)\n", sector_addr, erase_size);

    flash->status_reg &= ~SR_BIT_WEL;
    return true;
}

bool spi_page_program(SPIFlash *flash, uint32_t addr, const uint8_t *buf, uint32_t len)
{
    if (!flash || !buf || len == 0) return false;
    if (addr + len > flash->capacity) return false;

    if (len > flash->page_size) return false;

    if ((flash->status_reg & SR_BIT_WEL) == 0) {
        fprintf(stderr, "[SPI NOR] Write Enable Latch not set!\n");
        return false;
    }

    memcpy(&flash->data[addr], buf, len);
    printf("[SPI NOR] Programmed %u bytes at 0x%08X\n", len, addr);

    flash->status_reg &= ~SR_BIT_WEL;
    return true;
}

uint8_t spi_read_status(const SPIFlash *flash)
{
    if (!flash) return 0;
    return flash->status_reg;
}
