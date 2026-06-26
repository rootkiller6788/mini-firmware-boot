#include "smbios.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *build_smbios_data(size_t *out_size)
{
    size_t size = 4096;
    uint8_t *data = calloc(1, size);
    if (!data) return NULL;

    /* --- SMBIOS Entry Point (32-bit) --- */
    SMBIOSEntryPoint32 *ep = (SMBIOSEntryPoint32 *)data;
    memcpy(ep->anchor_string, SMBIOS_ANCHOR_32, 4);
    ep->checksum = 0;
    ep->entry_point_length = sizeof(SMBIOSEntryPoint32);
    ep->smbios_major_version = 3;
    ep->smbios_minor_version = 7;
    ep->max_structure_size = 512;
    ep->entry_point_revision = 0;
    memset(ep->formatted_area, 0, 5);
    memcpy(ep->intermediate_anchor, "_DMI_", 5);
    ep->intermediate_checksum = 0;

    /* Structures go right after entry point */
    size_t struct_offset = sizeof(SMBIOSEntryPoint32);
    ep->structure_table_address = (uint32_t)(uintptr_t)(data + struct_offset);
    uint8_t *s = data + struct_offset;

    /* --- Type 0: BIOS Information --- */
    s[0]  = 0;    /* type */
    s[1]  = 0x1A; /* length = 26 */
    s[2]  = 0x00; /* handle lo */
    s[3]  = 0x00; /* handle hi */
    s[4]  = 1;    /* vendor string #1 */
    s[5]  = 2;    /* version string #2 */
    s[6]  = 0x00; s[7] = 0xF0; /* starting addr seg */
    s[8]  = 3;    /* release date string #3 */
    s[9]  = 0x80; /* ROM size: 8MB */
    memset(s + 10, 0, 8); /* BIOS characteristics */
    s[18] = 0x07; s[19] = 0x00; /* ext chars */
    s[20] = 0x03; /* major release */
    s[21] = 0x07; /* minor release */
    s[22] = 0x00; /* EC major */
    s[23] = 0x00; /* EC minor */
    s[24] = 0x00; s[25] = 0x00; /* extended ROM size */

    s += 26;
    strcpy((char *)s, "NanoBIOS"); s += 9;  /* string #1 */
    strcpy((char *)s, "1.0.0"); s += 6;     /* string #2 */
    strcpy((char *)s, "05/16/2026"); s += 11; /* string #3 */
    *s++ = 0; /* double null terminator */
    s++; /* skip second null */

    /* --- Type 1: System Information --- */
    s[0]  = 1;    /* type */
    s[1]  = 0x1B; /* length = 27 */
    s[2]  = 0x01; s[3]  = 0x00; /* handle 1 */
    s[4]  = 1;    /* manufacturer #1 */
    s[5]  = 2;    /* product name #2 */
    s[6]  = 3;    /* version #3 */
    s[7]  = 4;    /* serial #4 */
    /* UUID */
    uint8_t uuid[16] = {0x55,0x01,0x02,0x03,0x04,0x05,0x06,0x07,
                        0x08,0x09,0x0A,0x0B,0x0C,0x0D,0x0E,0x0F};
    memcpy(s + 8, uuid, 16);
    s[24] = 0x06; /* Wake-up type: Power Switch */
    s[25] = 5;    /* SKU #5 */
    s[26] = 6;    /* family #6 */

    s += 27;
    strcpy((char *)s, "NanoHardware Inc."); s += 18; /* #1 */
    strcpy((char *)s, "NanoBoard Pro"); s += 14;      /* #2 */
    strcpy((char *)s, "Rev C"); s += 6;                /* #3 */
    strcpy((char *)s, "SN-000001"); s += 10;           /* #4 */
    strcpy((char *)s, "SKU-001"); s += 8;              /* #5 */
    strcpy((char *)s, "Nano Family"); s += 12;         /* #6 */
    *s++ = 0;

    /* --- Type 2: Baseboard Information --- */
    s[0]  = 2;    /* type */
    s[1]  = 0x10; /* length = 16 */
    s[2]  = 0x02; s[3]  = 0x00; /* handle 2 */
    s[4]  = 1;    /* manufacturer #1 */
    s[5]  = 2;    /* product #2 */
    s[6]  = 3;    /* version #3 */
    s[7]  = 4;    /* serial #4 */
    s[8]  = 0; s[9] = 0; s[10] = 0; s[11] = 0; /* asset tag = none */
    s[12] = 0x03; /* Feature flags: motherboard, replaceable */
    s[13] = 5;    /* location in chassis #5 */
    s[14] = 0x00; s[15] = 0x00; /* chassis handle = 0 (not specified) */

    s += 16;
    strcpy((char *)s, "NanoHardware Inc."); s += 18; /* #1 */
    strcpy((char *)s, "NB-1000"); s += 8;             /* #2 */
    strcpy((char *)s, "v2.1"); s += 5;                /* #3 */
    strcpy((char *)s, "BBSN-00002"); s += 12;         /* #4 */
    strcpy((char *)s, "Slot 1"); s += 7;              /* #5 */
    *s++ = 0;

    /* --- Type 4: Processor Information --- */
    s[0]  = 4;    /* type */
    s[1]  = 0x2A; /* length = 42 */
    s[2]  = 0x03; s[3]  = 0x00; /* handle 3 */
    s[4]  = 1;    /* socket #1 */
    s[5]  = 3;    /* processor type: CPU */
    s[6]  = 0x07; /* processor family: x86-64 */
    s[7]  = 2;    /* manufacturer #2 */
    /* processor ID */
    s[8]  = 0x0F; s[9]  = 0xAB; s[10] = 0x00; s[11] = 0x00;
    s[12] = 0xFF; s[13] = 0xFB; s[14] = 0x8B; s[15] = 0x1F;
    s[16] = 3;    /* version #3 */
    s[17] = 0x41; /* voltage: 3.3V */
    s[18] = 0x00; s[19] = 0x10; /* external clock: 100 MHz */
    s[20] = 0x80; s[21] = 0x0C; /* max speed: 3200 MHz */
    s[22] = 0x80; s[23] = 0x0C; /* current speed: 3200 MHz */
    s[24] = 0x76; /* status: populated, enabled */
    s[25] = 0x02; /* processor upgrade: socket */
    /* L1/L2/L3 cache handles */
    *(uint16_t *)(s + 26) = 0x0004; /* L1 */
    *(uint16_t *)(s + 28) = 0x0005; /* L2 */
    *(uint16_t *)(s + 30) = 0x0006; /* L3 */
    s[32] = 4;    /* serial #4 */
    s[33] = 5;    /* asset tag #5 */
    s[34] = 6;    /* part number #6 */
    s[35] = 4;    /* core count */
    s[36] = 8;    /* core enabled */
    s[37] = 8;    /* thread count */
    s[38] = 0x05; s[39] = 0x00; /* characteristics */
    s[40] = 0x06; s[41] = 0x00; /* processor family 2 */

    s += 42;
    strcpy((char *)s, "LGA-NANO"); s += 9;   /* #1 */
    strcpy((char *)s, "NanoChip"); s += 9;    /* #2 */
    strcpy((char *)s, "Nano-8C v3"); s += 11; /* #3 */
    strcpy((char *)s, "CPU-SN-12345"); s += 13; /* #4 */
    strcpy((char *)s, "AT-001"); s += 7;       /* #5 */
    strcpy((char *)s, "NCP-8000"); s += 9;     /* #6 */
    *s++ = 0;

    /* --- Type 7: Cache (L1) --- */
    s[0] = 7; s[1] = 0x1B; /* length = 27 */
    s[2] = 0x04; s[3] = 0x00; /* handle 4 */
    s[4] = 1; /* socket #1 */
    s[5] = 0x00; s[6] = 0x02; /* cache config */
    s[7] = 0x00; s[8] = 0x20; /* max size: 32 KB */
    s[9] = 0x00; s[10]= 0x20; /* installed: 32 KB */
    s[11]= 0x00; s[12]= 0x00; /* SRAM type */
    memset(s + 13, 0, 8); /* SRAM type */
    s[21]= 0x08; /* Cache Speed: 8 ns */
    s[22]= 0x04; /* Error Correction: Parity */
    s[23]= 0x03; /* System Cache Type: Data */
    s[24]= 0x04; /* Associativity: 4-way */
    s[25]= 0x40; /* Max Cache Size 2 (32K) */
    s[26]= 0x40; /* Installed Size 2 (32K) */

    s += 27;
    strcpy((char *)s, "L1 Cache"); s += 10; /* #1 */
    *s++ = 0;

    /* --- Type 17: Memory Device --- */
    s[0]  = 17;  /* type */
    s[1]  = 0x28; /* length = 40 */
    s[2]  = 0x07; s[3]  = 0x00; /* handle 7 */
    s[4]  = 0x00; s[5] = 0x00; /* phys memory array handle */
    s[6]  = 0x00; s[7] = 0x00; /* mem error info handle */
    s[8]  = 0x00; s[9] = 0x08; /* total width: 64 bits */
    s[10] = 0x00; s[11]= 0x08; /* data width: 64 bits */
    s[12] = 0x00; s[13]= 0x20; /* size: 8192 MB */
    s[14] = 0x1A; /* form factor: SODIMM */
    s[15] = 0x00; /* device set: none */
    s[16] = 1;    /* device locator #1 */
    s[17] = 2;    /* bank locator #2 */
    s[18] = 0x1A; /* memory type: DDR4 */
    s[19] = 0x00; s[20] = 0x00; /* type detail */
    s[21] = 0x80; s[22] = 0x13; /* speed: 3200 MHz */
    s[23] = 3;    /* manufacturer #3 */
    s[24] = 4;    /* serial #4 */
    s[25] = 5;    /* asset tag #5 */
    s[26] = 6;    /* part number #6 */
    s[27] = 0;    /* attributes */
    s[28] = 0x00; s[29] = 0x00; s[30] = 0x00; s[31] = 0x00; /* extended size */
    s[32] = 0x00; s[33] = 0x13; /* configured speed */
    s[34] = 0x00; s[35] = 0x00; /* min voltage */
    s[36] = 0x00; s[37] = 0x00; /* max voltage */
    s[38] = 0x00; s[39] = 0x00; /* configured voltage */

    s += 40;
    strcpy((char *)s, "DIMM 0"); s += 7;          /* #1 */
    strcpy((char *)s, "Channel A"); s += 10;       /* #2 */
    strcpy((char *)s, "NanoMemory"); s += 11;      /* #3 */
    strcpy((char *)s, "MEM-SN-67890"); s += 14;    /* #4 */
    strcpy((char *)s, "AT-MEM-002"); s += 11;      /* #5 */
    strcpy((char *)s, "NMD-32GB-3200"); s += 14;   /* #6 */
    *s++ = 0;

    /* End of table */
    s[0] = 127; /* type 127 */
    s[1] = 4;   /* length */
    s[2] = 0xFF; s[3] = 0xFF; /* handle 0xFFFF */
    s += 4;
    *s++ = 0;

    /* Update entry point */
    size_t struct_table_length = (size_t)(s - (data + struct_offset));
    ep->structure_table_length = (uint16_t)struct_table_length;
    ep->number_of_structures = 6;

    /* Compute entry point checksum */
    uint8_t sum = 0;
    for (size_t i = 0; i < sizeof(SMBIOSEntryPoint32); i++) sum += data[i];
    ep->checksum = (uint8_t)(256 - sum);

    /* Compute intermediate checksum */
    sum = 0;
    for (size_t i = 0x10; i < 0x1E; i++) sum += data[i];
    ep->intermediate_checksum = (uint8_t)(256 - sum);

    *out_size = (size_t)(s - data);
    return data;
}

