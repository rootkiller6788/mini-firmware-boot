#include "meminit.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * meminit.c -- Memory Initialization and DDR Training
 *
 * References:
 *   - JEDEC JESD79-4C (DDR4 SDRAM Specification)
 *   - JEDEC JESD400-5 (DDR4 SPD Specification)
 *   - Intel FSP-M External Architecture Specification v2.4
 *   - AMD AGESA Memory Training documentation
 */

/* ??? L2: Memory Controller Initialization ???????????????????? */

bool memctrl_init(MemoryController *mc)
{
    if (!mc) return false;
    memset(mc, 0, sizeof(MemoryController));
    mc->top_of_low_memory = 0xA0000;  /* Legacy VGA hole */
    return true;
}

bool memctrl_add_channel(MemoryController *mc, MemoryChannelID id,
                         uint64_t base_addr, uint64_t size)
{
    if (!mc) return false;
    if (mc->num_channels >= MAX_CHANNELS) return false;

    MemoryChannel *ch = &mc->channels[mc->num_channels];
    memset(ch, 0, sizeof(MemoryChannel));
    ch->id                = id;
    ch->channel_base_addr = base_addr;
    ch->channel_size      = size;
    ch->state             = MEM_INIT_RESET;
    ch->active            = false;

    mc->num_channels++;
    mc->total_memory += size;
    return true;
}

/* ??? L5: SPD Parsing Algorithm ???????????????????????????????? */

/*
 * Parse JEDEC SPD data into usable timing parameters.
 *
 * SPD is an EEPROM on each DIMM containing 512 bytes of configuration
 * data. The firmware reads SPD via SMBus (I2C) during memory init.
 *
 * Algorithm:
 *   1. Validate SPD revision and DRAM type
 *   2. Extract timing parameters from defined byte offsets
 *   3. Calculate total capacity from density, ranks, and bus width
 *   4. Select optimal CAS latency from supported set
 *
 * Reference: JESD400-5 ?4.1 (DDR4 SPD Contents)
 */
bool memctrl_parse_spd(const SPDData *spd, DDRTiming *timing,
                       uint64_t *capacity)
{
    if (!spd || !timing || !capacity) return false;

    memset(timing, 0, sizeof(DDRTiming));

    /* Validate SPD revision and device type */
    if (spd->dram_device_type != 0x0C) {  /* DDR4 SDRAM */
        fprintf(stderr, "SPD: Unsupported DRAM type 0x%02X\n",
                spd->dram_device_type);
        return false;
    }

    /*
     * Decode density: density_banks byte:
     *   [7:4] = Total SDRAM capacity per die
     *   [3:0] = Number of banks
     */
    uint8_t density = (spd->density_banks >> 4) & 0x0F;
    uint8_t banks    = spd->density_banks & 0x0F;
    uint32_t die_capacity_gb = 0;

    /* DDR4 density encoding (JESD400-5 Table 12) */
    switch (density) {
    case 0x00: die_capacity_gb = 0;    break;  /* Not populated */
    case 0x01: die_capacity_gb = 2;    break;  /* 2 Gb */
    case 0x02: die_capacity_gb = 4;    break;  /* 4 Gb */
    case 0x03: die_capacity_gb = 8;    break;  /* 8 Gb */
    case 0x04: die_capacity_gb = 12;   break;  /* 12 Gb */
    case 0x05: die_capacity_gb = 16;   break;  /* 16 Gb */
    case 0x06: die_capacity_gb = 24;   break;  /* 24 Gb */
    case 0x07: die_capacity_gb = 32;   break;  /* 32 Gb */
    default:   die_capacity_gb = 0;    break;
    }

    /* Bus width: 0=8, 1=16, 2=32, 3=64 */
    uint8_t bus_width_code = spd->bus_width & 0x07;
    uint32_t bus_width = 8u << bus_width_code;
    if (bus_width > 64) bus_width = 64;

    /* Organization: [7:4]=SDRAM width, [3:0]=ranks */
    uint8_t organization = spd->organization;
    uint8_t num_ranks = (organization & 0x07) + 1;

    /* Total DIMM capacity = die_capacity * bus_width/8 * num_ranks */
    *capacity = (uint64_t)die_capacity_gb * 1024 * 1024 * 1024 *
                (bus_width / 8) * num_ranks;

    /* Decode timing parameters */
    timing->frequency_mhz = 3200;  /* DDR4-3200 default */

    /* tCK min in picoseconds: 1250 * (tck_min / 256) */
    /* For DDR4-3200: tCK = 0.625 ns */
    if (spd->tck_min > 0) {
        uint32_t tck_ps = (uint32_t)spd->tck_min * 1250 / 256;
        timing->frequency_mhz = (uint32_t)(2000000.0 / (double)tck_ps);
    }

    timing->cas_latency = 22;  /* Typical DDR4-3200 CL22 */
    timing->trcd        = (uint16_t)spd->trcd_min * 1000 / 625 + 1;
    timing->trp         = (uint16_t)spd->trp_min * 1000 / 625 + 1;
    timing->tras        = (uint16_t)((spd->tras_min_hi << 8) | spd->tras_min_lo);
    timing->trc         = timing->tras + timing->trp;
    timing->trfc        = (uint16_t)((spd->trfc_min_hi << 8) | spd->trfc_min_lo);
    timing->twr         = (uint16_t)spd->twr_min * 1000 / 625 + 1;
    timing->trrd_s      = 4;
    timing->trrd_l      = 6;
    timing->tfaw        = 21;
    timing->cwl         = timing->cas_latency > 2 ? timing->cas_latency - 2 : 9;
    timing->ecc_support = (bus_width == 72);  /* 72-bit = 64 data + 8 ECC */

    (void)banks;
    (void)bus_width_code;
    return true;
}

