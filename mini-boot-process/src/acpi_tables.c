#include "acpi_tables.h"
#include <stdio.h>
#include <string.h>

/* ============================================================================
 * ACPI Table Checksum Algorithm - L4/L5
 * ACPI spec v6.5 Section 5.2: all table bytes must sum to 0 mod 256.
 * checksum_byte = -(sum_of_all_other_bytes) mod 256
 * Complexity: O(n) single-pass, O(1) memory.
 * ============================================================================ */

uint8_t acpi_checksum(const void *data, uint32_t length)
{
    if (!data || length == 0) return 0;
    const uint8_t *bytes = (const uint8_t *)data;
    uint8_t sum = 0;
    for (uint32_t i = 0; i < length; i++) sum += bytes[i];
    return (uint8_t)(-sum);
}

bool acpi_validate_checksum(const void *table, uint32_t length)
{
    if (!table || length == 0) return false;
    const uint8_t *bytes = (const uint8_t *)table;
    uint8_t sum = 0;
    for (uint32_t i = 0; i < length; i++) sum += bytes[i];
    return (sum == 0);
}

/* ============================================================================
 * SDT Header Initialization - L1
 * Every ACPI table begins with a 36-byte header. This fills the common
 * fields. Caller sets table-specific body and final checksum byte.
 * ============================================================================ */

void acpi_sdt_header_init(ACPISDTHeader *header, const char signature[4],
                          uint32_t length, uint8_t revision,
                          const char *oem_id, const char *oem_table_id)
{
    if (!header) return;
    memset(header, 0, sizeof(ACPISDTHeader));
    memcpy(header->signature, signature, 4);
    header->length = length;
    header->revision = revision;
    if (oem_id) memcpy(header->oem_id, oem_id, 6);
    if (oem_table_id) memcpy(header->oem_table_id, oem_table_id, 8);
    header->oem_revision = 0x00000001;
    header->creator_id = 0x4D494E49;   /* "MINI" */
    header->creator_revision = 0x00000001;
    header->checksum = 0;
}

/* ============================================================================
 * RSDP Construction - L1/L4
 * The Root System Description Pointer is the ACPI entry point.
 * OS discovery: scan 0xE0000-0xFFFFF on 16-byte boundaries for "RSD PTR ".
 * On UEFI, provided via EFI System Table configuration tables.
 * L2: Discovery chain: RSDP -> RSDT/XSDT -> child tables (MADT,FADT,MCFG...).
 * ============================================================================ */

uint32_t acpi_rsdp_build(ACPIRSDP *rsdp, uint64_t rsdt_addr, uint64_t xsdt_addr,
                         const char *oem_id, uint8_t revision)
{
    if (!rsdp) return 0;
    memset(rsdp, 0, sizeof(ACPIRSDP));
    rsdp->signature = ACPI_RSDP_SIGNATURE;
    rsdp->revision = revision;
    if (oem_id) memcpy(rsdp->oem_id, oem_id, 6);
    else memcpy(rsdp->oem_id, "MINI  ", 6);

    if (revision == 0) {
        /* ACPI 1.0: 32-bit RSDT only, 20-byte RSDP */
        rsdp->rsdt_address = (uint32_t)rsdt_addr;
        rsdp->checksum = acpi_checksum(rsdp, ACPI_RSDP_V1_SIZE);
        return ACPI_RSDP_V1_SIZE;
    }
    /* ACPI 2.0+: 36-byte RSDP, both RSDT and XSDT */
    rsdp->length = ACPI_RSDP_V2_SIZE;
    rsdp->rsdt_address = (uint32_t)rsdt_addr;
    rsdp->xsdt_address = xsdt_addr;
    rsdp->checksum = acpi_checksum(rsdp, ACPI_RSDP_V1_SIZE);
    rsdp->extended_checksum = acpi_checksum(rsdp, ACPI_RSDP_V2_SIZE);
    return ACPI_RSDP_V2_SIZE;
}

/* ============================================================================
 * RSDT/XSDT Construction - L1/L3
 * Directory tables listing all child ACPI table addresses.
 * RSDT uses 32-bit pointers; XSDT uses 64-bit pointers.
 * Entry count = (header.length - 36) / sizeof(pointer).
 * L3: Flat array of physical addresses acting as a namespace directory.
 * ============================================================================ */

uint32_t acpi_rsdt_build(ACPIRSDT *rsdt, const uint32_t *table_addrs,
                         uint32_t count, const char *oem_id)
{
    if (!rsdt || !table_addrs || count == 0) return 0;
    if (count > ACPI_MAX_TABLES) count = ACPI_MAX_TABLES;
    uint32_t total_len = (uint32_t)(sizeof(ACPISDTHeader) + count * sizeof(uint32_t));
    acpi_sdt_header_init(&rsdt->header, ACPI_RSDT_SIGNATURE, total_len, 1, oem_id, "RSDT    ");
    for (uint32_t i = 0; i < count; i++) rsdt->table_entries[i] = table_addrs[i];
    rsdt->header.checksum = acpi_checksum(rsdt, total_len);
    return total_len;
}

