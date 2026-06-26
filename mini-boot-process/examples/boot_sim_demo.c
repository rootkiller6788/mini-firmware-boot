#include "boot_phases.h"
#include "cache_as_ram.h"
#include "cpu_init.h"
#include "memory_init.h"
#include "device_enum.h"

#include <stdio.h>
#include <string.h>

#define SIMULATED_MEMORY_MB (8ULL * 1024)

int main(void)
{
    printf("============================================================\n");
    printf("  mini-boot-process: Full Boot Simulation Demo\n");
    printf("  UEFI PI Boot Flow: SEC -> PEI -> DXE -> BDS -> TSL -> RT\n");
    printf("============================================================\n\n");

    BootState state;
    boot_init(&state);
    printf("\n");

    boot_print_phase(&state);
    printf("\n");

    printf("------------------------- PHASE 1: SEC -------------------------\n");
    bool sec_ok = boot_transition(&state, BOOT_PHASE_SEC);
    if (!sec_ok) {
        printf("[FATAL] SEC phase failed. Halting.\n");
        return 1;
    }

    printf("\n[PROGRESS] SEC hand-off to PEI...\n");
    HandOffBlock *sec_hob = &state.phase_handoff_blocks[BOOT_PHASE_SEC];
    printf("[PROGRESS]   %u FVs published\n", sec_hob->fv_count);
    printf("[PROGRESS]   %u memory map entries\n", sec_hob->memory_map_count);
    for (uint32_t i = 0; i < sec_hob->memory_map_count; i++) {
        printf("[PROGRESS]     [%u] type=%s base=0x%08llX pages=%llu\n",
               i,
               memory_map_type_name(sec_hob->memory_map[i].type),
               (unsigned long long)sec_hob->memory_map[i].base,
               (unsigned long long)sec_hob->memory_map[i].pages);
    }

    printf("\n------------------------- PHASE 2: PEI -------------------------\n");
    bool pei_ok = boot_transition(&state, BOOT_PHASE_PEI);
    if (!pei_ok) {
        printf("[FATAL] PEI phase failed. Halting.\n");
        return 1;
    }

    printf("\n[PROGRESS] PEI HOBs published for DXE consumption.\n");
    HandOffBlock *pei_hob = &state.phase_handoff_blocks[BOOT_PHASE_PEI];
    printf("[PROGRESS]   %u FVs available for DXE dispatcher\n", pei_hob->fv_count);
    printf("[PROGRESS]   %u memory map entries\n", pei_hob->memory_map_count);

    printf("\n------------------------- PHASE 3: DXE -------------------------\n");
    bool dxe_ok = boot_transition(&state, BOOT_PHASE_DXE);
    if (!dxe_ok) {
        printf("[FATAL] DXE phase failed. Halting.\n");
        return 1;
    }

    printf("\n[PROGRESS] DXE platform initialization complete.\n");

    printf("\n------------------------- PHASE 4: BDS -------------------------\n");
    bool bds_ok = boot_transition(&state, BOOT_PHASE_BDS);
    if (!bds_ok) {
        printf("[FATAL] BDS phase failed. Halting.\n");
        return 1;
    }

    printf("\n[PROGRESS] BDS selected boot option #0001.\n");
    printf("[PROGRESS] Handing control to OS boot loader...\n");

    printf("\n------------------------- PHASE 5: TSL -------------------------\n");
    bool tsl_ok = boot_transition(&state, BOOT_PHASE_TSL);
    if (!tsl_ok) {
        printf("[FATAL] TSL phase failed.\n");
        return 1;
    }
    printf("[TSL] OS Loader: Loading kernel image...\n");
    printf("[TSL] OS Loader: Calling ExitBootServices()...\n");

    printf("\n------------------------- PHASE 6: RT -------------------------\n");
    bool rt_ok = boot_transition(&state, BOOT_PHASE_RT);
    if (!rt_ok) {
        printf("[FATAL] RT phase failed.\n");
        return 1;
    }
    printf("[RT] OS running. UEFI Runtime Services available.\n");

    printf("\n");
    printf("============================================================\n");
    printf("  Boot Simulation Complete!\n");
    printf("============================================================\n");

    boot_print_phase(&state);

    printf("\n\n============= FINAL MEMORY MAP (UEFI) =============\n");
    MemoryMap final_map;
    mem_build_map(&final_map, SIMULATED_MEMORY_MB * 1024ULL * 1024ULL, 0xFE000000);
    mem_print_map(&final_map);

    (void)SIMULATED_MEMORY_MB;

    return 0;
}
