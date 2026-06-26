#include "linux_boot.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void linux_boot_init(LinuxBootContext *ctx)
{
    memset(ctx, 0, sizeof(LinuxBootContext));
    ctx->kernel_load_addr = KERNEL_BASE_ADDR;
    ctx->entry_point      = KERNEL_BASE_ADDR + BZIMAGE_OFFSET;

    E820Entry initial_map[] = {
        { 0x00000000, 0x0009FC00, E820_RAM },
        { 0x0009FC00, 0x00000400, E820_RESERVED },
        { 0x000F0000, 0x00010000, E820_RESERVED },
        { 0x00100000, 0x0FEF0000, E820_RAM },
        { 0x0FFF0000, 0x00010000, E820_RESERVED },
        { 0xFEC00000, 0x00010000, E820_RESERVED },
        { 0xFEE00000, 0x00010000, E820_RESERVED },
        { 0xFFFC0000, 0x00010000, E820_RESERVED },
    };

    int nentries = sizeof(initial_map) / sizeof(initial_map[0]);
    linux_setup_e820(ctx, initial_map, nentries);
}

bool linux_parse_setup_header(LinuxBootContext *ctx, BootParams *params)
{
    if (params == NULL) return false;

    if (params->boot_flag != LINUX_BOOT_SIGNATURE) {
        fprintf(stderr, "Invalid boot flag: 0x%04X (expected 0x%04X)\n",
                params->boot_flag, LINUX_BOOT_SIGNATURE);
        return false;
    }

    if (params->header != LINUX_HEADER_MAGIC) {
        fprintf(stderr, "Invalid header magic: 0x%08X (expected 0x%08X)\n",
                params->header, LINUX_HEADER_MAGIC);
        return false;
    }

    printf("[linux_boot] Setup header valid\n");
    printf("[linux_boot] Setup sectors: %u\n", params->setup_sects);
    printf("[linux_boot] Kernel size: %u (in 16-byte paras)\n",
           params->syssize);
    printf("[linux_boot] Boot loader type: 0x%02X\n", params->type_of_loader);
    printf("[linux_boot] Load flags: 0x%02X\n", params->loadflags);

    if (params->loadflags & LOADFLAGS_LOADED_HIGH) {
        printf("[linux_boot] Kernel loaded high (protected mode)\n");
    }

    ctx->setup_size  = params->setup_sects * 512;
    ctx->kernel_size = params->syssize * 16;
    ctx->entry_point = params->code32_start;

    return true;
}

bool linux_load_kernel(LinuxBootContext *ctx, const char *kernel_path)
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
    if (fsize <= 0) { fclose(f); return false; }

    ctx->setup_data = (uint8_t *)malloc(fsize);
    if (ctx->setup_data == NULL) { fclose(f); return false; }

    size_t rd = fread(ctx->setup_data, 1, fsize, f);
    fclose(f);

    if (rd != (size_t)fsize) {
        free(ctx->setup_data);
        ctx->setup_data = NULL;
        return false;
    }

    ctx->setup_size  = fsize;
    ctx->kernel_size = fsize;

    BootParams temp_params;
    memcpy(&temp_params, ctx->setup_data + SETUP_HEADER_OFFSET,
           sizeof(SetupHeader));

    BootParams params;
    memset(&params, 0, sizeof(BootParams));
    memcpy(&params.setup_sects, ctx->setup_data + SETUP_SECTS_OFFSET, 1);

    memcpy(&params.boot_flag, ctx->setup_data + BOOT_FLAG_ADDR, 2);

    uint8_t *hdr = ctx->setup_data + HEADER_MAGIC_OFFSET;
    if (memcmp(hdr, LINUX_SETUP_MAGIC, 4) == 0) {
        memcpy(&params, ctx->setup_data + SETUP_HEADER_OFFSET,
               sizeof(SetupHeader));
        linux_parse_setup_header(ctx, &params);
    } else {
        printf("[linux_boot] Legacy boot protocol (no HdrS)\n");
    }

    printf("[linux_boot] Kernel loaded: %ld bytes @ 0x%08X\n",
           fsize, ctx->kernel_load_addr);
    printf("[linux_boot] Entry point: 0x%08X\n", ctx->entry_point);

    return true;
}

bool linux_setup_e820(LinuxBootContext *ctx, const E820Entry *entries,
                      uint32_t count)
{
    if (entries == NULL || count == 0 || count > E820MAX) {
        return false;
    }

    memcpy(ctx->e820_map, entries, count * sizeof(E820Entry));
    ctx->e820_count = count;

    return true;
}

bool linux_set_cmdline(LinuxBootContext *ctx, const char *cmdline)
{
    if (cmdline == NULL) return false;

    size_t len = strlen(cmdline);
    ctx->cmdline = (uint8_t *)malloc(len + 1);
    if (ctx->cmdline == NULL) return false;

    memcpy(ctx->cmdline, cmdline, len + 1);

    printf("[linux_boot] Cmdline: '%s'\n", cmdline);
    return true;
}

