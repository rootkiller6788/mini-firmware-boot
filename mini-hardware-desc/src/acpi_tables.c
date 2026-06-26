#include "acpi_tables.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ===== Checksum Validation (L4: ACPI 6.5 §5.2.6 - additive checksum) =====
 *
 * All ACPI tables use an 8-bit additive checksum: the sum of all bytes
 * in the table modulo 256 must equal zero. This is a simple integrity
 * check, not a cryptographic guarantee.
 */

bool acpi_validate_checksum(const uint8_t *table, uint32_t length)
{
    if (!table || length == 0) return false;
    uint8_t sum = 0;
    for (uint32_t i = 0; i < length; i++) sum += table[i];
    return sum == 0;
}

/* ===== RSDP Discovery (L2: ACPI 6.5 §5.2.5.1 - Finding RSDP) =====
 *
 * RSDP is found by scanning the BIOS Extended Data Area (0xE0000-0xFFFFF)
 * on 16-byte boundaries for the "RSD PTR " signature. On UEFI systems,
 * it's provided via the EFI System Table.
 */

static bool rsdp_validate_v1(const RSDPV1 *rsdp)
{
    if (memcmp(rsdp->signature, ACPI_RSDP_SIGNATURE, ACPI_RSDP_SIGNATURE_LEN) != 0) return false;
    return acpi_validate_checksum((const uint8_t *)rsdp, ACPI_RSDP_V1_LEN);
}

static bool rsdp_validate_v2(const RSDP *rsdp)
{
    if (!rsdp_validate_v1(&rsdp->v1)) return false;
    if (rsdp->v1.revision < 2) return false;
    return acpi_validate_checksum((const uint8_t *)rsdp, rsdp->length);
}

bool acpi_find_rsdp(ACPITableList *list, const uint8_t *bios_mem, size_t bios_size)
{
    if (!list || !bios_mem || bios_size < ACPI_RSDP_V1_LEN) return false;
    memset(list, 0, sizeof(ACPITableList));

    for (size_t offset = 0; offset <= bios_size - ACPI_RSDP_SIGNATURE_LEN; offset += 16) {
        const RSDP *candidate = (const RSDP *)(bios_mem + offset);

        if (memcmp(candidate->v1.signature, ACPI_RSDP_SIGNATURE, ACPI_RSDP_SIGNATURE_LEN) == 0) {
            if (candidate->v1.revision >= 2 && rsdp_validate_v2(candidate)) {
                memcpy(&list->rsdp, candidate, sizeof(RSDP));
                list->has_valid_rsdp = true;
                list->is_v2 = true;
                return true;
            }
            if (rsdp_validate_v1(&candidate->v1)) {
                memcpy(&list->rsdp, candidate, ACPI_RSDP_V1_LEN);
                list->rsdp.v1.revision = 0;
                list->has_valid_rsdp = true;
                list->is_v2 = false;
                return true;
            }
        }
    }
    return false;
}

/* ===== XSDT/RSDT Parsing (L3: ACPI 6.5 §5.2.7, §5.2.8) ===== */

bool acpi_parse_xsdt(ACPITableList *list, const uint8_t *bios_mem)
{
    if (!list || !list->is_v2 || !bios_mem) return false;
    if (list->rsdp.xsdt_address == 0) return false;

    const XSDT *xsdt = (const XSDT *)(uintptr_t)list->rsdp.xsdt_address;
    if (memcmp(xsdt->header.signature, ACPI_TABLE_XSDT, 4) != 0) return false;

    uint32_t entry_count = (xsdt->header.length - sizeof(ACPISDTHeader)) / sizeof(uint64_t);
    if (entry_count > ACPI_MAX_TABLES) entry_count = ACPI_MAX_TABLES;

    list->table_count = 0;
    for (uint32_t i = 0; i < entry_count; i++) {
        uint64_t addr = ((const uint64_t *)xsdt->entries)[i];
        if (addr == 0) continue;

        const ACPISDTHeader *hdr = (const ACPISDTHeader *)(uintptr_t)addr;
        list->tables[list->table_count].address = (uint32_t)(uintptr_t)hdr;
        list->tables[list->table_count].length  = hdr->length;
        list->tables[list->table_count].revision = hdr->revision;
        list->tables[list->table_count].is_xsdt  = true;
        memcpy(list->tables[list->table_count].signature, hdr->signature, 4);
        list->tables[list->table_count].signature[4] = '\0';
        list->table_count++;
    }
    return true;
}

