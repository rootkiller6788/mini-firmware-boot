#include "dma_attacks.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define DEMO_MEMORY_SIZE  (64 * 1024)
#define DEMO_DEV_ID_NIC   0x0200
#define DEMO_DEV_ID_GPU   0x0300
#define DEMO_DEV_ID_EVIL  0xFF00

static uint8_t demo_phys_memory[DEMO_MEMORY_SIZE];
static uint8_t demo_device_memory[DEMO_MEMORY_SIZE];

static uint64_t phys_addr_of(void *ptr) {
    return (uint64_t)(uintptr_t)ptr;
}

int main(void) {
    IOMMU iommu;
    DMADevice nic, gpu, evil_pcie;
    uint8_t stolen[256];
    bool result;
    uint32_t i;

    printf("===== IOMMU / DMA Attack Demo =====\n\n");

    memset(demo_phys_memory, 0, sizeof(demo_phys_memory));
    memset(demo_device_memory, 0xAA, sizeof(demo_device_memory));

    for (i = 0; i < 256; i++) {
        demo_phys_memory[i] = (uint8_t)(i & 0xFF);
    }

    printf("[1] Initializing IOMMU (VT-d simulation)...\n");
    iommu_init(&iommu);
    printf("    Capabilities: 0x%08X\n", iommu.capabilities);
    printf("    DMA Remapping: %s\n",
           (iommu.capabilities & DMA_REMAPPING_ENABLED) ? "ON" : "OFF");

    printf("\n[2] Creating domain for NIC device...\n");
    result = iommu_create_domain(&iommu, 1,
                                  phys_addr_of(demo_device_memory),
                                  0x10000000, 0x10000,
                                  IOMMU_FLAG_READ | IOMMU_FLAG_WRITE);
    printf("    Domain 1: IOVA 0x10000000 -> Phys 0x%llX, 64KB => %s\n",
           (unsigned long long)phys_addr_of(demo_device_memory),
           result ? "OK" : "FAILED");

    printf("\n[3] Registering known-good devices in device table...\n");

    nic.requester_id = DEMO_DEV_ID_NIC;
    nic.bus = 0; nic.device = 0x19; nic.function = 0;
    nic.dma_buffer = phys_addr_of(demo_device_memory);
    nic.enabled = true;
    nic.ats_capable = false;
    result = iommu_map_device(&iommu, DEMO_DEV_ID_NIC,
                               phys_addr_of(demo_device_memory), 1);
    printf("    NIC (BDF 00:19.0) rid=0x%04X => %s\n",
           DEMO_DEV_ID_NIC, result ? "MAPPED" : "FAILED");

    gpu.requester_id = DEMO_DEV_ID_GPU;
    gpu.bus = 0; gpu.device = 0x02; gpu.function = 0;
    gpu.dma_buffer = phys_addr_of(demo_device_memory);
    gpu.enabled = true;
    gpu.ats_capable = false;
    result = iommu_map_device(&iommu, DEMO_DEV_ID_GPU,
                               phys_addr_of(demo_device_memory), 1);
    printf("    GPU (BDF 00:02.0) rid=0x%04X => %s\n",
           DEMO_DEV_ID_GPU, result ? "MAPPED" : "FAILED");

    printf("\n[4] Evil PCIe device attempting DMA (NO IOMMU entry)...\n");
    evil_pcie.requester_id = DEMO_DEV_ID_EVIL;
    evil_pcie.bus = 0x40; evil_pcie.device = 0x00; evil_pcie.function = 0;
    evil_pcie.dma_buffer = 0;
    evil_pcie.enabled = true;
    evil_pcie.ats_capable = false;

    memset(stolen, 0, sizeof(stolen));
    result = dma_attack_sim(&evil_pcie,
                             phys_addr_of(demo_phys_memory),
                             stolen, 64, &iommu);
    printf("    Evil device DMA to phys 0x%llX => %s\n",
           (unsigned long long)phys_addr_of(demo_phys_memory),
           result ? "SUCCEEDED (UNPROTECTED!)" : "BLOCKED BY IOMMU");

    printf("\n[5] Legitimate NIC DMA access through IOMMU...\n");
    memset(stolen, 0, sizeof(stolen));
    result = dma_attack_sim(&nic,
                             phys_addr_of(demo_device_memory) + 0x100,
                             stolen, 32, &iommu);
    printf("    NIC DMA with remapping => %s\n",
           result ? "ALLOWED" : "BLOCKED");

    printf("\n[6] IOMMU address translation test...\n");
    for (i = 0; i < 5; i++) {
        uint64_t iova = 0x10000000 + (i * 0x1000);
        uint64_t phys;
        result = iommu_translate(&iommu, DEMO_DEV_ID_NIC, iova, &phys);
        if (result) {
            printf("    IOVA 0x%08llX -> Physical 0x%08llX\n",
                   (unsigned long long)iova, (unsigned long long)phys);
        } else {
            printf("    IOVA 0x%08llX -> FAULT\n",
                   (unsigned long long)iova);
        }
    }

    printf("\n[7] Unmapping NIC device...\n");
    result = iommu_unmap_device(&iommu, DEMO_DEV_ID_NIC);
    printf("    NIC unmapped => %s\n", result ? "OK" : "FAILED");

    memset(stolen, 0, sizeof(stolen));
    result = dma_attack_sim(&nic,
                             phys_addr_of(demo_device_memory),
                             stolen, 16, &iommu);
    printf("    NIC DMA after unmap => %s\n",
           result ? "ALLOWED (SHOULD BE BLOCKED!)" : "BLOCKED (CORRECT)");

    printf("\n[8] IOMMU protection summary...\n");
    printf("    IOMMU enabled:      %s\n", iommu.enabled ? "YES" : "NO");
    printf("    Mapped devices:     ");
    for (i = 0; i < IOMMU_MAX_DEVICES; i++) {
        if (iommu.device_table[i].present) {
            printf("0x%04X ", (unsigned int)i);
        }
    }
    printf("\n");
    printf("    Active domains:     %zu\n", iommu.domain_count);
    printf("    DMA Remapping:      %s\n",
           (iommu.capabilities & DMA_REMAPPING_ENABLED) ? "ENABLED" : "DISABLED");

    printf("\n===== Demo Complete =====\n");
    return 0;
}
