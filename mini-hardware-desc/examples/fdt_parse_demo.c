#include "device_tree.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void write_u32(uint8_t *buf, size_t offset, uint32_t val)
{
    buf[offset]     = (uint8_t)((val >> 24) & 0xFF);
    buf[offset + 1] = (uint8_t)((val >> 16) & 0xFF);
    buf[offset + 2] = (uint8_t)((val >> 8)  & 0xFF);
    buf[offset + 3] = (uint8_t)(val & 0xFF);
}

static uint8_t *build_dtb(size_t *out_size)
{
    /* Reserve space for header + structs + strings */
    uint8_t *dtb = calloc(1, 4096);
    if (!dtb) return NULL;

    /* Strings block */
    char strings_block[1024];
    memset(strings_block, 0, sizeof(strings_block));
#define STROFF(n) (size_t)((n) - strings_block)
    char *sp = strings_block;

    strcpy(sp, "model");        size_t off_model = STROFF(sp); sp += 6;
    strcpy(sp, "compatible");   size_t off_compat = STROFF(sp); sp += 11;
    strcpy(sp, "#address-cells"); size_t off_addr_cells = STROFF(sp); sp += 15;
    strcpy(sp, "#size-cells");  size_t off_size_cells = STROFF(sp); sp += 12;
    strcpy(sp, "reg");          size_t off_reg = STROFF(sp); sp += 4;
    strcpy(sp, "interrupts");   size_t off_interrupts = STROFF(sp); sp += 11;
    strcpy(sp, "clock-frequency"); size_t off_clock_freq = STROFF(sp); sp += 17;
    strcpy(sp, "status");       size_t off_status = STROFF(sp); sp += 7;
    strcpy(sp, "phandle");      size_t off_phandle = STROFF(sp); sp += 8;
    strcpy(sp, "my-vendor,my-board"); size_t off_board_compat = STROFF(sp); sp += 19;
    strcpy(sp, "my-vendor,my-soc");   size_t off_soc_compat = STROFF(sp); sp += 17;
    strcpy(sp, "my-vendor,my-serial"); size_t off_serial_compat = STROFF(sp); sp += 21;
    strcpy(sp, "my-vendor,my-intc");   size_t off_intc_compat = STROFF(sp); sp += 19;
    strcpy(sp, "okay");         size_t off_okay = STROFF(sp); sp += 5;

    size_t strings_size = (size_t)(sp - strings_block);
    size_t strings_aligned = (strings_size + 3) & ~3;

    /* Reserve map (empty) */
    size_t mem_rsvmap = 0x30; /* 8*3 = 24 bytes: two entries + terminator */
    size_t off_dt_struct = mem_rsvmap + 24;

    /* Build structure block */
    uint8_t *sb = dtb + off_dt_struct;
    size_t spos = 0;

    /* Root node */
    write_u32(sb, spos, FDT_BEGIN_NODE); spos += 4;
    /* name = "" */
    sb[spos] = '\0'; spos += 4; /* align */

    /* model */
    write_u32(sb, spos, FDT_PROP); spos += 4;
    write_u32(sb, spos, 19); spos += 4; /* len */
    write_u32(sb, spos, (uint32_t)off_model); spos += 4;
    strcpy((char *)(sb + spos), "My Nano Board v1.0"); spos += 20;
    spos = (spos + 3) & ~3;

    /* compatible */
    write_u32(sb, spos, FDT_PROP); spos += 4;
    write_u32(sb, spos, (uint32_t)(strlen(strings_block + off_board_compat) + 1)); spos += 4;
    write_u32(sb, spos, (uint32_t)off_board_compat); spos += 4;
    strcpy((char *)(sb + spos), strings_block + off_board_compat); spos += 19;
    spos = (spos + 3) & ~3;

    /* #address-cells */
    write_u32(sb, spos, FDT_PROP); spos += 4;
    write_u32(sb, spos, 4); spos += 4;
    write_u32(sb, spos, (uint32_t)off_addr_cells); spos += 4;
    uint32_t addr_cells = fdt_bswap32(2);
    memcpy(sb + spos, &addr_cells, 4); spos += 4;

    /* #size-cells */
    write_u32(sb, spos, FDT_PROP); spos += 4;
    write_u32(sb, spos, 4); spos += 4;
    write_u32(sb, spos, (uint32_t)off_size_cells); spos += 4;
    uint32_t size_cells = fdt_bswap32(1);
    memcpy(sb + spos, &size_cells, 4); spos += 4;

    /* SOC bus node */
    write_u32(sb, spos, FDT_BEGIN_NODE); spos += 4;
    strcpy((char *)(sb + spos), "soc"); spos += 4;

    /* soc compatible */
    write_u32(sb, spos, FDT_PROP); spos += 4;
    write_u32(sb, spos, (uint32_t)(strlen(strings_block + off_soc_compat) + 1)); spos += 4;
    write_u32(sb, spos, (uint32_t)off_soc_compat); spos += 4;
    strcpy((char *)(sb + spos), strings_block + off_soc_compat); spos += 17;
    spos = (spos + 3) & ~3;

    /* soc #address-cells */
    write_u32(sb, spos, FDT_PROP); spos += 4;
    write_u32(sb, spos, 4); spos += 4;
    write_u32(sb, spos, (uint32_t)off_addr_cells); spos += 4;
    uint32_t addr2 = fdt_bswap32(1);
    memcpy(sb + spos, &addr2, 4); spos += 4;

    /* soc #size-cells */
    write_u32(sb, spos, FDT_PROP); spos += 4;
    write_u32(sb, spos, 4); spos += 4;
    write_u32(sb, spos, (uint32_t)off_size_cells); spos += 4;
    uint32_t size1 = fdt_bswap32(1);
    memcpy(sb + spos, &size1, 4); spos += 4;

    /* soc ranges */
    write_u32(sb, spos, FDT_PROP); spos += 4;
    write_u32(sb, spos, 0); spos += 4;
    write_u32(sb, spos, 0xFFFF); spos += 4;

    /* serial@1000 */
    write_u32(sb, spos, FDT_BEGIN_NODE); spos += 4;
    strcpy((char *)(sb + spos), "serial@1000"); spos += 12;

    write_u32(sb, spos, FDT_PROP); spos += 4;
    write_u32(sb, spos, (uint32_t)(strlen(strings_block + off_serial_compat) + 1)); spos += 4;
    write_u32(sb, spos, (uint32_t)off_serial_compat); spos += 4;
    strcpy((char *)(sb + spos), strings_block + off_serial_compat); spos += 21;
    spos = (spos + 3) & ~3;

    write_u32(sb, spos, FDT_PROP); spos += 4;
    write_u32(sb, spos, 8); spos += 4;
    write_u32(sb, spos, (uint32_t)off_reg); spos += 4;
    uint32_t ser_addr = fdt_bswap32(0x1000);
    uint32_t ser_size = fdt_bswap32(0x100);
    memcpy(sb + spos, &ser_addr, 4); spos += 4;
    memcpy(sb + spos, &ser_size, 4); spos += 4;

    write_u32(sb, spos, FDT_PROP); spos += 4;
    write_u32(sb, spos, 4); spos += 4;
    write_u32(sb, spos, (uint32_t)off_interrupts); spos += 4;
    uint32_t irq0 = fdt_bswap32(17);
    memcpy(sb + spos, &irq0, 4); spos += 4;

    write_u32(sb, spos, FDT_PROP); spos += 4;
    write_u32(sb, spos, 4); spos += 4;
    write_u32(sb, spos, (uint32_t)off_clock_freq); spos += 4;
    uint32_t freq = fdt_bswap32(50000000);
    memcpy(sb + spos, &freq, 4); spos += 4;

    write_u32(sb, spos, FDT_PROP); spos += 4;
    write_u32(sb, spos, (uint32_t)(strlen(strings_block + off_okay) + 1)); spos += 4;
    write_u32(sb, spos, (uint32_t)off_status); spos += 4;
    strcpy((char *)(sb + spos), strings_block + off_okay); spos += 5;
    spos = (spos + 3) & ~3;

    write_u32(sb, spos, FDT_END_NODE); spos += 4;

    /* intc@2000 */
    write_u32(sb, spos, FDT_BEGIN_NODE); spos += 4;
    strcpy((char *)(sb + spos), "intc@2000"); spos += 10;

    write_u32(sb, spos, FDT_PROP); spos += 4;
    write_u32(sb, spos, (uint32_t)(strlen(strings_block + off_intc_compat) + 1)); spos += 4;
    write_u32(sb, spos, (uint32_t)off_intc_compat); spos += 4;
    strcpy((char *)(sb + spos), strings_block + off_intc_compat); spos += 19;
    spos = (spos + 3) & ~3;

    write_u32(sb, spos, FDT_PROP); spos += 4;
    write_u32(sb, spos, 4); spos += 4;
    write_u32(sb, spos, (uint32_t)off_reg); spos += 4;
    uint32_t int_addr = fdt_bswap32(0x2000);
    uint32_t int_size = fdt_bswap32(0x1000);
    memcpy(sb + spos, &int_addr, 4); spos += 4;
    memcpy(sb + spos, &int_size, 4); spos += 4;

    write_u32(sb, spos, FDT_PROP); spos += 4;
    write_u32(sb, spos, 4); spos += 4;
    write_u32(sb, spos, (uint32_t)off_phandle); spos += 4;
    uint32_t phandle = fdt_bswap32(1);
    memcpy(sb + spos, &phandle, 4); spos += 4;

    write_u32(sb, spos, FDT_END_NODE); spos += 4;

    write_u32(sb, spos, FDT_END_NODE); spos += 4; /* end soc */
    write_u32(sb, spos, FDT_END_NODE); spos += 4; /* end root */
    write_u32(sb, spos, FDT_END); spos += 4;

    size_t struct_size = spos;

    /* Build header */
    FDTHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic              = fdt_bswap32(FDT_MAGIC);
    hdr.totalsize          = fdt_bswap32((uint32_t)(off_dt_struct + struct_size + strings_aligned));
    hdr.off_dt_struct      = fdt_bswap32((uint32_t)off_dt_struct);
    hdr.off_dt_strings     = fdt_bswap32((uint32_t)(off_dt_struct + struct_size));
    hdr.off_mem_rsvmap     = fdt_bswap32((uint32_t)mem_rsvmap);
    hdr.version            = fdt_bswap32(FDT_SUPPORTED_VERSION);
    hdr.last_comp_version  = fdt_bswap32(FDT_COMPAT_VERSION);
    hdr.boot_cpuid_phys    = fdt_bswap32(0);
    hdr.size_dt_strings    = fdt_bswap32((uint32_t)strings_aligned);
    hdr.size_dt_struct     = fdt_bswap32((uint32_t)struct_size);

    memcpy(dtb, &hdr, sizeof(hdr));
    memset(dtb + mem_rsvmap, 0, 24);
    memcpy(dtb + off_dt_struct + struct_size, strings_block, strings_size);

    *out_size = off_dt_struct + struct_size + strings_aligned;
    return dtb;
}

