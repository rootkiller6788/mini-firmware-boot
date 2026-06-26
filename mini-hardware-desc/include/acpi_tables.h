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

/* ----- RSDP (Root System Description Pointer, ACPI 6.5 §5.2.5) ----- */

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

/* ----- SDT Header (ACPI 6.5 §5.2.6) ----- */

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

/* ----- Table entry cache ----- */

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

/* ----- FADT (Fixed ACPI Description Table, ACPI 6.5 §5.2.9) -----
 * Generic Address Structure (GAS, ACPI 6.5 §5.2.3.1)
 */

typedef struct __attribute__((packed)) {
    uint8_t  address_space_id;  /* 0=System Memory, 1=System I/O, 2=PCI Config, 3=Embedded Ctrl, 4=SMBus */
    uint8_t  register_bit_width;
    uint8_t  register_bit_offset;
    uint8_t  access_size;       /* 0=undefined, 1=byte, 2=word, 3=dword, 4=qword */
    uint64_t address;
} ACPIGenericAddress;

typedef struct __attribute__((packed)) {
    ACPISDTHeader      header;
    uint32_t           firmware_ctrl;
    uint32_t           dsdt;
    uint8_t            reserved0;
    uint8_t            preferred_pm_profile;
    uint16_t           sci_int;
    uint32_t           smi_cmd;
    uint8_t            acpi_enable;
    uint8_t            acpi_disable;
    uint8_t            s4bios_req;
    uint8_t            pstate_cnt;
    uint32_t           pm1a_evt_blk;
    uint32_t           pm1b_evt_blk;
    uint32_t           pm1a_cnt_blk;
    uint32_t           pm1b_cnt_blk;
    uint32_t           pm2_cnt_blk;
    uint32_t           pm_tmr_blk;
    uint32_t           gpe0_blk;
    uint32_t           gpe1_blk;
    uint8_t            pm1_evt_len;
    uint8_t            pm1_cnt_len;
    uint8_t            pm2_cnt_len;
    uint8_t            pm_tmr_len;
    uint8_t            gpe0_blk_len;
    uint8_t            gpe1_blk_len;
    uint8_t            gpe1_base;
    uint8_t            cst_cnt;
    uint16_t           p_lvl2_lat;
    uint16_t           p_lvl3_lat;
    uint16_t           flush_size;
    uint16_t           flush_stride;
    uint8_t            duty_offset;
    uint8_t            duty_width;
    uint8_t            day_alrm;
    uint8_t            mon_alrm;
    uint8_t            century;
    uint16_t           iapc_boot_arch;
    uint8_t            reserved1;
    uint32_t           flags;
    ACPIGenericAddress reset_reg;
    uint8_t            reset_value;
    uint16_t           arm_boot_arch;
    uint8_t            fadt_minor_version;
    uint64_t           x_firmware_ctrl;
    uint64_t           x_dsdt;
    ACPIGenericAddress x_pm1a_evt_blk;
    ACPIGenericAddress x_pm1b_evt_blk;
    ACPIGenericAddress x_pm1a_cnt_blk;
    ACPIGenericAddress x_pm1b_cnt_blk;
    ACPIGenericAddress x_pm2_cnt_blk;
    ACPIGenericAddress x_pm_tmr_blk;
    ACPIGenericAddress x_gpe0_blk;
    ACPIGenericAddress x_gpe1_blk;
    ACPIGenericAddress sleep_control_reg;
    ACPIGenericAddress sleep_status_reg;
    uint64_t           hypervisor_vendor_id;
} FADT;

/* FADT PM Profile values */
#define ACPI_PM_PROFILE_UNSPECIFIED  0
#define ACPI_PM_PROFILE_DESKTOP      1
#define ACPI_PM_PROFILE_MOBILE       2
#define ACPI_PM_PROFILE_WORKSTATION  3
#define ACPI_PM_PROFILE_ENTERPRISE   4
#define ACPI_PM_PROFILE_SOHO_SERVER  5
#define ACPI_PM_PROFILE_APPLIANCE_PC 6
#define ACPI_PM_PROFILE_PERFORMANCE  7
#define ACPI_PM_PROFILE_TABLET       8

