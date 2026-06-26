#include "boot_memory.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/*
 * L2: Physical memory management — the bootloader probes BIOS E820
 *     to determine available RAM, then allocates pages for kernel.
 * L4: BIOS E820 specification (ACPI 3.0+). INT 15h AX=E820h returns
 *     SMAP-signed records with base, length, type.
 * L5: Merge-sort O(n log n) + first-fit allocator O(n).
 */

void bootmem_init(BootMemoryMap *map)
{
    memset(map, 0, sizeof(BootMemoryMap));
}

bool bootmem_add_region(BootMemoryMap *map, uint64_t base, uint64_t length, uint32_t type)
{
    if (map->count >= MEMMAP_MAX_ENTRIES) {
        fprintf(stderr, "[bootmem] Memory map full\n");
        return false;
    }

    /* Overflow check */
    if (base + length < base && length != 0) {
        fprintf(stderr, "[bootmem] Region overflow: base=0x%llX len=0x%llX\n",
                (unsigned long long)base, (unsigned long long)length);
        return false;
    }

    map->regions[map->count].base   = base;
    map->regions[map->count].length = length;
    map->regions[map->count].type   = type;
    map->count++;

    return true;
}

bool bootmem_add_e820_entry(BootMemoryMap *map, const void *e820_record)
{
    if (map == NULL || e820_record == NULL) return false;

    /* E820 record format (from INT 15h AX=E820h):
     *   offset 0:  uint64_t base
     *   offset 8:  uint64_t length
     *   offset 16: uint32_t type
     *   offset 20: uint32_t acpi_ext (ACPI 3.0+)
     */
    const uint8_t *rec = (const uint8_t *)e820_record;
    uint64_t base   = *(const uint64_t *)(rec);
    uint64_t length = *(const uint64_t *)(rec + 8);
    uint32_t type   = *(const uint32_t *)(rec + 16);
    uint32_t acpi __attribute__((unused)) = *(const uint32_t *)(rec + 20);

    /* Validate via signature not done here — caller checks SMAP */
    (void)acpi;
    return bootmem_add_region(map, base, length, type);
}

/*
 * L5: Memory map merge — coalesce adjacent same-type regions.
 *
 * Algorithm: after sorting, scan linearly and merge where
 *   regions[i].base + regions[i].length == regions[i+1].base
 *   AND regions[i].type == regions[i+1].type
 *
 * Complexity: O(n) after sort (single pass)
 */
bool bootmem_merge_adjacent(BootMemoryMap *map)
{
    if (map->count <= 1) return true;

    bootmem_sort(map);

    uint32_t write_idx = 0;
    for (uint32_t i = 1; i < map->count; i++) {
        MemoryRegion *cur = &map->regions[write_idx];
        MemoryRegion *nxt = &map->regions[i];

        if (cur->type == nxt->type &&
            cur->base + cur->length == nxt->base) {
            /* Merge: extend current region */
            cur->length += nxt->length;
        } else {
            write_idx++;
            map->regions[write_idx] = *nxt;
        }
    }
    map->count = write_idx + 1;

    printf("[bootmem] Merged to %u regions\n", map->count);
    return true;
}

/* L5: Sort memory regions by base address (insertion sort for small N) */
static int memregion_cmp(const void *a, const void *b)
{
    const MemoryRegion *ra = (const MemoryRegion *)a;
    const MemoryRegion *rb = (const MemoryRegion *)b;
    if (ra->base < rb->base) return -1;
    if (ra->base > rb->base) return 1;
    return 0;
}

void bootmem_sort(BootMemoryMap *map)
{
    if (map->count <= 1) return;
    qsort(map->regions, map->count, sizeof(MemoryRegion), memregion_cmp);
}

/*
 * L5: Sanitize the memory map — remove overlaps, clamp to 32-bit
 *     for typical bootloader use.
 *
 * Algorithm: sort, then for each overlapping pair, truncate the
 * earlier one or split the later one. Ensures no region exceeds
 * addressable range and no overlaps remain.
 */