int main(void)
{
    size_t dtb_size = 0;
    uint8_t *dtb = build_dtb(&dtb_size);
    if (!dtb) {
        printf("Failed to build DTB\n");
        return 1;
    }

    printf("=== mini-hardware-desc: FDT Parse Demo ===\n\n");

    FDTTree tree;
    if (!fdt_parse(&tree, dtb, dtb_size)) {
        printf("Failed to parse device tree\n");
        free(dtb);
        return 1;
    }

    printf("FDT Header:\n");
    printf("  Magic:           0x%08X\n", tree.header.magic);
    printf("  Total Size:      %u bytes\n", tree.header.totalsize);
    printf("  Version:         %u\n", tree.header.version);
    printf("  Compat Version:  %u\n", tree.header.last_comp_version);
    printf("  Boot CPU:        %u\n", tree.header.boot_cpuid_phys);
    printf("  Strings Size:    %u\n", tree.header.size_dt_strings);
    printf("  Struct Size:     %u\n", tree.header.size_dt_struct);
    printf("\n");

    printf("Device Tree Structure:\n\n");
    fdt_print_tree(tree.root, 0);

    printf("\n--- Find Node by Path ---\n");
    FDTNode *serial = fdt_find_node_by_path(&tree, "/soc/serial@1000");
    if (serial) {
        printf("Found: %s (props: %zu, children: %zu)\n",
               serial->name, serial->prop_count, serial->child_count);
        printf("Properties:\n");
        for (size_t i = 0; i < serial->prop_count; i++) {
            printf("  - %s\n", serial->properties[i].name);
        }
    } else {
        printf("Path /soc/serial@1000 not found\n");
    }

    printf("\n--- Find Compatible Nodes ---\n");
    FDTNode *compat = fdt_find_compatible(tree.root, "my-vendor,my-serial");
    if (compat) {
        printf("Found compatible node: %s\n", compat->name);
    }

    compat = fdt_find_compatible(tree.root, "my-vendor,my-intc");
    if (compat) {
        printf("Found compatible node: %s\n", compat->name);
    }

    fdt_free_tree(&tree);
    free(dtb);
    printf("\nDone.\n");
    return 0;
}
