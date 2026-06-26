#include "uefi_acpi.h"
#include <stdio.h>
#include <string.h>

/* ================================================================
 * L5: ACPI Table Parsing
 *
 * Knowledge points:
 *   - ACPI table search algorithm (RSDP signature scan)
 *   - SDT checksum validation (8-bit sum mod 256 == 0)
 *   - XSDT/RSDT walk to locate specific tables
 *   - MADT entry parsing (LAPIC, IOAPIC, ISO, NMI)
 *   - MCFG PCIe ECAM region parsing
 * ================================================================ */

/* ---- ACPI checksum validation ---- */

/* Verify RSDP checksum (ACPI §5.2.5.3).
 * For ACPI 1.0: sum of first 20 bytes.
 * For ACPI 2.0+: two checksums — standard (bytes 0-19) and extended (entire table). */
bool acpi_rsdp_checksum(const ACPIRSDP *rsdp) {
    if (!rsdp) return false;
    uint8_t sum = 0;
    const uint8_t *p = (const uint8_t *)rsdp;
    for (int i = 0; i < 20; i++) sum += p[i];
    if (sum != 0) return false;
    if (rsdp->revision >= 2) {
        sum = 0;
        for (uint32_t i = 0; i < rsdp->length; i++) sum += p[i];
        if (sum != 0) return false;
    }
    return true;
}

/* Verify SDT table checksum.
 * Per ACPI §5.2.6: checksum field makes sum of all bytes in table = 0. */
bool acpi_sdt_checksum(const ACPISDTHeader *header) {
    if (!header) return false;
    uint8_t sum = 0;
    const uint8_t *p = (const uint8_t *)header;
    for (uint32_t i = 0; i < header->length; i++) sum += p[i];
    return sum == 0;
}

/* ---- RSDP Search ---- */

/* Search for RSDP in BIOS memory ranges.
 * Algorithm: scan 16-byte boundaries for "RSD PTR " signature,
 * then validate checksum. If revision >= 2, validate extended checksum.
 * Search order: 1. EBDA (Extended BIOS Data Area), 2. BIOS ROM area.
 * Complexity: O(n) where n = search range / 16. */
bool acpi_find_rsdp(ACPIRSDP *rsdp, const uint8_t *bios_rom, size_t rom_size) {
    if (!rsdp || !bios_rom || rom_size < sizeof(ACPIRSDP)) return false;

    printf("  ACPI: searching for RSDP in %zu-byte BIOS range\n", rom_size);

    for (size_t offset = 0; offset <= rom_size - sizeof(ACPIRSDP); offset += 16) {
        const ACPIRSDP *candidate = (const ACPIRSDP *)(bios_rom + offset);
        if (candidate->signature == ACPI_RSDP_SIG) {
            if (acpi_rsdp_checksum(candidate)) {
                memcpy(rsdp, candidate, sizeof(ACPIRSDP));
                printf("  ACPI: RSDP found at offset 0x%zX (revision %u)\n",
                       offset, rsdp->revision);
                return true;
            }
        }
    }
    printf("  ACPI: RSDP not found in provided range\n");
    return false;
}

/* ---- XSDT Parsing ---- */

/* Walk XSDT to find referenced tables.
 * XSDT contains an array of 64-bit pointers to other SDTs.
 * For each pointer, read the header, validate checksum, store in info. */
