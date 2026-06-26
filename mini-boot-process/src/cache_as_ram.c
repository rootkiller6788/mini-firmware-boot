#include "cache_as_ram.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static uint8_t  g_car_memory[CAR_NUM_LINES][CAR_LINE_SIZE];
static bool     g_car_valid[CAR_NUM_LINES];
static bool     g_car_dirty[CAR_NUM_LINES];
static bool     g_car_global = false;

void car_init(CARState *car)
{
    if (!car) return;

    memset(car, 0, sizeof(CARState));
    car->base_addr = CAR_BASE;
    car->size = CAR_SIZE;
    car->line_count = CAR_NUM_LINES;
    car->mode = CAR_MODE_DISABLED;
    car->enabled = false;
    car->teardown_complete = false;
    car->hit_count = 0;
    car->miss_count = 0;

    memset(g_car_memory, 0, sizeof(g_car_memory));
    memset(g_car_valid, 0, sizeof(g_car_valid));
    memset(g_car_dirty, 0, sizeof(g_car_dirty));

    printf("[CAR] Initialized: base=0x%08llX, size=%llu bytes (%u cache lines)\n",
           (unsigned long long)car->base_addr,
           (unsigned long long)car->size,
           car->line_count);
}

bool car_enable(CARState *car)
{
    if (!car) return false;

    if (car->enabled) {
        printf("[CAR] Already enabled.\n");
        return true;
    }

    printf("[CAR] Enabling Cache-as-RAM mode...\n");
    printf("[CAR] CR0.CD=1 (cache disabled for normal ops)\n");
    printf("[CAR] MTRR setup: UC for DRAM range, WB for CAR range (0x%08llX-0x%08llX)\n",
           (unsigned long long)car->base_addr,
           (unsigned long long)(car->base_addr + car->size - 1));

    car->mode = CAR_MODE_NO_EVICT_MODE;
    car->enabled = true;
    g_car_global = true;

    printf("[CAR] Cache fill mode: NO-EVICT\n");
    printf("[CAR] Stack top set to 0x%08llX.\n",
           (unsigned long long)CAR_STACK_TOP);

    printf("[CAR] Testing CAR read/write: writing pattern 0xDEADBEEF...\n");
    car_write(car, 0, 0xDEADBEEFCAFEBABEULL);
    uint64_t val = car_read(car, 0);
    printf("[CAR] Read-back: 0x%016llX %s\n",
           (unsigned long long)val,
           (val == 0xDEADBEEFCAFEBABEULL) ? "(OK)" : "(FAIL)");

    return true;
}

bool car_set_mode(CARState *car, CARCacheMode mode)
{
    if (!car) return false;

    const char *mode_names[] = {"DISABLED", "NO-FILL", "EVICT", "NO-EVICT"};
    printf("[CAR] Switching mode: %s -> %s\n",
           mode_names[car->mode], mode_names[mode]);

    car->mode = mode;
    return true;
}

static uint32_t car_get_line(uint64_t offset)
{
    return (uint32_t)((offset / CAR_LINE_SIZE) % CAR_NUM_LINES);
}

uint64_t car_read(CARState *car, uint64_t offset)
{
    if (!car || !car->enabled || !g_car_global) return 0;

    uint32_t line = car_get_line(offset);

    if (g_car_valid[line]) {
        uint32_t line_offset = offset % CAR_LINE_SIZE;
        uint64_t result = 0;
        for (int i = 0; i < 8; i++) {
            result |= ((uint64_t)g_car_memory[line][line_offset + i]) << (i * 8);
        }
        car->hit_count++;
        return result;
    }

    car->miss_count++;
    return 0;
}

void car_write(CARState *car, uint64_t offset, uint64_t value)
{
    if (!car || !car->enabled || !g_car_global) return;

    uint32_t line = car_get_line(offset);

    g_car_valid[line] = true;
    g_car_dirty[line] = true;

    uint32_t line_offset = offset % CAR_LINE_SIZE;
    for (int i = 0; i < 8; i++) {
        g_car_memory[line][line_offset + i] = (uint8_t)(value >> (i * 8));
    }
}

void car_read_block(CARState *car, uint64_t offset, void *dest, uint64_t size)
{
    if (!car || !car->enabled || !dest || !g_car_global) return;

    uint8_t *dst = (uint8_t *)dest;
    for (uint64_t i = 0; i < size; i++) {
        if ((i % CAR_LINE_SIZE) == 0) {
            uint32_t line = car_get_line(offset + i);
            if (g_car_valid[line]) {
                car->hit_count++;
            } else {
                car->miss_count++;
            }
        }

        uint32_t line = car_get_line(offset + i);
        uint32_t line_offset = (offset + i) % CAR_LINE_SIZE;

        if (g_car_valid[line]) {
            dst[i] = g_car_memory[line][line_offset];
        } else {
            dst[i] = 0;
        }
    }
}

