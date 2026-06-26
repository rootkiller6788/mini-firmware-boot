#include "boot_phases.h"
#include "cache_as_ram.h"
#include "cpu_init.h"
#include "memory_init.h"
#include "device_enum.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *boot_phase_name(BootPhase phase)
{
    switch (phase) {
    case BOOT_PHASE_SEC: return "SEC (Security)";
    case BOOT_PHASE_PEI: return "PEI (Pre-EFI Initialization)";
    case BOOT_PHASE_DXE: return "DXE (Driver Execution Environment)";
    case BOOT_PHASE_BDS: return "BDS (Boot Device Selection)";
    case BOOT_PHASE_TSL: return "TSL (Transient System Load)";
    case BOOT_PHASE_RT:  return "RT (Run Time)";
    default:             return "UNKNOWN";
    }
}

const char *memory_map_type_name(MemoryMapType type)
{
    switch (type) {
    case MEMMAP_RESERVED:       return "Reserved";
    case MEMMAP_LOADER_CODE:    return "LoaderCode";
    case MEMMAP_LOADER_DATA:    return "LoaderData";
    case MEMMAP_BOOT_SVC_CODE:  return "BootServicesCode";
    case MEMMAP_BOOT_SVC_DATA:  return "BootServicesData";
    case MEMMAP_RUNTIME_CODE:   return "RuntimeCode";
    case MEMMAP_RUNTIME_DATA:   return "RuntimeData";
    case MEMMAP_ACPI_RECLAIM:   return "ACPI Reclaim";
    case MEMMAP_ACPI_NVS:       return "ACPI NVS";
    case MEMMAP_MMIO:           return "MMIO";
    default:                    return "Unknown";
    }
}

void boot_init(BootState *state)
{
    if (!state) return;

    memset(state, 0, sizeof(BootState));
    state->current_phase = BOOT_PHASE_SEC;
    state->total_memory = 0;
    state->cpu_count = 1;
    state->cache_as_ram_active = false;

    for (int i = 0; i < MAX_HANDOFF_BLOCKS; i++) {
        state->phase_complete[i] = false;
        state->phase_handoff_blocks[i].fv_count = 0;
        state->phase_handoff_blocks[i].memory_map_count = 0;
    }

    printf("[BOOT] Boot state initialized. Starting at SEC phase.\n");
}

bool boot_sec_phase(BootState *state)
{
    if (!state || state->current_phase != BOOT_PHASE_SEC) return false;

    printf("\n========================================\n");
    printf("  [SEC] Security Phase - Reset Vector\n");
    printf("========================================\n");

    printf("[SEC] Initializing Cache-as-RAM (CAR)...\n");
    CARState car;
    car_init(&car);
    car_enable(&car);
    state->cache_as_ram_active = true;

    printf("[SEC] Setting up temporary stack in CAR at 0x%08llX\n",
           (unsigned long long)CAR_STACK_TOP);

    printf("[SEC] Verifying reset vector at 0xFFFFFFF0...\n");
    printf("[SEC] Initial jmp to firmware entry point.\n");

    printf("[SEC] Loading microcode for BSP...\n");
    printf("[SEC] Setting up GDT, IDT in CAR space.\n");

    printf("[SEC] Discovering memory controller...\n");
    state->total_memory = 8ULL * 1024 * 1024 * 1024;

    HandOffBlock *hob = &state->phase_handoff_blocks[BOOT_PHASE_SEC];
    hob->fv_count = 1;
    hob->fv_bases[0] = 0xFF000000;
    hob->fv_sizes[0] = 0x01000000;
    hob->fv_bases[1] = 0xFE000000;
    hob->fv_sizes[1] = 0x01000000;
    hob->fv_count = 2;

    hob->memory_map_count = 4;
    hob->memory_map[0] = (MemoryMapEntry){MEMMAP_RESERVED, 0x00000000, 0x100};
    hob->memory_map[1] = (MemoryMapEntry){MEMMAP_LOADER_CODE, 0xFF000000, 0x1000};
    hob->memory_map[2] = (MemoryMapEntry){MEMMAP_BOOT_SVC_CODE, 0xFE000000, 0x1000};
    hob->memory_map[3] = (MemoryMapEntry){MEMMAP_ACPI_RECLAIM, 0x000E0000, 0x20};

    state->phase_complete[BOOT_PHASE_SEC] = true;
    printf("[SEC] Phase complete. Hand-off block prepared.\n");

    if (!state->cache_as_ram_active) {
        printf("[SEC] WARNING: CAR not active during SEC!\n");
    }

    return car.enabled;
}