bool linux_load_initrd(LinuxBootContext *ctx, const char *initrd_path)
{
    if (initrd_path == NULL) return false;

    FILE *f = fopen(initrd_path, "rb");
    if (f == NULL) {
        fprintf(stderr, "Cannot open initrd: %s\n", initrd_path);
        return false;
    }

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    rewind(f);
    if (fsize <= 0) { fclose(f); return false; }

    ctx->initrd_data = (uint8_t *)malloc(fsize);
    if (ctx->initrd_data == NULL) { fclose(f); return false; }

    size_t rd = fread(ctx->initrd_data, 1, fsize, f);
    fclose(f);

    if (rd != (size_t)fsize) {
        free(ctx->initrd_data);
        ctx->initrd_data = NULL;
        return false;
    }

    ctx->initrd_size = fsize;
    printf("[linux_boot] Initrd loaded: %ld bytes\n", fsize);
    return true;
}

void linux_set_initrd_addr(LinuxBootContext *ctx, uint32_t initrd_addr)
{
    ctx->initrd_size = 0;
    printf("[linux_boot] Initrd addr set: 0x%08X (max: 0x%08X)\n",
           initrd_addr, INITRD_ADDR_MAX);
}

bool linux_boot_kernel(LinuxBootContext *ctx)
{
    printf("\n=== Linux Kernel Boot Sequence ===\n");
    printf("[linux_boot] Real mode setup...\n");
    printf("[linux_boot] SS:SP = 0x9000:0x%04X\n", 0xFFFF);
    printf("[linux_boot] Jump to setup code @ 0x90200\n");

    printf("[setup] Probing memory (e820)...\n");
    printf("[setup] Setting video mode: 0x%02X\n", VIDEO_MODE_80x25);
    printf("[setup] %u E820 entries recorded\n", ctx->e820_count);

    printf("[setup] Enabling A20 gate\n");
    printf("[setup] Loading GDT\n");

    printf("[setup] Switching to protected mode (CR0.PE = 1)\n");
    printf("[setup] Jumping to 32-bit code: 0x%08X\n", ctx->entry_point);

    printf("[kernel] Decompressing... (bzImage)\n");
    printf("[kernel] Parsing boot params @ 0x%08X\n", KERNEL_BASE_ADDR);
    printf("[kernel] Initrd @ 0x%08X (%u bytes)\n",
           ctx->initrd_size > 0 ? 0x02000000 : 0, ctx->initrd_size);
    printf("[kernel] Cmdline: '%s'\n",
           ctx->cmdline ? (char *)ctx->cmdline : "(null)");

    printf("[kernel] Setting up initial page tables\n");
    printf("[kernel] Enabling paging (CR0.PG = 1)\n");
    printf("[kernel] === Linux kernel starting ===\n");

    return true;
}

void linux_print_boot_params(const BootParams *params)
{
    printf("\n=== Linux Boot Parameters ===\n");
    printf("Setup sectors:       %u\n", params->setup_sects);
    printf("Root flags:          0x%04X\n", params->root_flags);
    printf("System size:         %u (16-byte paragraphs)\n", params->syssize);
    printf("RAM size:            %u KB\n", params->ram_size);
    printf("Video mode:          0x%04X\n", params->vid_mode);
    printf("Root device:         0x%04X\n", params->root_dev);
    printf("Boot flag:           0x%04X\n", params->boot_flag);
    printf("Header magic:        0x%08X (%s)\n", params->header,
           params->header == LINUX_HEADER_MAGIC ? "HdrS" : "INVALID");
    printf("Version:             0x%04X\n", params->version);
    printf("Realmode switch:     0x%08X\n", params->realmode_swtch);
    printf("Start sys seg:       0x%04X\n", params->start_sys_seg);
    printf("Loader type:         0x%02X\n", params->type_of_loader);
    printf("Load flags:          0x%02X\n", params->loadflags);
    printf("Setup move size:     %u\n", params->setup_move_size);
    printf("Code32 start:        0x%08X\n", params->code32_start);
    printf("Initrd image:        0x%08X\n", params->ramdisk_image);
    printf("Initrd size:         %u\n", params->ramdisk_size);
    printf("Cmd line ptr:        0x%08X\n", params->cmd_line_ptr);
    printf("Initrd addr max:     0x%08X\n", params->initrd_addr_max);
}

void linux_print_e820_map(const LinuxBootContext *ctx)
{
    printf("\n=== E820 Memory Map ===\n");
    printf("%-8s  %-20s  %-20s  %s\n",
           "Type", "Base", "Length", "Size");

    for (uint32_t i = 0; i < ctx->e820_count; i++) {
        const char *type_name;
        switch (ctx->e820_map[i].type) {
            case E820_RAM:       type_name = "RAM";       break;
            case E820_RESERVED:  type_name = "RESERVED";  break;
            case E820_ACPI:      type_name = "ACPI";      break;
            case E820_NVS:       type_name = "NVS";       break;
            default:             type_name = "UNKNOWN";   break;
        }

        uint64_t size_mb = ctx->e820_map[i].length / (1024 * 1024);
        printf("%-8s  0x%016llX  0x%016llX  %llu MB\n",
               type_name,
               (unsigned long long)ctx->e820_map[i].base,
               (unsigned long long)ctx->e820_map[i].length,
               (unsigned long long)size_mb);
    }
}
