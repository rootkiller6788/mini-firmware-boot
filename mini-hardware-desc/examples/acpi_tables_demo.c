#include "acpi_tables.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static uint8_t *build_acpi_bios(size_t *out_size)
{
    size_t total_size = 0x10000; /* 64KB BIOS region */
    uint8_t *bios = calloc(1, total_size);
    if (!bios) return NULL;

    size_t pos = 0;

    /* --- RSDP at known location (0xE0000 typically) --- */
    size_t rsdp_offset = 0xE000;
    RSDP *rsdp = (RSDP *)(bios + rsdp_offset);
    memcpy(rsdp->v1.signature, ACPI_RSDP_SIGNATURE, ACPI_RSDP_SIGNATURE_LEN);
    rsdp->v1.checksum = 0;
    memcpy(rsdp->v1.oem_id, "NANO  ", 6);
    rsdp->v1.revision = 2;
    rsdp->v1.rsdt_address = 0; /* use XSDT only */
    rsdp->length = ACPI_RSDP_V2_LEN;
    rsdp->xsdt_address = 0xB000; /* XSDT location */
    rsdp->extended_checksum = 0;
    memset(rsdp->reserved, 0, 3);

    /* --- XSDT at 0xB000 --- */
    size_t xsdt_offset = 0xB000;
    ACPISDTHeader *xsdt_hdr = (ACPISDTHeader *)(bios + xsdt_offset);
    memcpy(xsdt_hdr->signature, "XSDT", 4);
    xsdt_hdr->revision = 1;
    memcpy(xsdt_hdr->oem_id, "NANO  ", 6);
    memcpy(xsdt_hdr->oem_table_id, "NANOXSDT", 8);
    xsdt_hdr->oem_revision = 1;
    xsdt_hdr->creator_id = 0x4E414E4F;
    xsdt_hdr->creator_revision = 1;
    /* 3 entries: FADT, MADT, MCFG */
    uint32_t entry_count = 3;
    xsdt_hdr->length = sizeof(ACPISDTHeader) + (uint32_t)(entry_count * sizeof(uint64_t));
    xsdt_hdr->checksum = 0;

    uint64_t *xsdt_entries = (uint64_t *)(bios + xsdt_offset + sizeof(ACPISDTHeader));
    xsdt_entries[0] = 0xC000; /* FADT */
    xsdt_entries[1] = 0xD000; /* MADT */
    xsdt_entries[2] = 0xD800; /* MCFG */

    /* --- FADT at 0xC000 --- */
    size_t fadt_offset = 0xC000;
    uint8_t *fadt = bios + fadt_offset;
    ACPISDTHeader *fadt_hdr = (ACPISDTHeader *)fadt;
    memcpy(fadt_hdr->signature, "FACP", 4);
    fadt_hdr->length = 0x114; /* FADT v6.3: 276 bytes */
    fadt_hdr->revision = 6;
    memcpy(fadt_hdr->oem_id, "NANO  ", 6);
    memcpy(fadt_hdr->oem_table_id, "NANOFADT", 8);
    fadt_hdr->oem_revision = 1;
    fadt_hdr->creator_id = 0x4E414E4F;
    fadt_hdr->creator_revision = 1;
    fadt_hdr->checksum = 0;
    /* Fill in key FADT fields */
    *(uint32_t *)(fadt + 36) = 0x00000809; /* SCI_INT (IRQ 9) */
    *(uint32_t *)(fadt + 40) = 0x00008000; /* SMI_CMD */
    *(uint32_t *)(fadt + 48) = 0x0400; /* PM1a_EVT_BLK */
    *(uint32_t *)(fadt + 52) = 0x0800; /* PM1b_EVT_BLK (0 if not used) */
    *(uint32_t *)(fadt + 56) = 0x0500; /* PM1a_CNT_BLK */
    *(uint32_t *)(fadt + 64) = 0x0600; /* PM_TMR_BLK */
    *(uint32_t *)(fadt + 129) = 0x0810; /* RESET_REG.Address */
    *(uint8_t  *)(fadt + 132) = 0x01; /* RESET_REG.AddressSpace */
    *(uint8_t  *)(fadt + 134) = 0x08; /* RESET_REG.AccessSize */
    *(uint8_t  *)(fadt + 136) = 0x06; /* RESET_VALUE */

    /* --- MADT at 0xD000 --- */
    size_t madt_offset = 0xD000;
    uint8_t *madt = bios + madt_offset;
    ACPISDTHeader *madt_hdr = (ACPISDTHeader *)madt;
    memcpy(madt_hdr->signature, "APIC", 4);
    madt_hdr->revision = 4;
    memcpy(madt_hdr->oem_id, "NANO  ", 6);
    memcpy(madt_hdr->oem_table_id, "NANOMADT", 8);
    madt_hdr->oem_revision = 1;
    madt_hdr->creator_id = 0x4E414E4F;
    madt_hdr->creator_revision = 1;
    madt_hdr->checksum = 0;

    size_t madt_pos = 44;
    *(uint32_t *)(madt + 36) = 0xFEE00000; /* Local APIC Address */
    *(uint32_t *)(madt + 40) = 0x00000001; /* Flags: dual 8259 */

    /* Processor Local APIC (type 0) x4 */
    for (int cpu = 0; cpu < 4; cpu++) {
        madt[madt_pos + 0] = 0x00; /* type: Processor Local APIC */
        madt[madt_pos + 1] = 8;    /* length */
        madt[madt_pos + 2] = (uint8_t)cpu; /* ACPI Processor ID */
        madt[madt_pos + 3] = (uint8_t)cpu; /* APIC ID */
        *(uint32_t *)(madt + madt_pos + 4) = 0x00000001; /* flags: enabled */
        madt_pos += 8;
    }

    /* I/O APIC (type 1) */
    madt[madt_pos + 0] = 0x01; /* type */
    madt[madt_pos + 1] = 12;   /* length */
    madt[madt_pos + 2] = 1;    /* I/O APIC ID */
    madt[madt_pos + 3] = 0;    /* reserved */
    *(uint32_t *)(madt + madt_pos + 4) = 0xFEC00000; /* I/O APIC address */
    *(uint32_t *)(madt + madt_pos + 8) = 0; /* global system interrupt base */
    madt_pos += 12;

    /* Interrupt Source Override (type 2) for ISA IRQ0 -> ioapic intin 2 */
    madt[madt_pos + 0] = 0x02;
    madt[madt_pos + 1] = 10;
    madt[madt_pos + 2] = 0;   /* bus: ISA */
    madt[madt_pos + 3] = 0;   /* source: IRQ0 */
    *(uint32_t *)(madt + madt_pos + 4) = 2; /* gsi */
    *(uint16_t *)(madt + madt_pos + 8) = 0; /* flags: conforming */
    madt_pos += 10;

    madt_hdr->length = (uint32_t)madt_pos;

    /* --- MCFG at 0xD800 --- */
    size_t mcfg_offset = 0xD800;
    uint8_t *mcfg = bios + mcfg_offset;
    ACPISDTHeader *mcfg_hdr = (ACPISDTHeader *)mcfg;
    memcpy(mcfg_hdr->signature, "MCFG", 4);
    mcfg_hdr->revision = 1;
    memcpy(mcfg_hdr->oem_id, "NANO  ", 6);
    memcpy(mcfg_hdr->oem_table_id, "NANOMCFG", 8);
    mcfg_hdr->oem_revision = 1;
    mcfg_hdr->creator_id = 0x4E414E4F;
    mcfg_hdr->creator_revision = 1;
    mcfg_hdr->checksum = 0;
    mcfg_hdr->length = sizeof(ACPISDTHeader) + 8 + 16; /* header + reserved + 1 entry */
    memset(mcfg + sizeof(ACPISDTHeader), 0, 8); /* reserved */
    /* Segment: 0, Bus Start: 0, Bus End: 255, Address: 0xE0000000 */
    *(uint64_t *)(mcfg + sizeof(ACPISDTHeader) + 8) = 0xE0000000;
    *(uint16_t *)(mcfg + sizeof(ACPISDTHeader) + 16) = 0;
    *(uint8_t  *)(mcfg + sizeof(ACPISDTHeader) + 18) = 0;    /* PCI start bus */
    *(uint8_t  *)(mcfg + sizeof(ACPISDTHeader) + 19) = 0xFF; /* PCI end bus */
    memset(mcfg + sizeof(ACPISDTHeader) + 20, 0, 4); /* reserved */

    /* Compute checksums */
    rsdp->v1.checksum = 0;
    uint8_t sum = 0;
    for (size_t i = 0; i < 20; i++) sum += bios[rsdp_offset + i];
    rsdp->v1.checksum = (uint8_t)(256 - sum);

    rsdp->extended_checksum = 0;
    sum = 0;
    for (size_t i = 0; i < (size_t)rsdp->length; i++) sum += bios[rsdp_offset + i];
    rsdp->extended_checksum = (uint8_t)(256 - sum);

    /* XSDT checksum */
    sum = 0;
    for (uint32_t i = 0; i < xsdt_hdr->length; i++) sum += bios[xsdt_offset + i];
    xsdt_hdr->checksum = (uint8_t)(256 - sum);

    /* FADT checksum */
    sum = 0;
    for (uint32_t i = 0; i < fadt_hdr->length; i++) sum += bios[fadt_offset + i];
    fadt_hdr->checksum = (uint8_t)(256 - sum);

    /* MADT checksum */
    sum = 0;
    for (uint32_t i = 0; i < madt_hdr->length; i++) sum += bios[madt_offset + i];
    madt_hdr->checksum = (uint8_t)(256 - sum);

    /* MCFG checksum */
    sum = 0;
    for (uint32_t i = 0; i < mcfg_hdr->length; i++) sum += bios[mcfg_offset + i];
    mcfg_hdr->checksum = (uint8_t)(256 - sum);

    *out_size = total_size;
    return bios;
}

