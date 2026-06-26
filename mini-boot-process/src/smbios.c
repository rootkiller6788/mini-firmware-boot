#include "smbios.h"
#include <stdio.h>
#include <string.h>

/* ============================================================================
 * SMBIOS Checksum - L4
 * Same 8-bit sum-to-zero algorithm as ACPI. SMBIOS 3.4.0 Section 5.2.1.
 * Complexity: O(n).
 * ============================================================================ */

uint8_t smbios_checksum(const void *data, uint32_t length)
{
    if (!data || length == 0) return 0;
    const uint8_t *bytes = (const uint8_t *)data;
    uint8_t sum = 0;
    for (uint32_t i = 0; i < length; i++) sum += bytes[i];
    return (uint8_t)(-sum);
}

/* ============================================================================
 * SMBIOS 3.0 Entry Point Initialization - L1/L4
 * The SMBIOS 3.0 EPS provides a 64-bit pointer to the structure table,
 * enabling tables above 4GB. Located by OS at 0xF0000-0xFFFFF on
 * 16-byte boundaries, searching for "_SM3_" anchor string.
 * L4: SMBIOS 3.4.0 Section 5.2.2 - Entry Point Structure.
 * ============================================================================ */

void smbios3_eps_init(SMBIOS3EPS *eps, uint64_t table_addr,
                      uint32_t table_max_size, uint8_t major, uint8_t minor)
{
    if (!eps) return;
    memset(eps, 0, sizeof(SMBIOS3EPS));
    memcpy(eps->anchor, SMBIOS3_ANCHOR_STR, 5);
    eps->length = SMBIOS3_EPS_SIZE;
    eps->major_version = major;
    eps->minor_version = minor;
    eps->docrev = 0;
    eps->entry_point_revision = 1;
    eps->reserved = 0;
    eps->structure_table_max_size = table_max_size;
    eps->structure_table_address = table_addr;
    eps->checksum = smbios_checksum(eps, SMBIOS3_EPS_SIZE);
}

/* ============================================================================
 * SMBIOS String Table Management - L3/L5
 *
 * SMBIOS structures use a dual-section layout:
 *   1. Formatted area (fixed-length fields, same for all structures of a type)
 *   2. String table (variable-length, null-terminated strings)
 *
 * Fields in the formatted area reference strings by 1-based index.
 * Index 0 means "no string". The string table ends with a double-null.
 *
 * L3: Engineering structure - compact encoding that avoids pointers,
 *     making the table relocatable and suitable for memory-mapped access.
 *
 * L5: String append algorithm - maintains offset pointer, returns 1-based
 *     index. O(k) per string where k = string length.
 * ============================================================================ */

uint8_t smbios_add_string(uint8_t *buf, uint32_t buf_size, uint32_t *offset,
                          const char *str)
{
    if (!buf || !offset || !str || *offset >= buf_size) return 0;

    uint32_t pos = *offset;
    uint32_t len = (uint32_t)strlen(str);

    if (pos + len + 1 > buf_size) return 0;

    memcpy(&buf[pos], str, len);
    buf[pos + len] = '\0';
    *offset = pos + len + 1;

    /* Return 1-based string number */
    uint8_t str_num = 1;
    for (uint32_t i = 0; i < pos; i++) {
        if (buf[i] == '\0') str_num++;
    }
    return str_num;
}

/* ============================================================================
 * SMBIOS Type 0: BIOS Information - L1/L6
 *
 * Provides vendor, version, release date, and ROM size for system BIOS.
 * This information is used by OS management tools (dmidecode, MSInfo32)
 * to report platform firmware details.
 *
 * L6: Canonical problem - platform identification and inventory.
 *      Every x86 system must provide BIOS information via SMBIOS Type 0.
 *
 * BIOS Characteristics bitmask encodes supported features per SMBIOS 3.4.0
 * Section 7.1. We encode a modern UEFI platform with PCI, PnP, USB, and
 * selectable boot support.
 * ============================================================================ */

