#ifndef SMBIOS_H
#define SMBIOS_H

#include <stdbool.h>
#include <stdint.h>

/* ============================================================================
 * SMBIOS ? System Management BIOS
 * Reference: SMBIOS Specification v3.4.0 (DMTF DSP0134)
 *
 * L1: Core definitions ? entry point structures and type-based records.
 * L2: SMBIOS provides platform inventory (CPU, memory, chassis, etc.)
 *     to OS and management software via a table-driven interface.
 * L3: Each structure has a formatted section + variable-length string table.
 * L4: SMBIOS 3.0 introduced 64-bit entry point for tables above 4GB.
 * ============================================================================ */

/* SMBIOS 2.1 Entry Point Structure (legacy, searched at 0xF0000-0xFFFFF) */
#define SMBIOS_ANCHOR_STR   "_SM_"
#define SMBIOS_EPS_SIZE     31

typedef struct __attribute__((packed)) {
    char     anchor[4];          /* "_SM_" */
    uint8_t  checksum;
    uint8_t  length;             /* 0x1F */
    uint8_t  major_version;
    uint8_t  minor_version;
    uint16_t max_structure_size;
    uint8_t  entry_point_revision;
    char     formatted_area[5];
    char     intermediate_anchor[5]; /* "_DMI_" */
    uint8_t  intermediate_checksum;
    uint16_t structure_table_length;
    uint32_t structure_table_address;
    uint16_t number_of_structures;
    uint8_t  bcd_revision;
} SMBIOSEPS;

/* SMBIOS 3.0 Entry Point Structure (preferred, supports 64-bit addresses) */
#define SMBIOS3_ANCHOR_STR  "_SM3_"
#define SMBIOS3_EPS_SIZE    24

typedef struct __attribute__((packed)) {
    char     anchor[5];          /* "_SM3_" */
    uint8_t  checksum;
    uint8_t  length;             /* 0x18 */
    uint8_t  major_version;
    uint8_t  minor_version;
    uint8_t  docrev;
    uint8_t  entry_point_revision;
    uint8_t  reserved;
    uint32_t structure_table_max_size;
    uint64_t structure_table_address;
} SMBIOS3EPS;

/* SMBIOS structure header (common to all type records) */
typedef struct __attribute__((packed)) {
    uint8_t  type;
    uint8_t  length;             /* Length of formatted area */
    uint16_t handle;             /* Unique 16-bit identifier */
} SMBIOSHeader;

/* --- Type 0: BIOS Information ---
 * Provides vendor, version, release date, and ROM size for system BIOS.
 */
#define SMBIOS_TYPE_BIOS_INFO  0

typedef struct __attribute__((packed)) {
    SMBIOSHeader header;
    uint8_t  vendor_str;         /* String index for vendor name */
    uint8_t  bios_version_str;
    uint16_t bios_start_segment; /* 0xE000 typically */
    uint8_t  bios_release_date_str;
    uint8_t  bios_rom_size;      /* (n+1) * 64KB, or see extended field */
    uint64_t bios_characteristics;
    /* Extended fields (v2.4+) */
    uint8_t  bios_characteristics_ext[2];
    uint8_t  system_bios_major_release;
    uint8_t  system_bios_minor_release;
    uint8_t  embedded_controller_major;
    uint8_t  embedded_controller_minor;
    uint16_t extended_bios_rom_size; /* For > 16MB BIOS ROMs, in MB units */
} SMBIOSType0;

/* BIOS Characteristics bitmask */
#define BIOS_CHAR_ISA_SUPPORTED        (1ULL << 2)
#define BIOS_CHAR_PCI_SUPPORTED        (1ULL << 7)
#define BIOS_CHAR_PNP_SUPPORTED        (1ULL << 10)
#define BIOS_CHAR_APM_SUPPORTED        (1ULL << 11)
#define BIOS_CHAR_UPGRADEABLE          (1ULL << 12)
#define BIOS_CHAR_SHADOWING_ALLOWED    (1ULL << 13)
#define BIOS_CHAR_BOOT_FROM_CD         (1ULL << 15)
#define BIOS_CHAR_SELECTABLE_BOOT      (1ULL << 16)
#define BIOS_CHAR_EDD_SUPPORTED        (1ULL << 24)
#define BIOS_CHAR_UEFI_SUPPORTED       (1ULL << 32)
#define BIOS_CHAR_VIRTUAL_MACHINE      (1ULL << 36)

