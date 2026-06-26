#ifndef MMIO_H
#define MMIO_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define MMIO_MAX_REGIONS 32

typedef enum {
    MMIO_DEV_UART,
    MMIO_DEV_TIMER,
    MMIO_DEV_GPIO
} MMIODeviceType;

typedef struct MMIORegion MMIORegion;

typedef uint32_t (*mmio_read_fn)(const MMIORegion *region, uint32_t offset);
typedef void     (*mmio_write_fn)(MMIORegion *region, uint32_t offset, uint32_t value);

struct MMIORegion {
    uint32_t       base;
    uint32_t       size;
    char           name[32];
    MMIODeviceType dev_type;
    mmio_read_fn   read_func;
    mmio_write_fn  write_func;
    uint8_t       *backing;
};

typedef struct {
    MMIORegion regions[MMIO_MAX_REGIONS];
    uint32_t   num_regions;
} MMIOManager;

bool     mmio_init(MMIOManager *mgr);
bool     mmio_map(MMIOManager *mgr, uint32_t base, uint32_t size,
                  MMIODeviceType dev_type, const char *name);
uint32_t mmio_read32(MMIOManager *mgr, uint32_t addr);
void     mmio_write32(MMIOManager *mgr, uint32_t addr, uint32_t value);
uint8_t  mmio_read8(MMIOManager *mgr, uint32_t addr);
void     mmio_write8(MMIOManager *mgr, uint32_t addr, uint8_t value);

uint32_t uart_read_callback(const MMIORegion *region, uint32_t offset);
void     uart_write_callback(MMIORegion *region, uint32_t offset, uint32_t value);
uint32_t timer_read_callback(const MMIORegion *region, uint32_t offset);
void     timer_write_callback(MMIORegion *region, uint32_t offset, uint32_t value);
uint32_t gpio_read_callback(const MMIORegion *region, uint32_t offset);
void     gpio_write_callback(MMIORegion *region, uint32_t offset, uint32_t value);

#endif
