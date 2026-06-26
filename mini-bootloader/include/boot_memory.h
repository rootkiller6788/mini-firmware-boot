#ifndef BOOT_MEMORY_H
#define BOOT_MEMORY_H

#include <stdbool.h>
#include <stdint.h>

/*
 * L2: Physical memory management — bootloader must probe and
 *     reserve memory before handing off to the kernel.
 * L4: BIOS E820 specification (ACPI 3.0+). INT 15h AX=E820h.
 * L5: Memory map merge/sort algorithm, first-fit allocator.
 */

#define MEMMAP_MAX_ENTRIES  128
#define MEMMAP_E820_SIGNATURE 0x534D4150  /* 'SMAP' */

#define MEMTYPE_FREE        1
#define MEMTYPE_RESERVED    2
#define MEMTYPE_ACPI_RECL   3
#define MEMTYPE_ACPI_NVS    4
#define MEMTYPE_BAD         5
#define MEMTYPE_BOOTLOADER  0xFE
#define MEMTYPE_KERNEL      0xFD
#define MEMTYPE_MMIO        0xFC

#define BOOTMEM_PAGE_SIZE   4096
#define BOOTMEM_MIN_PAGES   64
#define BOOTMEM_MAX_PAGES   65536

typedef struct {
    uint64_t base;
    uint64_t length;
    uint32_t type;
    uint32_t acpi_ext;
} MemoryRegion;

typedef struct {
    MemoryRegion regions[MEMMAP_MAX_ENTRIES];
    uint32_t     count;

    /* Derived statistics */
    uint64_t total_memory;
    uint64_t free_memory;
    uint64_t reserved_memory;
    uint64_t largest_free_block;

    /* Page allocator state */
    uint8_t  *page_bitmap;
    uint32_t  page_bitmap_size;
    uint64_t  heap_base;
    uint64_t  heap_size;
    uint32_t  total_pages;
    uint32_t  free_pages;
} BootMemoryMap;

typedef struct {
    uint64_t base;
    uint64_t size;
    uint32_t type;
    bool     used;
} BootMemBlock;

/* ── API ────────────────────────────────────────────────── */
void  bootmem_init(BootMemoryMap *map);
bool  bootmem_add_region(BootMemoryMap *map, uint64_t base, uint64_t length, uint32_t type);
bool  bootmem_add_e820_entry(BootMemoryMap *map, const void *e820_record);
bool  bootmem_sanitize(BootMemoryMap *map);
bool  bootmem_merge_adjacent(BootMemoryMap *map);
void  bootmem_sort(BootMemoryMap *map);
bool  bootmem_find_largest_block(BootMemoryMap *map, uint64_t *base, uint64_t *size);
bool  bootmem_is_available(const BootMemoryMap *map, uint64_t addr, uint64_t size);
void  bootmem_print(const BootMemoryMap *map);
void  bootmem_print_summary(const BootMemoryMap *map);

/* ── First-fit page allocator ────────────────────────────── */
bool  bootmem_init_allocator(BootMemoryMap *map, uint64_t heap_base, uint64_t heap_size);
uint64_t bootmem_alloc_pages(BootMemoryMap *map, uint32_t num_pages);
bool  bootmem_free_pages(BootMemoryMap *map, uint64_t addr, uint32_t num_pages);
uint64_t bootmem_alloc_aligned(BootMemoryMap *map, uint32_t num_pages, uint32_t align_log2);
bool  bootmem_reserve_region(BootMemoryMap *map, uint64_t base, uint64_t size, uint32_t type);

#endif