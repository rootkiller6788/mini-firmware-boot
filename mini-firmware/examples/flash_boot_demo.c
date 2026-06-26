#include "firmware_layout.h"
#include "reset_vector.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
    printf("=== mini-firmware: Flash Boot Demo ===\n\n");

    printf("Step 1: Power-On — Initializing Flash Device\n");
    FlashDevice flash;
    if (!flash_init(&flash, 16 * 1024 * 1024)) {
        fprintf(stderr, "Failed to init flash\n");
        return 1;
    }
    printf("  Flash size: %u bytes, sector size: %u, %u sectors\n",
           flash.size, flash.sector_size, flash.size / flash.sector_size);

    printf("\nStep 2: Erasing sector 0 (boot sector)\n");
    if (!flash_erase_sector(&flash, 0)) {
        fprintf(stderr, "Failed to erase sector 0\n");
        return 1;
    }
    printf("  Erase count for sector 0: %u\n", flash.erase_count[0]);

    printf("\nStep 3: Programming firmware image into flash\n");
    FirmwareImage fw = {
        .base_addr  = 0xFFF00000,
        .entry_point = 0xFFF00100,
        .text_section   = { .offset = 0xFFF00000, .size = 0x00008000 },
        .rodata_section = { .offset = 0xFFF08000, .size = 0x00001000 },
        .data_section   = { .offset = 0xFFF09000, .size = 0x00002000 },
        .bss_section    = { .offset = 0xFFF0B000, .size = 0x00001000 }
    };

    uint8_t header_data[32];
    memset(header_data, 0, sizeof(header_data));
    memcpy(header_data, &fw.entry_point, sizeof(fw.entry_point));
    memcpy(header_data + 4, &fw.base_addr, sizeof(fw.base_addr));

    if (!flash_program_page(&flash, 0, header_data, sizeof(header_data))) {
        fprintf(stderr, "Failed to program header\n");
        return 1;
    }
    printf("  Firmware base: 0x%08X, entry point: 0x%08X\n",
           fw.base_addr, fw.entry_point);

    printf("\nStep 4: CPU reads reset vector at 0xFFFFFFF0\n");
    ResetVector rv;
    fw_find_entry_point(&fw);
    if (!reset_vector_init(&rv, fw.entry_point)) {
        fprintf(stderr, "Failed to init reset vector\n");
        return 1;
    }
    printf("  Reset vector startup IP: 0x%08X\n", rv.startup_ip);

    printf("\nStep 5: CPU reset — loads firmware from reset vector\n");
    CPUContext ctx;
    if (!cpu_reset(&ctx, &rv)) {
        fprintf(stderr, "Failed to reset CPU\n");
        return 1;
    }
    printf("  CPU EIP set to: 0x%08X\n", ctx.eip);
    printf("  CR0: 0x%08X (PE=%u)\n", ctx.cr0, ctx.cr0 & 1);
    printf("  CPU mode: Real Mode (16-bit)\n");

    printf("\nStep 6: Switching CPU modes (Real -> Protected -> Long)\n");
    printf("  Mode transition: Real Mode");
    cpu_init_gdt(&ctx);
    if (cpu_switch_mode(&ctx, CPU_MODE_PROTECTED)) {
        printf(" -> Protected Mode");
    }
    cpu_init_idt(&ctx);
    if (cpu_switch_mode(&ctx, CPU_MODE_LONG)) {
        printf(" -> Long Mode\n");
    }

    printf("\nStep 7: Firmware entry point — jumping to 0x%08X\n", fw.entry_point);
    ctx.eax = 0xDEADBEEF;
    ctx.ebx = 0xCAFEBABE;
    ctx.eip = fw.entry_point;
    printf("  Firmware execution started!\n");

    printf("\nStep 8: Final CPU State\n");
    cpu_print_registers(&ctx);

    printf("\n=== Boot Sequence Complete ===\n");
    return 0;
}