bool boot_pei_phase(BootState *state)
{
    if (!state || state->current_phase != BOOT_PHASE_PEI) return false;

    printf("\n========================================\n");
    printf("  [PEI] Pre-EFI Initialization Phase\n");
    printf("========================================\n");

    printf("[PEI] Dispatching PEI Modules (PEIMs)...\n");
    printf("[PEI] PEIM: CPU Init - initializing BSP...\n");

    CPUInitState cpu;
    cpu_init_bsp(&cpu);
    cpu_load_microcode(&cpu, 0x000000B5);
    cpu_init_msrs(&cpu);
    cpu_init_caches(&cpu);
    cpu_enable_paging(&cpu);
    cpu_print_state(&cpu);

    printf("[PEI] PEIM: Memory Init - SPD read and DDR training...\n");
    uint8_t raw_spd[256];
    memset(raw_spd, 0, sizeof(raw_spd));
    raw_spd[2] = 0x0C;
    raw_spd[4] = 0x10;
    raw_spd[12] = 0x0D;

    SPDData spd1, spd2, spd3, spd4;
    mem_init_spd(&spd1, raw_spd);
    mem_init_spd(&spd2, raw_spd);
    mem_init_spd(&spd3, raw_spd);
    mem_init_spd(&spd4, raw_spd);

    SPDData dimm_array[4] = {spd1, spd2, spd3, spd4};
    MemoryController mc;
    mem_init_controller(&mc, dimm_array, 4);
    mem_train_ddr(&mc);
    mem_print_controller(&mc);

    printf("[PEI] PEIM: CAR Teardown - flushing to DRAM...\n");
    uint8_t dram_buffer[CAR_SIZE];
    CARState car_teardown;
    car_init(&car_teardown);
    car_enable(&car_teardown);
    car_teardown(&car_teardown, dram_buffer);
    state->cache_as_ram_active = false;

    printf("[PEI] PEIM: Building memory map...\n");
    MemoryMap map;
    mem_build_map(&map, mc.total_memory_mb * 1024ULL * 1024ULL, 0xFE000000);
    mem_print_map(&map);

    printf("[PEI] PEIM: PCI Enumeration...\n");
    PCIBus pci_bus;
    pci_enumerate_bus(&pci_bus, 0);

    printf("[PEI] PEIM: Publishing PEI-to-DXE HOBs...\n");
    HandOffBlock *hob = &state->phase_handoff_blocks[BOOT_PHASE_PEI];
    hob->fv_count = 3;
    hob->fv_bases[0] = 0xFF000000;
    hob->fv_sizes[0] = 0x01000000;
    hob->fv_bases[1] = 0xFE000000;
    hob->fv_sizes[1] = 0x01000000;
    hob->fv_bases[2] = 0xFD000000;
    hob->fv_sizes[2] = 0x01000000;

    for (uint32_t i = 0; i < map.count && i < MAX_MEMMAP_ENTRIES; i++) {
        hob->memory_map[i].type = map.entries[i].type;
        hob->memory_map[i].base = map.entries[i].base;
        hob->memory_map[i].pages = map.entries[i].pages;
    }
    hob->memory_map_count = map.count;

    state->phase_complete[BOOT_PHASE_PEI] = true;
    printf("[PEI] Phase complete. %u HOBs published.\n", hob->fv_count);

    return true;
}