bool acpi_parse_xsdt(ACPISystemInfo *info, const uint8_t *tables, size_t size) {
    if (!info || !tables || size < sizeof(ACPISDTHeader)) return false;

    const ACPISDTHeader *xsdt = (const ACPISDTHeader *)tables;
    if (xsdt->signature != ACPI_XSDT_SIG &&
        xsdt->signature != ACPI_RSDT_SIG) {
        printf("  ACPI: not an XSDT/RSDT (sig=0x%08X)\n", xsdt->signature);
        return false;
    }

    bool is_64bit = (xsdt->signature == ACPI_XSDT_SIG);
    uint32_t entry_size = is_64bit ? 8 : 4;
    uint32_t entry_count = (xsdt->length - sizeof(ACPISDTHeader)) / entry_size;
    const uint8_t *entries = tables + sizeof(ACPISDTHeader);

    printf("  ACPI: %s with %u entries\n", is_64bit ? "XSDT" : "RSDT", entry_count);

    info->table_count = 0;
    for (uint32_t i = 0; i < entry_count && info->table_count < ACPI_MAX_TABLES; i++) {
        uint64_t addr = 0;
        if (is_64bit) {
            const uint8_t *e = entries + (size_t)i * 8;
            addr = (uint64_t)e[0] | ((uint64_t)e[1] << 8) |
                   ((uint64_t)e[2] << 16) | ((uint64_t)e[3] << 24) |
                   ((uint64_t)e[4] << 32) | ((uint64_t)e[5] << 40) |
                   ((uint64_t)e[6] << 48) | ((uint64_t)e[7] << 56);
        } else {
            const uint8_t *e = entries + (size_t)i * 4;
            addr = (uint32_t)(e[0] | (e[1] << 8) | (e[2] << 16) | (e[3] << 24));
        }
        if (addr == 0 || addr > size - sizeof(ACPISDTHeader)) continue;

        const ACPISDTHeader *sdt = (const ACPISDTHeader *)(tables + (size_t)addr);
        if (!acpi_sdt_checksum(sdt)) {
            printf("  ACPI: table at 0x%llX failed checksum\n",
                   (unsigned long long)addr);
            continue;
        }

        switch (sdt->signature) {
        case ACPI_MADT_SIG:
            acpi_parse_madt(info, sdt);
            break;
        case ACPI_MCFG_SIG:
            acpi_parse_mcfg(info, sdt);
            break;
        case ACPI_FADT_SIG: {
            const ACPIFADT *fadt = (const ACPIFADT *)sdt;
            info->dsdt_address = fadt->dsdt;
            info->pic_mode = (fadt->boot_architecture_flags & 1) == 0;
            if (info->pic_mode)
                printf("  ACPI: FADT reports PIC mode (8259 present)\n");
            break;
        }
        case ACPI_DSDT_SIG:
            /* DSDT saved via FADT pointer */
            break;
        default:
            break;
        }
        info->table_count++;
    }

    printf("  ACPI: parsed %u tables from %s\n",
           info->table_count, is_64bit ? "XSDT" : "RSDT");
    return true;
}

/* ---- MADT (APIC) Parsing ---- */

/* Parse MADT to extract LAPIC and IOAPIC entries.
 * MADT structure: header + local APIC address + flags + variable-length entries.
 * Each entry: type (1 byte) + length (1 byte) + type-specific data.
 * Reference: ACPI §5.2.12 */
bool acpi_parse_madt(ACPISystemInfo *info, const ACPISDTHeader *madt_hdr) {
    if (!info || !madt_hdr) return false;

    const ACPIMADT *madt = (const ACPIMADT *)madt_hdr;
    info->local_apic_addr = madt->local_apic_address;
    info->pic_mode        = (madt->flags & 1) != 0;

    printf("  ACPI: MADT found (LAPIC base=0x%08X, flags=0x%X)\n",
           madt->local_apic_address, madt->flags);

    uint32_t offset = sizeof(ACPIMADT);
    info->lapic_count = 0;
    info->ioapic_count = 0;

    while (offset + 2 <= madt_hdr->length) {
        const ACPIMADTEntryHeader *entry =
            (const ACPIMADTEntryHeader *)((const uint8_t *)madt + offset);
        if (entry->length == 0) break;
        if (offset + entry->length > madt_hdr->length) break;

        switch (entry->type) {
        case ACPI_MADT_LAPIC: {
            const ACPIMADTLAPIC *lapic = (const ACPIMADTLAPIC *)entry;
            if (info->lapic_count < ACPI_MAX_LAPIC) {
                info->lapics[info->lapic_count].acpi_id = lapic->acpi_processor_id;
                info->lapics[info->lapic_count].apic_id = lapic->apic_id;
                info->lapics[info->lapic_count].enabled = (lapic->flags & 1) != 0;
                info->lapic_count++;
            }
            break;
        }
        case ACPI_MADT_IOAPIC: {
            const ACPIMADTIOAPIC *ioapic = (const ACPIMADTIOAPIC *)entry;
            if (info->ioapic_count < ACPI_MAX_IOAPIC) {
                info->ioapics[info->ioapic_count].ioapic_id     = ioapic->ioapic_id;
                info->ioapics[info->ioapic_count].ioapic_address = ioapic->ioapic_address;
                info->ioapics[info->ioapic_count].gsi_base      = ioapic->global_system_interrupt_base;
                info->ioapic_count++;
            }
            break;
        }
        case ACPI_MADT_ISO: {
            const ACPIMADTISO *iso = (const ACPIMADTISO *)entry;
            printf("  ACPI: IRQ source override: ISA IRQ%u -> GSI %u\n",
                   iso->irq_source, iso->global_system_interrupt);
            break;
        }
        default:
            break;
        }
        offset += entry->length;
    }

    printf("  ACPI: MADT parsed: %u LAPIC(s), %u IOAPIC(s)\n",
           info->lapic_count, info->ioapic_count);
    return true;
}

/* ---- MCFG Parsing ---- */

/* Parse MCFG (PCI Express memory-mapped configuration space).
 * Contains entries describing ECAM regions (base address, bus range).
 * Reference: PCI Firmware Specification 3.0 §4.1.2 */