bool acpi_parse_rsdt(ACPITableList *list, const uint8_t *bios_mem)
{
    if (!list || !bios_mem) return false;
    if (list->rsdp.v1.rsdt_address == 0) return false;

    const RSDT *rsdt = (const RSDT *)(uintptr_t)list->rsdp.v1.rsdt_address;
    if (memcmp(rsdt->header.signature, ACPI_TABLE_RSDT, 4) != 0) return false;

    uint32_t entry_count = (rsdt->header.length - sizeof(ACPISDTHeader)) / sizeof(uint32_t);
    if (entry_count > ACPI_MAX_TABLES) entry_count = ACPI_MAX_TABLES;

    list->table_count = 0;
    for (uint32_t i = 0; i < entry_count; i++) {
        uint32_t addr = rsdt->entries[i];
        if (addr == 0) continue;

        const ACPISDTHeader *hdr = (const ACPISDTHeader *)(uintptr_t)addr;
        list->tables[list->table_count].address  = addr;
        list->tables[list->table_count].length   = hdr->length;
        list->tables[list->table_count].revision = hdr->revision;
        list->tables[list->table_count].is_xsdt  = false;
        memcpy(list->tables[list->table_count].signature, hdr->signature, 4);
        list->tables[list->table_count].signature[4] = '\0';
        list->table_count++;
    }
    return true;
}

bool acpi_find_table(const ACPITableList *list, const char *signature, ACPITableEntry *entry)
{
    if (!list || !signature || !entry) return false;
    for (size_t i = 0; i < list->table_count; i++) {
        if (memcmp(list->tables[i].signature, signature, 4) == 0) {
            *entry = list->tables[i];
            return true;
        }
    }
    return false;
}

const char *acpi_table_type_name(const char *signature)
{
    if (!signature) return "Unknown";
    if (memcmp(signature, ACPI_TABLE_FADT, 4) == 0) return "FADT (Fixed ACPI Description Table)";
    if (memcmp(signature, ACPI_TABLE_DSDT, 4) == 0) return "DSDT (Differentiated System Description Table)";
    if (memcmp(signature, ACPI_TABLE_SSDT, 4) == 0) return "SSDT (Secondary System Description Table)";
    if (memcmp(signature, ACPI_TABLE_MADT, 4) == 0) return "MADT (Multiple APIC Description Table)";
    if (memcmp(signature, ACPI_TABLE_MCFG, 4) == 0) return "MCFG (PCI Express Memory-mapped Configuration)";
    if (memcmp(signature, ACPI_TABLE_HPET, 4) == 0) return "HPET (High Precision Event Timer)";
    if (memcmp(signature, ACPI_TABLE_BGRT, 4) == 0) return "BGRT (Boot Graphics Resource Table)";
    if (memcmp(signature, ACPI_TABLE_SPCR, 4) == 0) return "SPCR (Serial Port Console Redirection)";
    if (memcmp(signature, ACPI_TABLE_DBG2, 4) == 0) return "DBG2 (Debug Port Table 2)";
    if (memcmp(signature, ACPI_TABLE_RSDT, 4) == 0) return "RSDT (Root System Description Table)";
    if (memcmp(signature, ACPI_TABLE_XSDT, 4) == 0) return "XSDT (Extended System Description Table)";
    return "Unknown ACPI";
}

void acpi_print_tables(const ACPITableList *list)
{
    if (!list || !list->has_valid_rsdp) {
        printf("No valid ACPI tables found.\n");
        return;
    }

    printf("=== ACPI Table Listing ===\n");
    printf("RSDP found (v%s): OEM=%.6s, RSDT=0x%08X",
           list->is_v2 ? "2" : "1",
           list->rsdp.v1.oem_id,
           list->rsdp.v1.rsdt_address);
    if (list->is_v2) {
        printf(", XSDT=0x%016llX", (unsigned long long)list->rsdp.xsdt_address);
    }
    printf("\n\n");

    printf("%-8s %-12s %-10s %-8s  %s\n", "Sig", "Address", "Length", "Rev", "Description");
    printf("%-8s %-12s %-10s %-8s  %s\n", "---", "-------", "------", "---", "-----------");

    for (size_t i = 0; i < list->table_count; i++) {
        const ACPITableEntry *e = &list->tables[i];
        printf("%-8s 0x%08X   %-8u  %-6u  %s\n",
               e->signature, e->address, e->length, e->revision,
               acpi_table_type_name(e->signature));
    }

    printf("\nTotal: %zu table(s)\n", list->table_count);

    size_t total_size = 0;
    for (size_t i = 0; i < list->table_count; i++) {
        total_size += list->tables[i].length;
    }
    printf("Total table space: %zu bytes\n", total_size);
}