bool boot_dxe_phase(BootState *state)
{
    if (!state || state->current_phase != BOOT_PHASE_DXE) return false;

    printf("\n========================================\n");
    printf("  [DXE] Driver Execution Environment\n");
    printf("========================================\n");

    printf("[DXE] DXE Core loaded. Initializing DXE services...\n");
    printf("[DXE] Installing EFI System Table...\n");
    printf("[DXE] Installing Boot Services...\n");
    printf("[DXE] Installing Runtime Services...\n");

    printf("[DXE] Loading DXE drivers from FVs...\n");
    const char *drivers[] = {
        "PciHostBridge", "CpuArch", "Metronome", "Timer",
        "RealTimeClock", "ResetSystem", "Runtime", "SecurityStub",
        "DataHub", "FirmwareVolumeBlock", "Variable", "WatchdogTimer",
        "PciBus", "IdeController", "UsbBus", "SataController",
        "NetworkStack", "GraphicsOutput", "ConsoleSplitter", "DiskIo"
    };
    int driver_count = sizeof(drivers) / sizeof(drivers[0]);

    for (int i = 0; i < driver_count; i++) {
        printf("[DXE]   Loading driver: %s... ", drivers[i]);
        printf("OK (EFI_SUCCESS)\n");
    }

    printf("[DXE] Connecting all controllers...\n");
    printf("[DXE] PCI Bus Driver binding started.\n");
    printf("[DXE] USB Host Controller initialized.\n");
    printf("[DXE] SATA Controller initialized.\n");
    printf("[DXE] Network stack initialized (SNP->MNP->ARP->IP4->UDP->TCP).\n");

    printf("[DXE] Graphics Output Protocol installed (1024x768).\n");
    printf("[DXE] Console devices: ConIn=USB Keyboard, ConOut=UEFI GOP\n");

    HandOffBlock *hob = &state->phase_handoff_blocks[BOOT_PHASE_DXE];
    hob->fv_count = 4;
    hob->fv_bases[0] = 0xFF000000;
    hob->fv_sizes[0] = 0x01000000;
    hob->fv_bases[1] = 0xFE000000;
    hob->fv_sizes[1] = 0x01000000;
    hob->fv_bases[2] = 0xFD000000;
    hob->fv_sizes[2] = 0x01000000;
    hob->fv_bases[3] = 0xFC000000;
    hob->fv_sizes[3] = 0x01000000;

    hob->memory_map_count = 1;
    hob->memory_map[0] = (MemoryMapEntry){MEMMAP_RUNTIME_CODE, 0x100000, 0x1000};

    state->phase_complete[BOOT_PHASE_DXE] = true;
    printf("[DXE] Phase complete. %d drivers loaded.\n", driver_count);

    return true;
}

bool boot_bds_phase(BootState *state)
{
    if (!state || state->current_phase != BOOT_PHASE_BDS) return false;

    printf("\n========================================\n");
    printf("  [BDS] Boot Device Selection\n");
    printf("========================================\n");

    printf("[BDS] Reading boot options from NVRAM...\n");
    printf("[BDS] Boot Manager started.\n");
    printf("[BDS] Platform BDS policy initialized.\n");

    printf("[BDS] Connecting console devices...\n");
    printf("[BDS] Processing boot option #0001: \"UEFI Hard Drive\"\n");
    printf("[BDS] Processing boot option #0002: \"UEFI CD/DVD Drive\"\n");
    printf("[BDS] Processing boot option #0003: \"UEFI USB Drive\"\n");
    printf("[BDS] Processing boot option #0004: \"UEFI PXE Network\"\n");
    printf("[BDS] Processing boot option #0005: \"UEFI Shell\"\n");

    printf("[BDS] BootOption #0001: DevicePath=HD(1,GPT,...)\n");
    printf("[BDS] Loading boot loader from HD(1,GPT,...)/EFI/BOOT/BOOTX64.EFI...\n");

    printf("[BDS] Verifying image signature (Secure Boot)...\n");
    printf("[BDS] Image verified. Calling EFI_BOOT_SERVICES.LoadImage().\n");
    printf("[BDS] Calling EFI_BOOT_SERVICES.StartImage().\n");

    HandOffBlock *hob = &state->phase_handoff_blocks[BOOT_PHASE_BDS];
    hob->fv_count = 5;
    hob->fv_bases[0] = 0xFF000000;
    hob->fv_sizes[0] = 0x01000000;
    hob->fv_bases[1] = 0xFE000000;
    hob->fv_sizes[1] = 0x01000000;
    hob->fv_bases[2] = 0xFD000000;
    hob->fv_sizes[2] = 0x01000000;
    hob->fv_bases[3] = 0xFC000000;
    hob->fv_sizes[3] = 0x01000000;
    hob->fv_bases[4] = 0xFB000000;
    hob->fv_sizes[4] = 0x01000000;

    hob->memory_map_count = 2;
    hob->memory_map[0] = (MemoryMapEntry){MEMMAP_RUNTIME_CODE, 0x100000, 0x1000};
    hob->memory_map[1] = (MemoryMapEntry){MEMMAP_RUNTIME_DATA, 0x200000, 0x2000};

    state->phase_complete[BOOT_PHASE_BDS] = true;
    printf("[BDS] Phase complete. Handing off to OS loader.\n");

    return true;
}

