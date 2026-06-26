#include "smbios.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

const char *smbios_type_to_string(uint8_t type)
{
    switch (type) {
    case SMBIOS_TYPE_BIOS:              return "BIOS Information";
    case SMBIOS_TYPE_SYSTEM:            return "System Information";
    case SMBIOS_TYPE_BASEBOARD:         return "Baseboard (Module)";
    case SMBIOS_TYPE_CHASSIS:           return "System Enclosure (Chassis)";
    case SMBIOS_TYPE_PROCESSOR:         return "Processor Information";
    case SMBIOS_TYPE_MEMORY_CONTROLLER: return "Memory Controller";
    case SMBIOS_TYPE_MEMORY:            return "Memory Module";
    case SMBIOS_TYPE_CACHE:             return "Cache Information";
    case SMBIOS_TYPE_PORT_CONNECTOR:    return "Port Connector";
    case SMBIOS_TYPE_SYSTEM_SLOTS:      return "System Slots";
    case SMBIOS_TYPE_ONBOARD_DEVICES:   return "Onboard Devices";
    case SMBIOS_TYPE_OEM_STRINGS:       return "OEM Strings";
    case SMBIOS_TYPE_SYSTEM_CONFIG:     return "System Configuration Options";
    case SMBIOS_TYPE_BIOS_LANG:         return "BIOS Language";
    case SMBIOS_TYPE_GROUP_ASSOC:       return "Group Associations";
    case SMBIOS_TYPE_SYSTEM_EVENT_LOG:  return "System Event Log";
    case SMBIOS_TYPE_PHYSICAL_MEMORY:   return "Physical Memory Array";
    case SMBIOS_TYPE_MEMORY_DEVICE:     return "Memory Device";
    case SMBIOS_TYPE_32BIT_MEMORY_ERROR: return "32-bit Memory Error Info";
    case SMBIOS_TYPE_MEMORY_ARRAY_MAPPED: return "Memory Array Mapped Address";
    case SMBIOS_TYPE_MEMORY_DEVICE_MAPPED: return "Memory Device Mapped Address";
    case SMBIOS_TYPE_BUILTIN_POINTING:  return "Built-in Pointing Device";
    case SMBIOS_TYPE_PORTABLE_BATTERY:  return "Portable Battery";
    case SMBIOS_TYPE_SYSTEM_RESET:      return "System Reset";
    case SMBIOS_TYPE_HARDWARE_SECURITY: return "Hardware Security";
    case SMBIOS_TYPE_SYSTEM_POWER_CTRL: return "System Power Controls";
    case SMBIOS_TYPE_VOLTAGE_PROBE:     return "Voltage Probe";
    case SMBIOS_TYPE_COOLING_DEVICE:    return "Cooling Device";
    case SMBIOS_TYPE_TEMP_PROBE:        return "Temperature Probe";
    case SMBIOS_TYPE_ELECTRICAL_CURRENT: return "Electrical Current Probe";
    case SMBIOS_TYPE_OUT_OF_BAND_REMOTE: return "Out-of-Band Remote Access";
    case SMBIOS_TYPE_BOOT_INTEGRITY:    return "Boot Integrity Services";
    case SMBIOS_TYPE_SYSTEM_BOOT:       return "System Boot Information";
    case SMBIOS_TYPE_64BIT_MEMORY_ERROR: return "64-bit Memory Error Info";
    case SMBIOS_TYPE_MGMT_DEVICE:       return "Management Device";
    case SMBIOS_TYPE_MGMT_DEVICE_COMP:  return "Management Device Component";
    case SMBIOS_TYPE_MGMT_DEVICE_THRESH: return "Management Device Threshold";
    case SMBIOS_TYPE_MEMORY_CHANNEL:    return "Memory Channel";
    case SMBIOS_TYPE_IPMI_DEVICE:       return "IPMI Device Information";
    case SMBIOS_TYPE_POWER_SUPPLY:      return "System Power Supply";
    case SMBIOS_TYPE_ADDITIONAL_INFO:   return "Additional Information";
    case SMBIOS_TYPE_ONBOARD_EXTENDED:  return "Onboard Extended";
    case SMBIOS_TYPE_MGMT_CONTROLLER:   return "Management Controller Host Interface";
    case SMBIOS_TYPE_TPM_DEVICE:        return "TPM Device";
    case SMBIOS_TYPE_PROCESSOR_ADDITIONAL: return "Processor Additional Info";
    case SMBIOS_TYPE_FIRMWARE_INVENTORY: return "Firmware Inventory Information";
    case SMBIOS_TYPE_STRING_PROPERTY:   return "String Property";
    case SMBIOS_TYPE_END_OF_TABLE:      return "End of Table";
    default:                            return "Unknown";
    }
}

