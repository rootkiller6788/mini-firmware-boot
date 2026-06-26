#include "hob.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

bool hob_init(HOBList *list, uint64_t memory_top, uint64_t memory_bottom,
              uint64_t free_top, uint64_t free_bottom)
{
    if (!list) return false;
    memset(list, 0, sizeof(HOBList));

    HOB *phit = &list->hobs[list->count];
    phit->header.hob_type   = HOB_TYPE_PHIT;
    phit->header.hob_length = sizeof(HOBPHIT);
    phit->header.reserved   = 0;
    phit->phit.boot_mode           = 0;
    phit->phit.memory_top          = memory_top;
    phit->phit.memory_bottom       = memory_bottom;
    phit->phit.free_memory_top     = free_top;
    phit->phit.free_memory_bottom  = free_bottom;
    phit->phit.reserved1[0]        = 0;
    list->count++;
    list->finalized = false;
    return true;
}

bool hob_add_memory_alloc(HOBList *list, uint64_t base, uint64_t length, uint8_t type)
{
    if (!list || list->finalized || list->count >= HOB_MAX_COUNT) return false;

    HOB *hob = &list->hobs[list->count];
    hob->header.hob_type   = HOB_TYPE_MEMORY_ALLOC;
    hob->header.hob_length = sizeof(HOBMemoryAlloc);
    hob->header.reserved   = 0;
    hob->memory_alloc.memory_base_address = base;
    hob->memory_alloc.memory_length       = length;
    hob->memory_alloc.memory_type         = type;
    memset(hob->memory_alloc.alloc_descriptor, 0, 16);
    memset(hob->memory_alloc.reserved, 0, sizeof(hob->memory_alloc.reserved));
    memset(hob->memory_alloc.reserved1, 0, sizeof(hob->memory_alloc.reserved1));
    list->count++;
    return true;
}

bool hob_add_resource_desc(HOBList *list, uint32_t res_type, uint32_t attributes,
                           uint64_t start, uint64_t length)
{
    if (!list || list->finalized || list->count >= HOB_MAX_COUNT) return false;

    HOB *hob = &list->hobs[list->count];
    hob->header.hob_type   = HOB_TYPE_RESOURCE_DESC;
    hob->header.hob_length = sizeof(HOBResourceDesc);
    hob->header.reserved   = 0;
    hob->resource_desc.resource_type      = res_type;
    hob->resource_desc.resource_attribute = attributes;
    hob->resource_desc.physical_start     = start;
    hob->resource_desc.resource_length    = length;
    list->count++;
    return true;
}

bool hob_add_firmware_volume(HOBList *list, uint64_t base, uint64_t length)
{
    if (!list || list->finalized || list->count >= HOB_MAX_COUNT) return false;

    HOB *hob = &list->hobs[list->count];
    hob->header.hob_type   = HOB_TYPE_FIRMWARE_VOLUME;
    hob->header.hob_length = sizeof(HOBFirmwareVolume);
    hob->header.reserved   = 0;
    hob->firmware_volume.base_address = base;
    hob->firmware_volume.length       = length;
    memset(hob->firmware_volume.reserved1, 0, sizeof(hob->firmware_volume.reserved1));
    list->count++;
    return true;
}

bool hob_add_cpu(HOBList *list, uint8_t mem_space, uint8_t io_space)
{
    if (!list || list->finalized || list->count >= HOB_MAX_COUNT) return false;

    HOB *hob = &list->hobs[list->count];
    hob->header.hob_type   = HOB_TYPE_CPU;
    hob->header.hob_length = sizeof(HOBCPU);
    hob->header.reserved   = 0;
    hob->cpu.size_of_memory_space = mem_space;
    hob->cpu.size_of_io_space     = io_space;
    memset(hob->cpu.reserved, 0, sizeof(hob->cpu.reserved));
    list->count++;
    return true;
}

bool hob_finalize(HOBList *list)
{
    if (!list || list->finalized || list->count >= HOB_MAX_COUNT) return false;

    HOB *end = &list->hobs[list->count];
    end->header.hob_type   = HOB_TYPE_END_OF_LIST;
    end->header.hob_length = sizeof(HOBHeader);
    end->header.reserved   = 0;
    list->count++;
    list->finalized = true;
    return true;
}

size_t hob_find_by_type(const HOBList *list, uint16_t type, HOB *results, size_t max_results)
{
    if (!list || !results || max_results == 0) return 0;
    size_t found = 0;

    for (size_t i = 0; i < list->count && found < max_results; i++) {
        if (list->hobs[i].header.hob_type == type) {
            memcpy(&results[found], &list->hobs[i], sizeof(HOB));
            found++;
        }
    }
    return found;
}

bool hob_get_phit(const HOBList *list, HOBPHIT *phit)
{
    if (!list || !phit) return false;
    for (size_t i = 0; i < list->count; i++) {
        if (list->hobs[i].header.hob_type == HOB_TYPE_PHIT) {
            memcpy(phit, &list->hobs[i].phit, sizeof(HOBPHIT));
            return true;
        }
    }
    return false;
}