/* FADT Boot Architecture Flags (ACPI 6.5 §5.2.9.3) */
#define ACPI_FADT_LEGACY_DEVICES     (1 << 0)
#define ACPI_FADT_8042               (1 << 1)
#define ACPI_FADT_NO_VGA             (1 << 2)
#define ACPI_FADT_NO_MSI             (1 << 3)
#define ACPI_FADT_NO_ASPM            (1 << 4)
#define ACPI_FADT_NO_CMOS_RTC        (1 << 5)

/* FADT Fixed Feature Flags (ACPI 6.5 §5.2.9.3) */
#define ACPI_FADT_WBINVD             (1 << 0)
#define ACPI_FADT_WBINVD_FLUSH       (1 << 1)
#define ACPI_FADT_PROC_C1            (1 << 2)
#define ACPI_FADT_P_LVL2_UP          (1 << 3)
#define ACPI_FADT_PWR_BUTTON         (1 << 4)
#define ACPI_FADT_SLP_BUTTON         (1 << 5)
#define ACPI_FADT_FIX_RTC            (1 << 6)
#define ACPI_FADT_RTC_S4             (1 << 7)
#define ACPI_FADT_TMR_VAL_EXT        (1 << 8)
#define ACPI_FADT_DCK_CAP            (1 << 9)
#define ACPI_FADT_RESET_REG_SUP      (1 << 10)
#define ACPI_FADT_SEALED_CASE        (1 << 11)
#define ACPI_FADT_HEADLESS           (1 << 12)
#define ACPI_FADT_CPU_SW_SLP         (1 << 13)
#define ACPI_FADT_PCI_EXP_WAK        (1 << 14)
#define ACPI_FADT_USE_PLATFORM_CLOCK (1 << 15)
#define ACPI_FADT_S4_RTC_STS_VALID   (1 << 16)
#define ACPI_FADT_REMOTE_POWER_ON    (1 << 17)
#define ACPI_FADT_APIC_CLUSTER       (1 << 18)
#define ACPI_FADT_APIC_PHYSICAL      (1 << 19)
#define ACPI_FADT_HW_REDUCED_ACPI    (1 << 20)
#define ACPI_FADT_LOW_POWER_S0_IDLE  (1 << 21)

/* ----- MADT (Multiple APIC Description Table, ACPI 6.5 §5.2.12) ----- */

#define MADT_TYPE_LAPIC              0x00  /* Processor Local APIC */
#define MADT_TYPE_IOAPIC             0x01  /* I/O APIC */
#define MADT_TYPE_INT_SRC_OVERRIDE   0x02  /* Interrupt Source Override */
#define MADT_TYPE_NMI                0x03  /* NMI Source */
#define MADT_TYPE_LAPIC_NMI          0x04  /* Local APIC NMI */
#define MADT_TYPE_LAPIC_ADDR_OVERRIDE 0x05 /* Local APIC Address Override */
#define MADT_TYPE_IOSAPIC            0x06  /* I/O SAPIC */
#define MADT_TYPE_LSAPIC             0x07  /* Local SAPIC */
#define MADT_TYPE_PLATFORM_INT_SRC   0x08  /* Platform Interrupt Sources */
#define MADT_TYPE_LX2APIC            0x09  /* Local x2APIC */
#define MADT_TYPE_LX2APIC_NMI        0x0A  /* Local x2APIC NMI */
#define MADT_TYPE_GICC               0x0B  /* GIC CPU Interface (ARM) */
#define MADT_TYPE_GICD               0x0C  /* GIC Distributor (ARM) */
#define MADT_TYPE_GIC_MSI_FRAME      0x0D  /* GIC MSI Frame (ARM) */
#define MADT_TYPE_GICR               0x0E  /* GIC Redistributor (ARM) */
#define MADT_TYPE_GIC_ITS            0x0F  /* GIC ITS (ARM) */
#define MADT_TYPE_MULTIPROC_WAKEUP   0x10  /* Multiprocessor Wakeup */

