#ifndef STAGE2_H
#define STAGE2_H

#include <stdbool.h>
#include <stdint.h>

#define MULTIBOOT_MAGIC         0x1BADB002
#define MULTIBOOT_FLAGS_PAGE    0x00000001
#define MULTIBOOT_FLAGS_MEM     0x00000002
#define MULTIBOOT_FLAGS_VBE     0x00000004
#define MULTIBOOT_FLAGS_AOUT    0x00010000

#define MULTIBOOT_INFO_MEM      0x00000001
#define MULTIBOOT_INFO_BOOTDEV  0x00000002
#define MULTIBOOT_INFO_CMDLINE  0x00000004
#define MULTIBOOT_INFO_MODS     0x00000008
#define MULTIBOOT_INFO_MMAP     0x00000040

#define BOOT_DEVICE_BASE        0xE0

#define MAX_MEMORY_MAP_ENTRIES  64
#define MAX_CMDLINE_LEN         256
#define KERNEL_LOAD_ADDR        0x00100000
#define ENTRY_POINT_BASE        0x00100000
#define STACK_TOP               0x0009FC00

typedef enum {
    MEMORY_FREE      = 1,
    MEMORY_RESERVED  = 2,
    MEMORY_ACPI      = 3,
    MEMORY_NVS       = 4,
    MEMORY_BAD       = 5
} MemoryType;

typedef struct {
    uint32_t magic;
    uint32_t flags;
    uint32_t checksum;
    uint32_t header_addr;
    uint32_t load_addr;
    uint32_t load_end_addr;
    uint32_t bss_end_addr;
    uint32_t entry_addr;
} MultibootHeader;

typedef struct {
    uint32_t flags;
    uint32_t mem_lower;
    uint32_t mem_upper;
    uint32_t boot_device;
    uint32_t cmdline;
    uint32_t mods_count;
    uint32_t mods_addr;
    uint32_t syms[4];
    uint32_t mmap_length;
    uint32_t mmap_addr;
    uint32_t drives_length;
    uint32_t drives_addr;
    uint32_t config_table;
    uint32_t boot_loader_name;
    uint32_t apm_table;
    uint32_t vbe_control_info;
    uint32_t vbe_mode_info;
    uint16_t vbe_mode;
    uint16_t vbe_interface_seg;
    uint16_t vbe_interface_off;
    uint16_t vbe_interface_len;
} MultibootInfo;

typedef struct {
    uint32_t size;
    uint64_t base_addr;
    uint64_t length;
    uint32_t type;
} MemoryMapEntry;

typedef struct {
    uint32_t mod_start;
    uint32_t mod_end;
    uint32_t cmdline;
    uint32_t reserved;
} MultibootModule;

void   stage2_init(MultibootHeader *header, MultibootInfo *info);
bool   stage2_load_kernel(MultibootInfo *info, const char *kernel_path,
                          uint32_t load_addr);
bool   stage2_parse_multiboot_header(MultibootHeader *header);
bool   stage2_setup_memory_map(MultibootInfo *info, MemoryMapEntry *entries,
                               uint32_t count);
bool   stage2_setup_vbe(MultibootInfo *info, uint16_t width,
                        uint16_t height, uint8_t bpp);
void   stage2_set_boot_device(MultibootInfo *info, uint8_t drive);
void   stage2_set_cmdline(MultibootInfo *info, const char *cmdline, uint32_t addr);
void   stage2_add_module(MultibootInfo *info, MultibootModule *modules,
                         uint32_t count, uint32_t addr);
void   stage2_jump_to_kernel(MultibootHeader *header, MultibootInfo *info);
void   stage2_print_info(const MultibootHeader *header, const MultibootInfo *info);

#endif
