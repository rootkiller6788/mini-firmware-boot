#include "linux_boot.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int main(void)
{
    printf("=== Linux Boot Demo ===\n\n");

    LinuxBootContext ctx;
    linux_boot_init(&ctx);

    linux_print_e820_map(&ctx);

    ctx.kernel_load_addr = KERNEL_BASE_ADDR;
    ctx.entry_point      = KERNEL_BASE_ADDR + BZIMAGE_OFFSET;

    {
        unsigned char setup_bin[8192];
        memset(setup_bin, 0, sizeof(setup_bin));
        memset(setup_bin + BOOT_FLAG_ADDR, 0x55, 1);
        memset(setup_bin + BOOT_FLAG_ADDR + 1, 0xAA, 1);

        unsigned char *hdr = setup_bin + HEADER_MAGIC_OFFSET;
        memcpy(hdr, LINUX_SETUP_MAGIC, 4);

        BootParams *sh = (BootParams *)(setup_bin + SETUP_HEADER_OFFSET);
        memset(sh, 0, sizeof(BootParams));
        sh->setup_sects   = 4;
        sh->boot_flag     = LINUX_BOOT_SIGNATURE;
        sh->header        = LINUX_HEADER_MAGIC;
        sh->version       = 0x020C;
        sh->loadflags     = LOADFLAGS_LOADED_HIGH | LOADFLAGS_CAN_USE_HEAP;
        sh->setup_move_size = BZIMAGE_SETUP_SIZE;
        sh->code32_start  = 0x00100200;
        sh->type_of_loader = 0xFF;
        sh->ramdisk_image = 0;
        sh->ramdisk_size  = 0;
        sh->cmd_line_ptr  = CMD_LINE_ADDR;
        sh->syssize       = 32768;
        sh->ram_size      = 1024;
        sh->vid_mode      = VIDEO_MODE_80x25;

        ctx.setup_data  = (uint8_t *)malloc(sizeof(setup_bin));
        ctx.setup_size  = sizeof(setup_bin);
        ctx.kernel_size = sh->syssize * 16;
        memcpy(ctx.setup_data, setup_bin, sizeof(setup_bin));

        BootParams params;
        memset(&params, 0, sizeof(BootParams));
        memcpy(&params, setup_bin + SETUP_HEADER_OFFSET, sizeof(SetupHeader));

        bool valid = linux_parse_setup_header(&ctx, &params);
        printf("Parse header: %s\n\n", valid ? "OK" : "FAIL");

        if (valid) {
            linux_print_boot_params(&params);
        }
    }

    const char *cmdline = "root=/dev/sda1 rw init=/sbin/init console=ttyS0,115200";
    linux_set_cmdline(&ctx, cmdline);

    BootParams *p = (BootParams *)(ctx.setup_data + SETUP_HEADER_OFFSET);
    p->cmd_line_ptr = (uint32_t)(uintptr_t)ctx.cmdline;

    printf("\n--- Simulating kernel boot ---\n");
    linux_boot_kernel(&ctx);

    free(ctx.setup_data);
    free(ctx.cmdline);
    ctx.setup_data = NULL;
    ctx.cmdline    = NULL;

    return 0;
}