const char *hob_type_to_string(uint16_t type)
{
    switch (type) {
    case HOB_TYPE_PHIT:             return "PHIT (Phase Handoff Information Table)";
    case HOB_TYPE_MEMORY_ALLOC:     return "Memory Allocation";
    case HOB_TYPE_RESOURCE_DESC:    return "Resource Descriptor";
    case HOB_TYPE_FIRMWARE_VOLUME:  return "Firmware Volume";
    case HOB_TYPE_CPU:              return "CPU Information";
    case HOB_TYPE_MEMORY_POOL:      return "Memory Pool";
    case HOB_TYPE_FIRMWARE_VOLUME2: return "Firmware Volume (2)";
    case HOB_TYPE_FIRMWARE_VOLUME3: return "Firmware Volume (3)";
    case HOB_TYPE_UEFI_CAPSULE:     return "UEFI Capsule";
    case HOB_TYPE_GUID_EXT:         return "GUID Extension";
    case HOB_TYPE_END_OF_LIST:      return "End of HOB List";
    default:                        return "Unknown HOB Type";
    }
}

const char *hob_memory_type_to_string(uint8_t type)
{
    switch (type) {
    case HOB_MEM_ALLOC_EOI_RESIDENT: return "EfiReservedMemoryType";
    case 0x01: return "EfiLoaderCode";
    case 0x02: return "EfiLoaderData";
    case 0x03: return "EfiACPIMemoryNVS";
    case 0x04: return "EfiACPIReclaimMemory";
    case 0x05: return "EfiRuntimeServicesCode";
    case 0x06: return "EfiRuntimeServicesData";
    case 0x07: return "EfiConventionalMemory";
    case 0x08: return "EfiUnusableMemory";
    case 0x09: return "EfiPersistentMemory";
    default:   return "Unknown Memory Type";
    }
}

const char *hob_resource_type_to_string(uint32_t type)
{
    switch (type) {
    case HOB_RESOURCE_SYSTEM_MEMORY:          return "System Memory";
    case HOB_RESOURCE_MEMORY_MAPPED_IO:       return "Memory Mapped I/O";
    case HOB_RESOURCE_IO:                     return "I/O Port";
    case HOB_RESOURCE_FIRMWARE_DEVICE:        return "Firmware Device";
    case HOB_RESOURCE_MEMORY_MAPPED_IO_PORT:  return "Memory Mapped I/O Port";
    case HOB_RESOURCE_MEMORY_RESERVED:        return "Memory Reserved";
    case HOB_RESOURCE_IO_RESERVED:            return "I/O Reserved";
    default:                                  return "Unknown Resource Type";
    }
}

uint32_t hob_calculate_total_size(const HOBList *list)
{
    if (!list) return 0;
    uint32_t total = 0;
    for (size_t i = 0; i < list->count; i++) {
        total += list->hobs[i].header.hob_length;
    }
    return total;
}

void hob_print_list(const HOBList *list)
{
    if (!list || list->count == 0) {
        printf("No HOB entries.\n");
        return;
    }

    printf("=== HOB List (%zu entries, %s finalized) ===\n",
           list->count, list->finalized ? "" : "not");

    HOBPHIT phit;
    if (hob_get_phit(list, &phit)) {
        printf("\n--- PHIT ---\n");
        printf("  Boot Mode: 0x%08X\n", phit.boot_mode);
        printf("  Memory Top: 0x%016llX\n", (unsigned long long)phit.memory_top);
        printf("  Memory Bottom: 0x%016llX\n", (unsigned long long)phit.memory_bottom);
        printf("  Free Memory Top: 0x%016llX\n", (unsigned long long)phit.free_memory_top);
        printf("  Free Memory Bottom: 0x%016llX\n", (unsigned long long)phit.free_memory_bottom);
    }

    size_t mem_count = 0, res_count = 0, fv_count = 0, cpu_count = 0;
    for (size_t i = 0; i < list->count; i++) {
        const HOB *hob = &list->hobs[i];
        printf("\n  [%3zu] %s\n", i, hob_type_to_string(hob->header.hob_type));

        switch (hob->header.hob_type) {
        case HOB_TYPE_PHIT:
            break;
        case HOB_TYPE_MEMORY_ALLOC:
            printf("    Base: 0x%016llX, Length: 0x%016llX\n",
                   (unsigned long long)hob->memory_alloc.memory_base_address,
                   (unsigned long long)hob->memory_alloc.memory_length);
            printf("    Type: %s\n", hob_memory_type_to_string(hob->memory_alloc.memory_type));
            mem_count++;
            break;
        case HOB_TYPE_RESOURCE_DESC:
            printf("    Start: 0x%016llX, Length: 0x%016llX\n",
                   (unsigned long long)hob->resource_desc.physical_start,
                   (unsigned long long)hob->resource_desc.resource_length);
            printf("    Type: %s\n", hob_resource_type_to_string(hob->resource_desc.resource_type));
            printf("    Attributes: 0x%08X\n", hob->resource_desc.resource_attribute);
            res_count++;
            break;
        case HOB_TYPE_FIRMWARE_VOLUME:
            printf("    Base: 0x%016llX, Length: 0x%016llX\n",
                   (unsigned long long)hob->firmware_volume.base_address,
                   (unsigned long long)hob->firmware_volume.length);
            fv_count++;
            break;
        case HOB_TYPE_CPU:
            printf("    Memory Space: %u bits, I/O Space: %u bits\n",
                   hob->cpu.size_of_memory_space,
                   hob->cpu.size_of_io_space);
            cpu_count++;
            break;
        case HOB_TYPE_END_OF_LIST:
            break;
        default:
            printf("    Length: %u bytes\n", hob->header.hob_length);
            break;
        }
    }

    printf("\n--- Summary ---\n");
    printf("  Memory Allocations: %zu\n", mem_count);
    printf("  Resource Descriptors: %zu\n", res_count);
    printf("  Firmware Volumes: %zu\n", fv_count);
    printf("  CPUs: %zu\n", cpu_count);
    printf("  Total HOB size: %u bytes\n", hob_calculate_total_size(list));
}