/* --- Type 1: System Information --- */
#define SMBIOS_TYPE_SYSTEM_INFO 1

#define SYS_WAKEUP_RESERVED     0x00
#define SYS_WAKEUP_POWER_SWITCH 0x03
#define SYS_WAKEUP_PCI_PME      0x09
#define SYS_WAKEUP_AC_POWER     0x0A

typedef struct __attribute__((packed)) {
    SMBIOSHeader header;
    uint8_t  manufacturer_str;
    uint8_t  product_name_str;
    uint8_t  version_str;
    uint8_t  serial_number_str;
    uint8_t  uuid[16];        /* Universal Unique ID (RFC 4122) */
    uint8_t  wakeup_type;
    uint8_t  sku_number_str;
    uint8_t  family_str;
} SMBIOSType1;

/* --- Type 2: Baseboard Information --- */
#define SMBIOS_TYPE_BASEBOARD_INFO 2

typedef struct __attribute__((packed)) {
    SMBIOSHeader header;
    uint8_t  manufacturer_str;
    uint8_t  product_str;
    uint8_t  version_str;
    uint8_t  serial_number_str;
    uint8_t  asset_tag_str;
    uint8_t  feature_flags;
    uint8_t  location_in_chassis_str;
    uint16_t chassis_handle;
    uint8_t  board_type;
    uint8_t  num_contained_handles;
    /* uint16_t contained_object_handles[] follows */
} SMBIOSType2;

/* Board Type values */
#define BOARD_TYPE_UNKNOWN         0x01
#define BOARD_TYPE_MOTHERBOARD     0x0A
#define BOARD_TYPE_PROCESSOR_MODULE 0x0E

/* --- Type 4: Processor Information --- */
#define SMBIOS_TYPE_PROC_INFO 4

#define PROC_TYPE_CENTRAL      0x03
#define PROC_FAMILY_X86        0xC3
#define PROC_FAMILY_X64        0xD3
#define PROC_UPGRADE_SOCKET_LGA 0x0D

typedef struct __attribute__((packed)) {
    SMBIOSHeader header;
    uint8_t  socket_designation_str;
    uint8_t  processor_type;
    uint8_t  processor_family;
    uint8_t  processor_manufacturer_str;
    uint8_t  processor_id[8];   /* CPUID results: EAX..EDX then feature flags */
    uint8_t  processor_version_str;
    uint8_t  voltage;
    uint16_t external_clock;    /* MHz */
    uint16_t max_speed;         /* MHz */
    uint16_t current_speed;     /* MHz */
    uint8_t  status;
    uint8_t  processor_upgrade;
    uint16_t l1_cache_handle;
    uint16_t l2_cache_handle;
    uint16_t l3_cache_handle;
    uint8_t  serial_number_str;
    uint8_t  asset_tag_str;
    uint8_t  part_number_str;
    uint8_t  core_count;
    uint8_t  core_enabled;
    uint8_t  thread_count;
    uint16_t processor_characteristics;
    uint16_t processor_family2;
    uint16_t core_count2;
    uint16_t core_enabled2;
    uint16_t thread_count2;
} SMBIOSType4;

/* --- Type 7: Cache Information --- */
#define SMBIOS_TYPE_CACHE_INFO 7

typedef struct __attribute__((packed)) {
    SMBIOSHeader header;
    uint8_t  socket_designation_str;
    uint16_t cache_configuration;
    uint16_t max_cache_size;     /* KB (legacy) */
    uint16_t installed_size;     /* KB (legacy) */
    uint16_t supported_sram_type;
    uint16_t current_sram_type;
    uint8_t  cache_speed;        /* ns */
    uint8_t  error_correction_type;
    uint8_t  system_cache_type;
    uint8_t  associativity;
    uint32_t max_cache_size2;    /* KB (v2.1+) */
    uint32_t installed_size2;    /* KB (v2.1+) */
} SMBIOSType7;

/* --- Type 17: Memory Device --- */
#define SMBIOS_TYPE_MEMORY_DEVICE 17

#define MEM_FORM_FACTOR_DIMM    0x09
#define MEM_FORM_FACTOR_SODIMM  0x0D
#define MEM_TYPE_DDR4           0x1A
#define MEM_TYPE_DDR5           0x22