uint32_t smbios_type0_build(uint8_t *buf, uint32_t buf_size, uint16_t handle,
                            const char *vendor, const char *version,
                            const char *release_date, uint8_t rom_size_mb)
{
    if (!buf || buf_size < sizeof(SMBIOSType0) + 4) return 0;

    memset(buf, 0, buf_size);
    SMBIOSType0 *t0 = (SMBIOSType0 *)buf;
    t0->header.type   = SMBIOS_TYPE_BIOS_INFO;
    t0->header.length = sizeof(SMBIOSType0);
    t0->header.handle = handle;
    t0->bios_start_segment = 0xE000;
    t0->bios_rom_size = 0xFF;  /* Use extended field */

    /* BIOS Characteristics: modern UEFI system */
    t0->bios_characteristics = BIOS_CHAR_PCI_SUPPORTED
                             | BIOS_CHAR_PNP_SUPPORTED
                             | BIOS_CHAR_UPGRADEABLE
                             | BIOS_CHAR_SHADOWING_ALLOWED
                             | BIOS_CHAR_BOOT_FROM_CD
                             | BIOS_CHAR_SELECTABLE_BOOT
                             | BIOS_CHAR_EDD_SUPPORTED
                             | BIOS_CHAR_UEFI_SUPPORTED;

    t0->system_bios_major_release = 1;
    t0->system_bios_minor_release = 0;
    t0->extended_bios_rom_size = rom_size_mb;

    /* Build string table */
    uint32_t soff = sizeof(SMBIOSType0);
    t0->vendor_str  = smbios_add_string(buf, buf_size, &soff, vendor ? vendor : "mini-boot");
    t0->bios_version_str = smbios_add_string(buf, buf_size, &soff, version ? version : "1.0");
    t0->bios_release_date_str = smbios_add_string(buf, buf_size, &soff, release_date ? release_date : "06/24/2026");

    /* Terminate string table with double-null */
    if (soff < buf_size) {
        buf[soff] = '\0';
        soff++;
    }

    return soff;
}

/* ============================================================================
 * SMBIOS Type 1: System Information - L1
 *
 * Identifies the system manufacturer, product name, version, serial number,
 * UUID (RFC 4122), and wake-up type. The UUID uniquely identifies the system
 * for asset management and licensing.
 * ============================================================================ */

uint32_t smbios_type1_build(uint8_t *buf, uint32_t buf_size, uint16_t handle,
                            const char *manufacturer, const char *product,
                            const char *version, const char *serial,
                            const uint8_t uuid[16], uint8_t wakeup_type)
{
    if (!buf || buf_size < sizeof(SMBIOSType1) + 4) return 0;

    memset(buf, 0, buf_size);
    SMBIOSType1 *t1 = (SMBIOSType1 *)buf;
    t1->header.type   = SMBIOS_TYPE_SYSTEM_INFO;
    t1->header.length = sizeof(SMBIOSType1);
    t1->header.handle = handle;
    t1->wakeup_type = wakeup_type;
    if (uuid) memcpy(t1->uuid, uuid, 16);

    uint32_t soff = sizeof(SMBIOSType1);
    t1->manufacturer_str = smbios_add_string(buf, buf_size, &soff, manufacturer ? manufacturer : "mini-boot Project");
    t1->product_name_str = smbios_add_string(buf, buf_size, &soff, product ? product : "mini-everything Platform");
    t1->version_str      = smbios_add_string(buf, buf_size, &soff, version ? version : "1.0");
    t1->serial_number_str = smbios_add_string(buf, buf_size, &soff, serial ? serial : "SN00000001");
    t1->sku_number_str    = smbios_add_string(buf, buf_size, &soff, "SKU1");
    t1->family_str        = smbios_add_string(buf, buf_size, &soff, "mini-everything Family");

    if (soff < buf_size) { buf[soff] = '\0'; soff++; }
    return soff;
}

/* ============================================================================
 * SMBIOS Type 2: Baseboard Information - L1
 *
 * Describes the motherboard (baseboard): manufacturer, product, version,
 * serial number, board type, and chassis reference handle.
 * ============================================================================ */

uint32_t smbios_type2_build(uint8_t *buf, uint32_t buf_size, uint16_t handle,
                            const char *manufacturer, const char *product,
                            const char *version, const char *serial,
                            uint16_t chassis_handle, uint8_t board_type)
{
    if (!buf || buf_size < sizeof(SMBIOSType2) + 4) return 0;

    memset(buf, 0, buf_size);
    SMBIOSType2 *t2 = (SMBIOSType2 *)buf;
    t2->header.type   = SMBIOS_TYPE_BASEBOARD_INFO;
    t2->header.length = sizeof(SMBIOSType2);
    t2->header.handle = handle;
    t2->feature_flags = 0x09;  /* Motherboard is replaceable + requires daughterboard */
    t2->chassis_handle = chassis_handle;
    t2->board_type = board_type;
    t2->num_contained_handles = 0;

    uint32_t soff = sizeof(SMBIOSType2);
    t2->manufacturer_str = smbios_add_string(buf, buf_size, &soff, manufacturer ? manufacturer : "mini-boot");
    t2->product_str      = smbios_add_string(buf, buf_size, &soff, product ? product : "Virtual Board");
    t2->version_str      = smbios_add_string(buf, buf_size, &soff, version ? version : "Rev A");
    t2->serial_number_str = smbios_add_string(buf, buf_size, &soff, serial ? serial : "MB000001");
    t2->asset_tag_str     = smbios_add_string(buf, buf_size, &soff, "AssetTag1");
    t2->location_in_chassis_str = smbios_add_string(buf, buf_size, &soff, "Main Board Location");

    if (soff < buf_size) { buf[soff] = '\0'; soff++; }
    return soff;
}