void car_write_block(CARState *car, uint64_t offset, const void *src, uint64_t size)
{
    if (!car || !car->enabled || !src || !g_car_global) return;

    const uint8_t *s = (const uint8_t *)src;
    for (uint64_t i = 0; i < size; i++) {
        uint32_t line = car_get_line(offset + i);
        uint32_t line_offset = (offset + i) % CAR_LINE_SIZE;

        g_car_valid[line] = true;
        g_car_dirty[line] = true;
        g_car_memory[line][line_offset] = s[i];
    }
}

void car_teardown(CARState *car, void *dram_target)
{
    if (!car || !car->enabled) return;

    printf("[CAR:TEARDOWN] Flushing cache to DRAM...\n");
    printf("[CAR:TEARDOWN] CAR base=0x%08llX, DRAM target=0x%p\n",
           (unsigned long long)car->base_addr, dram_target);

    uint32_t flushed = 0;
    if (dram_target) {
        uint8_t *dest_buf = (uint8_t *)dram_target;
        for (uint32_t line = 0; line < CAR_NUM_LINES; line++) {
            if (g_car_valid[line]) {
                memcpy(&dest_buf[line * CAR_LINE_SIZE],
                       g_car_memory[line], CAR_LINE_SIZE);
                flushed++;
            }
        }
    }

    printf("[CAR:TEARDOWN] %u/%u cache lines flushed.\n",
           flushed, CAR_NUM_LINES);

    printf("[CAR:TEARDOWN] Invalidating all CAR cache lines...\n");
    car_invalidate(car);

    printf("[CAR:TEARDOWN] CR0.CD=0 (cache enabled for normal ops)\n");
    printf("[CAR:TEARDOWN] Switching to normal DRAM-based stack.\n");

    car->enabled = false;
    car->mode = CAR_MODE_DISABLED;
    car->teardown_complete = true;
    g_car_global = false;

    printf("[CAR:TEARDOWN] Teardown complete. DRAM now accessible.\n");
}

bool car_is_addr_in_car(uint64_t addr)
{
    return (addr >= CAR_BASE && addr < CAR_BASE + CAR_SIZE);
}

void car_invalidate(CARState *car)
{
    if (!car) return;

    memset(g_car_valid, 0, sizeof(g_car_valid));
    memset(g_car_dirty, 0, sizeof(g_car_dirty));

    printf("[CAR] All cache lines invalidated (WBINVD equivalent).\n");
}

void car_print_state(const CARState *car)
{
    if (!car) return;

    const char *mode_names[] = {"DISABLED", "NO-FILL", "EVICT", "NO-EVICT"};

    printf("\n   ===== CACHE-AS-RAM STATE =====\n");
    printf("   Enabled:       %s\n", car->enabled ? "Yes" : "No");
    printf("   Base Address:  0x%08llX\n", (unsigned long long)car->base_addr);
    printf("   Size:          %llu bytes (%llu KB)\n",
           (unsigned long long)car->size,
           (unsigned long long)car->size / 1024);
    printf("   Cache Mode:    %s (%d)\n",
           mode_names[car->mode], car->mode);
    printf("   Cache Lines:   %u (%u bytes each)\n",
           car->line_count, CAR_LINE_SIZE);
    printf("   Hit Count:     %u\n", car->hit_count);
    printf("   Miss Count:    %u\n", car->miss_count);
    printf("   Teardown:      %s\n",
           car->teardown_complete ? "Complete" : "Not complete");

    uint32_t valid_lines = 0;
    uint32_t dirty_lines = 0;
    for (uint32_t i = 0; i < CAR_NUM_LINES; i++) {
        if (g_car_valid[i]) valid_lines++;
        if (g_car_dirty[i]) dirty_lines++;
    }
    printf("   Valid Lines:   %u/%u\n", valid_lines, CAR_NUM_LINES);
    printf("   Dirty Lines:   %u/%u\n", dirty_lines, CAR_NUM_LINES);

    if (valid_lines > 0 && valid_lines <= 8) {
        printf("   First valid line data:\n");
        for (uint32_t i = 0; i < CAR_NUM_LINES && i < 4; i++) {
            if (g_car_valid[i]) {
                printf("     Line %u: ", i);
                for (int j = 0; j < 16; j++) {
                    printf("%02X ", g_car_memory[i][j]);
                }
                printf("\n");
            }
        }
    }
    printf("   ===============================\n");
}
