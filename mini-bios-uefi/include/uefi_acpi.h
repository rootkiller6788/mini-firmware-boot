#ifndef UEFI_ACPI_H
#define UEFI_ACPI_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* ================================================================
 * L1-L5: ACPI (Advanced Configuration and Power Interface)
 *
 * Knowledge points:
 *   L1: struct definitions for RSDP, XSDT, MADT, FADT, MCFG
 *   L2: ACPI table architecture (Root → XSDT → [MADT, FADT, ...])
 *   L3: UEFI ConfigurationTable → ACPI RSDP → XSDT chain
 *   L4: ACPI Specification 6.5 §5.2 (Table definitions)
 *   L5: RSDP search algorithm (signature scan in EBDA + BIOS ROM),
 *       ACPI table checksum (sum-of-bytes mod 256)
 * ================================================================ */

/* ACPI signatures (ASCII, not null-terminated) */
#define ACPI_RSDP_SIG  0x2052545020445352ULL  /* "RSD PTR " */
#define ACPI_XSDT_SIG  0x54445358             /* "XSDT" */
#define ACPI_RSDT_SIG  0x54445352             /* "RSDT" */
#define ACPI_MADT_SIG  0x43495041             /* "APIC" */
#define ACPI_FADT_SIG  0x50434146             /* "FACP" */
#define ACPI_MCFG_SIG  0x4746434D             /* "MCFG" */
#define ACPI_DSDT_SIG  0x54445344             /* "DSDT" */
#define ACPI_SSDT_SIG  0x54445353             /* "SSDT" */
#define ACPI_HPET_SIG  0x54455048             /* "HPET" */
#define ACPI_BGRT_SIG  0x54524742             /* "BGRT" */
#define ACPI_SPCR_SIG  0x52435053             /* "SPCR" */

/* ACPI RSDP search ranges */
#define ACPI_EBDA_START      0x00080000
#define ACPI_EBDA_SIZE       0x00020000        /* 128KB after 512KB */
#define ACPI_BIOS_BASE_START 0x000E0000
#define ACPI_BIOS_BASE_SIZE  0x00020000        /* 128KB at end of 1MB */

/* Generic ACPI table header (SDT) */
#pragma pack(push, 1)
typedef struct {
    uint32_t signature;      /* 4-char ASCII */
    uint32_t length;         /* entire table length including header */
    uint8_t  revision;
    uint8_t  checksum;       /* sum of all bytes in table = 0 */
    uint8_t  oem_id[6];
    uint8_t  oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} ACPISDTHeader;

/* Root System Description Pointer */
typedef struct {
    uint64_t signature;      /* "RSD PTR " */
    uint8_t  checksum;       /* sum of first 20 bytes = 0 */
    uint8_t  oem_id[6];
    uint8_t  revision;       /* 0 = ACPI 1.0, 2 = ACPI 2.0+ */
    uint32_t rsdt_address;   /* 32-bit physical address of RSDT */
    /* ACPI 2.0+ fields follow */
    uint32_t length;
    uint64_t xsdt_address;   /* 64-bit physical address of XSDT */
    uint8_t  extended_checksum; /* sum of entire table = 0 */
    uint8_t  reserved[3];
} ACPIRSDP;

/* Extended System Description Table */
typedef struct {
    ACPISDTHeader header;
    uint64_t      table_offsets[];  /* array of 64-bit pointers to other SDTs */
} ACPIXSDT;

/* Multiple APIC Description Table (MADT) */
#define ACPI_MADT_LAPIC        0x00   /* Local APIC */
#define ACPI_MADT_IOAPIC       0x01   /* I/O APIC */
#define ACPI_MADT_ISO          0x02   /* Interrupt Source Override */
#define ACPI_MADT_NMI          0x04   /* NMI Source */
#define ACPI_MADT_LAPIC_OVERRIDE 0x05 /* Local APIC Address Override */
#define ACPI_MADT_X2APIC       0x09   /* x2APIC */
#define ACPI_MADT_X2APIC_NMI   0x0A   /* x2APIC NMI */

typedef struct {
    ACPISDTHeader header;
    uint32_t      local_apic_address;
    uint32_t      flags;             /* bit 0 = PCAT compat (PIC mode) */
} ACPIMADT;

typedef struct {
    uint8_t  type;
    uint8_t  length;
} ACPIMADTEntryHeader;

typedef struct {
    ACPIMADTEntryHeader header;
    uint8_t  acpi_processor_id;
    uint8_t  apic_id;
    uint32_t flags;      /* bit 0 = enabled */
} ACPIMADTLAPIC;

typedef struct {
    ACPIMADTEntryHeader header;
    uint8_t  ioapic_id;
    uint8_t  reserved;
    uint32_t ioapic_address;
    uint32_t global_system_interrupt_base;
} ACPIMADTIOAPIC;

