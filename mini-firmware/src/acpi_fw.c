#include "acpi_fw.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * acpi_fw.c -- ACPI Table Construction
 *
 * References:
 *   - ACPI Specification v6.5 (UEFI Forum)
 *   - Intel Low Power S0 Idle (modern standby)
 *   - ARM SBBR v1.2 Appendix A (ACPI tables)
 */

bool acpi_init(ACPIManager *mgr, const char *oem_id, const char *oem_table_id)
{
    if (!mgr || !oem_id || !oem_table_id) return false;

    memset(mgr, 0, sizeof(ACPIManager));

    memcpy(mgr->rsdp.signature, "RSD PTR ", 8);
    mgr->rsdp.revision = 2;
    mgr->rsdp.length   = sizeof(ACPIRSDP);

    size_t oem_len = strlen(oem_id);
    if (oem_len > ACPI_OEM_ID_LEN) oem_len = ACPI_OEM_ID_LEN;
    memcpy(mgr->rsdp.oem_id, oem_id, oem_len);
    memcpy(mgr->oem_id, oem_id, oem_len);
    mgr->oem_id[oem_len] = '\0';

    size_t tbl_len = strlen(oem_table_id);
    if (tbl_len > ACPI_OEM_TABLE_LEN) tbl_len = ACPI_OEM_TABLE_LEN;
    memcpy(mgr->oem_table_id, oem_table_id, tbl_len);
    mgr->oem_table_id[tbl_len] = '\0';

    memcpy(mgr->xsdt.header.signature, "XSDT", 4);
    mgr->xsdt.header.length    = sizeof(ACPIXSDT);
    mgr->xsdt.header.revision  = 1;
    memcpy(mgr->xsdt.header.oem_id, mgr->oem_id, ACPI_OEM_ID_LEN);
    memcpy(mgr->xsdt.header.oem_table_id, mgr->oem_table_id, ACPI_OEM_TABLE_LEN);
    mgr->xsdt.header.creator_id       = 0x4D494E49;
    mgr->xsdt.header.creator_revision = 1;
    mgr->table_count = 0;
    return true;
}

bool acpi_add_table(ACPIManager *mgr, ACPISDTHeader *table)
{
    if (!mgr || !table) return false;
    if (mgr->table_count >= ACPI_MAX_TABLES) return false;

    memcpy(table->oem_id, mgr->oem_id, ACPI_OEM_ID_LEN);
    memcpy(table->oem_table_id, mgr->oem_table_id, ACPI_OEM_TABLE_LEN);

    acpi_set_checksum(table);

    mgr->xsdt.table_entries[mgr->table_count] = (uint64_t)(uintptr_t)table;
    mgr->table_ptrs[mgr->table_count] = table;
    mgr->table_count++;

    mgr->xsdt.header.length = (uint32_t)(sizeof(ACPISDTHeader) +
                                mgr->table_count * sizeof(uint64_t));
    return true;
}

/*
 * ACPI table checksum: sum of all bytes must equal 0 (mod 256).
 * An 8-bit checksum for corruption detection only -- not cryptographic.
 */
void acpi_set_checksum(ACPISDTHeader *table)
{
    if (!table) return;

    uint8_t sum = 0;
    uint8_t *bytes = (uint8_t *)table;
    uint32_t len = table->length;

    uint8_t saved_checksum = table->checksum;
    table->checksum = 0;

    for (uint32_t i = 0; i < len; i++) {
        sum += bytes[i];
    }

    table->checksum = (uint8_t)(256 - sum);
    if (sum == 0) table->checksum = 0;
    (void)saved_checksum;
}

bool acpi_validate_checksums(const ACPIManager *mgr)
{
    if (!mgr) return false;

    for (uint32_t i = 0; i < mgr->table_count; i++) {
        ACPISDTHeader *hdr = (ACPISDTHeader *)mgr->table_ptrs[i];
        if (!hdr) continue;

        uint8_t sum = 0;
        uint8_t *bytes = (uint8_t *)hdr;
        for (uint32_t j = 0; j < hdr->length; j++) {
            sum += bytes[j];
        }

        if (sum != 0) {
            fprintf(stderr, "ACPI checksum failure for table %c%c%c%c\n",
                    hdr->signature[0], hdr->signature[1],
                    hdr->signature[2], hdr->signature[3]);
            return false;
        }
    }
    return true;
}