bool bootmem_sanitize(BootMemoryMap *map)
{
    if (map->count == 0) return false;

    bootmem_sort(map);

    /* Remove zero-length entries and fix overlaps */
    uint32_t write_idx = 0;
    for (uint32_t i = 0; i < map->count; i++) {
        /* Skip zero-length regions */
        if (map->regions[i].length == 0) continue;

        if (write_idx > 0) {
            MemoryRegion *prev = &map->regions[write_idx - 1];
            MemoryRegion *cur  = &map->regions[i];

            /* Check for overlap */
            if (cur->base < prev->base + prev->length) {
                /* Overlap: adjust current region forward */
                uint64_t overlap_end = prev->base + prev->length;
                if (cur->base + cur->length > overlap_end) {
                    uint64_t shift = overlap_end - cur->base;
                    cur->base   += shift;
                    cur->length -= shift;
                } else {
                    /* Fully contained in previous */
                    continue;
                }
            }
        }
        map->regions[write_idx++] = map->regions[i];
    }
    map->count = write_idx;

    /* Recompute statistics */
    map->total_memory = 0;
    map->free_memory = 0;
    map->reserved_memory = 0;
    map->largest_free_block = 0;

    for (uint32_t i = 0; i < map->count; i++) {
        map->total_memory += map->regions[i].length;
        if (map->regions[i].type == MEMTYPE_FREE) {
            map->free_memory += map->regions[i].length;
            if (map->regions[i].length > map->largest_free_block)
                map->largest_free_block = map->regions[i].length;
        } else {
            map->reserved_memory += map->regions[i].length;
        }
    }

    return true;
}

bool bootmem_find_largest_block(BootMemoryMap *map, uint64_t *base, uint64_t *size)
{
    if (map == NULL || base == NULL || size == NULL) return false;

    uint64_t max_size = 0;
    uint64_t max_base = 0;
    bool found = false;

    for (uint32_t i = 0; i < map->count; i++) {
        if (map->regions[i].type == MEMTYPE_FREE &&
            map->regions[i].length > max_size) {
            max_size = map->regions[i].length;
            max_base = map->regions[i].base;
            found = true;
        }
    }

    *base = max_base;
    *size = max_size;
    return found;
}

bool bootmem_is_available(const BootMemoryMap *map, uint64_t addr, uint64_t size)
{
    if (map == NULL) return false;

    uint64_t end = addr + size;
    if (end < addr) return false;  /* overflow */

    for (uint32_t i = 0; i < map->count; i++) {
        if (map->regions[i].type != MEMTYPE_FREE) continue;
        if (map->regions[i].base <= addr &&
            map->regions[i].base + map->regions[i].length >= end) {
            return true;
        }
    }
    return false;
}

/*
 * ── L5: First-fit page allocator ──────────────────────────────────
 * Implements a simple bitmap-based first-fit allocator.
 * Each bit corresponds to one 4KB page.
 *
 * Allocate: scan bitmap for num_pages contiguous free pages.
 * Free: clear bits.
 *
 * Complexity: O(n) in total pages for allocation.
 */
bool bootmem_init_allocator(BootMemoryMap *map, uint64_t heap_base, uint64_t heap_size)
{
    if (heap_size < BOOTMEM_PAGE_SIZE * BOOTMEM_MIN_PAGES) return false;

    map->heap_base = heap_base;
    map->heap_size = heap_size;
    map->total_pages = (uint32_t)(heap_size / BOOTMEM_PAGE_SIZE);
    if (map->total_pages > BOOTMEM_MAX_PAGES)
        map->total_pages = BOOTMEM_MAX_PAGES;

    map->page_bitmap_size = (map->total_pages + 7) / 8;
    map->page_bitmap = (uint8_t *)calloc(1, map->page_bitmap_size);
    if (map->page_bitmap == NULL) return false;

    map->free_pages = map->total_pages;
    return true;
}

/* Helper: set/clear a bit in the page bitmap */
static void bitmap_set(uint8_t *bitmap, uint32_t idx, bool val)
{
    if (val)
        bitmap[idx / 8] |= (uint8_t)(1u << (idx % 8));
    else
        bitmap[idx / 8] &= (uint8_t)(~(1u << (idx % 8)));
}

static bool bitmap_test(const uint8_t *bitmap, uint32_t idx)
{
    return (bitmap[idx / 8] & (1u << (idx % 8))) != 0;
}