bool acpi_parse_mcfg(ACPISystemInfo *info, const ACPISDTHeader *mcfg_hdr) {
    if (!info || !mcfg_hdr) return false;

    const ACPIMCFG *mcfg = (const ACPIMCFG *)mcfg_hdr;
    uint32_t entry_count = (mcfg_hdr->length - sizeof(ACPIMCFG)) /
                           sizeof(ACPIMCFGEntry);

    printf("  ACPI: MCFG found with %u ECAM region(s)\n", entry_count);

    if (entry_count > 0) {
        info->mcfg_base    = mcfg->entries[0].base_address;
        info->mcfg_segment = mcfg->entries[0].pci_segment_group;
        printf("  ACPI: MCFG[0]: base=0x%llX, seg=%u, bus %u-%u\n",
               (unsigned long long)info->mcfg_base,
               info->mcfg_segment,
               mcfg->entries[0].start_bus,
               mcfg->entries[0].end_bus);
    }
    return true;
}

/* ---- Utility functions ---- */

void acpi_init_info(ACPISystemInfo *info) {
    if (!info) return;
    memset(info, 0, sizeof(ACPISystemInfo));
}

const char *acpi_sig_to_name(uint32_t sig) {
    switch (sig) {
    case ACPI_XSDT_SIG: return "XSDT";
    case ACPI_RSDT_SIG: return "RSDT";
    case ACPI_MADT_SIG: return "MADT (APIC)";
    case ACPI_FADT_SIG: return "FADT (FACP)";
    case ACPI_MCFG_SIG: return "MCFG (PCIe ECAM)";
    case ACPI_DSDT_SIG: return "DSDT";
    case ACPI_SSDT_SIG: return "SSDT";
    case ACPI_HPET_SIG: return "HPET";
    case ACPI_BGRT_SIG: return "BGRT";
    case ACPI_SPCR_SIG: return "SPCR";
    default:            return "Unknown";
    }
}

/* Find a table by signature in a buffer containing multiple SDTs.
 * Linear scan: align to 16-byte boundaries (ACPI tables are 16-byte aligned).
 * Complexity: O(n) where n = size / 16. */
int acpi_find_table(const uint8_t *tables, size_t size,
                     uint32_t sig, uint32_t *offset) {
    if (!tables || !offset) return -1;
    for (size_t i = 0; i + sizeof(ACPISDTHeader) <= size; i += 16) {
        const ACPISDTHeader *hdr = (const ACPISDTHeader *)(tables + i);
        if (hdr->signature == sig && acpi_sdt_checksum(hdr)) {
            *offset = (uint32_t)i;
            return 0;
        }
    }
    return -1;
}

void acpi_print_sdt_header(const ACPISDTHeader *hdr) {
    if (!hdr) return;
    char sig[5] = {0};
    memcpy(sig, &hdr->signature, 4);
    char oem_id[7] = {0};
    memcpy(oem_id, hdr->oem_id, 6);
    char table_id[9] = {0};
    memcpy(table_id, hdr->oem_table_id, 8);

    printf("  %s: len=%u rev=%u OEM=\"%s\" table=\"%s\" creator=0x%08X\n",
           sig, hdr->length, hdr->revision, oem_id, table_id,
           hdr->creator_revision);
}

void acpi_print_system_info(const ACPISystemInfo *info) {
    if (!info) return;

    printf("\n=== ACPI System Information ===\n");
    printf("  ACPI Version:        %s\n",
           info->acpi_20_plus ? "2.0+" : "1.0");
    printf("  RSDP Revision:       %u\n", info->rsdp.revision);
    printf("  Tables Found:        %u\n", info->table_count);

    printf("\n  --- CPU Topology ---\n");
    printf("  LAPICs Detected:     %u\n", info->lapic_count);
    for (uint32_t i = 0; i < info->lapic_count && i < 16; i++) {
        printf("    CPU %u: ACPI ID=%u APIC ID=%u %s\n",
               i, info->lapics[i].acpi_id, info->lapics[i].apic_id,
               info->lapics[i].enabled ? "[ENABLED]" : "[DISABLED]");
    }

    printf("\n  --- I/O APIC ---\n");
    printf("  IOAPICs Detected:    %u\n", info->ioapic_count);
    for (uint32_t i = 0; i < info->ioapic_count; i++) {
        printf("    IOAPIC %u: ID=%u addr=0x%08X GSI base=%u\n",
               i, info->ioapics[i].ioapic_id,
               info->ioapics[i].ioapic_address,
               info->ioapics[i].gsi_base);
    }

    printf("\n  --- PIC Mode ---\n");
    printf("  8259 PIC Present:    %s\n", info->pic_mode ? "YES" : "NO");

    printf("\n  --- MCFG ---\n");
    if (info->mcfg_base != 0) {
        printf("  ECAM Base:           0x%016llX (segment %u)\n",
               (unsigned long long)info->mcfg_base, info->mcfg_segment);
    } else {
        printf("  No MCFG table found\n");
    }
}