/* ===== FADT Parsing (L3: ACPI 6.5 §5.2.9 - Fixed ACPI Description Table) =====
 *
 * The FADT defines fixed hardware registers for power management:
 * PM1a/PM1b event blocks, PM1a/PM1b control blocks, PM timer, GPE blocks,
 * and the reset register. It also describes boot architecture flags and
 * the DSDT location.
 *
 * Key registers:
 *   PM_TMR_BLK: 32-bit free-running timer @3.579545 MHz (ACPI PM Timer)
 *   RESET_REG:  write reset_value to initiate system reset
 *   SCI_INT:     system control interrupt number
 */

const char *acpi_pm_profile_name(uint8_t profile)
{
    switch (profile) {
    case ACPI_PM_PROFILE_UNSPECIFIED:  return "Unspecified";
    case ACPI_PM_PROFILE_DESKTOP:      return "Desktop";
    case ACPI_PM_PROFILE_MOBILE:       return "Mobile";
    case ACPI_PM_PROFILE_WORKSTATION:  return "Workstation";
    case ACPI_PM_PROFILE_ENTERPRISE:   return "Enterprise Server";
    case ACPI_PM_PROFILE_SOHO_SERVER:  return "SOHO Server";
    case ACPI_PM_PROFILE_APPLIANCE_PC: return "Appliance PC";
    case ACPI_PM_PROFILE_PERFORMANCE:  return "Performance Server";
    case ACPI_PM_PROFILE_TABLET:       return "Tablet";
    default:                           return "Reserved";
    }
}

bool acpi_parse_fadt(const uint8_t *fadt_raw, uint32_t length, FADTInfo *info)
{
    if (!fadt_raw || length < sizeof(FADT) || !info) return false;
    memset(info, 0, sizeof(FADTInfo));

    /* Validate signature and checksum */
    const FADT *fadt = (const FADT *)fadt_raw;
    if (memcmp(fadt->header.signature, ACPI_TABLE_FADT, 4) != 0) return false;
    if (!acpi_validate_checksum(fadt_raw, fadt->header.length)) return false;

    info->has_fadt = true;
    info->pm_profile   = fadt->preferred_pm_profile;
    info->sci_int      = fadt->sci_int;
    info->smi_cmd      = fadt->smi_cmd;
    info->acpi_enable  = fadt->acpi_enable;
    info->acpi_disable = fadt->acpi_disable;
    info->pm_tmr_blk   = fadt->pm_tmr_blk;
    info->pm_tmr_len   = fadt->pm_tmr_len;
    info->flags        = fadt->flags;
    info->iapc_boot_arch = fadt->iapc_boot_arch;

    /* Hardware-reduced ACPI mode (ACPI 5.0+) */
    info->hw_reduced = (fadt->flags & ACPI_FADT_HW_REDUCED_ACPI) != 0;

    /* Extended PM Timer via Generic Address Structure */
    if (fadt->x_pm_tmr_blk.address != 0) {
        info->x_pm_tmr_blk = fadt->x_pm_tmr_blk.address;
    }

    /* Reset register support */
    if (fadt->flags & ACPI_FADT_RESET_REG_SUP) {
        info->has_reset_reg = true;
        info->reset_address = fadt->reset_reg.address;
        info->reset_value   = fadt->reset_value;
    }

    /* DSDT address: prefer 64-bit X_DSDT on v2+, fall back to 32-bit */
    if (fadt->header.revision >= 2 && fadt->x_dsdt != 0) {
        info->dsdt_address = (uint32_t)fadt->x_dsdt;
    } else {
        info->dsdt_address = fadt->dsdt;
    }

    return true;
}

