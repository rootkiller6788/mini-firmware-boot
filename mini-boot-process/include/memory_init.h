#ifndef MEMORY_INIT_H
#define MEMORY_INIT_H

#include <stdbool.h>
#include <stdint.h>

#define SPD_MODULE_SIZE_OFFSET   4
#define SPD_SPEED_OFFSET         12
#define SPD_RANKS_OFFSET         7
#define SPD_TIMING_OFFSET        18
#define SPD_DDR4_TYPE            0x0C
#define SPD_DDR5_TYPE            0x12

#define MAX_CHANNELS             2
#define MAX_DIMM_PER_CHANNEL     2
#define JEDEC_DDR4_TCL_MASK      0xF0
#define JEDEC_DDR4_TRCD_MASK     0x0F
#define JEDEC_DDR4_TRP_MASK      0xF0
#define JEDEC_DDR4_TRAS_MASK     0x0F

#define MRS0_ADDR                0x0000
#define MRS1_ADDR                0x0001
#define MRS2_ADDR                0x0002
#define MRS3_ADDR                0x0003
#define MRS4_ADDR                0x0004

#define MEM_TRAIN_DQS_DELAY      0x0020
#define MEM_TRAIN_READ_DQS       0x0024
#define MEM_TRAIN_WRITE_DQ       0x0028

#define MEMORY_MAP_MAX_ENTRIES   64

typedef enum {
    MEM_SPEED_DDR3_800  = 800,
    MEM_SPEED_DDR3_1066 = 1066,
    MEM_SPEED_DDR3_1333 = 1333,
    MEM_SPEED_DDR3_1600 = 1600,
    MEM_SPEED_DDR4_2133 = 2133,
    MEM_SPEED_DDR4_2400 = 2400,
    MEM_SPEED_DDR4_2666 = 2666,
    MEM_SPEED_DDR4_3200 = 3200,
    MEM_SPEED_DDR5_4800 = 4800,
    MEM_SPEED_DDR5_5600 = 5600
} MemorySpeed;

typedef enum {
    RANK_SINGLE = 1,
    RANK_DUAL   = 2,
    RANK_QUAD   = 4
} MemoryRank;

typedef struct {
    uint64_t    module_size_mb;
    MemorySpeed speed;
    MemoryRank  ranks;
    uint32_t    tCL;
    uint32_t    tRCD;
    uint32_t    tRP;
    uint32_t    tRAS;
    uint32_t    tRFC;
    bool        ecc_support;
    bool        registered;
    uint8_t     spd_bytes[256];
} SPDData;

typedef struct {
    SPDData dimm[MAX_DIMM_PER_CHANNEL];
    bool    dimm_populated[MAX_DIMM_PER_CHANNEL];
    bool    channel_enabled;
} MemoryChannel;

typedef struct {
    MemoryChannel channels[MAX_CHANNELS];
    uint64_t      total_memory_mb;
    uint32_t      active_channels;
    bool          interleaved;
    bool          ecc_enabled;
    uint64_t      base_address;
} MemoryController;

typedef enum {
    MEMMAP_TYPE_RESERVED    = 0,
    MEMMAP_TYPE_LOADER_CODE = 1,
    MEMMAP_TYPE_LOADER_DATA = 2,
    MEMMAP_TYPE_BOOT_SVC    = 3,
    MEMMAP_TYPE_RUNTIME     = 4,
    MEMMAP_TYPE_ACPI        = 5,
    MEMMAP_TYPE_MMIO        = 6,
    MEMMAP_TYPE_MAX         = 7
} MemMapType;

typedef struct {
    MemMapType type;
    uint64_t   base;
    uint64_t   pages;
    uint64_t   attributes;
} MemMapEntry;

typedef struct {
    uint32_t    count;
    MemMapEntry entries[MEMORY_MAP_MAX_ENTRIES];
    uint64_t    total_pages;
} MemoryMap;

bool mem_init_spd(SPDData *spd, const uint8_t *raw_bytes);
bool mem_init_controller(MemoryController *ctrl, const SPDData *dimms, uint32_t count);
bool mem_train_ddr(MemoryController *ctrl);
bool mem_train_write_leveling(MemoryController *ctrl);
bool mem_train_read_dqs(MemoryController *ctrl);
bool mem_build_map(MemoryMap *map, uint64_t total_memory, uint64_t mmio_base);
void mem_print_spd(const SPDData *spd);
void mem_print_controller(const MemoryController *ctrl);
void mem_print_map(const MemoryMap *map);

#endif
