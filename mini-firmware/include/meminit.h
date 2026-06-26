#ifndef MEMINIT_H
#define MEMINIT_H

/*
 * meminit.h -- Memory Initialization (DDR Training)
 *
 * Simulates the Memory Reference Code (MRC) / DDR training process
 * that firmware performs during early boot.
 *
 * References:
 *   - Intel FSP-M (Memory Initialization)
 *   - JEDEC DDR4 Specification (JESD79-4C)
 *   - JEDEC SPD Specification (JESD400-5)
 *
 * Knowledge coverage:
 *   L1: DDRTiming, MemoryChannel, SPDData, RankConfig structs
 *   L2: Memory training concept (ZQ cal, DQS gating, read/write leveling)
 *   L3: DDR initialization sequence with state machine
 *   L5: Memory timing calculation algorithm
 *   L7: SPD parsing for DIMM detection
 */

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define SPD_DATA_SIZE       512
#define MAX_DIMMS_PER_CH    2
#define MAX_CHANNELS        4
#define MAX_RANKS_PER_DIMM  4

/* ??? L1: SPD (Serial Presence Detect) Data ???????????????????????? */

/*
 * SPD data is stored in an EEPROM on each DIMM.
 * It describes timing parameters, capacity, organization, and vendor.
 * Standard: JEDEC JESD400-5 (DDR4 SPD), JESD300-5B (DDR5 SPD).
 */
typedef struct {
    uint8_t  bytes_used;            /* Bytes used / CRC coverage      */
    uint8_t  spd_revision;          /* SPD revision (0x12 = DDR4)     */
    uint8_t  dram_device_type;      /* DDR4 SDRAM = 0x0C              */
    uint8_t  module_type;           /* RDIMM, UDIMM, LRDIMM, etc.    */
    uint8_t  density_banks;         /* SDRAM density and banks        */
    uint8_t  addressing;            /* Row/column address bits        */
    uint8_t  voltage;               /* Nominal voltage                */
    uint8_t  organization;          /* SDRAM organization             */
    uint8_t  bus_width;             /* Primary bus width              */
    uint16_t module_mfg_id;         /* JEDEC manufacturer ID          */
    uint8_t  module_revision;       /* Module revision                */
    uint8_t  serial_number[4];      /* Module serial number           */
    uint16_t manufacturing_date;    /* Year (BCD) + Week (BCD)        */
    /* Timing parameters (DDR4: bytes 17-21) */
    uint8_t  tck_min;               /* Min cycle time (tCK)           */
    uint8_t  tck_max;               /* Max cycle time                 */
    uint8_t  cas_latencies_byte1;   /* Supported CAS latencies (byte1)*/
    uint8_t  cas_latencies_byte2;   /* Supported CAS latencies (byte2)*/
    uint8_t  taa_min;               /* Min CAS Latency time (tAA)      */
    uint8_t  twr_min;               /* Min Write Recovery time (tWR)   */
    uint8_t  trcd_min;              /* Min RAS to CAS delay (tRCD)     */
    uint8_t  trrd_min;              /* Min Row Active to Row Active    */
    uint8_t  trp_min;               /* Min Row Precharge time (tRP)    */
    uint8_t  tras_min_hi;           /* Min Active to Precharge (tRAS)  */
    uint8_t  tras_min_lo;
    uint8_t  trc_min_hi;            /* Min Active to Active (tRC)      */
    uint8_t  trc_min_lo;
    uint8_t  trfc_min_hi;           /* Min Refresh Recovery (tRFC)     */
    uint8_t  trfc_min_lo;
    /* Additional fields truncated for simulation */
    uint8_t  reserved[512 - 34];
} SPDData;

/* ??? L1: Memory Timing Parameters ????????????????????????????????? */

typedef struct {
    uint32_t frequency_mhz;       /* DRAM clock frequency (e.g., 3200) */
    uint16_t cas_latency;          /* CAS Latency in clock cycles       */
    uint16_t trcd;                 /* RAS-to-CAS delay (tRCD)           */
    uint16_t trp;                  /* Row Precharge time (tRP)          */
    uint16_t tras;                 /* Active to Precharge delay (tRAS)  */
    uint16_t trc;                  /* Row Cycle time (tRC = tRAS + tRP) */
    uint16_t trfc;                 /* Refresh Cycle time (tRFC)         */
    uint16_t twr;                  /* Write Recovery time (tWR)         */
    uint16_t trrd_s;               /* Row-to-Row delay (short, same BG) */
    uint16_t trrd_l;               /* Row-to-Row delay (long, diff BG)  */
    uint16_t tfaw;                 /* Four Activate Window (tFAW)       */
    uint16_t cwl;                  /* CAS Write Latency                 */
    bool     gear2;                /* Gear 2 mode (DDR5, 1/2 rate)     */
    bool     ecc_support;          /* ECC DIMM?                        */
} DDRTiming;

/* ??? L1: Memory Rank Configuration ???????????????????????????????? */

typedef enum {
    RANK_TYPE_SINGLE = 1,
    RANK_TYPE_DUAL   = 2,
    RANK_TYPE_QUAD   = 4
} RankType;

typedef struct {
    uint8_t     rank_id;           /* 0..3 within DIMM                 */
    uint32_t    size_bytes;        /* Size of this rank                */
    bool        enabled;           /* Is rank populated and usable?    */
    bool        ecc_enabled;       /* ECC active for this rank?        */
} RankConfig;

/* ??? L1: DIMM (Dual In-line Memory Module) ???????????????????????? */