typedef struct {
    ACPIMADTEntryHeader header;
    uint8_t  bus_source;
    uint8_t  irq_source;
    uint32_t global_system_interrupt;
    uint16_t flags;
} ACPIMADTISO;

/* Fixed ACPI Description Table (FADT) */
typedef struct {
    ACPISDTHeader header;
    uint32_t firmware_ctrl;
    uint32_t dsdt;             /* pointer to DSDT */
    uint8_t  reserved0;
    uint8_t  preferred_pm_profile;
    uint16_t sci_interrupt;
    uint32_t smi_command_port;
    uint8_t  acpi_enable;
    uint8_t  acpi_disable;
    uint8_t  s4bios_req;
    uint8_t  pstate_control;
    uint32_t pm1a_event_block;
    uint32_t pm1b_event_block;
    uint32_t pm1a_control_block;
    uint32_t pm1b_control_block;
    uint32_t pm2_control_block;
    uint32_t pm_timer_block;
    uint32_t gpe0_block;
    uint32_t gpe1_block;
    uint8_t  pm1_event_len;
    uint8_t  pm1_control_len;
    uint8_t  pm2_control_len;
    uint8_t  pm_timer_len;
    uint8_t  gpe0_block_len;
    uint8_t  gpe1_block_len;
    uint8_t  gpe1_base;
    uint8_t  cstate_control;
    uint16_t worst_c2_latency;
    uint16_t worst_c3_latency;
    uint16_t flush_size;
    uint16_t flush_stride;
    uint8_t  duty_offset;
    uint8_t  duty_width;
    uint8_t  day_alarm;
    uint8_t  month_alarm;
    uint8_t  century;
    uint16_t boot_architecture_flags;
    uint8_t  reserved1;
    uint32_t flags;
    /* ACPI 2.0+ Generic Address Structures follow (12 bytes each) */
} ACPIFADT;

/* MCFG (PCI Express Memory-mapped Configuration Space) */
typedef struct {
    uint64_t base_address;
    uint16_t pci_segment_group;
    uint8_t  start_bus;
    uint8_t  end_bus;
    uint32_t reserved;
} ACPIMCFGEntry;

typedef struct {
    ACPISDTHeader header;
    uint64_t      reserved;
    ACPIMCFGEntry entries[];
} ACPIMCFG;

#pragma pack(pop)

/* Parsed ACPI table collection */
#define ACPI_MAX_TABLES 64
#define ACPI_MAX_LAPIC  256
#define ACPI_MAX_IOAPIC 8

typedef struct {
    uint8_t  acpi_id;
    uint8_t  apic_id;
    bool     enabled;
} ACPILAPICInfo;

typedef struct {
    uint8_t  ioapic_id;
    uint32_t ioapic_address;
    uint32_t gsi_base;
} ACPIIOAPICInfo;

typedef struct {
    ACPIRSDP  rsdp;
    bool      has_xsdt;
    uint32_t  table_count;
    uint32_t  lapic_count;
    uint32_t  ioapic_count;
    bool      pic_mode;        /* true = 8259 PIC present */
    uint32_t  local_apic_addr;
    uint64_t  xsdt_address;
    uint32_t  rsdt_address;
    ACPILAPICInfo   lapics[ACPI_MAX_LAPIC];
    ACPIIOAPICInfo  ioapics[ACPI_MAX_IOAPIC];
    uint32_t  dsdt_address;
    uint64_t  mcfg_base;
    uint16_t  mcfg_segment;
    bool      acpi_20_plus;
} ACPISystemInfo;

/* ---- Function declarations ---- */
bool            acpi_rsdp_checksum(const ACPIRSDP *rsdp);
bool            acpi_sdt_checksum(const ACPISDTHeader *header);
bool            acpi_find_rsdp(ACPIRSDP *rsdp, const uint8_t *bios_rom, size_t rom_size);
bool            acpi_parse_xsdt(ACPISystemInfo *info, const uint8_t *tables, size_t size);
bool            acpi_parse_madt(ACPISystemInfo *info, const ACPISDTHeader *madt);
bool            acpi_parse_mcfg(ACPISystemInfo *info, const ACPISDTHeader *mcfg);
void            acpi_init_info(ACPISystemInfo *info);
int             acpi_find_table(const uint8_t *tables, size_t size,
                                 uint32_t sig, uint32_t *offset);
void            acpi_print_system_info(const ACPISystemInfo *info);
void            acpi_print_sdt_header(const ACPISDTHeader *hdr);
const char     *acpi_sig_to_name(uint32_t sig);

#endif /* UEFI_ACPI_H */