uint32_t acpi_xsdt_build(ACPIXSDT *xsdt, const uint64_t *table_addrs,
                         uint32_t count, const char *oem_id)
{
    if (!xsdt || !table_addrs || count == 0) return 0;
    if (count > ACPI_MAX_TABLES) count = ACPI_MAX_TABLES;
    uint32_t total_len = (uint32_t)(sizeof(ACPISDTHeader) + count * sizeof(uint64_t));
    acpi_sdt_header_init(&xsdt->header, ACPI_XSDT_SIGNATURE, total_len, 1, oem_id, "XSDT    ");
    for (uint32_t i = 0; i < count; i++) xsdt->table_entries[i] = table_addrs[i];
    xsdt->header.checksum = acpi_checksum(xsdt, total_len);
    return total_len;
}

/* ============================================================================
 * Table Lookup by Signature - L5
 * Linear scan O(n) through directory entries. Production firmware would use
 * a hash table keyed on signature for O(1) lookup.
 * ============================================================================ */

int32_t acpi_find_table_rsdt(const ACPIRSDT *rsdt, const char signature[4])
{
    if (!rsdt || !signature) return -1;
    uint32_t n = (rsdt->header.length - sizeof(ACPISDTHeader)) / sizeof(uint32_t);
    for (uint32_t i = 0; i < n; i++) {
        const ACPISDTHeader *c = (const ACPISDTHeader *)(uintptr_t)rsdt->table_entries[i];
        if (memcmp(c->signature, signature, 4) == 0) return (int32_t)i;
    }
    return -1;
}

int32_t acpi_find_table_xsdt(const ACPIXSDT *xsdt, const char signature[4])
{
    if (!xsdt || !signature) return -1;
    uint32_t n = (xsdt->header.length - sizeof(ACPISDTHeader)) / sizeof(uint64_t);
    for (uint32_t i = 0; i < n; i++) {
        const ACPISDTHeader *c = (const ACPISDTHeader *)(uintptr_t)xsdt->table_entries[i];
        if (memcmp(c->signature, signature, 4) == 0) return (int32_t)i;
    }
    return -1;
}

/* ============================================================================
 * MADT Construction - L1/L2/L3
 * The Multiple APIC Description Table describes interrupt controller topology.
 * Entry types: LAPIC(0), IOAPIC(1), IntSrcOverride(2), LAPIC_NMI(4).
 * L2: OS uses MADT to initialize APIC interrupt routing for SMP operation.
 * L3: Serializes variable-length records into a flat byte buffer.
 * L4: ACPI 6.5 Section 5.2.12 - MADT format specification.
 * ============================================================================ */