typedef struct {
    uint8_t     dimm_slot;         /* Physical slot number (0..1)     */
    SPDData     spd;               /* SPD data from EEPROM             */
    RankConfig  ranks[MAX_RANKS_PER_DIMM];
    uint8_t     num_ranks;         /* 1, 2, or 4                      */
    uint64_t    total_capacity;    /* Total DIMM capacity in bytes     */
    uint32_t    manufacturer_id;   /* From SPD                        */
    char        part_number[32];   /* DIMM part number                */
} DIMMConfig;

/* ??? L1: Memory Channel ??????????????????????????????????????????? */

typedef enum {
    MEM_CHANNEL_A = 0,
    MEM_CHANNEL_B = 1,
    MEM_CHANNEL_C = 2,
    MEM_CHANNEL_D = 3
} MemoryChannelID;

typedef enum {
    MEM_INIT_RESET      = 0,   /* Controller reset                    */
    MEM_INIT_DLL_LOCK   = 1,   /* PLL/DLL frequency lock              */
    MEM_INIT_ZQCAL      = 2,   /* ZQ calibration (impedance matching) */
    MEM_INIT_MRS        = 3,   /* Mode Register Set                  */
    MEM_INIT_READ_LEVEL = 4,   /* Read data strobe leveling           */
    MEM_INIT_WRITE_LEVEL = 5,  /* Write data strobe leveling          */
    MEM_INIT_DQS_GATING  = 6,  /* DQS gating training                 */
    MEM_INIT_VREF_TRAIN  = 7,  /* Vref training (DDR4+)              */
    MEM_INIT_MPR_READ    = 8,  /* Multi-Purpose Register readback     */
    MEM_INIT_COMPLETE    = 9,  /* Training complete, channels active  */
    MEM_INIT_FAILED      = 10  /* Training failed                    */
} MemInitState;

typedef struct {
    MemoryChannelID id;
    DIMMConfig       dimms[MAX_DIMMS_PER_CH];
    uint8_t          num_dimms;
    DDRTiming        timing;
    MemInitState     state;
    uint64_t         channel_base_addr;  /* Physical base address     */
    uint64_t         channel_size;       /* Total mapped size         */
    bool             interleaved;        /* Interleaved with other ch */
    bool             active;             /* Training succeeded?       */
} MemoryChannel;

/* ??? L1: Memory Controller ???????????????????????????????????????? */

typedef struct {
    MemoryChannel channels[MAX_CHANNELS];
    uint8_t       num_channels;
    uint64_t      total_memory;       /* Total system memory (bytes)  */
    uint64_t      usable_memory;      /* After firmware reservation   */
    uint64_t      tseg_base;          /* TSEG (SMM) base address      */
    uint32_t      tseg_size;          /* TSEG size                     */
    uint64_t      mmio_base;          /* MMIO hole base               */
    uint64_t      mmio_limit;         /* MMIO hole limit              */
    uint64_t      top_of_low_memory;  /* TOLM (typically < 4 GB)      */
    bool          memory_trained;     /* All channels trained?        */
    uint32_t      error_count;        /* Training errors              */
} MemoryController;

/* ??? L2/L3: Memory Initialization API ?????????????????????????????? */

/* Initialize the memory controller subsystem */
bool memctrl_init(MemoryController *mc);

/* Add a memory channel with specified DIMM configuration */
bool memctrl_add_channel(MemoryController *mc, MemoryChannelID id,
                         uint64_t base_addr, uint64_t size);

/* Parse SPD data from a DIMM into timing parameters */
bool memctrl_parse_spd(const SPDData *spd, DDRTiming *timing,
                       uint64_t *capacity);

/* ??? L5: Memory Training Algorithm ?????????????????????????????? */

/*
 * Execute DDR training for a channel.
 *
 * Training sequence (per JEDEC DDR4 initialization):
 *   1. RESET: Assert CKE low, wait tRFC
 *   2. DLL_LOCK: Wait for PLL/DLL lock (> 512 clocks)
 *   3. ZQCAL: Issue ZQCL command, wait tZQinit
 *   4. MRS: Program Mode Registers (MR0-MR6)
 *   5. WRITE_LEVELING: Fly-by topology compensation
 *   6. READ_LEVELING: DQS-to-DQ centering per byte lane
 *   7. DQS_GATING: Train DQS gate timing
 *   8. VREF_TRAINING: Optimize reference voltage (DDR4+)
 *   9. MPR_READ: Verify training with MPR readback
 *
 * Complexity: Each step involves reading/writing MRS registers and
 * measuring timing margins. Full training on a real DIMM takes
 * 100-500 ms depending on DIMM density and speed grade.
 */
bool memctrl_train_channel(MemoryChannel *ch);

/* Train all channels in the memory controller */
bool memctrl_train_all(MemoryController *mc);

/* ??? L5: Timing Calculation ????????????????????????????????????? */

/*
 * Calculate minimum read latency.
 *
 * RL = AL + CL + PL
 * where:
 *   AL = Additive Latency (AL = tRCD / tCK, typically 0 for DDR4)
 *   CL = CAS Latency (programmed via MR0)
 *   PL = Parity Latency (4 cycles if CRC enabled, else 0)
 *
 * Reference: JEDEC JESD79-4C ?4.19 (Read Operation)
 */
uint32_t memctrl_calc_read_latency(const DDRTiming *t);

/*
 * Calculate minimum write latency.
 *
 * WL = AL + CWL
 * where:
 *   CWL = CAS Write Latency (typically CL - 1 or CL - 2)
 */
uint32_t memctrl_calc_write_latency(const DDRTiming *t);

/* Print memory topology for diagnostics */
void memctrl_print_topology(const MemoryController *mc);

/* Print SPD data for a DIMM */
void memctrl_print_spd(const DIMMConfig *dimm);

#endif /* MEMINIT_H */