void acpi_print_fadt(const FADTInfo *info)
{
    if (!info || !info->has_fadt) {
        printf("No FADT data available.\n");
        return;
    }

    printf("=== FADT (Fixed ACPI Description Table) ===\n");
    printf("  PM Profile: %s (%u)\n", acpi_pm_profile_name(info->pm_profile), info->pm_profile);
    printf("  SCI IRQ: %u\n", info->sci_int);
    printf("  SMI Command Port: 0x%08X\n", info->smi_cmd);
    printf("  ACPI Enable/Disable: 0x%02X / 0x%02X\n", info->acpi_enable, info->acpi_disable);
    printf("  PM Timer Block: 0x%08X (%u bytes)\n", info->pm_tmr_blk, info->pm_tmr_len);
    if (info->x_pm_tmr_blk) {
        printf("  PM Timer (extended): 0x%016llX\n", (unsigned long long)info->x_pm_tmr_blk);
    }
    printf("  DSDT Address: 0x%08X\n", info->dsdt_address);
    printf("  Hardware-reduced ACPI: %s\n", info->hw_reduced ? "yes" : "no");
    printf("  Reset Register: %s (addr=0x%016llX, val=0x%02X)\n",
           info->has_reset_reg ? "supported" : "not supported",
           (unsigned long long)info->reset_address,
           info->reset_value);
    printf("  Boot Architecture: 0x%04X\n", info->iapc_boot_arch);
    if (info->iapc_boot_arch & ACPI_FADT_LEGACY_DEVICES)  printf("    - Legacy devices present\n");
    if (info->iapc_boot_arch & ACPI_FADT_8042)            printf("    - 8042 keyboard controller\n");
    if (info->iapc_boot_arch & ACPI_FADT_NO_VGA)          printf("    - No VGA\n");
    if (info->iapc_boot_arch & ACPI_FADT_NO_MSI)          printf("    - No MSI\n");
    if (info->iapc_boot_arch & ACPI_FADT_NO_ASPM)         printf("    - No ASPM\n");
    if (info->iapc_boot_arch & ACPI_FADT_NO_CMOS_RTC)     printf("    - No CMOS RTC\n");
}

/* ===== MADT Parsing (L5: ACPI 6.5 §5.2.12 - APIC enumeration) =====
 *
 * MADT describes the system's interrupt controller topology:
 * - LAPIC (Local APIC): per-CPU, identified by ACPI processor ID and APIC ID
 * - IOAPIC: routes external interrupts to LAPICs via GSI (Global System Interrupt)
 * - Interrupt Source Override: maps legacy ISA IRQ to GSI
 * - LAPIC NMI: configures NMI delivery to specific CPUs
 * - x2APIC: extended APIC ID support (>255 CPUs, 32-bit APIC ID)
 *
 * IRQ-to-GSI mapping (L4: ISA IRQ routing in ACPI systems):
 *   By default, GSI = IRQ for IRQs 0-15 (ISA compatibility).
 *   Override entries change this mapping (e.g., IRQ0→GSI2 for HPET).
 */

const char *acpi_madt_entry_type_name(uint8_t type)
{
    switch (type) {
    case MADT_TYPE_LAPIC:              return "Local APIC";
    case MADT_TYPE_IOAPIC:             return "I/O APIC";
    case MADT_TYPE_INT_SRC_OVERRIDE:   return "Interrupt Source Override";
    case MADT_TYPE_NMI:                return "NMI Source";
    case MADT_TYPE_LAPIC_NMI:          return "Local APIC NMI";
    case MADT_TYPE_LAPIC_ADDR_OVERRIDE: return "Local APIC Address Override";
    case MADT_TYPE_IOSAPIC:            return "I/O SAPIC";
    case MADT_TYPE_LSAPIC:             return "Local SAPIC";
    case MADT_TYPE_PLATFORM_INT_SRC:   return "Platform Interrupt Source";
    case MADT_TYPE_LX2APIC:            return "Local x2APIC";
    case MADT_TYPE_LX2APIC_NMI:        return "Local x2APIC NMI";
    case MADT_TYPE_GICC:               return "GIC CPU Interface";
    case MADT_TYPE_GICD:               return "GIC Distributor";
    case MADT_TYPE_GIC_MSI_FRAME:      return "GIC MSI Frame";
    case MADT_TYPE_GICR:               return "GIC Redistributor";
    case MADT_TYPE_GIC_ITS:            return "GIC ITS";
    case MADT_TYPE_MULTIPROC_WAKEUP:   return "Multiprocessor Wakeup";
    default:                           return "Unknown MADT Entry";
    }
}