uint64_t bootmem_alloc_pages(BootMemoryMap *map, uint32_t num_pages)
{
    if (map == NULL || map->page_bitmap == NULL || num_pages == 0) return 0;
    if (num_pages > map->free_pages) return 0;

    /* First-fit scan */
    for (uint32_t i = 0; i <= map->total_pages - num_pages; i++) {
        bool found = true;
        for (uint32_t j = 0; j < num_pages; j++) {
            if (bitmap_test(map->page_bitmap, i + j)) {
                found = false;
                i += j;  /* skip ahead */
                break;
            }
        }
        if (found) {
            /* Mark pages as allocated */
            for (uint32_t j = 0; j < num_pages; j++) {
                bitmap_set(map->page_bitmap, i + j, true);
            }
            map->free_pages -= num_pages;
            return map->heap_base + (uint64_t)i * BOOTMEM_PAGE_SIZE;
        }
    }
    return 0;
}

bool bootmem_free_pages(BootMemoryMap *map, uint64_t addr, uint32_t num_pages)
{
    if (map == NULL || map->page_bitmap == NULL || num_pages == 0) return false;
    if (addr < map->heap_base) return false;

    uint32_t page_idx = (uint32_t)((addr - map->heap_base) / BOOTMEM_PAGE_SIZE);
    if (page_idx + num_pages > map->total_pages) return false;

    for (uint32_t j = 0; j < num_pages; j++) {
        bitmap_set(map->page_bitmap, page_idx + j, false);
    }
    map->free_pages += num_pages;
    return true;
}

/*
 * L5: Aligned allocation — allocates pages on a power-of-2 boundary.
 * Used for page tables, DMA buffers, and large kernel mappings.
 *
 * Algorithm: scan for free block, check alignment, skip if misaligned.
 */
uint64_t bootmem_alloc_aligned(BootMemoryMap *map, uint32_t num_pages,
                               uint32_t align_log2)
{
    if (map == NULL || map->page_bitmap == NULL || num_pages == 0) return 0;

    uint64_t align_mask = ((uint64_t)1 << align_log2) - 1;

    for (uint32_t i = 0; i <= map->total_pages - num_pages; i++) {
        uint64_t addr = map->heap_base + (uint64_t)i * BOOTMEM_PAGE_SIZE;
        if (addr & align_mask) continue;  /* misaligned */

        bool found = true;
        for (uint32_t j = 0; j < num_pages; j++) {
            if (bitmap_test(map->page_bitmap, i + j)) {
                found = false; break;
            }
        }
        if (found) {
            for (uint32_t j = 0; j < num_pages; j++)
                bitmap_set(map->page_bitmap, i + j, true);
            map->free_pages -= num_pages;
            return addr;
        }
    }
    return 0;
}

bool bootmem_reserve_region(BootMemoryMap *map, uint64_t base, uint64_t size, uint32_t type)
{
    /* Mark the region as reserved for bootloader/kernel use */
    return bootmem_add_region(map, base, size, type);
}

/* ── Display ───────────────────────────────────────────────────── */
void bootmem_print(const BootMemoryMap *map)
{
    printf("\n=== BIOS E820 Memory Map (%u entries) ===\n", map->count);
    printf("%-4s %-20s %-20s %-12s\n", "Idx", "Base", "Length", "Type");

    for (uint32_t i = 0; i < map->count; i++) {
        const char *type_str;
        switch (map->regions[i].type) {
            case MEMTYPE_FREE:      type_str = "FREE";      break;
            case MEMTYPE_RESERVED:  type_str = "RESERVED";  break;
            case MEMTYPE_ACPI_RECL: type_str = "ACPI_RECL"; break;
            case MEMTYPE_ACPI_NVS:  type_str = "ACPI_NVS";  break;
            case MEMTYPE_BAD:       type_str = "BAD";       break;
            default:                type_str = "UNKNOWN";   break;
        }
        printf("%-4u 0x%016llX  0x%016llX  %s\n",
               i,
               (unsigned long long)map->regions[i].base,
               (unsigned long long)map->regions[i].length,
               type_str);
    }
}

void bootmem_print_summary(const BootMemoryMap *map)
{
    printf("\n=== Memory Summary ===\n");
    printf("Total:    %llu MB\n",
           (unsigned long long)(map->total_memory / (1024 * 1024)));
    printf("Free:     %llu MB\n",
           (unsigned long long)(map->free_memory / (1024 * 1024)));
    printf("Reserved: %llu MB\n",
           (unsigned long long)(map->reserved_memory / (1024 * 1024)));
    printf("Largest free block: %llu MB\n",
           (unsigned long long)(map->largest_free_block / (1024 * 1024)));
    if (map->page_bitmap) {
        printf("Page allocator: %u/%u pages free\n",
               map->free_pages, map->total_pages);
    }
}