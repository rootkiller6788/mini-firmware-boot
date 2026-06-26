#include "stage2.h"
#include <stdio.h>
#include <string.h>

static uint8_t g_kernel_buffer[4096];

void stage2_init(MultibootHeader *header, MultibootInfo *info)
{
    memset(header, 0, sizeof(MultibootHeader));
    memset(info, 0, sizeof(MultibootInfo));

    header->magic     = MULTIBOOT_MAGIC;
    header->flags     = MULTIBOOT_FLAGS_PAGE | MULTIBOOT_FLAGS_MEM;
    header->header_addr = (uint32_t)(uintptr_t)header;

    uint32_t sum = header->magic + header->flags + header->checksum
                 + header->header_addr + header->load_addr
                 + header->load_end_addr + header->bss_end_addr
                 + header->entry_addr;
    header->checksum = (uint32_t)(-(int32_t)sum);

    info->flags       = MULTIBOOT_INFO_MEM | MULTIBOOT_INFO_BOOTDEV;
    info->mem_lower   = 640;
    info->mem_upper   = 130048;
    info->boot_device = 0xE0;
}

bool stage2_load_kernel(MultibootInfo *info, const char *kernel_path,
                        uint32_t load_addr)
{
    if (kernel_path == NULL) return false;

    FILE *f = fopen(kernel_path, "rb");
    if (f == NULL) {
        fprintf(stderr, "Cannot open kernel image: %s\n", kernel_path);
        return false;
    }

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    rewind(f);

    size_t read_sz = fread(g_kernel_buffer, 1, sizeof(g_kernel_buffer), f);
    fclose(f);

    if (fsize <= 0 || read_sz == 0) {
        fprintf(stderr, "Empty or unreadable kernel image\n");
        return false;
    }

    printf("[stage2] Loading kernel '%s' (%ld bytes) to 0x%08X\n",
           kernel_path, fsize, load_addr);

    uint32_t entry_guess = *(uint32_t *)(g_kernel_buffer + 24);
    if (entry_guess != 0 && entry_guess < 0x01000000) {
        printf("[stage2] Detected Multiboot header in kernel\n");
        printf("[stage2] Entry point: 0x%08X\n", entry_guess);
    }

    return true;
}

bool stage2_parse_multiboot_header(MultibootHeader *header)
{
    if (header->magic != MULTIBOOT_MAGIC) {
        fprintf(stderr, "Invalid Multiboot magic: 0x%08X\n", header->magic);
        return false;
    }

    uint32_t sum = header->magic + header->flags + header->checksum;
    if (sum != 0) {
        fprintf(stderr, "Checksum mismatch (got 0x%08X, expected 0x%08X)\n",
                header->checksum, (uint32_t)(-(int32_t)(sum - header->checksum)));
        return false;
    }

    printf("[stage2] Multiboot header valid\n");
    printf("[stage2] Flags: 0x%08X\n", header->flags);
    printf("[stage2] Header addr: 0x%08X\n", header->header_addr);
    printf("[stage2] Load addr: 0x%08X\n", header->load_addr);
    printf("[stage2] Entry addr: 0x%08X\n", header->entry_addr);

    return true;
}

bool stage2_setup_memory_map(MultibootInfo *info, MemoryMapEntry *entries,
                             uint32_t count)
{
    if (entries == NULL || count == 0 || count > MAX_MEMORY_MAP_ENTRIES) {
        return false;
    }

    info->mmap_length = count * sizeof(MemoryMapEntry);
    info->mmap_addr   = (uint32_t)(uintptr_t)entries;
    info->flags      |= MULTIBOOT_INFO_MMAP;

    printf("[stage2] Memory map: %u entries, %u bytes at 0x%08X\n",
           count, info->mmap_length, info->mmap_addr);

    for (uint32_t i = 0; i < count; i++) {
        const char *type_str;
        switch (entries[i].type) {
            case MEMORY_FREE:     type_str = "FREE";     break;
            case MEMORY_RESERVED: type_str = "RESERVED"; break;
            case MEMORY_ACPI:     type_str = "ACPI";     break;
            case MEMORY_NVS:      type_str = "NVS";      break;
            case MEMORY_BAD:      type_str = "BAD";      break;
            default:              type_str = "UNKNOWN";  break;
        }
        printf("  [%u] 0x%016llX - 0x%016llX (%llu MB) %s\n",
               i, entries[i].base_addr,
               entries[i].base_addr + entries[i].length - 1,
               entries[i].length / (1024 * 1024), type_str);
    }

    return true;
}