typedef struct __attribute__((packed)) {
    uint8_t  type;          /* Entry type (0 = LAPIC, 1 = IOAPIC, etc.) */
    uint8_t  length;        /* Record length */
} MADTEntryHeader;

typedef struct __attribute__((packed)) {
    MADTEntryHeader hdr;
    uint8_t         acpi_processor_id;
    uint8_t         apic_id;
    uint32_t        flags;        /* bit 0 = enabled */
} MADTLAPIC;

typedef struct __attribute__((packed)) {
    MADTEntryHeader hdr;
    uint8_t         io_apic_id;
    uint8_t         reserved;
    uint32_t        io_apic_address;
    uint32_t        global_system_interrupt_base;
} MADTIOAPIC;

typedef struct __attribute__((packed)) {
    MADTEntryHeader hdr;
    uint8_t         bus_source;
    uint8_t         irq_source;
    uint32_t        global_system_interrupt;
    uint16_t        flags;        /* bit 0=active low, bit 1=level triggered */
} MADTIntSrcOverride;

typedef struct __attribute__((packed)) {
    MADTEntryHeader hdr;
    uint8_t         acpi_processor_id;
    uint16_t        flags;
    uint8_t         lint_number;
} MADTLAPICNMI;

typedef struct __attribute__((packed)) {
    MADTEntryHeader hdr;
    uint16_t        reserved;
    uint64_t        x2apic_address;
} MADTLAPICAddrOverride;

typedef struct __attribute__((packed)) {
    MADTEntryHeader hdr;
    uint16_t        reserved;
    uint32_t        x2apic_id;
    uint32_t        flags;
    uint32_t        acpi_processor_uid;
} MADTLX2APIC;

typedef struct __attribute__((packed)) {
    ACPISDTHeader    header;
    uint32_t         lapic_address;
    uint32_t         flags;           /* bit 0 = PC-AT compatible dual-8259 */
} MADT;

/* ----- MCFG (PCI Express MMCONFIG, ACPI 6.5 §5.2.29) ----- */

typedef struct __attribute__((packed)) {
    uint64_t base_address;
    uint16_t pci_segment_group;
    uint8_t  start_bus;
    uint8_t  end_bus;
    uint32_t reserved;
} MCFGEntry;

typedef struct __attribute__((packed)) {
    ACPISDTHeader header;
    uint64_t      reserved;
    MCFGEntry     entries[];
} MCFG;

#define ACPI_MAX_MCFG_ENTRIES 64

/* ----- HPET (High Precision Event Timer, ACPI 6.5 §5.2.28) ----- */

typedef struct __attribute__((packed)) {
    ACPISDTHeader header;
    uint32_t      event_timer_block_id;
    ACPIGenericAddress base_address;
    uint8_t       hpet_number;
    uint16_t      min_clock_tick;  /* in femtoseconds (10^-15) */
    uint8_t       page_protection; /* bit 0=4KB, bit 1=64KB OSPM */
} HPET;

/* ----- FADT parsed data ----- */

typedef struct {
    bool     has_fadt;
    uint8_t  pm_profile;
    uint16_t sci_int;
    uint32_t smi_cmd;
    uint8_t  acpi_enable;
    uint8_t  acpi_disable;
    uint32_t pm_tmr_blk;         /* legacy PM Timer block (I/O) */
    uint8_t  pm_tmr_len;
    uint64_t x_pm_tmr_blk;       /* extended PM Timer (GAS) */
    uint32_t flags;
    uint16_t iapc_boot_arch;
    bool     has_reset_reg;
    uint64_t reset_address;
    uint8_t  reset_value;
    uint32_t dsdt_address;
    bool     hw_reduced;
} FADTInfo;

/* ----- MADT parsed data ----- */