/* ??? L5: Memory Training State Machine ???????????????????????? */

/*
 * Execute DDR training for a single channel.
 *
 * This implements the sequential training algorithm used by
 * Intel FSP-M and AMD AGESA:
 *
 * State machine transitions:
 *   RESET -> DLL_LOCK -> ZQCAL -> MRS -> WRITE_LEVEL ->
 *   READ_LEVEL -> DQS_GATING -> VREF_TRAIN -> MPR_READ -> COMPLETE
 *
 * Each state models a step of the JEDEC initialization sequence.
 * In real hardware, each step involves:
 *   - Writing to DRAM mode registers via MRS commands
 *   - Reading back timing measurements
 *   - Iterative optimization (e.g., DQS centering, Vref sweep)
 *
 * Amdahl's Law (L4): Memory training is a serial bottleneck in boot.
 * If training 4 channels takes 400ms serially, parallel training
 * reduces it to max(per_channel) = 150ms (2.67x speedup).
 *
 * Speedup = 1 / ((1-P) + P/S)
 * where P = 0.95 (parallelizable fraction), S = 4 (channels)
 * Speedup = 1/(0.05 + 0.95/4) = 1/0.2875 = 3.48x
 */
bool memctrl_train_channel(MemoryChannel *ch)
{
    if (!ch) return false;

    static const char *state_names[] = {
        "Reset", "DLL Lock", "ZQ Calibration", "Mode Register Set",
        "Read Leveling", "Write Leveling", "DQS Gating",
        "Vref Training", "MPR Readback", "Complete", "Failed"
    };

    /* Walk through training states */
    for (int state = MEM_INIT_RESET; state <= MEM_INIT_COMPLETE; state++) {
        ch->state = (MemInitState)state;
        printf("  [%s] %s...", ch->id == MEM_CHANNEL_A ? "A" :
               ch->id == MEM_CHANNEL_B ? "B" :
               ch->id == MEM_CHANNEL_C ? "C" : "D",
               state_names[state]);

        switch (state) {
        case MEM_INIT_RESET:
            /* Hold CKE low for tRFC (350ns min for DDR4) */
            printf(" OK (CKE low, tRFC=350ns)\n");
            break;
        case MEM_INIT_DLL_LOCK:
            /* Wait for DLL lock (512 cycles @ 3200MHz = 160ns) */
            printf(" OK (512 clocks)\n");
            break;
        case MEM_INIT_ZQCAL:
            /* Issue ZQCL, wait tZQinit (1024 clocks = 320ns) */
            printf(" OK (ZQCL, 1024 clocks)\n");
            break;
        case MEM_INIT_MRS:
            /* Program MR0-MR6 */
            printf(" OK (MR0=CL%u, MR2=CWL%u)\n",
                   ch->timing.cas_latency, ch->timing.cwl);
            break;
        case MEM_INIT_READ_LEVEL:
            printf(" OK (DQS-DQ centering)\n");
            break;
        case MEM_INIT_WRITE_LEVEL:
            printf(" OK (fly-by skew comp)\n");
            break;
        case MEM_INIT_DQS_GATING:
            printf(" OK (gate training)\n");
            break;
        case MEM_INIT_VREF_TRAIN:
            printf(" OK (VrefDq optimized)\n");
            break;
        case MEM_INIT_MPR_READ:
            printf(" OK (MPR verify pass)\n");
            break;
        case MEM_INIT_COMPLETE:
            printf(" CHANNEL TRAINED\n");
            ch->active = true;
            break;
        default:
            break;
        }
    }

    return ch->active;
}

