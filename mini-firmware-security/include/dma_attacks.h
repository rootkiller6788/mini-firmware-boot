#ifndef DMA_ATTACKS_H
#define DMA_ATTACKS_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define IOMMU_MAX_DEVICES           256
#define IOMMU_PAGE_TABLE_ENTRIES    512
#define IOMMU_PAGE_SIZE             4096
#define IOMMU_DOMAIN_MAX            64

#define IOMMU_FLAG_PRESENT          (1 << 0)
#define IOMMU_FLAG_READ             (1 << 1)
#define IOMMU_FLAG_WRITE            (1 << 2)
#define IOMMU_FLAG_SUPERVISOR       (1 << 3)
#define IOMMU_FLAG_NX               (1 << 63)

#define DMA_REMAPPING_ENABLED       (1 << 0)
#define DMA_INTERRUPT_REMAPPING     (1 << 1)
#define DMA_QUEUE_INVALIDATION      (1 << 2)

#define ATS_ENABLE                  (1 << 15)

typedef struct {
    uint16_t requester_id;
    uint64_t dma_buffer;
    bool     enabled;
    bool     ats_capable;
    uint8_t  bus;
    uint8_t  device;
    uint8_t  function;
} DMADevice;

typedef struct {
    bool     valid;
    uint64_t page_table_ptr;
    uint16_t domain_id;
    bool     present;
} DeviceTableEntry;

typedef struct {
    uint64_t phys_addr;
    uint64_t virt_addr;
    uint64_t length;
    uint16_t flags;
} PageTableEntry;

typedef struct {
    uint16_t domain_id;
    PageTableEntry entries[IOMMU_PAGE_TABLE_ENTRIES];
    size_t   entry_count;
} DomainTable;

typedef struct {
    bool             enabled;
    DeviceTableEntry device_table[IOMMU_MAX_DEVICES];
    DomainTable     *domains[IOMMU_DOMAIN_MAX];
    size_t           domain_count;
    uint32_t         capabilities;
} IOMMU;

typedef struct {
    bool     enabled;
    bool     ats_request;
    uint16_t requester_id;
    uint32_t translated_address;
} ATS;

bool dma_attack_sim(DMADevice *device, uint64_t target_phys_addr,
                    uint8_t *stolen_data, size_t data_size,
                    IOMMU *iommu);
void iommu_init(IOMMU *iommu);
bool iommu_map_device(IOMMU *iommu, uint16_t requester_id,
                      uint64_t page_table_ptr, uint16_t domain_id);
bool iommu_translate(IOMMU *iommu, uint16_t requester_id,
                     uint64_t dma_addr, uint64_t *phys_addr);
bool iommu_protect(IOMMU *iommu, uint16_t requester_id);
bool iommu_unmap_device(IOMMU *iommu, uint16_t requester_id);
bool iommu_create_domain(IOMMU *iommu, uint16_t domain_id,
                         uint64_t device_phys_base,
                         uint64_t iova_base, size_t size,
                         uint16_t perm_flags);
bool iommu_page_fault(IOMMU *iommu, uint16_t requester_id,
                      uint64_t fault_addr);

#endif