static bool smbios_validate_entry_32(const SMBIOSEntryPoint32 *ep)
{
    if (!ep) return false;
    if (memcmp(ep->anchor_string, SMBIOS_ANCHOR_32, SMBIOS_ANCHOR_LEN) != 0) return false;
    if (ep->entry_point_length < 0x1E) return false;
    uint8_t sum = 0;
    const uint8_t *p = (const uint8_t *)ep;
    for (uint8_t i = 0; i < ep->entry_point_length; i++) sum += p[i];
    if (sum != 0) return false;
    return memcmp(ep->intermediate_anchor, "_DMI_", 5) == 0;
}

static bool smbios_validate_entry_64(const SMBIOSEntryPoint64 *ep)
{
    if (!ep) return false;
    if (memcmp(ep->anchor_string, SMBIOS_ANCHOR_64, 5) != 0) return false;
    uint8_t sum = 0;
    const uint8_t *p = (const uint8_t *)ep;
    for (uint8_t i = 0; i < ep->entry_point_length; i++) sum += p[i];
    return sum == 0;
}

static uint8_t smbios_string_at(const uint8_t *strings_block, uint8_t index,
                                char *out, size_t out_size)
{
    if (!strings_block || !out || out_size == 0 || index == 0) {
        if (out_size > 0) out[0] = '\0';
        return 0;
    }

    const uint8_t *p = strings_block;
    uint8_t current = 1;

    while (current < index) {
        while (*p != 0) p++;
        p++;
        if (*p == 0) {
            if (out_size > 0) out[0] = '\0';
            return 0;
        }
        current++;
    }

    size_t len = 0;
    while (*p != 0 && len < out_size - 1) {
        out[len++] = (char)*p++;
    }
    out[len] = '\0';
    return (uint8_t)len;
}

static bool smbios_parse_structure(const uint8_t *raw_data, size_t offset, size_t max_size,
                                   SMBIOSParsedStructure *out)
{
    if (!raw_data || !out) return false;
    if (offset + sizeof(SMBIOSStructure) > max_size) return false;

    const SMBIOSStructure *hdr = (const SMBIOSStructure *)(raw_data + offset);
    if (hdr->type == SMBIOS_TYPE_END_OF_TABLE) return false;

    out->type   = hdr->type;
    out->length = hdr->length;
    out->handle = hdr->handle;

    size_t data_copy = hdr->length;
    if (data_copy > sizeof(out->data)) data_copy = sizeof(out->data);
    if (data_copy > max_size - offset) data_copy = max_size - offset;
    memcpy(out->data, raw_data + offset, data_copy);
    out->data_size = data_copy;

    const uint8_t *strings_start = raw_data + offset + hdr->length;
    out->string_count = 0;

    if (strings_start[0] != 0) {
        const uint8_t *s = strings_start;
        size_t field_idx = 0;
        while (*s != 0 && field_idx < SMBIOS_MAX_STRINGS) {
            size_t slen = 0;
            while (*s != 0 && slen < SMBIOS_STRING_MAX_LEN - 1) {
                out->strings[field_idx][slen++] = (char)*s++;
            }
            out->strings[field_idx][slen] = '\0';
            s++;
            field_idx++;
            if (*s == 0) break;
        }
        out->string_count = field_idx;
    }

    return true;
}

