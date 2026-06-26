#include "memory_init.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const uint32_t g_timing_table_ddr4[][4] = {
    {14, 14, 14, 34},
    {16, 16, 16, 36},
    {18, 18, 18, 38},
    {22, 22, 22, 52},
};

bool mem_init_spd(SPDData *spd, const uint8_t *raw_bytes)
{
    if (!spd || !raw_bytes) return false;

    memset(spd, 0, sizeof(SPDData));
    memcpy(spd->spd_bytes, raw_bytes, 256);

    uint8_t type = raw_bytes[2];
    if (type == SPD_DDR4_TYPE) {
        uint32_t density_bits = raw_bytes[4] & 0x0F;
        uint32_t banks = raw_bytes[4] >> 4;
        spd->module_size_mb = (uint64_t)(8UL << density_bits) * (1UL << banks);

        uint16_t speed_encoded = raw_bytes[12] | (raw_bytes[13] << 8);
        if (speed_encoded > 0) {
            spd->speed = (MemorySpeed)(speed_encoded * 2);
        } else {
            spd->speed = MEM_SPEED_DDR4_2666;
        }

        spd->ranks = (raw_bytes[7] & 0xF8) ? RANK_DUAL : RANK_SINGLE;
    } else if (type == SPD_DDR5_TYPE) {
        spd->module_size_mb = 16384;
        spd->speed = MEM_SPEED_DDR5_4800;
        spd->ranks = RANK_DUAL;
    } else {
        spd->module_size_mb = 2048;
        spd->speed = MEM_SPEED_DDR3_1600;
        spd->ranks = RANK_SINGLE;
    }

    spd->tCL  = 22;
    spd->tRCD = 22;
    spd->tRP  = 22;
    spd->tRAS = 52;
    spd->tRFC = 350;
    spd->ecc_support = false;
    spd->registered = false;

    printf("[SPD] Module: %llu MB, Speed: %d MT/s, Ranks: %d\n",
           (unsigned long long)spd->module_size_mb,
           (int)spd->speed, (int)spd->ranks);

    return true;
}

bool mem_init_controller(MemoryController *ctrl, const SPDData *dimms, uint32_t count)
{
    if (!ctrl || !dimms || count == 0) return false;

    memset(ctrl, 0, sizeof(MemoryController));
    printf("[MEM] Initializing memory controller...\n");

    uint32_t dimm_idx = 0;
    for (int ch = 0; ch < MAX_CHANNELS; ch++) {
        ctrl->channels[ch].channel_enabled = false;

        for (int slot = 0; slot < MAX_DIMM_PER_CHANNEL; slot++) {
            if (dimm_idx < count) {
                ctrl->channels[ch].dimm[slot] = dimms[dimm_idx];
                ctrl->channels[ch].dimm_populated[slot] = true;
                ctrl->total_memory_mb += dimms[dimm_idx].module_size_mb;
                dimm_idx++;

                if (!ctrl->channels[ch].channel_enabled) {
                    ctrl->channels[ch].channel_enabled = true;
                    ctrl->active_channels++;
                }

                printf("[MEM]   Channel %d, DIMM %d: %llu MB @ %d MT/s\n",
                       ch, slot,
                       (unsigned long long)dimms[dimm_idx - 1].module_size_mb,
                       (int)dimms[dimm_idx - 1].speed);
            } else {
                ctrl->channels[ch].dimm_populated[slot] = false;
            }
        }
    }

    ctrl->interleaved = (ctrl->active_channels > 1);
    ctrl->ecc_enabled = false;
    ctrl->base_address = 0x00000000;

    printf("[MEM] %u channels active, %llu MB total memory.\n",
           ctrl->active_channels,
           (unsigned long long)ctrl->total_memory_mb);

    return true;
}

