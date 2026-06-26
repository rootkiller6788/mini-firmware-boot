#ifndef HOB_H
#define HOB_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define HOB_TYPE_PHIT              0x0000
#define HOB_TYPE_MEMORY_ALLOC      0x0001
#define HOB_TYPE_RESOURCE_DESC     0x0002
#define HOB_TYPE_FIRMWARE_VOLUME   0x0004
#define HOB_TYPE_CPU               0x0005
#define HOB_TYPE_MEMORY_POOL       0x0007
#define HOB_TYPE_FIRMWARE_VOLUME2  0x0009
#define HOB_TYPE_FIRMWARE_VOLUME3  0x000A
#define HOB_TYPE_UEFI_CAPSULE      0x000B
#define HOB_TYPE_GUID_EXT          0xFFFE
#define HOB_TYPE_END_OF_LIST       0xFFFF

#define HOB_MEM_ALLOC_EOI_RESIDENT    0x00
#define HOB_MEM_ALLOC_ACPI_RECLAIM    0x03
#define HOB_MEM_ALLOC_ACPI_NVS        0x04
#define HOB_MEM_ALLOC_RESERVED        0x05
#define HOB_MEM_ALLOC_CAPSULE         0x06
#define HOB_MEM_ALLOC_MODULE          0x07

#define HOB_RESOURCE_SYSTEM_MEMORY    0x00000000
#define HOB_RESOURCE_MEMORY_MAPPED_IO 0x00000001
#define HOB_RESOURCE_IO               0x00000002
#define HOB_RESOURCE_FIRMWARE_DEVICE  0x00000003
#define HOB_RESOURCE_MEMORY_MAPPED_IO_PORT 0x00000004
#define HOB_RESOURCE_MEMORY_RESERVED  0x00000005
#define HOB_RESOURCE_IO_RESERVED      0x00000006

#define HOB_RESOURCE_ATTR_UNCACHED    0x00000001
#define HOB_RESOURCE_ATTR_WRITE_COMBINE 0x00000002
#define HOB_RESOURCE_ATTR_WRITE_THROUGH 0x00000004
#define HOB_RESOURCE_ATTR_WRITE_BACK  0x00000008
#define HOB_RESOURCE_ATTR_UC_EXPRESS  0x00000010
#define HOB_RESOURCE_ATTR_WP          0x00000020
#define HOB_RESOURCE_ATTR_RP          0x00000040
#define HOB_RESOURCE_ATTR_XP          0x00000080
#define HOB_RESOURCE_ATTR_RUNTIME     0x00008000
#define HOB_RESOURCE_ATTR_TESTED      0x00010000

#define HOB_MAX_COUNT 256

typedef struct __attribute__((packed)) {
    uint16_t hob_type;
    uint16_t hob_length;
    uint32_t reserved;
} HOBHeader;

typedef struct __attribute__((packed)) {
    HOBHeader header;
    uint32_t  boot_mode;
    uint64_t  memory_top;
    uint64_t  memory_bottom;
    uint64_t  free_memory_top;
    uint64_t  free_memory_bottom;
    uint32_t  reserved1[1];
} HOBPHIT;

typedef struct __attribute__((packed)) {
    HOBHeader header;
    uint8_t   alloc_descriptor[16];
    uint64_t  memory_base_address;
    uint64_t  memory_length;
    uint8_t   memory_type;
    uint8_t   reserved[3];
    uint32_t  reserved1[1];
} HOBMemoryAlloc;

typedef struct __attribute__((packed)) {
    HOBHeader header;
    uint32_t  resource_type;
    uint32_t  resource_attribute;
    uint64_t  physical_start;
    uint64_t  resource_length;
} HOBResourceDesc;

typedef struct __attribute__((packed)) {
    HOBHeader header;
    uint64_t  base_address;
    uint64_t  length;
    uint32_t  reserved1[1];
} HOBFirmwareVolume;

typedef struct __attribute__((packed)) {
    HOBHeader header;
    uint8_t   size_of_memory_space;
    uint8_t   size_of_io_space;
    uint8_t   reserved[6];
} HOBCPU;

typedef struct __attribute__((packed)) {
    HOBHeader header;
    uint8_t   guid[16];
    uint8_t   data[];
} HOBGuidExt;

typedef struct {
    HOBHeader header;
    union {
        HOBPHIT           phit;
        HOBMemoryAlloc    memory_alloc;
        HOBResourceDesc   resource_desc;
        HOBFirmwareVolume firmware_volume;
        HOBCPU            cpu;
        uint8_t           raw[256];
    };
} HOB;

typedef struct {
    HOB     hobs[HOB_MAX_COUNT];
    size_t  count;
    bool    finalized;
} HOBList;

bool hob_init(HOBList *list, uint64_t memory_top, uint64_t memory_bottom,
              uint64_t free_top, uint64_t free_bottom);
bool hob_add_memory_alloc(HOBList *list, uint64_t base, uint64_t length, uint8_t type);
bool hob_add_resource_desc(HOBList *list, uint32_t res_type, uint32_t attributes,
                           uint64_t start, uint64_t length);
bool hob_add_firmware_volume(HOBList *list, uint64_t base, uint64_t length);
bool hob_add_cpu(HOBList *list, uint8_t mem_space, uint8_t io_space);
bool hob_finalize(HOBList *list);
size_t hob_find_by_type(const HOBList *list, uint16_t type, HOB *results, size_t max_results);
bool hob_get_phit(const HOBList *list, HOBPHIT *phit);
void hob_print_list(const HOBList *list);
const char *hob_type_to_string(uint16_t type);
const char *hob_memory_type_to_string(uint8_t type);
const char *hob_resource_type_to_string(uint32_t type);
uint32_t hob_calculate_total_size(const HOBList *list);

#endif
