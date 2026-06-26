#include "smbios_fw.h"
#include <stdio.h>
#include <string.h>

int main(void)
{
    printf("=== mini-firmware: SMBIOS Demo ===\n\n");

    SMBIOS smbios;
    if (!smbios_init(&smbios, 3, 2)) {
        fprintf(stderr, "Failed to init SMBIOS\n");
        return 1;
    }
    printf("SMBIOS %u.%u initialized\n",
           smbios.entry_point.major_ver,
           smbios.entry_point.minor_ver);

    printf("\nStep 1: Adding BIOS Information (Type 0)\n");
    SMBIOSBIOSInfo bios_info;
    memset(&bios_info, 0, sizeof(bios_info));
    bios_info.header.type = SMBIOS_TYPE_BIOS_INFO;
    bios_info.header.length = sizeof(SMBIOSBIOSInfo);
    bios_info.header.handle = 0x0001;
    bios_info.vendor = 1;
    bios_info.version = 2;
    bios_info.starting_addr_seg = 0xE800;
    bios_info.release_date = 3;
    bios_info.rom_size = 128;

    if (!smbios_add_table(&smbios, &bios_info, sizeof(bios_info))) {
        fprintf(stderr, "Failed to add BIOS info table\n");
        return 1;
    }

    printf("\nStep 2: Adding System Information (Type 1)\n");
    SMBIOSSystemInfo sys_info;
    memset(&sys_info, 0, sizeof(sys_info));
    sys_info.header.type = SMBIOS_TYPE_SYSTEM_INFO;
    sys_info.header.length = sizeof(SMBIOSSystemInfo);
    sys_info.header.handle = 0x0002;
    sys_info.manufacturer = 1;
    sys_info.product_name = 2;
    sys_info.version = 3;
    sys_info.serial_number = 4;

    if (!smbios_add_table(&smbios, &sys_info, sizeof(sys_info))) {
        fprintf(stderr, "Failed to add System info table\n");
        return 1;
    }

    printf("\nStep 3: Adding Baseboard Information (Type 2)\n");
    SMBIOSBaseboard bb_info;
    memset(&bb_info, 0, sizeof(bb_info));
    bb_info.header.type = SMBIOS_TYPE_BASEBOARD;
    bb_info.header.length = sizeof(SMBIOSBaseboard);
    bb_info.header.handle = 0x0003;
    bb_info.manufacturer = 1;
    bb_info.product = 2;
    bb_info.version = 3;
    bb_info.serial_number = 4;

    if (!smbios_add_table(&smbios, &bb_info, sizeof(bb_info))) {
        fprintf(stderr, "Failed to add Baseboard table\n");
        return 1;
    }

    printf("  Total tables: %u\n", smbios.num_tables);

    printf("\nStep 4: Finding table by type\n");
    SMBIOSBIOSInfo *found_bios = (SMBIOSBIOSInfo *)smbios_find_by_type(&smbios, SMBIOS_TYPE_BIOS_INFO);
    SMBIOSSystemInfo *found_sys = (SMBIOSSystemInfo *)smbios_find_by_type(&smbios, SMBIOS_TYPE_SYSTEM_INFO);
    SMBIOSBaseboard *found_bb = (SMBIOSBaseboard *)smbios_find_by_type(&smbios, SMBIOS_TYPE_BASEBOARD);

    printf("  BIOS Info found:    %s\n", found_bios ? "yes" : "no");
    printf("  System Info found:  %s\n", found_sys ? "yes" : "no");
    printf("  Baseboard found:    %s\n", found_bb ? "yes" : "no");

    {
        void *proc = smbios_find_by_type(&smbios, SMBIOS_TYPE_PROCESSOR);
        printf("  Processor found:    %s\n", proc ? "yes" : "no");
    }

    printf("\nStep 5: Printing all SMBIOS information\n\n");
    smbios_print_bios_info(&smbios);

    printf("\n=== SMBIOS Demo Complete ===\n");
    return 0;
}
