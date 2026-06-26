#ifndef ACPI_TABLES_H
#define ACPI_TABLES_H

#include <stdbool.h>
#include <stdint.h>

/* ============================================================================
 * ACPI Table Definitions ? L1: Core Definitions
 * Reference: ACPI Specification v6.5 (UEFI Forum)
 *
 * ACPI tables are firmware-to-OS data structures describing platform
 * hardware topology, power management, and interrupt routing.
 * L2: OS discovery via RSDP?RSDT/XSDT?child tables chain.
 * L4: Table checksum algorithm ? 8-bit sum must equal 0 (mod 256).
 * ============================================================================ */

/* RSDP ? Root System Description Pointer */
#define ACPI_RSDP_SIGNATURE  0x2052545020445352ULL /* "RSD PTR " */
#define ACPI_RSDP_V1_SIZE    20
#define ACPI_RSDP_V2_SIZE    36

typedef struct __attribute__((packed)) {
    uint64_t signature;
    uint8_t  checksum;
    char     oem_id[6];
    uint8_t  revision;
    uint32_t rsdt_address;
    uint32_t length;
    uint64_t xsdt_address;
    uint8_t  extended_checksum;
    uint8_t  reserved[3];
} ACPIRSDP;

/* SDT Header (common 36-byte prefix for all ACPI tables) */
#define ACPI_SDT_HEADER_SIZE  36

typedef struct __attribute__((packed)) {
    char     signature[4];
    uint32_t length;
    uint8_t  revision;
    uint8_t  checksum;
    char     oem_id[6];
    char     oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} ACPISDTHeader;

/* RSDT / XSDT ? Root System Description Table variants */
#define ACPI_RSDT_SIGNATURE  "RSDT"
#define ACPI_XSDT_SIGNATURE  "XSDT"
#define ACPI_MAX_TABLES      32

typedef struct __attribute__((packed)) {
    ACPISDTHeader header;
    uint32_t      table_entries[];
} ACPIRSDT;

typedef struct __attribute__((packed)) {
    ACPISDTHeader header;
    uint64_t      table_entries[];
} ACPIXSDT;

/* FADT ? Fixed ACPI Description Table (signature "FACP") */
#define ACPI_FADT_SIGNATURE  "FACP"

#define FADT_BOOT_LEGACY_DEVICES       (1 << 0)
#define FADT_BOOT_8042                 (1 << 1)
#define FADT_BOOT_VGA_NOT_PRESENT      (1 << 2)
#define FADT_BOOT_MSI_NOT_SUPPORTED    (1 << 3)
#define FADT_BOOT_PCIE_ASPM_CTRL       (1 << 4)
#define FADT_BOOT_CMOS_RTC_NOT_PRESENT (1 << 5)
#define FADT_FEAT_WBINVD              (1 << 0)
#define FADT_FEAT_WBINVD_FLUSH        (1 << 1)
#define FADT_FEAT_PROC_C1             (1 << 2)
#define FADT_FEAT_PWR_BUTTON          (1 << 4)
#define FADT_FEAT_SLP_BUTTON          (1 << 5)
#define FADT_FEAT_FIX_RTC             (1 << 6)
#define FADT_FEAT_TMR_VAL_EXT         (1 << 8)
#define FADT_FEAT_RESET_REG_SUP       (1 << 10)
#define FADT_FEAT_USE_PLATFORM_CLOCK  (1 << 15)
#define FADT_FEAT_APIC_CLUSTER        (1 << 18)
#define FADT_FEAT_APIC_PHYSICAL       (1 << 19)
#define FADT_FEAT_HW_REDUCED          (1 << 20)

#define FADT_PM_PROFILE_UNSPECIFIED   0
#define FADT_PM_PROFILE_DESKTOP       1
#define FADT_PM_PROFILE_MOBILE        2
#define FADT_PM_PROFILE_WORKSTATION   3
#define FADT_PM_PROFILE_ENTERPRISE    4
#define FADT_PM_PROFILE_PERF_SERVER   7
#define FADT_PM_PROFILE_TABLET        8