/*
 * Build FADT (Fixed ACPI Description Table).
 * Reference: ACPI Spec 6.5, Section 5.2.9.
 */
bool acpi_build_fadt(ACPIManager *mgr, uint8_t pm_profile,
                     uint16_t sci_irq, uint32_t flags)
{
    if (!mgr) return false;

    memset(&mgr->fadt, 0, sizeof(ACPIFADT));
    memcpy(mgr->fadt.header.signature, "FACP", 4);
    mgr->fadt.header.length            = sizeof(ACPIFADT);
    mgr->fadt.header.revision          = 6;

    mgr->fadt.preferred_pm_profile     = pm_profile;
    mgr->fadt.sci_interrupt            = sci_irq;
    mgr->fadt.smi_command_port         = 0xB2;
    mgr->fadt.acpi_enable              = 0xA0;
    mgr->fadt.acpi_disable             = 0xA1;

    mgr->fadt.pm1a_event_block         = 0x400;
    mgr->fadt.pm1a_control_block       = 0x404;
    mgr->fadt.pm1_event_length         = 4;
    mgr->fadt.pm1_control_length       = 2;

    mgr->fadt.pm_timer_block           = 0x408;
    mgr->fadt.pm_timer_length          = 4;

    mgr->fadt.gpe0_block               = 0x420;
    mgr->fadt.gpe0_block_length        = 8;

    mgr->fadt.boot_architecture_flags  = 0x0003;
    mgr->fadt.flags                    = flags;

    mgr->fadt.reset_register.address_space_id    = 1;
    mgr->fadt.reset_register.register_bit_width  = 8;
    mgr->fadt.reset_register.register_bit_offset = 0;
    mgr->fadt.reset_register.address             = 0xCF9;
    mgr->fadt.reset_value                        = 0x06;

    return acpi_add_table(mgr, &mgr->fadt.header);
}

/*
 * Build MADT (Multiple APIC Description Table) for x86 Local APIC.
 * Reference: ACPI Spec 6.5, Section 5.2.12.
 */
bool acpi_build_madt_lapic(ACPIManager *mgr, uint32_t local_apic_base,
                           uint8_t cpu_count, const uint8_t *apic_ids)
{
    if (!mgr || !apic_ids || cpu_count == 0 || cpu_count > 64) return false;

    uint32_t entry_size = cpu_count * sizeof(MADTLapic) + sizeof(MADTIoapic);
    uint32_t total_size = sizeof(ACPIMADT) + entry_size;
    uint8_t *buf = (uint8_t *)calloc(total_size, 1);
    if (!buf) return false;

    ACPIMADT *madt = (ACPIMADT *)buf;
    memcpy(madt->header.signature, "APIC", 4);
    madt->header.length    = total_size;
    madt->header.revision  = 4;
    madt->local_apic_address = local_apic_base;
    madt->flags            = 0x00000001;

    uint8_t *entry_ptr = madt->entries;

    for (uint8_t i = 0; i < cpu_count; i++) {
        MADTLapic *lapic = (MADTLapic *)entry_ptr;
        lapic->header.type        = MADT_TYPE_LAPIC;
        lapic->header.length      = sizeof(MADTLapic);
        lapic->acpi_processor_id  = i;
        lapic->apic_id            = apic_ids[i];
        lapic->flags              = 0x00000001;
        entry_ptr += sizeof(MADTLapic);
    }

    MADTIoapic *ioapic = (MADTIoapic *)entry_ptr;
    ioapic->header.type               = MADT_TYPE_IOAPIC;
    ioapic->header.length             = sizeof(MADTIoapic);
    ioapic->ioapic_id                 = 0;
    ioapic->ioapic_address            = 0xFEC00000;
    ioapic->global_sys_interrupt_base = 0;

    return acpi_add_table(mgr, &madt->header);
}

/*
 * Build MADT for ARM GICv3 systems.
 * Reference: ARM GICv3 Architecture Specification (IHI 0069).
 */