int main(void)
{
    printf("=== mini-hardware-desc: ACPI Tables Demo ===\n\n");

    size_t bios_size = 0;
    uint8_t *bios = build_acpi_bios(&bios_size);
    if (!bios) {
        printf("Failed to build ACPI BIOS\n");
        return 1;
    }

    /* Find RSDP */
    ACPITableList list;
    if (!acpi_find_rsdp(&list, bios, bios_size)) {
        printf("Failed to find RSDP\n");
        free(bios);
        return 1;
    }
    printf("RSDP found at offset 0x%05zX (v%u)\n\n",
           (size_t)((uint8_t *)&list.rsdp - bios),
           list.is_v2 ? 2 : 1);

    /* Parse XSDT */
    if (!acpi_parse_xsdt(&list, bios)) {
        printf("Failed to parse XSDT\n");
        free(bios);
        return 1;
    }

    /* Print all tables */
    acpi_print_tables(&list);

    /* Validate checksums */
    printf("\n--- Checksum Validation ---\n");
    for (size_t i = 0; i < list.table_count; i++) {
        const ACPITableEntry *e = &list.tables[i];
        const uint8_t *table = bios + e->address;
        bool valid = acpi_validate_checksum(table, e->length);
        printf("  %-4s (0x%08X): %s\n",
               e->signature, e->address,
               valid ? "VALID" : "INVALID");
    }

    /* Find specific tables */
    printf("\n--- Find Specific Tables ---\n");
    ACPITableEntry entry;
    if (acpi_find_table(&list, ACPI_TABLE_FADT, &entry)) {
        printf("FADT: addr=0x%08X len=%u rev=%u\n",
               entry.address, entry.length, entry.revision);
        /* Dump raw header */
        ACPISDTHeader *fadt = (ACPISDTHeader *)(bios + entry.address);
        printf("  OEM: %.6s, TableID: %.8s\n",
               fadt->oem_id, fadt->oem_table_id);
    }

    if (acpi_find_table(&list, ACPI_TABLE_MADT, &entry)) {
        printf("MADT: addr=0x%08X len=%u rev=%u\n",
               entry.address, entry.length, entry.revision);
    }

    if (acpi_find_table(&list, ACPI_TABLE_MCFG, &entry)) {
        printf("MCFG: addr=0x%08X len=%u rev=%u\n",
               entry.address, entry.length, entry.revision);
    }

    printf("\nDone.\n");
    free(bios);
    return 0;
}