typedef struct __attribute__((packed)) {
    ACPISDTHeader header;
    uint32_t firmware_ctrl;
    uint32_t dsdt;
    uint8_t  reserved0;
    uint8_t  preferred_pm_profile;
    uint16_t sci_int;
    uint32_t smi_cmd;
    uint8_t  acpi_enable;
    uint8_t  acpi_disable;
    uint8_t  s4bios_req;
    uint8_t  pstate_cnt;
    uint32_t pm1a_evt_blk;
    uint32_t pm1b_evt_blk;
    uint32_t pm1a_cnt_blk;
    uint32_t pm1b_cnt_blk;
    uint32_t pm2_cnt_blk;
    uint32_t pm_tmr_blk;
    uint32_t gpe0_blk;
    uint32_t gpe1_blk;
    uint8_t  pm1_evt_len;
    uint8_t  pm1_cnt_len;
    uint8_t  pm2_cnt_len;
    uint8_t  pm_tmr_len;
    uint8_t  gpe0_blk_len;
    uint8_t  gpe1_blk_len;
    uint8_t  gpe1_base;
    uint8_t  cst_cnt;
    uint16_t p_lvl2_lat;
    uint16_t p_lvl3_lat;
    uint16_t flush_size;
    uint16_t flush_stride;
    uint8_t  duty_offset;
    uint8_t  duty_width;
    uint8_t  day_alrm;
    uint8_t  mon_alrm;
    uint8_t  century;
    uint16_t iapc_boot_arch;
    uint8_t  reserved1;
    uint32_t flags;
    uint8_t  reset_reg[12];
    uint8_t  reset_value;
    uint16_t arm_boot_arch;
    uint8_t  fadt_minor_version;
    uint64_t x_firmware_ctrl;
    uint64_t x_dsdt;
    uint8_t  x_pm1a_evt_blk[12];
    uint8_t  x_pm1b_evt_blk[12];
    uint8_t  x_pm1a_cnt_blk[12];
    uint8_t  x_pm1b_cnt_blk[12];
    uint8_t  x_pm2_cnt_blk[12];
    uint8_t  x_pm_tmr_blk[12];
    uint8_t  x_gpe0_blk[12];
    uint8_t  x_gpe1_blk[12];
    uint8_t  sleep_control_reg[12];
    uint8_t  sleep_status_reg[12];
    uint8_t  hypervisor_vendor_id[8];
} ACPIFADT;

/* MADT ? Multiple APIC Description Table (signature "APIC") */
#define ACPI_MADT_SIGNATURE  "APIC"

#define MADT_ENTRY_LAPIC              0x00
#define MADT_ENTRY_IOAPIC             0x01
#define MADT_ENTRY_INT_SRC_OVERRIDE   0x02
#define MADT_ENTRY_NMI_SOURCE         0x03
#define MADT_ENTRY_LAPIC_NMI          0x04
#define MADT_ENTRY_LAPIC_ADDR_OVERRIDE 0x05
#define MADT_ENTRY_LX2APIC            0x09
#define MADT_ENTRY_LX2APIC_NMI        0x0A

#define MADT_LAPIC_ENABLED  (1 << 0)
#define MADT_MAX_ENTRIES    64

typedef struct __attribute__((packed)) {
    uint8_t type;
    uint8_t length;
} MADTEntryHeader;

typedef struct __attribute__((packed)) {
    MADTEntryHeader header;
    uint8_t  acpi_processor_id;
    uint8_t  apic_id;
    uint32_t flags;
} MADTLAPIC;

typedef struct __attribute__((packed)) {
    MADTEntryHeader header;
    uint8_t  ioapic_id;
    uint8_t  reserved;
    uint32_t ioapic_address;
    uint32_t gsi_base;
} MADTIOAPIC;

typedef struct __attribute__((packed)) {
    MADTEntryHeader header;
    uint8_t  bus;
    uint8_t  source;
    uint32_t gsi;
    uint16_t flags;
} MADTIntSrcOverride;

typedef struct __attribute__((packed)) {
    MADTEntryHeader header;
    uint8_t  acpi_processor_id;
    uint16_t flags;
    uint8_t  lint_number;
} MADTLAPICNMI;

typedef struct __attribute__((packed)) {
    ACPISDTHeader header;
    uint32_t local_apic_address;
    uint32_t flags;
    uint8_t  entry_data[];
} ACPIMADT;

/* MCFG ? PCIe Memory Mapped Configuration (signature "MCFG") */
#define ACPI_MCFG_SIGNATURE  "MCFG"
#define MCFG_MAX_SEGMENTS    16

typedef struct __attribute__((packed)) {
    uint64_t base_address;
    uint16_t pci_segment_group;
    uint8_t  start_bus;
    uint8_t  end_bus;
    uint32_t reserved;
} MCFGEntry;

typedef struct __attribute__((packed)) {
    ACPISDTHeader header;
    uint64_t reserved;
    MCFGEntry entries[];
} ACPIMCFG;

/* HPET ? High Precision Event Timer (signature "HPET") */
#define ACPI_HPET_SIGNATURE  "HPET"
#define HPET_EVENT_TIMER_BLOCK_ID  0x8086A201