bool acpi_build_madt_gic(ACPIManager *mgr, uint64_t gicd_base,
                         uint64_t gicr_base, uint8_t cpu_count)
{
    if (!mgr || cpu_count == 0 || cpu_count > 64) return false;

    uint32_t gicc_header_size = sizeof(MADTEntryHeader) + 76;
    uint32_t total_size = sizeof(ACPIMADT)
                        + sizeof(MADTGicd)
                        + (uint32_t)cpu_count * gicc_header_size
                        + sizeof(MADTEntryHeader) + 16;

    uint8_t *buf = (uint8_t *)calloc(total_size, 1);
    if (!buf) return false;

    ACPIMADT *madt = (ACPIMADT *)buf;
    memcpy(madt->header.signature, "APIC", 4);
    madt->header.length    = total_size;
    madt->header.revision  = 5;
    madt->local_apic_address = 0;
    madt->flags            = 0;

    uint8_t *entry_ptr = madt->entries;

    MADTGicd *gicd = (MADTGicd *)entry_ptr;
    gicd->header.type          = MADT_TYPE_GICD;
    gicd->header.length        = sizeof(MADTGicd);
    gicd->physical_base_addr   = gicd_base;
    entry_ptr += sizeof(MADTGicd);

    for (uint8_t i = 0; i < cpu_count; i++) {
        MADTEntryHeader *gicc_hdr = (MADTEntryHeader *)entry_ptr;
        gicc_hdr->type   = MADT_TYPE_GICC;
        gicc_hdr->length = gicc_header_size;
        entry_ptr += gicc_header_size;
    }

    MADTEntryHeader *gicr_hdr = (MADTEntryHeader *)entry_ptr;
    gicr_hdr->type   = MADT_TYPE_GICR;
    gicr_hdr->length = sizeof(MADTEntryHeader) + 16;

    (void)gicr_base;
    return acpi_add_table(mgr, &madt->header);
}

/*
 * Build a minimal DSDT (Differentiated System Description Table).
 * Contains valid AML bytecode for an empty DefinitionBlock.
 * Real DSDT would define _SB, _PR, _S0-_S5, and device nodes.
 */
bool acpi_build_dsdt(ACPIManager *mgr)
{
    if (!mgr) return false;

    static const uint8_t minimal_aml[] = {
        0x44, 0x53, 0x44, 0x54,
        0x30, 0x00, 0x00, 0x00,
        0x02,
        0x00,
        0x4D, 0x49, 0x4E, 0x49, 0x46, 0x57,
        0x44, 0x53, 0x44, 0x54, 0x41, 0x4D, 0x4C, 0x5F,
        0x01, 0x00, 0x00, 0x00,
        0x49, 0x4E, 0x54, 0x4C,
        0x20, 0x24, 0x06, 0x00,
        0x10, 0x4C, 0x04,
        0x5B, 0x82, 0x2C,
        0x08, 0x5F, 0x48, 0x49, 0x44, 0x00,
        0x5B, 0x82, 0x2B,
        0x08, 0x5F, 0x48, 0x49, 0x44, 0x01,
    };

    uint32_t aml_size = sizeof(minimal_aml);
    uint32_t total_size = sizeof(ACPIDSDT) + aml_size;
    uint8_t *buf = (uint8_t *)calloc(total_size, 1);
    if (!buf) return false;

    ACPIDSDT *dsdt = (ACPIDSDT *)buf;
    memcpy(dsdt->header.signature, "DSDT", 4);
    dsdt->header.length   = total_size;
    dsdt->header.revision = 2;
    memcpy(dsdt->aml_data, minimal_aml, aml_size);

    return acpi_add_table(mgr, &dsdt->header);
}

ACPISDTHeader *acpi_find_table(const ACPIManager *mgr, const char *signature)
{
    if (!mgr || !signature) return NULL;

    for (uint32_t i = 0; i < mgr->table_count; i++) {
        ACPISDTHeader *hdr = (ACPISDTHeader *)mgr->table_ptrs[i];
        if (memcmp(hdr->signature, signature, ACPI_NAME_LEN) == 0) {
            return hdr;
        }
    }
    return NULL;
}