typedef struct {
    uint8_t  acpi_processor_id;
    uint8_t  apic_id;
    uint32_t flags;
} MADTLAPICInfo;

typedef struct {
    uint8_t  io_apic_id;
    uint32_t io_apic_address;
    uint32_t gsi_base;
} MADTIOAPICInfo;

typedef struct {
    uint8_t  bus;
    uint8_t  irq;
    uint32_t gsi;
    uint16_t flags;
} MADTIntOverrideInfo;

#define MADT_MAX_LAPICS      256
#define MADT_MAX_IOAPICS     32
#define MADT_MAX_OVERRIDES   64

typedef struct {
    bool               has_madt;
    uint32_t           lapic_address;
    bool               pic_cascade;    /* dual-8259 mode */
    MADTLAPICInfo      lapics[MADT_MAX_LAPICS];
    size_t             lapic_count;
    MADTIOAPICInfo     ioapics[MADT_MAX_IOAPICS];
    size_t             ioapic_count;
    MADTIntOverrideInfo overrides[MADT_MAX_OVERRIDES];
    size_t             override_count;
} MADTInfo;

/* ----- MCFG parsed data ----- */

typedef struct {
    uint64_t base;
    uint16_t segment;
    uint8_t  bus_start;
    uint8_t  bus_end;
} MCFGBusEntry;

typedef struct {
    bool          has_mcfg;
    MCFGBusEntry  buses[ACPI_MAX_MCFG_ENTRIES];
    size_t        entry_count;
} MCFGInfo;

/* ----- HPET parsed data ----- */

typedef struct {
    bool     has_hpet;
    uint32_t block_id;
    uint64_t base_address;
    uint8_t  hpet_number;
    uint16_t min_clock_tick_fs;  /* femtoseconds */
    bool     is_4kb_protected;
    bool     is_64kb_protected;
} HPETInfo;

/* ----- Core ACPI API ----- */

bool acpi_find_rsdp(ACPITableList *list, const uint8_t *bios_mem, size_t bios_size);
bool acpi_parse_xsdt(ACPITableList *list, const uint8_t *bios_mem);
bool acpi_parse_rsdt(ACPITableList *list, const uint8_t *bios_mem);
bool acpi_find_table(const ACPITableList *list, const char *signature, ACPITableEntry *entry);
bool acpi_validate_checksum(const uint8_t *table, uint32_t length);
void acpi_print_tables(const ACPITableList *list);
const char *acpi_table_type_name(const char *signature);

/* ----- FADT API (ACPI 6.5 §5.2.9) ----- */

bool acpi_parse_fadt(const uint8_t *fadt_raw, uint32_t length, FADTInfo *info);
const char *acpi_pm_profile_name(uint8_t profile);
void acpi_print_fadt(const FADTInfo *info);

/* ----- MADT API (ACPI 6.5 §5.2.12) ----- */

bool acpi_parse_madt(const uint8_t *madt_raw, uint32_t length, MADTInfo *info);
const char *acpi_madt_entry_type_name(uint8_t type);
void acpi_print_madt(const MADTInfo *info);
uint32_t acpi_madt_irq_to_gsi(const MADTInfo *info, uint8_t irq);

/* ----- MCFG API (ACPI 6.5 §5.2.29) ----- */

bool acpi_parse_mcfg(const uint8_t *mcfg_raw, uint32_t length, MCFGInfo *info);
uint64_t acpi_mcfg_get_ecam_base(const MCFGInfo *info, uint8_t bus, uint8_t device, uint8_t function);
void acpi_print_mcfg(const MCFGInfo *info);

/* ----- HPET API (ACPI 6.5 §5.2.28) ----- */

bool acpi_parse_hpet(const uint8_t *hpet_raw, uint32_t length, HPETInfo *info);
uint64_t acpi_hpet_ms_to_ticks(const HPETInfo *info, uint32_t ms);
void acpi_print_hpet(const HPETInfo *info);

#endif