uint32_t acpi_madt_build(uint8_t *buffer, uint32_t buffer_size,
                         const MADTLAPIC *lapics, uint32_t lapic_count,
                         const MADTIOAPIC *ioapics, uint32_t ioapic_count,
                         const MADTIntSrcOverride *overrides, uint32_t override_count,
                         const char *oem_id)
{
    if (!buffer || buffer_size < (sizeof(ACPISDTHeader) + 8)) return 0;
    if (lapic_count > MADT_MAX_ENTRIES) lapic_count = MADT_MAX_ENTRIES;
    if (ioapic_count > MADT_MAX_ENTRIES) ioapic_count = MADT_MAX_ENTRIES;
    if (override_count > MADT_MAX_ENTRIES) override_count = MADT_MAX_ENTRIES;

    uint32_t entry_size = 0;
    if (lapics)    entry_size += lapic_count * sizeof(MADTLAPIC);
    if (ioapics)   entry_size += ioapic_count * sizeof(MADTIOAPIC);
    if (overrides) entry_size += override_count * sizeof(MADTIntSrcOverride);

    uint32_t total_len = sizeof(ACPISDTHeader) + 8 + entry_size;
    if (total_len > buffer_size) return 0;

    ACPIMADT *madt = (ACPIMADT *)buffer;
    memset(madt, 0, total_len);
    acpi_sdt_header_init(&madt->header, ACPI_MADT_SIGNATURE, total_len, 4, oem_id, "MADT    ");
    madt->local_apic_address = 0xFEE00000;  /* Standard x86 LAPIC MMIO base */
    madt->flags = 0;  /* PIC not present: APIC-only system */

    uint8_t *ep = madt->entry_data;

    if (lapics) {
        for (uint32_t i = 0; i < lapic_count; i++) {
            MADTLAPIC *dst = (MADTLAPIC *)ep;
            dst->header.type   = MADT_ENTRY_LAPIC;
            dst->header.length = sizeof(MADTLAPIC);
            dst->acpi_processor_id = lapics[i].acpi_processor_id;
            dst->apic_id  = lapics[i].apic_id;
            dst->flags    = lapics[i].flags;
            ep += sizeof(MADTLAPIC);
        }
    }

    if (ioapics) {
        for (uint32_t i = 0; i < ioapic_count; i++) {
            MADTIOAPIC *dst = (MADTIOAPIC *)ep;
            dst->header.type   = MADT_ENTRY_IOAPIC;
            dst->header.length = sizeof(MADTIOAPIC);
            dst->ioapic_id      = ioapics[i].ioapic_id;
            dst->reserved       = 0;
            dst->ioapic_address = ioapics[i].ioapic_address;
            dst->gsi_base       = ioapics[i].gsi_base;
            ep += sizeof(MADTIOAPIC);
        }
    }

    if (overrides) {
        for (uint32_t i = 0; i < override_count; i++) {
            MADTIntSrcOverride *dst = (MADTIntSrcOverride *)ep;
            dst->header.type   = MADT_ENTRY_INT_SRC_OVERRIDE;
            dst->header.length = sizeof(MADTIntSrcOverride);
            dst->bus    = overrides[i].bus;
            dst->source = overrides[i].source;
            dst->gsi    = overrides[i].gsi;
            dst->flags  = overrides[i].flags;
            ep += sizeof(MADTIntSrcOverride);
        }
    }

    madt->header.checksum = acpi_checksum(madt, total_len);
    return total_len;
}

/* ============================================================================
 * FADT Construction - L1/L2
 * The Fixed ACPI Description Table describes hardware register blocks for
 * power management (PM1a, PM_TMR, GPE, Reset) and points to the DSDT.
 * L2: Enables OS-directed power management: S3/S4/S5 sleep, C-states, D-states.
 * L4: ACPI 6.5 Section 5.2.9 - FADT definition.
 * ============================================================================ */

uint32_t acpi_fadt_build(ACPIFADT *fadt, uint32_t dsdt_addr, uint16_t sci_int,
                         uint32_t pm1a_evt, uint32_t pm1a_cnt, uint32_t pm_tmr,
                         uint16_t boot_arch, uint32_t feature_flags,
                         uint8_t pm_profile, const char *oem_id)
{
    if (!fadt) return 0;
    memset(fadt, 0, sizeof(ACPIFADT));
    acpi_sdt_header_init(&fadt->header, ACPI_FADT_SIGNATURE, sizeof(ACPIFADT), 6, oem_id, "FACP    ");

    fadt->firmware_ctrl = 0;
    fadt->dsdt = dsdt_addr;
    fadt->preferred_pm_profile = pm_profile;
    fadt->sci_int = sci_int;
    fadt->smi_cmd = 0;   /* ACPI-only mode: no SMI */
    fadt->acpi_enable = 0;
    fadt->acpi_disable = 0;
    fadt->pm1a_evt_blk = pm1a_evt;
    fadt->pm1b_evt_blk = 0;
    fadt->pm1a_cnt_blk = pm1a_cnt;
    fadt->pm1b_cnt_blk = 0;
    fadt->pm2_cnt_blk = 0;
    fadt->pm_tmr_blk = pm_tmr;
    fadt->gpe0_blk = 0;
    fadt->gpe1_blk = 0;
    fadt->pm1_evt_len = 4;
    fadt->pm1_cnt_len = 2;
    fadt->pm2_cnt_len = 0;
    fadt->pm_tmr_len = 4;
    fadt->gpe0_blk_len = 0;
    fadt->gpe1_blk_len = 0;
    fadt->gpe1_base = 0;
    fadt->cst_cnt = 0;
    fadt->p_lvl2_lat = 101;   /* C2 latency: ~100 us */
    fadt->p_lvl3_lat = 1001;  /* C3 latency: ~1000 us */
    fadt->flush_size = 0;
    fadt->flush_stride = 0;
    fadt->duty_offset = 1;
    fadt->duty_width = 0;
    fadt->day_alrm = 0x0D;
    fadt->mon_alrm = 0x00;
    fadt->century = 0x32;
    fadt->iapc_boot_arch = boot_arch;
    fadt->flags = feature_flags;

    /* Reset Register: Generic Address Structure */
    memset(fadt->reset_reg, 0, 12);
    fadt->reset_reg[0] = 1;      /* Address Space: System I/O */
    fadt->reset_reg[1] = 8;
    fadt->reset_reg[2] = 0;
    fadt->reset_reg[4] = 0xF9;   /* 0x0CF9 = standard reset port */
    fadt->reset_reg[5] = 0x0C;
    fadt->reset_value  = 0x06;
    fadt->fadt_minor_version = 0;
    fadt->x_dsdt = (uint64_t)dsdt_addr;

    fadt->header.checksum = acpi_checksum(fadt, sizeof(ACPIFADT));
    return sizeof(ACPIFADT);
}