/* ============================================================================
 * SMBIOS Type 4: Processor Information - L1/L6
 *
 * Reports CPU details: socket, manufacturer, speed, core/thread count,
 * CPUID results, and cache handles. The OS uses this to populate
 * /proc/cpuinfo (Linux) and Task Manager CPU details (Windows).
 *
 * L6: Canonical problem - processor inventory for system management.
 * ============================================================================ */

uint32_t smbios_type4_build(uint8_t *buf, uint32_t buf_size, uint16_t handle,
                            const char *socket, const char *manufacturer,
                            const char *version, uint32_t max_mhz,
                            uint32_t curr_mhz, uint8_t core_count,
                            uint8_t thread_count)
{
    if (!buf || buf_size < sizeof(SMBIOSType4) + 4) return 0;

    memset(buf, 0, buf_size);
    SMBIOSType4 *t4 = (SMBIOSType4 *)buf;
    t4->header.type   = SMBIOS_TYPE_PROC_INFO;
    t4->header.length = sizeof(SMBIOSType4);
    t4->header.handle = handle;
    t4->processor_type = PROC_TYPE_CENTRAL;
    t4->processor_family = PROC_FAMILY_X64;
    t4->voltage = 0x80;  /* 0.8V - voltage specified in Processor Voltage field */
    t4->external_clock = 100;    /* 100 MHz base clock */
    t4->max_speed = (uint16_t)max_mhz;
    t4->current_speed = (uint16_t)curr_mhz;
    t4->status = 0x41;           /* CPU enabled, socket populated */
    t4->processor_upgrade = PROC_UPGRADE_SOCKET_LGA;
    t4->l1_cache_handle = 0x0000;
    t4->l2_cache_handle = 0x0000;
    t4->l3_cache_handle = 0x0000;
    t4->core_count = core_count;
    t4->core_enabled = core_count;
    t4->thread_count = thread_count;
    t4->processor_family2 = PROC_FAMILY_X64;

    /* CPUID values (simulated for an x86_64 processor) */
    t4->processor_id[0] = 0xBF;  /* Stepping */
    t4->processor_id[1] = 0xEB;  /* Model */
    t4->processor_id[2] = 0xFB;  /* Family */
    t4->processor_id[3] = 0xFF;

    uint32_t soff = sizeof(SMBIOSType4);
    t4->socket_designation_str = smbios_add_string(buf, buf_size, &soff, socket ? socket : "LGA1700");
    t4->processor_manufacturer_str = smbios_add_string(buf, buf_size, &soff, manufacturer ? manufacturer : "Intel");
    t4->processor_version_str = smbios_add_string(buf, buf_size, &soff, version ? version : "x86_64 Processor");
    t4->serial_number_str = smbios_add_string(buf, buf_size, &soff, "CPU-SN-001");
    t4->asset_tag_str     = smbios_add_string(buf, buf_size, &soff, "CPU-AT-001");
    t4->part_number_str   = smbios_add_string(buf, buf_size, &soff, "CPU-PN-001");

    if (soff < buf_size) { buf[soff] = '\0'; soff++; }
    return soff;
}

/* ============================================================================
 * SMBIOS Type 7: Cache Information - L1
 * ============================================================================ */

uint32_t smbios_type7_build(uint8_t *buf, uint32_t buf_size, uint16_t handle,
                            const char *designation, uint32_t max_size_kb,
                            uint32_t installed_size_kb, uint8_t cache_type,
                            uint8_t associativity)
{
    if (!buf || buf_size < sizeof(SMBIOSType7) + 4) return 0;

    memset(buf, 0, buf_size);
    SMBIOSType7 *t7 = (SMBIOSType7 *)buf;
    t7->header.type   = SMBIOS_TYPE_CACHE_INFO;
    t7->header.length = sizeof(SMBIOSType7);
    t7->header.handle = handle;
    t7->max_cache_size = 0xFFFF;    /* Use extended field */
    t7->installed_size = 0xFFFF;    /* Use extended field */
    t7->system_cache_type = cache_type;
    t7->associativity = associativity;
    t7->max_cache_size2 = max_size_kb;
    t7->installed_size2 = installed_size_kb;
    t7->error_correction_type = 0x05;  /* Single-bit ECC */

    uint32_t soff = sizeof(SMBIOSType7);
    t7->socket_designation_str = smbios_add_string(buf, buf_size, &soff, designation ? designation : "L1 Cache");

    if (soff < buf_size) { buf[soff] = '\0'; soff++; }
    return soff;
}