bool acpi_parse_madt(const uint8_t *madt_raw, uint32_t length, MADTInfo *info)
{
    if (!madt_raw || length < sizeof(MADT) || !info) return false;
    memset(info, 0, sizeof(MADTInfo));

    const MADT *madt = (const MADT *)madt_raw;
    if (memcmp(madt->header.signature, ACPI_TABLE_MADT, 4) != 0) return false;
    if (!acpi_validate_checksum(madt_raw, madt->header.length)) return false;

    info->has_madt = true;
    info->lapic_address = madt->lapic_address;
    info->pic_cascade = (madt->flags & 1) != 0;  /* bit 0 = PC-AT dual 8259 */

    /* Iterate MADT entries */
    uint32_t offset = sizeof(MADT);
    uint32_t table_length = madt->header.length;

    while (offset < table_length) {
        const MADTEntryHeader *entry = (const MADTEntryHeader *)(madt_raw + offset);
        if (entry->length == 0) break;  /* safety */
        if (offset + entry->length > table_length) break;

        switch (entry->type) {
        case MADT_TYPE_LAPIC: {
            if (info->lapic_count < MADT_MAX_LAPICS) {
                const MADTLAPIC *lapic = (const MADTLAPIC *)entry;
                MADTLAPICInfo *dst = &info->lapics[info->lapic_count++];
                dst->acpi_processor_id = lapic->acpi_processor_id;
                dst->apic_id           = lapic->apic_id;
                dst->flags             = lapic->flags;
            }
            break;
        }
        case MADT_TYPE_IOAPIC: {
            if (info->ioapic_count < MADT_MAX_IOAPICS) {
                const MADTIOAPIC *ioapic = (const MADTIOAPIC *)entry;
                MADTIOAPICInfo *dst = &info->ioapics[info->ioapic_count++];
                dst->io_apic_id      = ioapic->io_apic_id;
                dst->io_apic_address = ioapic->io_apic_address;
                dst->gsi_base        = ioapic->global_system_interrupt_base;
            }
            break;
        }
        case MADT_TYPE_INT_SRC_OVERRIDE: {
            if (info->override_count < MADT_MAX_OVERRIDES) {
                const MADTIntSrcOverride *iso = (const MADTIntSrcOverride *)entry;
                MADTIntOverrideInfo *dst = &info->overrides[info->override_count++];
                dst->bus  = iso->bus_source;
                dst->irq  = iso->irq_source;
                dst->gsi  = iso->global_system_interrupt;
                dst->flags = iso->flags;
            }
            break;
        }
        case MADT_TYPE_LAPIC_NMI: {
            /* Store as override with special bus=0xFF for NMI via LAPIC */
            if (info->override_count < MADT_MAX_OVERRIDES) {
                const MADTLAPICNMI *nmi = (const MADTLAPICNMI *)entry;
                MADTIntOverrideInfo *dst = &info->overrides[info->override_count++];
                dst->bus   = 0xFF;                      /* marker for LAPIC NMI */
                dst->irq   = nmi->lint_number;
                dst->gsi   = nmi->lint_number;
                dst->flags = nmi->flags;
            }
            break;
        }
        case MADT_TYPE_LAPIC_ADDR_OVERRIDE: {
            const MADTLAPICAddrOverride *ao = (const MADTLAPICAddrOverride *)entry;
            info->lapic_address = (uint32_t)ao->x2apic_address;
            break;
        }
        case MADT_TYPE_LX2APIC: {
            if (info->lapic_count < MADT_MAX_LAPICS) {
                const MADTLX2APIC *x2 = (const MADTLX2APIC *)entry;
                MADTLAPICInfo *dst = &info->lapics[info->lapic_count++];
                dst->acpi_processor_id = (uint8_t)x2->acpi_processor_uid;
                dst->apic_id           = (uint8_t)x2->x2apic_id;  /* truncated for display */
                dst->flags             = x2->flags;
            }
            break;
        }
        default:
            break;
        }

        offset += entry->length;
    }

    return true;
}

/* irq_to_gsi: apply ISA IRQ → GSI remapping per MADT override entries
 *
 * Default ISA mapping (ACPI-compliant): GSI = IRQ (0-15).
 * Override entries change this, e.g., HPET timer on IRQ0 may remap to GSI2.
 */
