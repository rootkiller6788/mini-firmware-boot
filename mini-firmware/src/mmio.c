#include "mmio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static MMIORegion *find_region(const MMIOManager *mgr, uint32_t addr)
{
    for (uint32_t i = 0; i < mgr->num_regions; i++) {
        if (addr >= mgr->regions[i].base &&
            addr < mgr->regions[i].base + mgr->regions[i].size) {
            return (MMIORegion *)&mgr->regions[i];
        }
    }
    return NULL;
}

bool mmio_init(MMIOManager *mgr)
{
    if (!mgr) return false;
    memset(mgr, 0, sizeof(MMIOManager));
    return true;
}

bool mmio_map(MMIOManager *mgr, uint32_t base, uint32_t size,
              MMIODeviceType dev_type, const char *name)
{
    if (!mgr || !name) return false;
    if (mgr->num_regions >= MMIO_MAX_REGIONS) return false;

    MMIORegion *reg = &mgr->regions[mgr->num_regions];
    reg->base = base;
    reg->size = size;
    reg->dev_type = dev_type;

    size_t name_len = strlen(name);
    if (name_len >= sizeof(reg->name)) name_len = sizeof(reg->name) - 1;
    memcpy(reg->name, name, name_len);
    reg->name[name_len] = '\0';

    reg->backing = (uint8_t *)calloc(size, 1);
    if (!reg->backing) return false;

    switch (dev_type) {
        case MMIO_DEV_UART:
            reg->read_func = uart_read_callback;
            reg->write_func = uart_write_callback;
            break;
        case MMIO_DEV_TIMER:
            reg->read_func = timer_read_callback;
            reg->write_func = timer_write_callback;
            break;
        case MMIO_DEV_GPIO:
            reg->read_func = gpio_read_callback;
            reg->write_func = gpio_write_callback;
            break;
        default:
            reg->read_func = NULL;
            reg->write_func = NULL;
            break;
    }

    mgr->num_regions++;
    return true;
}

uint32_t mmio_read32(MMIOManager *mgr, uint32_t addr)
{
    if (!mgr) return 0;

    MMIORegion *reg = find_region(mgr, addr);
    if (!reg) {
        fprintf(stderr, "MMIO read32: no region at 0x%08X\n", addr);
        return 0;
    }

    uint32_t offset = addr - reg->base;
    if (reg->read_func) {
        return reg->read_func(reg, offset);
    }

    uint32_t value = 0;
    memcpy(&value, &reg->backing[offset], sizeof(uint32_t));
    return value;
}

void mmio_write32(MMIOManager *mgr, uint32_t addr, uint32_t value)
{
    if (!mgr) return;

    MMIORegion *reg = find_region(mgr, addr);
    if (!reg) {
        fprintf(stderr, "MMIO write32: no region at 0x%08X\n", addr);
        return;
    }

    uint32_t offset = addr - reg->base;
    if (reg->write_func) {
        reg->write_func(reg, offset, value);
        return;
    }

    memcpy(&reg->backing[offset], &value, sizeof(uint32_t));
}

uint8_t mmio_read8(MMIOManager *mgr, uint32_t addr)
{
    if (!mgr) return 0;

    MMIORegion *reg = find_region(mgr, addr);
    if (!reg) {
        fprintf(stderr, "MMIO read8: no region at 0x%08X\n", addr);
        return 0;
    }

    uint32_t offset = addr - reg->base;
    return reg->backing[offset];
}

void mmio_write8(MMIOManager *mgr, uint32_t addr, uint8_t value)
{
    if (!mgr) return;

    MMIORegion *reg = find_region(mgr, addr);
    if (!reg) {
        fprintf(stderr, "MMIO write8: no region at 0x%08X\n", addr);
        return;
    }

    uint32_t offset = addr - reg->base;
    reg->backing[offset] = value;
}

uint32_t uart_read_callback(const MMIORegion *region, uint32_t offset)
{
    (void)offset;
    uint32_t value = 0;
    memcpy(&value, &region->backing[offset], sizeof(uint32_t));
    return value;
}

void uart_write_callback(MMIORegion *region, uint32_t offset, uint32_t value)
{
    memcpy(&region->backing[offset], &value, sizeof(uint32_t));
    if (offset == 0) {
        printf("[UART TX] 0x%08X ('%c')\n", value, (char)(value & 0xFF));
    }
}

uint32_t timer_read_callback(const MMIORegion *region, uint32_t offset)
{
    uint32_t value = 0;
    memcpy(&value, &region->backing[offset], sizeof(uint32_t));
    return value;
}

void timer_write_callback(MMIORegion *region, uint32_t offset, uint32_t value)
{
    memcpy(&region->backing[offset], &value, sizeof(uint32_t));
    if (offset == 0) {
        printf("[TIMER] Counter set to %u\n", value);
    }
    if (offset == 4) {
        printf("[TIMER] Period set to %u ticks\n", value);
    }
}

uint32_t gpio_read_callback(const MMIORegion *region, uint32_t offset)
{
    uint32_t value = 0;
    memcpy(&value, &region->backing[offset], sizeof(uint32_t));
    return value;
}

void gpio_write_callback(MMIORegion *region, uint32_t offset, uint32_t value)
{
    memcpy(&region->backing[offset], &value, sizeof(uint32_t));
    if (offset == 0) {
        printf("[GPIO] Output set: 0x%08X\n", value);
    }
    if (offset == 4) {
        printf("[GPIO] Direction set: 0x%08X\n", value);
    }
}
