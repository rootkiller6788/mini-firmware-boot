#ifndef LINUX_BOOT_H
#define LINUX_BOOT_H

#include <stdbool.h>
#include <stdint.h>

#define LINUX_BOOT_SIGNATURE    0xAA55
#define LINUX_HEADER_MAGIC      0x53726448
#define LINUX_SETUP_MAGIC       "HdrS"
#define SETUP_SECTS_DEFAULT     4
#define SETUP_MAX_SECTS        64

#define E820MAX                 128
#define E820_RAM                1
#define E820_RESERVED           2
#define E820_ACPI               3
#define E820_NVS                4

#define VIDEO_MODE_80x25        0x03
#define VIDEO_MODE_LINUX_EXT    0xFF

#define LOADFLAGS_LOADED_HIGH   0x01
#define LOADFLAGS_CAN_USE_HEAP  0x80

#define BOOT_FLAG_ADDR          0x1FE
#define SETUP_HEADER_OFFSET     0x01F1
#define SETUP_SECTS_OFFSET      0x01F1
#define HEADER_MAGIC_OFFSET     0x0202

#define KERNEL_BASE_ADDR        0x00100000
#define INITRD_ADDR_MAX         0x37FFFFFF
#define CMD_LINE_ADDR           0x00010000
#define E820_MAP_ADDR           0x00020000

#define BZIMAGE_SETUP_SIZE      0x4000
#define BZIMAGE_OFFSET          0x200

typedef struct {
    uint32_t base;
    uint32_t length;
    uint32_t type;
} E820Entry;

typedef struct {
    uint8_t  setup_sects;
    uint16_t root_flags;
    uint32_t syssize;
    uint16_t ram_size;
    uint16_t vid_mode;
    uint16_t root_dev;
    uint16_t boot_flag;
    uint16_t jump;
    uint32_t header;
    uint16_t version;
} SetupHeader;

typedef struct {
    uint8_t  setup_sects;
    uint16_t root_flags;
    uint32_t syssize;
    uint16_t ram_size;
    uint16_t vid_mode;
    uint16_t root_dev;
    uint16_t boot_flag;
    uint16_t jump;
    uint32_t header;
    uint16_t version;
    uint32_t realmode_swtch;
    uint16_t start_sys_seg;
    uint16_t kernel_version;
    uint8_t  type_of_loader;
    uint8_t  loadflags;
    uint16_t setup_move_size;
    uint32_t code32_start;
    uint32_t ramdisk_image;
    uint32_t ramdisk_size;
    uint32_t bootsect_kludge;
    uint16_t heap_end_ptr;
    uint8_t  ext_loader_ver;
    uint8_t  ext_loader_type;
    uint32_t cmd_line_ptr;
    uint32_t initrd_addr_max;
} BootParams;

typedef struct {
    uint8_t  *setup_data;
    uint32_t  setup_size;
    uint8_t  *kernel_data;
    uint32_t  kernel_size;
    uint8_t  *initrd_data;
    uint32_t  initrd_size;
    uint8_t  *cmdline;
    E820Entry e820_map[E820MAX];
    uint32_t  e820_count;
    uint32_t  kernel_load_addr;
    uint32_t  entry_point;
} LinuxBootContext;

void   linux_boot_init(LinuxBootContext *ctx);
bool   linux_parse_setup_header(LinuxBootContext *ctx, BootParams *params);
bool   linux_load_kernel(LinuxBootContext *ctx, const char *kernel_path);
bool   linux_setup_e820(LinuxBootContext *ctx, const E820Entry *entries,
                        uint32_t count);
bool   linux_set_cmdline(LinuxBootContext *ctx, const char *cmdline);
bool   linux_load_initrd(LinuxBootContext *ctx, const char *initrd_path);
void   linux_set_initrd_addr(LinuxBootContext *ctx, uint32_t initrd_addr);
bool   linux_boot_kernel(LinuxBootContext *ctx);
void   linux_print_boot_params(const BootParams *params);
void   linux_print_e820_map(const LinuxBootContext *ctx);

#endif