uint32_t acpi_madt_irq_to_gsi(const MADTInfo *info, uint8_t irq)
{
    if (!info || !info->has_madt) return irq;  /* identity if no MADT */

    for (size_t i = 0; i < info->override_count; i++) {
        if (info->overrides[i].bus == 0 && info->overrides[i].irq == irq) {
            return info->overrides[i].gsi;
        }
    }
    return irq;  /* default identity mapping */
}

void acpi_print_madt(const MADTInfo *info)
{
    if (!info || !info->has_madt) {
        printf("No MADT data available.\n");
        return;
    }

    printf("=== MADT (Multiple APIC Description Table) ===\n");
    printf("  Local APIC Address: 0x%08X\n", info->lapic_address);
    printf("  PC-AT Dual 8259: %s\n", info->pic_cascade ? "yes" : "no");

    printf("\n  --- Local APICs (%zu) ---\n", info->lapic_count);
    for (size_t i = 0; i < info->lapic_count; i++) {
        const MADTLAPICInfo *lapic = &info->lapics[i];
        printf("    [CPU %3u] APIC ID=%3u  %s\n",
               lapic->acpi_processor_id, lapic->apic_id,
               (lapic->flags & 1) ? "enabled" : "disabled");
    }

    printf("\n  --- I/O APICs (%zu) ---\n", info->ioapic_count);
    for (size_t i = 0; i < info->ioapic_count; i++) {
        const MADTIOAPICInfo *ioapic = &info->ioapics[i];
        printf("    I/O APIC ID=%u  Address=0x%08X  GSI Base=%u\n",
               ioapic->io_apic_id, ioapic->io_apic_address, ioapic->gsi_base);
    }

    if (info->override_count > 0) {
        printf("\n  --- Interrupt Source Overrides (%zu) ---\n", info->override_count);
        for (size_t i = 0; i < info->override_count; i++) {
            const MADTIntOverrideInfo *ov = &info->overrides[i];
            if (ov->bus == 0xFF) {
                printf("    LAPIC NMI: LINT#%u  flags=0x%04X\n", ov->irq, ov->flags);
            } else {
                printf("    Bus=%u IRQ=%u → GSI=%u  flags=0x%04X (%s %s)\n",
                       ov->bus, ov->irq, ov->gsi, ov->flags,
                       (ov->flags & 1) ? "active-low" : "active-high",
                       (ov->flags & 2) ? "level" : "edge");
            }
        }
    }
}

/* ===== MCFG Parsing (L2: ACPI 6.5 §5.2.29 - PCIe ECAM) =====
 *
 * MCFG provides Enhanced Configuration Access Mechanism (ECAM) for PCIe:
 * each entry maps a PCI segment group + bus range to a memory-mapped
 * configuration space base address.
 *
 * ECAM address formula:
 *   addr = base + ((bus - start_bus) << 20 | device << 15 | function << 12) + offset
 */

bool acpi_parse_mcfg(const uint8_t *mcfg_raw, uint32_t length, MCFGInfo *info)
{
    if (!mcfg_raw || length < sizeof(MCFG) || !info) return false;
    memset(info, 0, sizeof(MCFGInfo));

    const MCFG *mcfg = (const MCFG *)mcfg_raw;
    if (memcmp(mcfg->header.signature, ACPI_TABLE_MCFG, 4) != 0) return false;
    if (!acpi_validate_checksum(mcfg_raw, mcfg->header.length)) return false;

    info->has_mcfg = true;

    uint32_t entry_count = (mcfg->header.length - sizeof(MCFG)) / sizeof(MCFGEntry);
    if (entry_count > ACPI_MAX_MCFG_ENTRIES) entry_count = ACPI_MAX_MCFG_ENTRIES;

    for (uint32_t i = 0; i < entry_count; i++) {
        info->buses[info->entry_count].base    = mcfg->entries[i].base_address;
        info->buses[info->entry_count].segment = mcfg->entries[i].pci_segment_group;
        info->buses[info->entry_count].bus_start = mcfg->entries[i].start_bus;
        info->buses[info->entry_count].bus_end   = mcfg->entries[i].end_bus;
        info->entry_count++;
    }
    return true;
}

uint64_t acpi_mcfg_get_ecam_base(const MCFGInfo *info, uint8_t bus, uint8_t device, uint8_t function)
{
    if (!info || !info->has_mcfg) return 0;

    for (size_t i = 0; i < info->entry_count; i++) {
        if (bus >= info->buses[i].bus_start && bus <= info->buses[i].bus_end) {
            return info->buses[i].base +
                   ((((uint64_t)(bus - info->buses[i].bus_start)) << 20) |
                   ((uint64_t)device << 15) |
                   ((uint64_t)function << 12));
        }
    }
    return 0;
}

