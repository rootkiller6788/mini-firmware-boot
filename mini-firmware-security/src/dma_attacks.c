#include "dma_attacks.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static bool iommu_pte_valid(const PageTableEntry *pte) {
    return (pte->flags & IOMMU_FLAG_PRESENT) != 0;
}

static uint64_t iommu_pte_phys_addr(const PageTableEntry *pte) {
    return pte->phys_addr & 0x0000FFFFFFFFF000ULL;
}

bool dma_attack_sim(DMADevice *device, uint64_t target_phys_addr,
                    uint8_t *stolen_data, size_t data_size,
                    IOMMU *iommu) {
    uint64_t translated_addr;
    uint8_t *phys_mem;

    if (device == NULL || stolen_data == NULL || iommu == NULL)
        return false;

    if (!device->enabled)
        return false;

    if (iommu->enabled) {
        if (!iommu_translate(iommu, device->requester_id,
                             target_phys_addr, &translated_addr)) {
            return false;
        }
        phys_mem = (uint8_t *)(uintptr_t)translated_addr;
    } else {
        phys_mem = (uint8_t *)(uintptr_t)target_phys_addr;
    }

    if (phys_mem == NULL)
        return false;

    memcpy(stolen_data, phys_mem, data_size);

    return true;
}

void iommu_init(IOMMU *iommu) {
    uint32_t i;

    if (iommu == NULL)
        return;

    memset(iommu, 0, sizeof(IOMMU));
    iommu->enabled = true;
    iommu->capabilities = DMA_REMAPPING_ENABLED | DMA_INTERRUPT_REMAPPING;
    iommu->domain_count = 0;

    for (i = 0; i < IOMMU_MAX_DEVICES; i++) {
        iommu->device_table[i].valid = false;
        iommu->device_table[i].page_table_ptr = 0;
        iommu->device_table[i].domain_id = 0;
        iommu->device_table[i].present = false;
    }

    for (i = 0; i < IOMMU_DOMAIN_MAX; i++) {
        iommu->domains[i] = NULL;
    }
}

bool iommu_map_device(IOMMU *iommu, uint16_t requester_id,
                      uint64_t page_table_ptr, uint16_t domain_id) {
    if (iommu == NULL)
        return false;

    if (requester_id >= IOMMU_MAX_DEVICES)
        return false;

    iommu->device_table[requester_id].valid = true;
    iommu->device_table[requester_id].page_table_ptr = page_table_ptr;
    iommu->device_table[requester_id].domain_id = domain_id;
    iommu->device_table[requester_id].present = true;

    return true;
}

bool iommu_translate(IOMMU *iommu, uint16_t requester_id,
                     uint64_t dma_addr, uint64_t *phys_addr) {
    DeviceTableEntry *dev_entry;
    DomainTable *domain;
    uint64_t page_offset;
    uint64_t page_index;
    size_t i;

    if (iommu == NULL || phys_addr == NULL)
        return false;

    if (!iommu->enabled) {
        *phys_addr = dma_addr;
        return true;
    }

    if (requester_id >= IOMMU_MAX_DEVICES)
        return false;

    dev_entry = &iommu->device_table[requester_id];
    if (!dev_entry->valid || !dev_entry->present) {
        return false;
    }

    domain = NULL;
    for (i = 0; i < IOMMU_DOMAIN_MAX; i++) {
        if (iommu->domains[i] != NULL &&
            iommu->domains[i]->domain_id == dev_entry->domain_id) {
            domain = iommu->domains[i];
            break;
        }
    }

    if (domain == NULL)
        return false;

    page_offset = dma_addr & (IOMMU_PAGE_SIZE - 1);
    page_index = (dma_addr / IOMMU_PAGE_SIZE);

    if (page_index >= domain->entry_count)
        return false;

    if (!iommu_pte_valid(&domain->entries[page_index])) {
        iommu_page_fault(iommu, requester_id, dma_addr);
        return false;
    }

    *phys_addr = iommu_pte_phys_addr(&domain->entries[page_index]) + page_offset;
    return true;
}

