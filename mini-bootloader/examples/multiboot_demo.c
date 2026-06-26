#include "stage2.h"
#include <stdio.h>
#include <string.h>

int main(void)
{
    printf("=== Multiboot Demo ===\n\n");

    MultibootHeader header;
    MultibootInfo  info;
    stage2_init(&header, &info);

    header.load_addr    = KERNEL_LOAD_ADDR;
    header.load_end_addr = KERNEL_LOAD_ADDR + 0x400000;
    header.bss_end_addr  = KERNEL_LOAD_ADDR + 0x500000;
    header.entry_addr    = KERNEL_LOAD_ADDR + 0x1000;

    uint32_t sum = header.magic + header.flags
                 + header.header_addr + header.load_addr
                 + header.load_end_addr + header.bss_end_addr
                 + header.entry_addr;
    header.checksum = (uint32_t)(-(int32_t)sum);

    if (!stage2_parse_multiboot_header(&header)) {
        printf("Multiboot header validation FAILED\n");
        return 1;
    }

    MemoryMapEntry mmap[4];
    mmap[0].base_addr = 0x00000000;
    mmap[0].length    = 0x0009FC00;
    mmap[0].size      = sizeof(MemoryMapEntry);
    mmap[0].type      = MEMORY_FREE;

    mmap[1].base_addr = 0x00100000;
    mmap[1].length    = 0x0FEF0000;
    mmap[1].size      = sizeof(MemoryMapEntry);
    mmap[1].type      = MEMORY_FREE;

    mmap[2].base_addr = 0x0FFF0000;
    mmap[2].length    = 0x00010000;
    mmap[2].size      = sizeof(MemoryMapEntry);
    mmap[2].type      = MEMORY_RESERVED;

    mmap[3].base_addr = 0xFEE00000;
    mmap[3].length    = 0x00010000;
    mmap[3].size      = sizeof(MemoryMapEntry);
    mmap[3].type      = MEMORY_RESERVED;

    stage2_setup_memory_map(&info, mmap, 4);

    printf("\nTotal RAM: %u KB conventional, %u KB extended = ~%u MB\n",
           info.mem_lower, info.mem_upper,
           (info.mem_lower + info.mem_upper) / 1024);

    stage2_set_boot_device(&info, 0x80);
    (void)stage2_setup_vbe(&info, 1024, 768, 32);

    char cmdline_buf[MAX_CMDLINE_LEN + 1] = {0};
    const char *cmd = "root=/dev/sda1 ro quiet splash";
    strncpy(cmdline_buf, cmd, MAX_CMDLINE_LEN);
    stage2_set_cmdline(&info, cmd, (uint32_t)(uintptr_t)cmdline_buf);

    printf("\n--- Kernel Handoff Simulation ---\n");
    stage2_load_kernel(&info, "kernel.bin", KERNEL_LOAD_ADDR);

    stage2_print_info(&header, &info);

    stage2_jump_to_kernel(&header, &info);

    return 0;
}
