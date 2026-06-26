#ifndef ACPI_TABLES_H
#define ACPI_TABLES_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define ACPI_RSDP_SIGNATURE    "RSD PTR "
#define ACPI_RSDP_V1_LEN       20
#define ACPI_RSDP_V2_LEN       36
#define ACPI_RSDP_SIGNATURE_LEN 8
#define ACPI_OEM_ID_LEN        6
#define ACPI_OEM_TABLE_ID_LEN  8

#define ACPI_TABLE_RSDT        "RSDT"
#define ACPI_TABLE_XSDT        "XSDT"
#define ACPI_TABLE_FADT        "FACP"
#define ACPI_TABLE_DSDT        "DSDT"
#define ACPI_TABLE_SSDT        "SSDT"
#define ACPI_TABLE_MADT        "APIC"
#define ACPI_TABLE_MCFG        "MCFG"
#define ACPI_TABLE_HPET        "HPET"
#define ACPI_TABLE_BGRT        "BGRT"
#define ACPI_TABLE_SPCR        "SPCR"
#define ACPI_TABLE_DBG2        "DBG2"
#define ACPI_TABLE_MAX_SIG     4

typedef struct __attribute__((packed)) {
    char     signature[ACPI_RSDP_SIGNATURE_LEN];
    uint8_t  checksum;
    char     oem_id[ACPI_OEM_ID_LEN];
    uint8_t  revision;
    uint32_t rsdt_address;
} RSDPV1;

typedef struct __attribute__((packed)) {
    RSDPV1   v1;
    uint32_t length;
    uint64_t xsdt_address;
    uint8_t  extended_checksum;
    uint8_t  reserved[3];
} RSDP;

typedef struct __attribute__((packed)) {
    char     signature[4];
    uint32_t length;
    uint8_t  revision;
    uint8_t  checksum;
    char     oem_id[ACPI_OEM_ID_LEN];
    char     oem_table_id[ACPI_OEM_TABLE_ID_LEN];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} ACPISDTHeader;

typedef struct __attribute__((packed)) {
    ACPISDTHeader header;
    uint32_t      entries[];
} XSDT;

typedef struct __attribute__((packed)) {
    ACPISDTHeader header;
    uint32_t      entries[];
} RSDT;

typedef struct {
    char     signature[5];
    uint32_t address;
    uint32_t length;
    uint8_t  revision;
    bool     is_xsdt;
} ACPITableEntry;

#define ACPI_MAX_TABLES 64

typedef struct {
    RSDP           rsdp;
    ACPITableEntry tables[ACPI_MAX_TABLES];
    size_t         table_count;
    bool           has_valid_rsdp;
    bool           is_v2;
} ACPITableList;

bool acpi_find_rsdp(ACPITableList *list, const uint8_t *bios_mem, size_t bios_size);
bool acpi_parse_xsdt(ACPITableList *list, const uint8_t *bios_mem);
bool acpi_parse_rsdt(ACPITableList *list, const uint8_t *bios_mem);
bool acpi_find_table(const ACPITableList *list, const char *signature, ACPITableEntry *entry);
bool acpi_validate_checksum(const uint8_t *table, uint32_t length);
void acpi_print_tables(const ACPITableList *list);
const char *acpi_table_type_name(const char *signature);

#endif