bool smbios_parse(SMBIOSTable *table, const uint8_t *data, size_t size)
{
    if (!table || !data || size < sizeof(SMBIOSEntryPoint32)) return false;
    memset(table, 0, sizeof(SMBIOSTable));

    const SMBIOSEntryPoint32 *ep32 = (const SMBIOSEntryPoint32 *)data;
    const SMBIOSEntryPoint64 *ep64 = (const SMBIOSEntryPoint64 *)data;

    if (smbios_validate_entry_32(ep32)) {
        table->entry_point.is_v3 = false;
        memcpy(&table->entry_point.ep32, ep32, sizeof(SMBIOSEntryPoint32));
        memcpy(table->entry_point.anchor, SMBIOS_ANCHOR_32, SMBIOS_ANCHOR_LEN);

        const uint8_t *struct_table = (const uint8_t *)(uintptr_t)ep32->structure_table_address;
        size_t struct_table_len = ep32->structure_table_length;
        uint16_t num_structures = ep32->number_of_structures;

        if (num_structures > SMBIOS_MAX_STRUCTURES) num_structures = SMBIOS_MAX_STRUCTURES;
        table->structures = calloc(num_structures, sizeof(SMBIOSParsedStructure));
        if (!table->structures) return false;

        size_t offset = 0;
        for (uint16_t i = 0; i < num_structures && offset < struct_table_len; i++) {
            if (smbios_parse_structure(struct_table, offset,
                                       struct_table_len, &table->structures[i])) {
                table->structure_count++;
                /* skip past string section */
                const uint8_t *s = struct_table + offset + table->structures[i].length;
                while (*s != 0) s++;
                s++;
                if (*s != 0) s++;
                offset = (size_t)(s - struct_table);
            } else {
                break;
            }
        }
        table->parsed = true;
        return true;
    }

    if (smbios_validate_entry_64(ep64)) {
        table->entry_point.is_v3 = true;
        memcpy(&table->entry_point.ep64, ep64, sizeof(SMBIOSEntryPoint64));
        memcpy(table->entry_point.anchor, SMBIOS_ANCHOR_64, 5);

        const uint8_t *struct_table = (const uint8_t *)(uintptr_t)ep64->structure_table_address;
        size_t max_size = (size_t)ep64->structure_table_max_size;
        if (max_size > 64 * 1024 * 1024) max_size = 64 * 1024 * 1024;

        table->structures = calloc(SMBIOS_MAX_STRUCTURES, sizeof(SMBIOSParsedStructure));
        if (!table->structures) return false;

        size_t offset = 0;
        while (offset < max_size) {
            if (table->structure_count >= SMBIOS_MAX_STRUCTURES) break;
            if (smbios_parse_structure(struct_table, offset, max_size,
                                       &table->structures[table->structure_count])) {
                const uint8_t *s = struct_table + offset + table->structures[table->structure_count].length;
                while (*s != 0) s++;
                s++;
                if (*s != 0) s++;
                offset = (size_t)(s - struct_table);
                table->structure_count++;
            } else {
                break;
            }
        }
        table->parsed = true;
        return true;
    }

    return false;
}

void smbios_print_bios(const SMBIOSParsedStructure *s)
{
    if (!s || s->type != SMBIOS_TYPE_BIOS) return;
    printf("BIOS Information:\n");
    if (s->string_count >= 1) printf("  Vendor: %s\n", s->strings[0]);
    if (s->string_count >= 2) printf("  Version: %s\n", s->strings[1]);
    if (s->string_count >= 3) printf("  Release Date: %s\n", s->strings[2]);

    if (s->data_size > 8) {
        printf("  BIOS Starting Address: 0x%04X%04X\n",
               s->data[7] << 8 | s->data[6],
               s->data[5] << 8 | s->data[4]);
    }
    if (s->data_size > 0x16) {
        printf("  Characteristics: 0x%08X%08X\n",
               *(uint32_t *)(s->data + 0x10),
               *(uint32_t *)(s->data + 0x14));
    }
}