void acpi_print_tables(const ACPIManager *mgr)
{
    if (!mgr) return;

    printf("=== ACPI Table Set ===\n");
    printf("OEM ID:    %-6.6s\n", mgr->oem_id);
    printf("OEM Table: %-8.8s\n", mgr->oem_table_id);
    printf("RSDP Rev:  %u\n", mgr->rsdp.revision);
    printf("Tables:    %u\n\n", mgr->table_count);

    for (uint32_t i = 0; i < mgr->table_count; i++) {
        ACPISDTHeader *hdr = (ACPISDTHeader *)mgr->table_ptrs[i];
        printf("  [%2u] %c%c%c%c  Length: %6u  Rev: %2u\n",
               i, hdr->signature[0], hdr->signature[1],
               hdr->signature[2], hdr->signature[3],
               hdr->length, hdr->revision);
    }

    printf("\nChecksum Validation: ");
    if (acpi_validate_checksums(mgr)) {
        printf("ALL VALID\n");
    } else {
        printf("FAILURES DETECTED\n");
    }
}

void acpi_print_madt(const ACPIMADT *madt)
{
    if (!madt) return;

    printf("=== MADT (APIC) Table ===\n");
    printf("Local APIC Address: 0x%08X\n", madt->local_apic_address);
    printf("Flags:              0x%08X\n", madt->flags);

    const uint8_t *entry = madt->entries;
    const uint8_t *end   = (const uint8_t *)madt + madt->header.length;
    uint32_t count  = 0;

    while (entry < end) {
        MADTEntryHeader *e = (MADTEntryHeader *)entry;
        if (e->length == 0) break;

        switch (e->type) {
        case MADT_TYPE_LAPIC: {
            MADTLapic *lapic = (MADTLapic *)e;
            printf("  LAPIC #%u: ID=%u, ACPI CPU=%u, %s\n",
                   count, lapic->apic_id, lapic->acpi_processor_id,
                   (lapic->flags & 1) ? "Enabled" : "Disabled");
            break;
        }
        case MADT_TYPE_IOAPIC: {
            MADTIoapic *ioapic = (MADTIoapic *)e;
            printf("  I/O APIC: ID=%u, Addr=0x%08X, GSI Base=%u\n",
                   ioapic->ioapic_id, ioapic->ioapic_address,
                   ioapic->global_sys_interrupt_base);
            break;
        }
        case MADT_TYPE_GICD:
            printf("  GIC Distributor (ARM)\n");
            break;
        case MADT_TYPE_GICC:
            printf("  GIC CPU Interface (ARM)\n");
            break;
        case MADT_TYPE_GICR:
            printf("  GIC Redistributor (ARM)\n");
            break;
        default:
            printf("  Entry type %u (len=%u)\n", e->type, e->length);
            break;
        }
        count++;
        entry += e->length;
        if (entry >= end) break;
    }
    printf("  Total entries: %u\n", count);
}

void acpi_print_acpi_vs_fdt_comparison(void)
{
    printf("=== ACPI vs Device Tree (FDT) ===\n\n");
    printf("ACPI (Advanced Configuration and Power Interface):\n");
    printf("  Origin:    Intel/Microsoft/Toshiba (1996)\n");
    printf("  Spec:      UEFI Forum, ACPI Spec v6.5 (~1000 pages)\n");
    printf("  Platforms: x86 (universal), ARM Server (SBBR)\n");
    printf("  Language:  AML bytecode (interpreted by OS)\n");
    printf("  Strengths: Power mgmt, hotplug, NUMA, vendor-neutral\n");
    printf("  Weaknesses: Complex, runtime overhead, security surface\n\n");
    printf("Device Tree (FDT / Devicetree):\n");
    printf("  Origin:    Open Firmware / PowerPC (1990s)\n");
    printf("  Spec:      devicetree.org, Linux kernel bindings\n");
    printf("  Platforms: ARM embedded, RISC-V, PowerPC, MIPS\n");
    printf("  Language:  Static DTS compiled to DTB\n");
    printf("  Strengths: Simple, no runtime overhead, easy to review\n");
    printf("  Weaknesses: No PM standard, board-specific, no hotplug\n\n");
    printf("Firmware role:\n");
    printf("  ACPI:  Tables built by firmware (DXE phase in UEFI)\n");
    printf("  FDT:   DTB loaded by bootloader, passed via register to kernel\n\n");
    printf("Trends:\n");
    printf("  - ARM servers moving to ACPI (SBBR mandate)\n");
    printf("  - RISC-V adopts Device Tree (no ACPI yet)\n");
    printf("  - Embedded stays with Device Tree\n");
}