typedef struct __attribute__((packed)) {
    SMBIOSHeader header;
    uint16_t physical_memory_array_handle;
    uint16_t memory_error_info_handle;
    uint16_t total_width;        /* bits */
    uint16_t data_width;         /* bits */
    uint16_t size;               /* MB (0x7FFF = > 32GB, use extended) */
    uint8_t  form_factor;
    uint8_t  device_set;
    uint8_t  device_locator_str;
    uint8_t  bank_locator_str;
    uint8_t  memory_type;
    uint16_t type_detail;
    uint16_t speed;              /* MT/s */
    uint8_t  manufacturer_str;
    uint8_t  serial_number_str;
    uint8_t  asset_tag_str;
    uint8_t  part_number_str;
    uint8_t  attributes;
    uint32_t extended_size;      /* MB (v2.3+) */
    uint16_t configured_memory_speed; /* MT/s (v2.4+) */
    uint16_t minimum_voltage;    /* mV (v2.5+) */
    uint16_t maximum_voltage;    /* mV (v2.5+) */
    uint16_t configured_voltage; /* mV (v2.5+) */
} SMBIOSType17;

/* --- SMBIOS Builder API ---
 * L5: String-table management algorithm.
 * SMBIOS uses a dual-pointer encoding: formatted area pointers are
 * 1-based indices into the string table at the end of the structure.
 * Empty string at table end signals termination (double null).
 */

/* Compute 8-bit checksum for EPS structures (same algorithm as ACPI). */
uint8_t smbios_checksum(const void *data, uint32_t length);

/* Initialize SMBIOS 3.0 entry point (preferred on UEFI platforms). */
void smbios3_eps_init(SMBIOS3EPS *eps, uint64_t table_addr,
                      uint32_t table_max_size, uint8_t major, uint8_t minor);

/* Build Type 0 (BIOS Information) structure.
 * Returns the number of bytes written to buf (including string table).
 * The formatted area + strings = total length. */
uint32_t smbios_type0_build(uint8_t *buf, uint32_t buf_size, uint16_t handle,
                            const char *vendor, const char *version,
                            const char *release_date, uint8_t rom_size_mb);

/* Build Type 1 (System Information) structure. */
uint32_t smbios_type1_build(uint8_t *buf, uint32_t buf_size, uint16_t handle,
                            const char *manufacturer, const char *product,
                            const char *version, const char *serial,
                            const uint8_t uuid[16], uint8_t wakeup_type);

/* Build Type 2 (Baseboard Information) structure. */
uint32_t smbios_type2_build(uint8_t *buf, uint32_t buf_size, uint16_t handle,
                            const char *manufacturer, const char *product,
                            const char *version, const char *serial,
                            uint16_t chassis_handle, uint8_t board_type);

/* Build Type 4 (Processor Information) structure. */
uint32_t smbios_type4_build(uint8_t *buf, uint32_t buf_size, uint16_t handle,
                            const char *socket, const char *manufacturer,
                            const char *version, uint32_t max_mhz,
                            uint32_t curr_mhz, uint8_t core_count,
                            uint8_t thread_count);

/* Build Type 7 (Cache Information) structure. */
uint32_t smbios_type7_build(uint8_t *buf, uint32_t buf_size, uint16_t handle,
                            const char *designation, uint32_t max_size_kb,
                            uint32_t installed_size_kb, uint8_t cache_type,
                            uint8_t associativity);

/* Build Type 17 (Memory Device) structure. */
uint32_t smbios_type17_build(uint8_t *buf, uint32_t buf_size, uint16_t handle,
                             const char *device_locator, const char *manufacturer,
                             const char *part_number, uint32_t size_mb,
                             uint16_t speed_mts, uint8_t memory_type,
                             uint8_t form_factor);

/* Internal: append a string to the SMBIOS string table region.
 * Returns the 1-based string number, or 0 on failure. */
uint8_t smbios_add_string(uint8_t *buf, uint32_t buf_size, uint32_t *offset,
                          const char *str);

/* Print a generic SMBIOS structure header. */
void smbios_print_header(const SMBIOSHeader *hdr);

/* Print Type 1 system info fields. */
void smbios_print_type1(const SMBIOSType1 *t1, const uint8_t *string_table);

/* Print Type 4 processor info fields. */
void smbios_print_type4(const SMBIOSType4 *t4, const uint8_t *string_table);

#endif /* SMBIOS_H */