void smbios_print_system(const SMBIOSParsedStructure *s)
{
    if (!s || s->type != SMBIOS_TYPE_SYSTEM) return;
    printf("System Information:\n");
    if (s->string_count >= 1) printf("  Manufacturer: %s\n", s->strings[0]);
    if (s->string_count >= 2) printf("  Product Name: %s\n", s->strings[1]);
    if (s->string_count >= 3) printf("  Version: %s\n", s->strings[2]);
    if (s->string_count >= 4) printf("  Serial Number: %s\n", s->strings[3]);
    if (s->data_size > 8) {
        uint8_t uuid[16];
        memcpy(uuid, s->data + 8, 16);
        printf("  UUID: %02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X\n",
               uuid[0], uuid[1], uuid[2], uuid[3], uuid[4], uuid[5], uuid[6], uuid[7],
               uuid[8], uuid[9], uuid[10], uuid[11], uuid[12], uuid[13], uuid[14], uuid[15]);
    }
    if (s->data_size > 0x19) {
        printf("  Wake-up Type: %u\n", s->data[0x19]);
    }
}

void smbios_print_memory(const SMBIOSParsedStructure *s)
{
    if (!s || (s->type != SMBIOS_TYPE_MEMORY && s->type != SMBIOS_TYPE_MEMORY_DEVICE)) return;
    printf("Memory Device:\n");
    if (s->string_count >= 2) printf("  Device Locator: %s\n", s->strings[1]);
    if (s->string_count >= 3) printf("  Bank Locator: %s\n", s->strings[2]);
    if (s->string_count >= 1) printf("  Manufacturer: %s\n", s->strings[0]);
    if (s->string_count >= 6) printf("  Part Number: %s\n", s->strings[5]);

    if (s->data_size > 0x0F) {
        uint16_t size_val = *(uint16_t *)(s->data + 0x0C);
        printf("  Size: ");
        if (size_val == 0xFFFF) {
            uint32_t ext_size = *(uint32_t *)(s->data + 0x1C);
            printf("%u MB\n", ext_size);
        } else if (size_val == 0x0000) {
            printf("Not populated\n");
        } else {
            printf("%u MB\n", size_val & 0x7FFF);
        }
    }
    if (s->data_size > 0x12) {
        uint8_t mem_type = s->data[0x12];
        const char *mt[] = {"Other","Unknown","DRAM","EDRAM","VRAM","SRAM","RAM","ROM",
                            "FLASH","EEPROM","FEPROM","EPROM","CDRAM","3DRAM","SDRAM",
                            "SGRAM","RDRAM","DDR","DDR2","DDR2 FB-DIMM","Reserved",
                            "DDR3","FBD2","DDR4","LPDDR","LPDDR2","LPDDR3","LPDDR4",
                            "LPDDR5","DDR5"};
        printf("  Memory Type: %s\n", mem_type < 30 ? mt[mem_type] : "Unknown");
    }
    if (s->data_size > 0x15) {
        printf("  Speed: %u MHz\n", *(uint16_t *)(s->data + 0x15));
    }
}

void smbios_print_processor(const SMBIOSParsedStructure *s)
{
    if (!s || s->type != SMBIOS_TYPE_PROCESSOR) return;
    printf("Processor Information:\n");
    if (s->string_count >= 1) printf("  Socket Designation: %s\n", s->strings[0]);
    if (s->string_count >= 2) printf("  Manufacturer: %s\n", s->strings[1]);
    if (s->string_count >= 3) printf("  Version: %s\n", s->strings[2]);

    if (s->data_size > 5) {
        uint8_t proc_type = s->data[5];
        const char *types[] = {"Other","Unknown","CPU","Math Processor","DSP","Video"};
        printf("  Processor Type: %s\n", proc_type < 6 ? types[proc_type] : "Unknown");
    }
    if (s->data_size > 0x20) {
        printf("  Max Speed: %u MHz\n", *(uint16_t *)(s->data + 0x14));
        printf("  Current Speed: %u MHz\n", *(uint16_t *)(s->data + 0x16));
        printf("  Core Count: %u\n", s->data[0x23]);
        printf("  Thread Count: %u\n", s->data[0x25]);
    }
}

void smbios_print_baseboard(const SMBIOSParsedStructure *s)
{
    if (!s || s->type != SMBIOS_TYPE_BASEBOARD) return;
    printf("Baseboard Information:\n");
    if (s->string_count >= 1) printf("  Manufacturer: %s\n", s->strings[0]);
    if (s->string_count >= 2) printf("  Product: %s\n", s->strings[1]);
    if (s->string_count >= 3) printf("  Version: %s\n", s->strings[2]);
    if (s->string_count >= 4) printf("  Serial Number: %s\n", s->strings[3]);
}