bool iommu_protect(IOMMU *iommu, uint16_t requester_id) {
    DeviceTableEntry *dev_entry;

    if (iommu == NULL)
        return false;

    if (requester_id >= IOMMU_MAX_DEVICES)
        return false;

    dev_entry = &iommu->device_table[requester_id];
    dev_entry->present = true;
    dev_entry->valid = true;

    return true;
}

bool iommu_unmap_device(IOMMU *iommu, uint16_t requester_id) {
    if (iommu == NULL)
        return false;

    if (requester_id >= IOMMU_MAX_DEVICES)
        return false;

    iommu->device_table[requester_id].valid = false;
    iommu->device_table[requester_id].present = false;

    return true;
}

bool iommu_create_domain(IOMMU *iommu, uint16_t domain_id,
                         uint64_t device_phys_base,
                         uint64_t iova_base, size_t size,
                         uint16_t perm_flags) {
    DomainTable *domain;
    size_t page_count;
    size_t i;

    if (iommu == NULL || size == 0)
        return false;

    if (iommu->domain_count >= IOMMU_DOMAIN_MAX)
        return false;

    for (i = 0; i < IOMMU_DOMAIN_MAX; i++) {
        if (iommu->domains[i] != NULL &&
            iommu->domains[i]->domain_id == domain_id) {
            return false;
        }
    }

    domain = (DomainTable *)malloc(sizeof(DomainTable));
    if (domain == NULL)
        return false;

    memset(domain, 0, sizeof(DomainTable));
    domain->domain_id = domain_id;

    page_count = (size + IOMMU_PAGE_SIZE - 1) / IOMMU_PAGE_SIZE;
    if (page_count > IOMMU_PAGE_TABLE_ENTRIES)
        page_count = IOMMU_PAGE_TABLE_ENTRIES;

    for (i = 0; i < page_count; i++) {
        domain->entries[i].phys_addr = device_phys_base + (i * IOMMU_PAGE_SIZE);
        domain->entries[i].virt_addr = iova_base + (i * IOMMU_PAGE_SIZE);
        domain->entries[i].length = IOMMU_PAGE_SIZE;
        domain->entries[i].flags = IOMMU_FLAG_PRESENT | perm_flags;
    }
    domain->entry_count = page_count;

    iommu->domains[iommu->domain_count] = domain;
    iommu->domain_count++;

    return true;
}

/*
 * IOMMU Page Fault Handler.
 * Called when a DMA request addresses a page not present in
 * the IOMMU page tables (VT-d A/D bit fault or AMD-Vi ILLEGAL_DEV_TABLE).
 *
 * L4: Intel VT-d Spec Rev 3.3 Section 7.4 (Fault Recording)
 *
 * Actions:
 *   1. Record faulting device and address
 *   2. Check if device is valid in device table
 *   3. Block the DMA if device is unknown
 *   4. Log fault for audit
 *
 * Complexity: O(D) for device table lookup
 */
bool iommu_page_fault(IOMMU *iommu, uint16_t requester_id,
                      uint64_t fault_addr) {
    uint32_t i;
    bool device_found = false;

    if (iommu == NULL)
        return false;

    /* Check if the faulting device is in the device table */
    for (i = 0; i < IOMMU_MAX_DEVICES; i++) {
        if (iommu->device_table[i].valid &&
            iommu->device_table[i].present &&
            i == requester_id) {
            device_found = true;
            break;
        }
    }

    /*
     * If device is unknown, this is a malicious DMA attempt.
     * If device is known, this is a legitimate page fault
     * that should be handled by page fault recording.
     *
     * VT-d fault recording register (FRCD_L/H):
     *   FRCD_L[31] = F (fault)
     *   FRCD_L[12:0] = fault reason
     *   FRCD_H[63:12] = faulting GPA
     */
    if (!device_found) {
        /* Block the DMA - this is a potential attack */
        return false;
    }

    /*
     * For known devices, this is a recoverable fault.
     * In real hardware, IOMMU would:
     * 1. Record fault in fault recording registers
     * 2. Generate interrupt to OS/VMM
     * 3. OS handles by mapping the page
     * For our simulation, we return true to allow the
     * OS to handle it gracefully.
     */
    return true;
}
