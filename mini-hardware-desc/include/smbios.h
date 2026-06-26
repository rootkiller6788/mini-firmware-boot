#ifndef SMBIOS_H
#define SMBIOS_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define SMBIOS_ANCHOR_32  "_SM_"
#define SMBIOS_ANCHOR_64  "_SM3_"
#define SMBIOS_ANCHOR_LEN 4

#define SMBIOS_TYPE_BIOS               0
#define SMBIOS_TYPE_SYSTEM             1
#define SMBIOS_TYPE_BASEBOARD          2
#define SMBIOS_TYPE_CHASSIS            3
#define SMBIOS_TYPE_PROCESSOR          4
#define SMBIOS_TYPE_MEMORY_CONTROLLER  5
#define SMBIOS_TYPE_MEMORY             6
#define SMBIOS_TYPE_CACHE              7
#define SMBIOS_TYPE_PORT_CONNECTOR     8
#define SMBIOS_TYPE_SYSTEM_SLOTS       9
#define SMBIOS_TYPE_ONBOARD_DEVICES    10
#define SMBIOS_TYPE_OEM_STRINGS        11
#define SMBIOS_TYPE_SYSTEM_CONFIG      12
#define SMBIOS_TYPE_BIOS_LANG          13
#define SMBIOS_TYPE_GROUP_ASSOC        14
#define SMBIOS_TYPE_SYSTEM_EVENT_LOG   15
#define SMBIOS_TYPE_PHYSICAL_MEMORY    16
#define SMBIOS_TYPE_MEMORY_DEVICE      17
#define SMBIOS_TYPE_32BIT_MEMORY_ERROR 18
#define SMBIOS_TYPE_MEMORY_ARRAY_MAPPED 19
#define SMBIOS_TYPE_MEMORY_DEVICE_MAPPED 20
#define SMBIOS_TYPE_BUILTIN_POINTING   21
#define SMBIOS_TYPE_PORTABLE_BATTERY   22
#define SMBIOS_TYPE_SYSTEM_RESET       23
#define SMBIOS_TYPE_HARDWARE_SECURITY  24
#define SMBIOS_TYPE_SYSTEM_POWER_CTRL  25
#define SMBIOS_TYPE_VOLTAGE_PROBE      26
#define SMBIOS_TYPE_COOLING_DEVICE     27
#define SMBIOS_TYPE_TEMP_PROBE         28
#define SMBIOS_TYPE_ELECTRICAL_CURRENT 29
#define SMBIOS_TYPE_OUT_OF_BAND_REMOTE 30
#define SMBIOS_TYPE_BOOT_INTEGRITY     31
#define SMBIOS_TYPE_SYSTEM_BOOT        32
#define SMBIOS_TYPE_64BIT_MEMORY_ERROR 33
#define SMBIOS_TYPE_MGMT_DEVICE        34
#define SMBIOS_TYPE_MGMT_DEVICE_COMP   35
#define SMBIOS_TYPE_MGMT_DEVICE_THRESH 36
#define SMBIOS_TYPE_MEMORY_CHANNEL     37
#define SMBIOS_TYPE_IPMI_DEVICE        38
#define SMBIOS_TYPE_POWER_SUPPLY       39
#define SMBIOS_TYPE_ADDITIONAL_INFO    40
#define SMBIOS_TYPE_ONBOARD_EXTENDED   41
#define SMBIOS_TYPE_MGMT_CONTROLLER    42
#define SMBIOS_TYPE_TPM_DEVICE         43
#define SMBIOS_TYPE_PROCESSOR_ADDITIONAL 44
#define SMBIOS_TYPE_FIRMWARE_INVENTORY 45
#define SMBIOS_TYPE_STRING_PROPERTY    46
#define SMBIOS_TYPE_END_OF_TABLE       127

#define SMBIOS_MAX_STRINGS 64
#define SMBIOS_MAX_STRUCTURES 256
#define SMBIOS_STRING_MAX_LEN 256

typedef struct __attribute__((packed)) {
    char     anchor_string[4];
    uint8_t  checksum;
    uint8_t  entry_point_length;
    uint8_t  smbios_major_version;
    uint8_t  smbios_minor_version;
    uint16_t max_structure_size;
    uint8_t  entry_point_revision;
    char     formatted_area[5];
    char     intermediate_anchor[5];
    uint8_t  intermediate_checksum;
    uint16_t structure_table_length;
    uint32_t structure_table_address;
    uint16_t number_of_structures;
    uint8_t  smbios_bcd_revision;
} SMBIOSEntryPoint32;

typedef struct __attribute__((packed)) {
    char     anchor_string[5];
    uint8_t  checksum;
    uint8_t  entry_point_length;
    uint8_t  smbios_major_version;
    uint8_t  smbios_minor_version;
    uint8_t  smbios_docrev;
    uint8_t  entry_point_revision;
    uint8_t  reserved;
    uint32_t structure_table_max_size;
    uint64_t structure_table_address;
} SMBIOSEntryPoint64;

typedef struct __attribute__((packed)) {
    uint8_t  anchor[SMBIOS_ANCHOR_LEN];
    union {
        SMBIOSEntryPoint32 ep32;
        SMBIOSEntryPoint64 ep64;
    };
    bool     is_v3;
} SMBIOSEntryPoint;

typedef struct __attribute__((packed)) {
    uint8_t  type;
    uint8_t  length;
    uint16_t handle;
} SMBIOSStructure;

typedef struct {
    uint8_t  type;
    uint8_t  length;
    uint16_t handle;
    uint8_t  data[1024];
    size_t   data_size;
    char     strings[SMBIOS_MAX_STRINGS][SMBIOS_STRING_MAX_LEN];
    size_t   string_count;
} SMBIOSParsedStructure;

typedef struct {
    SMBIOSEntryPoint         entry_point;
    SMBIOSParsedStructure   *structures;
    size_t                   structure_count;
    bool                     parsed;
} SMBIOSTable;

bool smbios_parse(SMBIOSTable *table, const uint8_t *data, size_t size);
void smbios_print_bios(const SMBIOSParsedStructure *s);
void smbios_print_system(const SMBIOSParsedStructure *s);
void smbios_print_memory(const SMBIOSParsedStructure *s);
void smbios_print_processor(const SMBIOSParsedStructure *s);
void smbios_print_baseboard(const SMBIOSParsedStructure *s);
void smbios_print_chassis(const SMBIOSParsedStructure *s);
void smbios_print_cache(const SMBIOSParsedStructure *s);
void smbios_print_all(const SMBIOSTable *table);
const char *smbios_type_to_string(uint8_t type);
void smbios_free_table(SMBIOSTable *table);

#endif