void acpi_print_mcfg(const MCFGInfo *info)
{
    if (!info || !info->has_mcfg) {
        printf("No MCFG data available.\n");
        return;
    }

    printf("=== MCFG (PCI Express MMCONFIG) ===\n");
    printf("  %-8s %-6s %-10s %-10s  %s\n", "Segment", "Bus", "Start Bus", "End Bus", "Base Address");
    printf("  %-8s %-6s %-10s %-10s  %s\n", "-------", "---", "---------", "-------", "------------");

    for (size_t i = 0; i < info->entry_count; i++) {
        const MCFGBusEntry *e = &info->buses[i];
        printf("  %-8u %-6u %-10u %-10u  0x%016llX\n",
               e->segment,
               e->bus_end - e->bus_start + 1,
               e->bus_start, e->bus_end,
               (unsigned long long)e->base);
    }
}

/* ===== HPET Parsing (L2: ACPI 6.5 §5.2.28) =====
 *
 * HPET is a high-precision timer with at least 3 independent counters.
 * The main counter increments at a frequency derived from min_clock_tick
 * (reported in femtoseconds, 10^-15 s).
 *
 * Clock period = min_clock_tick × 10^-15 seconds
 * Counter frequency = 1 / period = 10^15 / min_clock_tick Hz
 *
 * For a typical HPET: min_clock_tick = 69841279 fs → ~14.31818 MHz
 */

bool acpi_parse_hpet(const uint8_t *hpet_raw, uint32_t length, HPETInfo *info)
{
    if (!hpet_raw || length < sizeof(HPET) || !info) return false;
    memset(info, 0, sizeof(HPETInfo));

    const HPET *hpet = (const HPET *)hpet_raw;
    if (memcmp(hpet->header.signature, ACPI_TABLE_HPET, 4) != 0) return false;
    if (!acpi_validate_checksum(hpet_raw, hpet->header.length)) return false;

    info->has_hpet = true;
    info->block_id        = hpet->event_timer_block_id;
    info->base_address    = hpet->base_address.address;
    info->hpet_number     = hpet->hpet_number;
    info->min_clock_tick_fs = hpet->min_clock_tick;

    /* Page protection flags */
    info->is_4kb_protected  = (hpet->page_protection & 0x01) != 0;
    info->is_64kb_protected = (hpet->page_protection & 0x02) != 0;

    return true;
}

uint64_t acpi_hpet_ms_to_ticks(const HPETInfo *info, uint32_t ms)
{
    if (!info || !info->has_hpet || info->min_clock_tick_fs == 0) return 0;
    /* ticks = (ms × 10^-3) / (min_clock_tick × 10^-15)
     *       = ms × 10^12 / min_clock_tick */
    return (uint64_t)ms * 1000000000000ULL / info->min_clock_tick_fs;
}

void acpi_print_hpet(const HPETInfo *info)
{
    if (!info || !info->has_hpet) {
        printf("No HPET data available.\n");
        return;
    }

    /* Derive counter frequency */
    uint64_t freq_hz = 0;
    if (info->min_clock_tick_fs > 0) {
        freq_hz = 1000000000000000ULL / info->min_clock_tick_fs;
    }

    printf("=== HPET (High Precision Event Timer) ===\n");
    printf("  Block ID: 0x%08X\n", info->block_id);
    printf("  HPET Number: %u\n", info->hpet_number);
    printf("  Base Address: 0x%016llX\n", (unsigned long long)info->base_address);
    printf("  Min Clock Tick: %u femtoseconds\n", info->min_clock_tick_fs);
    if (freq_hz > 0) {
        printf("  Counter Frequency: ~%llu.%03llu MHz\n",
               (unsigned long long)(freq_hz / 1000000),
               (unsigned long long)((freq_hz % 1000000) / 1000));
    }
    printf("  1 ms = %llu ticks\n", (unsigned long long)acpi_hpet_ms_to_ticks(info, 1));
    printf("  Page Protection: %s%s\n",
           info->is_4kb_protected ? "4KB " : "",
           info->is_64kb_protected ? "64KB" : "none");
}
