#ifndef BOOT_PHASES_H
#define BOOT_PHASES_H

#include <stdbool.h>
#include <stdint.h>

#define MAX_HANDOFF_BLOCKS 6
#define MAX_FV_COUNT       8
#define MAX_MEMMAP_ENTRIES 64

typedef enum {
    BOOT_PHASE_SEC = 0,
    BOOT_PHASE_PEI = 1,
    BOOT_PHASE_DXE = 2,
    BOOT_PHASE_BDS = 3,
    BOOT_PHASE_TSL = 4,
    BOOT_PHASE_RT  = 5
} BootPhase;

typedef enum {
    MEMMAP_RESERVED    = 0,
    MEMMAP_LOADER_CODE = 1,
    MEMMAP_LOADER_DATA = 2,
    MEMMAP_BOOT_SVC_CODE = 3,
    MEMMAP_BOOT_SVC_DATA = 4,
    MEMMAP_RUNTIME_CODE  = 5,
    MEMMAP_RUNTIME_DATA  = 6,
    MEMMAP_ACPI_RECLAIM  = 7,
    MEMMAP_ACPI_NVS      = 8,
    MEMMAP_MMIO          = 9,
    MEMMAP_MAX           = 10
} MemoryMapType;

typedef struct {
    MemoryMapType type;
    uint64_t      base;
    uint64_t      pages;
} MemoryMapEntry;

typedef struct {
    uint32_t       fv_count;
    uint64_t       fv_bases[MAX_FV_COUNT];
    uint64_t       fv_sizes[MAX_FV_COUNT];
    uint32_t       memory_map_count;
    MemoryMapEntry memory_map[MAX_MEMMAP_ENTRIES];
} HandOffBlock;

typedef struct {
    BootPhase     current_phase;
    HandOffBlock  phase_handoff_blocks[MAX_HANDOFF_BLOCKS];
    bool          phase_complete[MAX_HANDOFF_BLOCKS];
    uint64_t      total_memory;
    uint32_t      cpu_count;
    bool          cache_as_ram_active;
} BootState;

const char *boot_phase_name(BootPhase phase);
const char *memory_map_type_name(MemoryMapType type);

void boot_init(BootState *state);
bool boot_sec_phase(BootState *state);
bool boot_pei_phase(BootState *state);
bool boot_dxe_phase(BootState *state);
bool boot_bds_phase(BootState *state);
bool boot_transition(BootState *state, BootPhase next);
void boot_print_phase(BootState *state);

#endif
