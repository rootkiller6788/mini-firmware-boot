#ifndef SMBIOS_FW_H
#define SMBIOS_FW_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define SMBIOS_MAX_TABLES       64
#define SMBIOS_TABLE_NAME_LEN   128

typedef enum {
    SMBIOS_TYPE_BIOS_INFO      = 0,
    SMBIOS_TYPE_SYSTEM_INFO    = 1,
    SMBIOS_TYPE_BASEBOARD      = 2,
    SMBIOS_TYPE_SYSTEM_ENCLOSURE = 3,
    SMBIOS_TYPE_PROCESSOR      = 4
} SMBIOSTableType;

typedef struct {
    uint8_t  type;
    uint8_t  length;
    uint16_t handle;
} SMBIOSHeader;

typedef struct {
    SMBIOSHeader header;
    uint8_t      vendor;
    uint8_t      version;
    uint16_t     starting_addr_seg;
    uint8_t      release_date;
    uint8_t      rom_size;
} SMBIOSBIOSInfo;

typedef struct {
    SMBIOSHeader header;
    uint8_t      manufacturer;
    uint8_t      product_name;
    uint8_t      version;
    uint8_t      serial_number;
} SMBIOSSystemInfo;

typedef struct {
    SMBIOSHeader header;
    uint8_t      manufacturer;
    uint8_t      product;
    uint8_t      version;
    uint8_t      serial_number;
} SMBIOSBaseboard;

typedef struct {
    SMBIOSHeader header;
    uint8_t      socket_designation;
    uint8_t      processor_type;
    uint8_t      family;
    uint8_t      manufacturer;
    uint32_t     processor_id;
    uint8_t      version;
    uint16_t     max_speed;
    uint16_t     current_speed;
} SMBIOSProcessor;

typedef struct {
    uint8_t  entry_point_string[4];
    uint8_t  checksum;
    uint8_t  length;
    uint8_t  major_ver;
    uint8_t  minor_ver;
    uint16_t max_structure_size;
    uint8_t  entry_point_revision;
} SMBIOSEntryPoint;

typedef struct {
    SMBIOSEntryPoint entry_point;
    void            *tables[SMBIOS_MAX_TABLES];
    uint32_t         num_tables;
    uint8_t          table_data[SMBIOS_MAX_TABLES][512];
} SMBIOS;

bool     smbios_init(SMBIOS *smbios, uint8_t major, uint8_t minor);
bool     smbios_add_table(SMBIOS *smbios, const void *table, uint32_t table_len);
void    *smbios_find_by_type(const SMBIOS *smbios, SMBIOSTableType type);
void     smbios_print_bios_info(const SMBIOS *smbios);

#endif