bool boot_transition(BootState *state, BootPhase next)
{
    if (!state) return false;

    BootPhase current = state->current_phase;

    if (next <= current && next != BOOT_PHASE_SEC) {
        printf("[BOOT] ERROR: Cannot transition backwards from %s to %s\n",
               boot_phase_name(current), boot_phase_name(next));
        return false;
    }

    printf("\n[BOOT] Transition: %s -> %s\n",
           boot_phase_name(current), boot_phase_name(next));
    printf("[BOOT] Saving hand-off block for %s...\n", boot_phase_name(current));

    printf("[BOOT] Phase hand-off: passing %u FVs and %u memory map entries.\n",
           state->phase_handoff_blocks[current].fv_count,
           state->phase_handoff_blocks[current].memory_map_count);

    state->current_phase = next;

    printf("[BOOT] Next phase: %s\n", boot_phase_name(next));

    bool result = false;
    switch (next) {
    case BOOT_PHASE_SEC:
        result = boot_sec_phase(state);
        break;
    case BOOT_PHASE_PEI:
        result = boot_pei_phase(state);
        break;
    case BOOT_PHASE_DXE:
        result = boot_dxe_phase(state);
        break;
    case BOOT_PHASE_BDS:
        result = boot_bds_phase(state);
        break;
    case BOOT_PHASE_TSL:
        printf("[BOOT] Entering TSL phase - OS loader running...\n");
        result = true;
        break;
    case BOOT_PHASE_RT:
        printf("[BOOT] Entering RT phase - OS runtime, UEFI runtime services active.\n");
        result = true;
        break;
    default:
        printf("[BOOT] Unknown phase: %d\n", next);
        result = false;
        break;
    }

    return result;
}

void boot_print_phase(BootState *state)
{
    if (!state) return;

    printf("\n============= BOOT STATE =============\n");
    printf("Current Phase: %s (%d)\n",
           boot_phase_name(state->current_phase), state->current_phase);
    printf("Total Memory:  %llu bytes (%.2f GB)\n",
           (unsigned long long)state->total_memory,
           state->total_memory / (1024.0 * 1024.0 * 1024.0));
    printf("CPU Count:     %u\n", state->cpu_count);
    printf("CAR Active:    %s\n", state->cache_as_ram_active ? "Yes" : "No");
    printf("Phases Complete: ");
    for (int i = 0; i < MAX_HANDOFF_BLOCKS; i++) {
        if (state->phase_complete[i]) {
            printf("[%s] ", boot_phase_name((BootPhase)i));
        }
    }
    printf("\n");

    printf("Hand-off Blocks:\n");
    for (int i = 0; i < MAX_HANDOFF_BLOCKS; i++) {
        HandOffBlock *hob = &state->phase_handoff_blocks[i];
        if (hob->fv_count > 0) {
            printf("  Phase %d: %u FVs, %u memmap entries\n",
                   i, hob->fv_count, hob->memory_map_count);
            for (uint32_t j = 0; j < hob->fv_count; j++) {
                printf("    FV[%u]: base=0x%08llX, size=0x%08llX\n",
                       j, (unsigned long long)hob->fv_bases[j],
                       (unsigned long long)hob->fv_sizes[j]);
            }
        }
    }
    printf("======================================\n");
}