void smbios_print_chassis(const SMBIOSParsedStructure *s)
{
    if (!s || s->type != SMBIOS_TYPE_CHASSIS) return;
    printf("Chassis Information:\n");
    if (s->string_count >= 1) printf("  Manufacturer: %s\n", s->strings[0]);
    if (s->data_size > 5) {
        uint8_t chassis_type = s->data[5] & 0x7F;
        const char *ctypes[] = {"Other","Unknown","Desktop","Low Profile Desktop",
                                "Pizza Box","Mini Tower","Tower","Portable",
                                "Laptop","Notebook","Hand Held","Docking Station",
                                "All in One","Sub Notebook","Space-saving",
                                "Lunch Box","Main Server","Expansion","SubChassis",
                                "Bus Expansion","Peripheral","RAID","Rack Mount",
                                "Sealed-case PC","Multi-system","Compact PCI",
                                "Advanced TCA","Blade","Blade Enclosure","Tablet",
                                "Convertible","Detachable","IoT Gateway",
                                "Embedded PC","Mini PC","Stick PC"};
        printf("  Type: %s\n", chassis_type < 36 ? ctypes[chassis_type] : "Unknown");
    }
    if (s->string_count >= 3) printf("  Version: %s\n", s->strings[2]);
    if (s->string_count >= 4) printf("  Serial Number: %s\n", s->strings[3]);
}

void smbios_print_cache(const SMBIOSParsedStructure *s)
{
    if (!s || s->type != SMBIOS_TYPE_CACHE) return;
    printf("Cache Information:\n");
    if (s->string_count >= 1) printf("  Socket Designation: %s\n", s->strings[0]);

    if (s->data_size > 0x11) {
        uint16_t max_size = *(uint16_t *)(s->data + 7);
        uint16_t installed = *(uint16_t *)(s->data + 9);
        printf("  Maximum Size: %u KB\n", max_size);
        printf("  Installed Size: %u KB\n", installed);
    }
}

void smbios_print_all(const SMBIOSTable *table)
{
    if (!table || !table->parsed) {
        printf("No SMBIOS data to display.\n");
        return;
    }

    printf("=== SMBIOS %u.%u System Information ===\n",
           table->entry_point.is_v3 ? table->entry_point.ep64.smbios_major_version
                                    : table->entry_point.ep32.smbios_major_version,
           table->entry_point.is_v3 ? table->entry_point.ep64.smbios_minor_version
                                    : table->entry_point.ep32.smbios_minor_version);
    printf("Structure count: %zu\n\n", table->structure_count);

    for (size_t i = 0; i < table->structure_count; i++) {
        const SMBIOSParsedStructure *s = &table->structures[i];
        printf("[Handle 0x%04X] %s (Type %u)\n", s->handle, smbios_type_to_string(s->type), s->type);

        switch (s->type) {
        case SMBIOS_TYPE_BIOS:      smbios_print_bios(s);       break;
        case SMBIOS_TYPE_SYSTEM:    smbios_print_system(s);     break;
        case SMBIOS_TYPE_BASEBOARD: smbios_print_baseboard(s);  break;
        case SMBIOS_TYPE_CHASSIS:   smbios_print_chassis(s);    break;
        case SMBIOS_TYPE_PROCESSOR: smbios_print_processor(s);  break;
        case SMBIOS_TYPE_MEMORY:
        case SMBIOS_TYPE_MEMORY_DEVICE: smbios_print_memory(s);  break;
        case SMBIOS_TYPE_CACHE:     smbios_print_cache(s);      break;
        default:
            printf("  (data: %zu bytes)\n", s->data_size);
            break;
        }
        printf("\n");
    }
}

void smbios_free_table(SMBIOSTable *table)
{
    if (!table) return;
    free(table->structures);
    table->structures = NULL;
    table->structure_count = 0;
    table->parsed = false;
}