/* ============================================================================
 * MCFG Construction - L1/L4
 * PCIe ECAM maps PCI config space into MMIO. Each segment gets its own window.
 * L4: PCI Firmware Specification 3.0, Section 4.1.
 * ============================================================================ */

uint32_t acpi_mcfg_build(ACPIMCFG *mcfg, const MCFGEntry *segments,
                         uint32_t count, const char *oem_id)
{
    if (!mcfg || !segments || count == 0) return 0;
    if (count > MCFG_MAX_SEGMENTS) count = MCFG_MAX_SEGMENTS;
    uint32_t total_len = (uint32_t)(sizeof(ACPISDTHeader) + 8 + count * sizeof(MCFGEntry));
    acpi_sdt_header_init(&mcfg->header, ACPI_MCFG_SIGNATURE, total_len, 1, oem_id, "MCFG    ");
    mcfg->reserved = 0;
    for (uint32_t i = 0; i < count; i++) {
        mcfg->entries[i].base_address      = segments[i].base_address;
        mcfg->entries[i].pci_segment_group = segments[i].pci_segment_group;
        mcfg->entries[i].start_bus         = segments[i].start_bus;
        mcfg->entries[i].end_bus           = segments[i].end_bus;
        mcfg->entries[i].reserved          = 0;
    }
    mcfg->header.checksum = acpi_checksum(mcfg, total_len);
    return total_len;
}

/* ============================================================================
 * HPET Construction - L1/L7
 * High Precision Event Timer: >10MHz resolution timer for multimedia.
 * L7: Linux/Windows use HPET as a reliable clock source when TSC is unstable.
 * ============================================================================ */

uint32_t acpi_hpet_build(ACPIHPET *hpet, uint64_t hpet_addr, uint8_t hpet_num,
                         const char *oem_id)
{
    if (!hpet) return 0;
    memset(hpet, 0, sizeof(ACPIHPET));
    acpi_sdt_header_init(&hpet->header, ACPI_HPET_SIGNATURE, sizeof(ACPIHPET), 1, oem_id, "HPET    ");
    hpet->event_timer_block_id = HPET_EVENT_TIMER_BLOCK_ID;
    hpet->address_space_id     = 0;  /* System Memory */
    hpet->register_bit_width   = 0;
    hpet->register_bit_offset  = 0;
    hpet->address       = hpet_addr;
    hpet->hpet_number   = hpet_num;
    hpet->minimum_tick  = 0;
    hpet->page_protection = 0;
    hpet->header.checksum = acpi_checksum(hpet, sizeof(ACPIHPET));
    return sizeof(ACPIHPET);
}

/* ============================================================================
 * BGRT Construction - L7 Application
 * Boot Graphics Resource Table describes OEM logo displayed during POST.
 * L7: Windows 8+ and systemd-boot use BGRT for seamless logo transition
 *     from firmware POST through OS boot (flicker-free boot experience).
 * ============================================================================ */

uint32_t acpi_bgrt_build(ACPIBGRT *bgrt, uint64_t image_addr,
                         uint32_t offset_x, uint32_t offset_y,
                         uint8_t image_type, const char *oem_id)
{
    if (!bgrt) return 0;
    memset(bgrt, 0, sizeof(ACPIBGRT));
    acpi_sdt_header_init(&bgrt->header, ACPI_BGRT_SIGNATURE, sizeof(ACPIBGRT), 1, oem_id, "BGRT    ");
    bgrt->version       = 1;
    bgrt->status        = 1;  /* valid, displayed */
    bgrt->image_type    = image_type;
    bgrt->image_address = image_addr;
    bgrt->image_offset_x = offset_x;
    bgrt->image_offset_y = offset_y;
    bgrt->header.checksum = acpi_checksum(bgrt, sizeof(ACPIBGRT));
    return sizeof(ACPIBGRT);
}

/* ============================================================================
 * Debug Print Utilities
 * ============================================================================ */

void acpi_print_header(const ACPISDTHeader *header)
{
    if (!header) return;
    printf("  ACPI Table: %.4s  Rev=%u  Len=%-6u  OEM=%-6.6s  TblID=%-8.8s  Checksum=0x%02X\n",
           header->signature, header->revision, header->length,
           header->oem_id, header->oem_table_id, header->checksum);
}