/* ============================================================================
 * SMBIOS Type 17: Memory Device - L1/L7
 *
 * Reports DIMM details: locator, manufacturer, part number, size, speed,
 * memory type (DDR4/DDR5), and form factor (DIMM/SODIMM).
 *
 * L7 Application: Data center asset management tools use SMBIOS Type 17
 * to track memory module inventory, warranty status, and failure prediction.
 * ============================================================================ */

uint32_t smbios_type17_build(uint8_t *buf, uint32_t buf_size, uint16_t handle,
                             const char *device_locator, const char *manufacturer,
                             const char *part_number, uint32_t size_mb,
                             uint16_t speed_mts, uint8_t memory_type,
                             uint8_t form_factor)
{
    if (!buf || buf_size < sizeof(SMBIOSType17) + 4) return 0;

    memset(buf, 0, buf_size);
    SMBIOSType17 *t17 = (SMBIOSType17 *)buf;
    t17->header.type   = SMBIOS_TYPE_MEMORY_DEVICE;
    t17->header.length = sizeof(SMBIOSType17);
    t17->header.handle = handle;
    t17->total_width = 72;  /* 64 data + 8 ECC */
    t17->data_width  = 64;
    t17->size = (size_mb > 0x7FFF) ? 0x7FFF : (uint16_t)size_mb;
    t17->form_factor = form_factor;
    t17->memory_type = memory_type;
    t17->speed = speed_mts;
    t17->extended_size = size_mb;
    t17->configured_memory_speed = speed_mts;
    t17->minimum_voltage  = 1100;   /* mV - DDR4 min */
    t17->maximum_voltage  = 1350;   /* mV - DDR4 max */
    t17->configured_voltage = 1200;  /* mV */

    uint32_t soff = sizeof(SMBIOSType17);
    t17->device_locator_str = smbios_add_string(buf, buf_size, &soff, device_locator ? device_locator : "DIMM_1");
    t17->bank_locator_str   = smbios_add_string(buf, buf_size, &soff, "Channel A");
    t17->manufacturer_str   = smbios_add_string(buf, buf_size, &soff, manufacturer ? manufacturer : "Samsung");
    t17->serial_number_str  = smbios_add_string(buf, buf_size, &soff, "MEM-SN-001");
    t17->asset_tag_str      = smbios_add_string(buf, buf_size, &soff, "MEM-AT-001");
    t17->part_number_str    = smbios_add_string(buf, buf_size, &soff, part_number ? part_number : "M471A1K43DB1");

    if (soff < buf_size) { buf[soff] = '\0'; soff++; }
    return soff;
}

/* ============================================================================
 * Debug Print Utilities
 * ============================================================================ */

void smbios_print_header(const SMBIOSHeader *hdr)
{
    if (!hdr) return;
    printf("  SMBIOS Type=%u Handle=0x%04X Length=%u\n",
           hdr->type, hdr->handle, hdr->length);
}

void smbios_print_type1(const SMBIOSType1 *t1, const uint8_t *string_table)
{
    if (!t1) return;
    const char *strings = (const char *)string_table;
    printf("  System: Mfr=%s Product=%s Version=%s Serial=%s\n",
           t1->manufacturer_str ? strings + t1->manufacturer_str - 1 : "(none)",
           t1->product_name_str ? strings + t1->product_name_str - 1 : "(none)",
           t1->version_str ? strings + t1->version_str - 1 : "(none)",
           t1->serial_number_str ? strings + t1->serial_number_str - 1 : "(none)");
}

void smbios_print_type4(const SMBIOSType4 *t4, const uint8_t *string_table)
{
    if (!t4) return;
    const char *strings = (const char *)string_table;
    printf("  CPU: Socket=%s Mfr=%s Version=%s Speed=%u/%u MHz Cores=%u/%u Threads=%u\n",
           t4->socket_designation_str ? strings + t4->socket_designation_str - 1 : "(none)",
           t4->processor_manufacturer_str ? strings + t4->processor_manufacturer_str - 1 : "(none)",
           t4->processor_version_str ? strings + t4->processor_version_str - 1 : "(none)",
           t4->max_speed, t4->current_speed, t4->core_enabled, t4->core_count, t4->thread_count);
}