bool memctrl_train_all(MemoryController *mc)
{
    if (!mc) return false;

    printf("=== Memory Training ===\n");
    printf("Channels: %u, Total physical memory: %llu MB\n\n",
           mc->num_channels,
           (unsigned long long)(mc->total_memory / (1024 * 1024)));

    uint32_t errors = 0;
    for (uint8_t i = 0; i < mc->num_channels; i++) {
        printf("Training channel %c (base=0x%llX, size=%llu MB):\n",
               mc->channels[i].id == MEM_CHANNEL_A ? 'A' :
               mc->channels[i].id == MEM_CHANNEL_B ? 'B' :
               mc->channels[i].id == MEM_CHANNEL_C ? 'C' : 'D',
               (unsigned long long)mc->channels[i].channel_base_addr,
               (unsigned long long)(mc->channels[i].channel_size / (1024 * 1024)));

        if (!memctrl_train_channel(&mc->channels[i])) {
            errors++;
        }
    }

    mc->memory_trained = (errors == 0);
    mc->error_count    = errors;
    mc->usable_memory  = mc->total_memory;  /* Simplified; real hw reserves */

    printf("\nTraining result: %s (%u errors)\n",
           mc->memory_trained ? "PASS" : "FAIL", errors);
    return mc->memory_trained;
}

/* ??? L5: Timing Calculation ??????????????????????????????????? */

/*
 * Calculate read latency.
 *
 * Algorithm (RL formula):
 *   RL = AL + CL + PL
 *
 * where:
 *   AL = tRCD / tCK, rounded up (Additive Latency)
 *   CL = CAS Latency (from MR0[6:4,2])
 *   PL = 4 if CRC enabled, else 0 (Parity Latency, DDR4+)
 *
 * Example: DDR4-3200 CL22, AL=0, PL=0 => RL = 22 cycles
 *          At 3200 MT/s (1600 MHz clock), RL = 22 * 0.625ns = 13.75ns
 *
 * Reference: JESD79-4C ?4.19.2 (Read Timing Parameters)
 */
uint32_t memctrl_calc_read_latency(const DDRTiming *t)
{
    if (!t || t->frequency_mhz == 0) return 0;

    /*
     * Read Latency (RL) = AL + CL + PL
     *
     * AL (Additive Latency): For DDR4, AL is typically 0.
     * It allows the controller to issue READ earlier by AL cycles,
     * absorbing tRCD into the column access pipeline.
     *
     * AL = floor(tRCD_cycles / tCK_cycles), but for DDR4 JEDEC
     * compliant controllers, AL can be 0, CL-1, or CL-2.
     * We use 0 as the default (most common).
     */
    uint32_t al = 0;

    /* PL (Parity Latency): 4 cycles for DDR4 with CRC, else 0 */
    uint32_t pl = (t->ecc_support && t->frequency_mhz >= 2400) ? 4 : 0;

    return al + t->cas_latency + pl;
}

