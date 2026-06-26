#include "acpi_tables.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

bool acpi_validate_checksum(const uint8_t *table, uint32_t length)
{
    if (!table || length == 0) return false;
    uint8_t sum = 0;
    for (uint32_t i = 0; i < length; i++) {
        sum += table[i];
    }
    return sum == 0;
}

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