bool stage2_setup_vbe(MultibootInfo *info, uint16_t width,
                      uint16_t height, uint8_t bpp)
{
    info->flags |= MULTIBOOT_FLAGS_VBE;
    info->vbe_mode = 0x118;
    info->vbe_interface_seg = 0;
    info->vbe_interface_off = 0;
    info->vbe_interface_len = 0;

    printf("[stage2] VBE mode: %ux%ux%d\n", width, height, bpp);
    return true;
}

void stage2_set_boot_device(MultibootInfo *info, uint8_t drive)
{
    info->boot_device = (uint32_t)drive | (BOOT_DEVICE_BASE << 24);
    printf("[stage2] Boot device: 0x%02X\n", drive);
}

void stage2_set_cmdline(MultibootInfo *info, const char *cmdline, uint32_t addr)
{
    if (cmdline == NULL) return;

    size_t len = strlen(cmdline);
    if (len > MAX_CMDLINE_LEN) len = MAX_CMDLINE_LEN;
    memcpy((void *)(uintptr_t)addr, cmdline, len);
    ((char *)(uintptr_t)addr)[len] = '\0';

    info->cmdline = addr;
    info->flags  |= MULTIBOOT_INFO_CMDLINE;

    printf("[stage2] Cmdline: '%s' @ 0x%08X\n", cmdline, addr);
}

void stage2_add_module(MultibootInfo *info, MultibootModule *modules,
                       uint32_t count, uint32_t addr)
{
    info->mods_count = count;
    info->mods_addr  = addr;
    info->flags     |= MULTIBOOT_INFO_MODS;

    printf("[stage2] %u module(s) loaded @ 0x%08X\n", count, addr);
}

void stage2_jump_to_kernel(MultibootHeader *header, MultibootInfo *info)
{
    printf("\n=== Kernel Handoff ===\n");
    printf("[stage2] Setting up protected mode environment\n");
    printf("[stage2] EAX = 0x%08X (Multiboot magic)\n", MULTIBOOT_MAGIC);
    printf("[stage2] EBX = 0x%08X (Multiboot info pointer)\n",
           (uint32_t)(uintptr_t)info);
    printf("[stage2] CR0: PE bit set for protected mode\n");
    printf("[stage2] GDT loaded: 32-bit code/data segments\n");
    printf("[stage2] Stack @ 0x%08X\n", STACK_TOP);
    printf("[stage2] Jumping to kernel entry: 0x%08X\n",
           header->entry_addr);
    printf("[stage2] === Entering kernel space ===\n");
}

void stage2_print_info(const MultibootHeader *header,
                       const MultibootInfo *info)
{
    printf("\n=== Stage 2 Bootloader Info ===\n");
    printf("Header magic:  0x%08X\n", header->magic);
    printf("Header flags:  0x%08X\n", header->flags);
    printf("Entry point:   0x%08X\n", header->entry_addr);

    printf("Info flags:    0x%08X\n", info->flags);
    printf("Mem lower:     %u KB\n", info->mem_lower);
    printf("Mem upper:     %u KB (%u MB total)\n",
           info->mem_upper, (info->mem_upper + info->mem_lower) / 1024);
    printf("Boot device:   0x%08X\n", info->boot_device);

    if (info->flags & MULTIBOOT_INFO_CMDLINE) {
        printf("Cmdline @:     0x%08X\n", info->cmdline);
    }
    if (info->flags & MULTIBOOT_INFO_MMAP) {
        printf("MMAP entries:  %u bytes @ 0x%08X\n",
               info->mmap_length, info->mmap_addr);
    }
    if (info->flags & MULTIBOOT_INFO_MODS) {
        printf("Modules:       %u @ 0x%08X\n",
               info->mods_count, info->mods_addr);
    }
}
