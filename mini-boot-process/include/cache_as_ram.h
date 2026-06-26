#ifndef CACHE_AS_RAM_H
#define CACHE_AS_RAM_H

#include <stdbool.h>
#include <stdint.h>

#define CAR_BASE       0xFEC00000
#define CAR_SIZE       (256 * 1024)
#define CAR_LINE_SIZE  64
#define CAR_TAG_SHIFT  6
#define CAR_NUM_LINES  (CAR_SIZE / CAR_LINE_SIZE)
#define CAR_MODE_NORMAL    0
#define CAR_MODE_NO_EVICT  1

#define CAR_STACK_TOP  (CAR_BASE + CAR_SIZE - 16)

typedef enum {
    CAR_MODE_DISABLED  = 0,
    CAR_MODE_NOFILL    = 1,
    CAR_MODE_EVICT     = 2,
    CAR_MODE_NO_EVICT_MODE = 3
} CARCacheMode;

typedef struct {
    bool        enabled;
    uint64_t    base_addr;
    uint64_t    size;
    CARCacheMode mode;
    uint32_t    line_count;
    uint32_t    hit_count;
    uint32_t    miss_count;
    bool        teardown_complete;
} CARState;

void car_init(CARState *car);
bool car_enable(CARState *car);
bool car_set_mode(CARState *car, CARCacheMode mode);
uint64_t car_read(CARState *car, uint64_t offset);
void car_write(CARState *car, uint64_t offset, uint64_t value);
void car_read_block(CARState *car, uint64_t offset, void *dest, uint64_t size);
void car_write_block(CARState *car, uint64_t offset, const void *src, uint64_t size);
void car_teardown(CARState *car, void *dram_target);
bool car_is_addr_in_car(uint64_t addr);
void car_print_state(const CARState *car);
void car_invalidate(CARState *car);

#endif