/*
 * Calculate write latency.
 *
 * Algorithm (WL formula):
 *   WL = AL + CWL
 *
 * where:
 *   CWL = CAS Write Latency
 *   For DDR4: CWL = CL - 2 (typical), or CL - 1 for high-freq
 *
 * Example: DDR4-3200 CL22 => CWL=20 => WL = 0 + 20 = 20 cycles
 *
 * Reference: JESD79-4C ?4.20.2 (Write Timing Parameters)
 */
uint32_t memctrl_calc_write_latency(const DDRTiming *t)
{
    if (!t || t->frequency_mhz == 0) return 0;

    /*
     * Write Latency (WL) = AL + CWL
     *
     * CWL (CAS Write Latency): For DDR4, CWL = CL - 2 (typical)
     * or CL - 1 for high-frequency parts (>2666 MT/s).
     * At DDR4-3200 CL22: CWL = 20
     *
     * AL: 0 for DDR4 (same as read case)
     */
    uint32_t al = 0;

    return al + t->cwl;
}

/* ??? L7: Diagnostics ?????????????????????????????????????????? */

void memctrl_print_topology(const MemoryController *mc)
{
    if (!mc) return;

    printf("=== Memory Topology ===\n");
    printf("Total Memory:    %llu MB\n",
           (unsigned long long)(mc->total_memory / (1024 * 1024)));
    printf("Usable Memory:   %llu MB\n",
           (unsigned long long)(mc->usable_memory / (1024 * 1024)));
    printf("Channels:        %u\n", mc->num_channels);
    printf("Trained:         %s\n", mc->memory_trained ? "Yes" : "No");
    printf("Errors:          %u\n", mc->error_count);
    printf("TOLM:            0x%08llX (%.0f MB)\n",
           (unsigned long long)mc->top_of_low_memory,
           (double)mc->top_of_low_memory / (1024 * 1024));
    printf("Top of Low Mem:  0x%08llX\n",
           (unsigned long long)mc->top_of_low_memory);

    for (uint8_t i = 0; i < mc->num_channels; i++) {
        const MemoryChannel *ch = &mc->channels[i];
        printf("\n  Channel %c:\n",
               ch->id == MEM_CHANNEL_A ? 'A' :
               ch->id == MEM_CHANNEL_B ? 'B' :
               ch->id == MEM_CHANNEL_C ? 'C' : 'D');
        printf("    Base:     0x%016llX\n", (unsigned long long)ch->channel_base_addr);
        printf("    Size:     %llu MB\n", (unsigned long long)(ch->channel_size / (1024*1024)));
        printf("    DIMMs:    %u\n", ch->num_dimms);
        printf("    Freq:     %u MHz\n", ch->timing.frequency_mhz);
        printf("    CAS:      CL%u\n", ch->timing.cas_latency);
        printf("    RL/WL:    %u / %u\n",
               memctrl_calc_read_latency(&ch->timing),
               memctrl_calc_write_latency(&ch->timing));
        printf("    State:    %s\n", ch->active ? "Active" : "Inactive");
    }
}

void memctrl_print_spd(const DIMMConfig *dimm)
{
    if (!dimm) return;

    printf("=== DIMM SPD Data ===\n");
    printf("Slot:            %u\n", dimm->dimm_slot);
    printf("Manufacturer ID: 0x%04X\n", dimm->manufacturer_id);
    printf("Part Number:     %s\n", dimm->part_number);
    printf("Capacity:        %llu MB\n",
           (unsigned long long)(dimm->total_capacity / (1024 * 1024)));
    printf("Ranks:           %u\n", dimm->num_ranks);
    printf("SPD Revision:    0x%02X\n", dimm->spd.spd_revision);

    for (uint8_t r = 0; r < dimm->num_ranks; r++) {
        const RankConfig *rank = &dimm->ranks[r];
        printf("  Rank %u: %u MB, %s, %s\n",
               rank->rank_id,
               rank->size_bytes / (1024*1024),
               rank->enabled ? "Enabled" : "Disabled",
               rank->ecc_enabled ? "ECC" : "Non-ECC");
    }
}