typedef struct __attribute__((packed)) {
    ACPISDTHeader header;
    uint32_t event_timer_block_id;
    uint8_t  address_space_id;
    uint8_t  register_bit_width;
    uint8_t  register_bit_offset;
    uint8_t  reserved0;
    uint64_t address;
    uint8_t  hpet_number;
    uint16_t minimum_tick;
    uint8_t  page_protection;
} ACPIHPET;

/* BGRT ? Boot Graphics Resource Table (signature "BGRT")
 * L7 Application: OS uses BGRT to display OEM logo during boot sequence. */
#define ACPI_BGRT_SIGNATURE  "BGRT"

typedef struct __attribute__((packed)) {
    ACPISDTHeader header;
    uint16_t version;
    uint8_t  status;
    uint8_t  image_type;
    uint64_t image_address;
    uint32_t image_offset_x;
    uint32_t image_offset_y;
} ACPIBGRT;

/* === API: Table Construction and Validation ===
 * L5: Algorithms for building valid ACPI tables with correct checksums.
 * Each builder function handles L1 structure definitions, L3 memory layout,
 * and L4 standards compliance (ACPI ?5.2 checksum rule).
 */

/* Compute 8-bit ACPI checksum. Returns value such that
 * (sum_over_all_bytes(table) + checksum_byte) mod 256 = 0.
 * O(n) complexity. */
uint8_t acpi_checksum(const void *data, uint32_t length);

/* Build RSDP. Returns size (20 for v1, 36 for v2+). */
uint32_t acpi_rsdp_build(ACPIRSDP *rsdp, uint64_t rsdt_addr, uint64_t xsdt_addr,
                         const char *oem_id, uint8_t revision);

/* Initialize SDT header fields. */
void acpi_sdt_header_init(ACPISDTHeader *header, const char signature[4],
                          uint32_t length, uint8_t revision,
                          const char *oem_id, const char *oem_table_id);

/* Build RSDT from 32-bit child addresses. Returns total length. */
uint32_t acpi_rsdt_build(ACPIRSDT *rsdt, const uint32_t *table_addrs,
                         uint32_t count, const char *oem_id);

/* Build XSDT from 64-bit child addresses. */
uint32_t acpi_xsdt_build(ACPIXSDT *xsdt, const uint64_t *table_addrs,
                         uint32_t count, const char *oem_id);

/* Build MADT with LAPIC, IOAPIC, and interrupt source override entries.
 * buffer must be large enough to hold header + all entries.
 * Returns total length, 0 on error. */
uint32_t acpi_madt_build(uint8_t *buffer, uint32_t buffer_size,
                         const MADTLAPIC *lapics, uint32_t lapic_count,
                         const MADTIOAPIC *ioapics, uint32_t ioapic_count,
                         const MADTIntSrcOverride *overrides, uint32_t override_count,
                         const char *oem_id);

/* Build FADT. Returns sizeof(ACPIFADT). */
uint32_t acpi_fadt_build(ACPIFADT *fadt, uint32_t dsdt_addr, uint16_t sci_int,
                         uint32_t pm1a_evt, uint32_t pm1a_cnt, uint32_t pm_tmr,
                         uint16_t boot_arch, uint32_t feature_flags,
                         uint8_t pm_profile, const char *oem_id);

/* Build MCFG from PCI segment entries. */
uint32_t acpi_mcfg_build(ACPIMCFG *mcfg, const MCFGEntry *segments,
                         uint32_t count, const char *oem_id);

/* Build HPET table. */
uint32_t acpi_hpet_build(ACPIHPET *hpet, uint64_t hpet_addr, uint8_t hpet_num,
                         const char *oem_id);

/* Build BGRT ? boot graphics table. */
uint32_t acpi_bgrt_build(ACPIBGRT *bgrt, uint64_t image_addr,
                         uint32_t offset_x, uint32_t offset_y,
                         uint8_t image_type, const char *oem_id);

/* Validate checksum ? returns true if sum of all bytes mod 256 = 0. */
bool acpi_validate_checksum(const void *table, uint32_t length);

/* Print SDT header information. */
void acpi_print_header(const ACPISDTHeader *header);

/* Find child table index in RSDT/XSDT by 4-char signature. -1 if absent. */
int32_t acpi_find_table_rsdt(const ACPIRSDT *rsdt, const char signature[4]);
int32_t acpi_find_table_xsdt(const ACPIXSDT *xsdt, const char signature[4]);

#endif /* ACPI_TABLES_H */