bool mem_train_write_leveling(MemoryController *ctrl)
{
    if (!ctrl) return false;

    printf("[MEM:TRAIN] Write leveling training...\n");

    for (int ch = 0; ch < MAX_CHANNELS; ch++) {
        if (!ctrl->channels[ch].channel_enabled) continue;

        for (int slot = 0; slot < MAX_DIMM_PER_CHANNEL; slot++) {
            if (!ctrl->channels[ch].dimm_populated[slot]) continue;

            uint32_t dqs_delay = MEM_TRAIN_DQS_DELAY;
            printf("[MEM:TRAIN]   Ch%d DIMM%d: MRS4 write leveling...\n", ch, slot);
            printf("[MEM:TRAIN]     DQS delay calibrated to %u cycles.\n", dqs_delay);
        }
    }

    printf("[MEM:TRAIN] Write leveling complete.\n");
    return true;
}

bool mem_train_read_dqs(MemoryController *ctrl)
{
    if (!ctrl) return false;

    printf("[MEM:TRAIN] Read DQS training...\n");

    for (int ch = 0; ch < MAX_CHANNELS; ch++) {
        if (!ctrl->channels[ch].channel_enabled) continue;

        for (int slot = 0; slot < MAX_DIMM_PER_CHANNEL; slot++) {
            if (!ctrl->channels[ch].dimm_populated[slot]) continue;

            uint32_t read_dqs = MEM_TRAIN_READ_DQS;
            printf("[MEM:TRAIN]   Ch%d DIMM%d: read DQS gate training...\n", ch, slot);
            printf("[MEM:TRAIN]     DQS gate aligned, skew = %u ps.\n", read_dqs * 15);
        }
    }

    printf("[MEM:TRAIN] Read DQS training complete.\n");
    return true;
}

bool mem_train_ddr(MemoryController *ctrl)
{
    if (!ctrl) return false;

    printf("[MEM:TRAIN] Starting DDR training sequence...\n");
    printf("[MEM:TRAIN] MRS0: CAS Latency = CL%d\n", g_timing_table_ddr4[0][0]);
    printf("[MEM:TRAIN] MRS1: DLL Enable, ODT RTT_NOM = RZQ/6\n");
    printf("[MEM:TRAIN] MRS2: CWL = CL-2, RTT_WR = Dynamic ODT\n");
    printf("[MEM:TRAIN] MRS3: MPR Readout disabled\n");
    printf("[MEM:TRAIN] MRS4: Write leveling mode entered.\n");

    mem_train_write_leveling(ctrl);
    mem_train_read_dqs(ctrl);

    printf("[MEM:TRAIN] MPR pattern read: 0xAA55 (pass).\n");
    printf("[MEM:TRAIN] DQ deskew: calibrating per-byte delays...\n");
    printf("[MEM:TRAIN] VrefDQ calibration: setting to 75%% VDDQ.\n");
    printf("[MEM:TRAIN] Training complete. DRAM ready.\n");

    return true;
}

bool mem_build_map(MemoryMap *map, uint64_t total_memory, uint64_t mmio_base)
{
    if (!map) return false;

    memset(map, 0, sizeof(MemoryMap));
    uint64_t total_pages = total_memory / 4096;

    printf("[MEM:MAP] Building UEFI-compatible memory map...\n");
    printf("[MEM:MAP] Total memory: %llu bytes (%llu pages)\n",
           (unsigned long long)total_memory,
           (unsigned long long)total_pages);

    map->entries[map->count++] = (MemMapEntry){
        .type = MEMMAP_TYPE_RESERVED,
        .base = 0x00000000,
        .pages = 0x100,
        .attributes = 0
    };

    map->entries[map->count++] = (MemMapEntry){
        .type = MEMMAP_TYPE_LOADER_CODE,
        .base = 0x00100000,
        .pages = 0x100,
        .attributes = 0xF
    };

    map->entries[map->count++] = (MemMapEntry){
        .type = MEMMAP_TYPE_LOADER_DATA,
        .base = 0x01100000,
        .pages = 0x200,
        .attributes = 0xF
    };

    uint64_t boot_svc_pages = total_pages / 4;
    map->entries[map->count++] = (MemMapEntry){
        .type = MEMMAP_TYPE_BOOT_SVC,
        .base = 0x03100000,
        .pages = boot_svc_pages,
        .attributes = 0xF
    };

    uint64_t runtime_pages = total_pages / 8;
    map->entries[map->count++] = (MemMapEntry){
        .type = MEMMAP_TYPE_RUNTIME,
        .base = 0x03100000 + boot_svc_pages * 4096,
        .pages = runtime_pages,
        .attributes = 0x800000000000000FULL
    };

    map->entries[map->count++] = (MemMapEntry){
        .type = MEMMAP_TYPE_ACPI,
        .base = 0xE0000000,
        .pages = 0x100,
        .attributes = 0
    };

    map->entries[map->count++] = (MemMapEntry){
        .type = MEMMAP_TYPE_MMIO,
        .base = mmio_base,
        .pages = 0x2000,
        .attributes = 0
    };

    for (uint32_t i = 0; i < map->count; i++) {
        map->total_pages += map->entries[i].pages;
    }

    printf("[MEM:MAP] %u entries, %llu pages total.\n",
           map->count, (unsigned long long)map->total_pages);

    return true;
}

