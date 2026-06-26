#include "smbios_fw.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool smbios_init(SMBIOS *smbios, uint8_t major, uint8_t minor)
{
    if (!smbios) return false;

    memset(smbios, 0, sizeof(SMBIOS));
    smbios->entry_point.entry_point_string[0] = '_';
    smbios->entry_point.entry_point_string[1] = 'S';
    smbios->entry_point.entry_point_string[2] = 'M';
    smbios->entry_point.entry_point_string[3] = '_';
    smbios->entry_point.major_ver = major;
    smbios->entry_point.minor_ver = minor;
    smbios->entry_point.length = 0x1F;
    smbios->entry_point.entry_point_revision = 0;
    smbios->entry_point.max_structure_size = 512;
    smbios->entry_point.checksum = 0;

    return true;
}

bool smbios_add_table(SMBIOS *smbios, const void *table, uint32_t table_len)
{
    if (!smbios || !table) return false;
    if (smbios->num_tables >= SMBIOS_MAX_TABLES) return false;
    if (table_len > 512) return false;

    memset(&smbios->table_data[smbios->num_tables], 0, 512);
    memcpy(&smbios->table_data[smbios->num_tables], table, table_len);

    smbios->tables[smbios->num_tables] = &smbios->table_data[smbios->num_tables];
    smbios->num_tables++;

    return true;
}

void *smbios_find_by_type(const SMBIOS *smbios, SMBIOSTableType type)
{
    if (!smbios) return NULL;

    for (uint32_t i = 0; i < smbios->num_tables; i++) {
        const SMBIOSHeader *hdr = (const SMBIOSHeader *)smbios->tables[i];
        if (hdr->type == (uint8_t)type) {
            return smbios->tables[i];
        }
    }
    return NULL;
}

void smbios_print_bios_info(const SMBIOS *smbios)
{
    if (!smbios) return;

    const SMBIOSBIOSInfo *bios = (const SMBIOSBIOSInfo *)smbios_find_by_type(smbios, SMBIOS_TYPE_BIOS_INFO);
    if (!bios) {
        printf("No BIOS Information table found.\n");
        return;
    }

    printf("=== SMBIOS BIOS Information ===\n");
    printf("Type:   %u\n", bios->header.type);
    printf("Length: %u bytes\n", bios->header.length);
    printf("Handle: 0x%04X\n", bios->header.handle);
    printf("Vendor string index: %u\n", bios->vendor);
    printf("BIOS Version index:  %u\n", bios->version);
    printf("Starting Address Seg: 0x%04X\n", bios->starting_addr_seg);
    printf("Release Date index:   %u\n", bios->release_date);
    printf("ROM Size: %u KB\n", (uint32_t)bios->rom_size * 64);

    printf("\n=== All SMBIOS Tables (%u total) ===\n", smbios->num_tables);
    for (uint32_t i = 0; i < smbios->num_tables; i++) {
        const SMBIOSHeader *hdr = (const SMBIOSHeader *)smbios->tables[i];
        const char *type_name = "Unknown";
        switch (hdr->type) {
            case SMBIOS_TYPE_BIOS_INFO:      type_name = "BIOS Information (0)"; break;
            case SMBIOS_TYPE_SYSTEM_INFO:    type_name = "System Information (1)"; break;
            case SMBIOS_TYPE_BASEBOARD:      type_name = "Baseboard (2)"; break;
            case SMBIOS_TYPE_SYSTEM_ENCLOSURE: type_name = "System Enclosure (3)"; break;
            case SMBIOS_TYPE_PROCESSOR:      type_name = "Processor (4)"; break;
        }
        printf("  [%u] Type %u - %s, Length: %u, Handle: 0x%04X\n",
               i, hdr->type, type_name, hdr->length, hdr->handle);
    }
}