int main(void)
{
    printf("=== mini-hardware-desc: SMBIOS Demo ===\n\n");

    size_t data_size = 0;
    uint8_t *data = build_smbios_data(&data_size);
    if (!data) {
        printf("Failed to build SMBIOS data\n");
        return 1;
    }

    SMBIOSTable table;
    if (!smbios_parse(&table, data, data_size)) {
        printf("Failed to parse SMBIOS\n");
        free(data);
        return 1;
    }

    printf("Entry Point: %s, Version %u.%u\n",
           table.entry_point.anchor,
           table.entry_point.ep32.smbios_major_version,
           table.entry_point.ep32.smbios_minor_version);

    smbios_print_all(&table);

    /* Type name test */
    printf("\n--- SMBIOS Type Reference ---\n");
    printf("Type %2d: %s\n", 0,  smbios_type_to_string(0));
    printf("Type %2d: %s\n", 1,  smbios_type_to_string(1));
    printf("Type %2d: %s\n", 2,  smbios_type_to_string(2));
    printf("Type %2d: %s\n", 3,  smbios_type_to_string(3));
    printf("Type %2d: %s\n", 4,  smbios_type_to_string(4));
    printf("Type %2d: %s\n", 6,  smbios_type_to_string(6));
    printf("Type %2d: %s\n", 7,  smbios_type_to_string(7));
    printf("Type %2d: %s\n", 17, smbios_type_to_string(17));
    printf("Type%3d: %s\n", 127,smbios_type_to_string(127));

    smbios_free_table(&table);
    free(data);
    printf("\nDone.\n");
    return 0;
}