void mem_print_spd(const SPDData *spd)
{
    if (!spd) return;

    printf("\n   ===== SPD DATA =====\n");
    printf("   Module Size:  %llu MB\n", (unsigned long long)spd->module_size_mb);
    printf("   Speed:        %d MT/s\n", (int)spd->speed);
    printf("   Ranks:        %d\n", (int)spd->ranks);
    printf("   tCL-tRCD-tRP-tRAS: %u-%u-%u-%u\n",
           spd->tCL, spd->tRCD, spd->tRP, spd->tRAS);
    printf("   tRFC:         %u\n", spd->tRFC);
    printf("   ECC:          %s\n", spd->ecc_support ? "Yes" : "No");
    printf("   Registered:   %s\n", spd->registered ? "Yes" : "No");
    printf("   ====================\n");
}

void mem_print_controller(const MemoryController *ctrl)
{
    if (!ctrl) return;

    printf("\n   ===== MEMORY CONTROLLER =====\n");
    printf("   Total Memory:  %llu MB (%.2f GB)\n",
           (unsigned long long)ctrl->total_memory_mb,
           ctrl->total_memory_mb / 1024.0);
    printf("   Channels:      %u active\n", ctrl->active_channels);
    printf("   Interleaved:   %s\n", ctrl->interleaved ? "Yes" : "No");
    printf("   ECC:           %s\n", ctrl->ecc_enabled ? "Enabled" : "Disabled");

    for (int ch = 0; ch < MAX_CHANNELS; ch++) {
        if (!ctrl->channels[ch].channel_enabled) continue;
        printf("   Channel %d:\n", ch);
        for (int slot = 0; slot < MAX_DIMM_PER_CHANNEL; slot++) {
            if (ctrl->channels[ch].dimm_populated[slot]) {
                printf("     DIMM %d: %llu MB\n",
                       slot,
                       (unsigned long long)ctrl->channels[ch].dimm[slot].module_size_mb);
            } else {
                printf("     DIMM %d: (empty)\n", slot);
            }
        }
    }
    printf("   =================================\n");
}

void mem_print_map(const MemoryMap *map)
{
    if (!map) return;

    const char *type_names[] = {
        "Reserved", "LoaderCode", "LoaderData",
        "BootServices", "Runtime", "ACPI", "MMIO"
    };

    printf("\n   ===== MEMORY MAP =====\n");
    printf("   %-3s %-18s %-18s %-12s %s\n",
           "#", "Base", "End", "Pages", "Type");
    printf("   %-3s %-18s %-18s %-12s %s\n",
           "---", "------------------", "------------------", "------------", "-----------");

    for (uint32_t i = 0; i < map->count; i++) {
        const MemMapEntry *e = &map->entries[i];
        uint64_t end = e->base + e->pages * 4096 - 1;
        const char *type_name = (e->type < MEMMAP_TYPE_MAX)
            ? type_names[e->type] : "Unknown";

        printf("   %-3u 0x%016llX 0x%016llX %-12llu %s\n",
               i, (unsigned long long)e->base, (unsigned long long)end,
               (unsigned long long)e->pages, type_name);
    }
    printf("   =======================\n");
    printf("   Total pages: %llu\n", (unsigned long long)map->total_pages);
}
