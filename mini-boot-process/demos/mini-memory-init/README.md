# mini-memory-init -- DRAM Initialization Deep Dive

> 参考规范: JEDEC DDR4 Specification (JESD79-4C)
> JEDEC DDR5 Specification (JESD79-5B)
> Intel Memory Reference Code (MRC) Design Guide
> AMD AGESA Memory Initialization Flow

## 1. Overview

DRAM initialization is one of the most complex tasks in early firmware boot. Before any
code or data can be stored in system DRAM, the firmware must:

1. **Read SPD** (Serial Presence Detect) data from each DIMM via SMBus/I2C
2. **Configure the Memory Controller** (IMC on Intel, UMC on AMD) with correct timing parameters
3. **Train the DDR interface** -- calibrate signal timing at the physical layer
4. **Build the Memory Map** -- produce a UEFI-compatible memory resource map

This document describes each step as implemented in `mini-boot-process`'s memory
initialization module (`memory_init.c`).

## 2. SPD (Serial Presence Detect)

Each DDR DIMM contains an EEPROM (typically 256 or 512 bytes) that stores the module's
capabilities and timing parameters. The firmware reads this via the SMBus (System
Management Bus) on I2C addresses 0x50-0x53.

### 2.1 SPD Byte Layout (DDR4)

| Byte Offset | Size | Description |
|-------------|------|-------------|
| 0 | 1 | SPD Bytes Used / Total Bytes / CRC Coverage |
| 1 | 1 | SPD Revision |
| 2 | 1 | DRAM Device Type (0x0C = DDR4 SDRAM) |
| 3 | 1 | Module Type (RDIMM, UDIMM, LRDIMM, etc.) |
| 4 | 1 | SDRAM Density and Banks |
| 5 | 1 | SDRAM Addressing (Row/Column bits) |
| 6 | 1 | Reserved |
| 7 | 1 | Module Organization (ranks, device width) |
| 8 | 1 | Module Memory Bus Width |
| 12-13 | 2 | Timebase: MTB (Medium Timebase) and FTB |
| 18 | 1 | tCKmin (minimum clock cycle time) |
| 20 | 1 | CAS Latencies Supported (bitmask, byte 0) |
| 24 | 1 | tAAmin (minimum CAS-to-CAS delay) |
| 25 | 1 | tRCDmin (RAS-to-CAS delay) |
| 26 | 1 | tRPmin (Row Precharge time) |
| 27-28 | 2 | tRASmin / tRCmin (upper nibbles) |
| 29 | 1 | tRFC1min (Refresh cycle time) |
| 40-41 | 2 | Module Manufacturer ID Code (JEDEC assigned) |

### 2.2 SPD Reading Flow (SMBus)

```
For each SMBus address (0x50, 0x51, 0x52, 0x53):
  1. Send SMBus START condition
  2. Send I2C address + R/W bit
  3. Send SPD byte offset (2 bytes)
  4. Send SMBus REPEATED START
  5. Read 256 bytes
  6. Send SMBus STOP condition
  7. Validate CRC (byte 126 for DDR4)
  8. Parse timing parameters
```

### 2.3 DDR5 SPD Differences
- DDR5 uses SPD5 Hub (I3C Basic, 1 MHz)
- SPD size increased to 1024 bytes
- New timing parameters: tCCD_L, tCCD_S, tCCD_L_WR, tRFCsb
- Two separate SPD regions: Block 0 (general), Block 1 (thermal)

## 3. Memory Controller Configuration

### 3.1 Memory Controller Registers (Intel IMC)

| Register | Address | Description |
|----------|---------|-------------|
| MC_CHANNEL_MAP | 0xFED1C008 | Channel-to-DIMM mapping |
| MC_BIOS_REQ | 0xFED1C028 | BIOS request for MRS writes |
| MC_BIOS_DATA | 0xFED1C02C | MRS command data |
| MC_SCHEDULER_CONTROL | 0xFED1C038 | Scheduler enable/disable |
| MC_ODT_CONTROL | 0xFED1C088 | On-Die Termination settings |
| MC_ZQ_CAL | 0xFED1C0D0 | ZQ calibration control |

### 3.2 Channel Topology

```
Memory Controller
 ├── Channel 0
 │   ├── DIMM 0 (Slot 0) -- typically closest to CPU
 │   └── DIMM 1 (Slot 1) -- or CS# enabled on same slot
 └── Channel 1
     ├── DIMM 0 (Slot 0)
     └── DIMM 1 (Slot 1)
```

Dual-channel interleaved mode:
- Memory is interleaved at 128-byte granularity (cache-line interleave)
- Sequential addresses alternate between channels
- Doubles effective bandwidth for sequential access patterns

## 4. DDR Training

DDR training is the process of calibrating the physical-layer signaling between the
memory controller and DRAM devices. Due to PCB trace length variations, temperature,
and voltage margins, each data lane (DQ) needs individual timing calibration.

### 4.1 MRS (Mode Register Set) Commands

Before training, the controller programs Mode Registers on each rank:

| MRS | Key Settings |
|-----|-------------|
| MRS0 | CAS Latency (CL), Burst Length (BL8), Read Burst Type |
| MRS1 | DLL Enable/Disable, Output Driver Strength, RTT_NOM (ODT for reads) |
| MRS2 | CAS Write Latency (CWL), RTT_WR (Dynamic ODT for writes), C/A parity |
| MRS3 | MPR (Multi-Purpose Register) Read/Write control |
| MRS4 | Write Leveling Enable, CRC, Command/Address parity |
| MRS5 | CA ODT, RTT_PARK, ODT input buffer |
| MRS6 | VrefDQ Training Mode |

### 4.2 Write Leveling (DDR4 JESD79-4C §4.22)

Purpose: Align DQS (Data Strobe) with CK (Clock) at each DRAM device by compensating
for fly-by topology delays.

Procedure:
1. Enter write leveling mode (MRS4 write leveling enable)
2. Controller drives CK at normal rate, DQS at half rate
3. DRAM samples CK at each DQS rising edge; stores result in DQ[0]
4. Controller adjusts DQS delay until DQ[0] transitions from 0 to 1
5. Optimal DQS delay = transition point - 1/4 tCK
6. Store per-byte-lane DQS delays in controller registers
7. Exit write leveling mode

### 4.3 Read DQS Gate Training (DDR4 JESD79-4C §4.23)

Purpose: Determine when to assert DQS input gating so the controller captures the
correct DQS preamble.

Procedure:
1. Controller issues consecutive READ commands with known data pattern (e.g., 0xAA55)
2. Controller sweeps DQS gate delay through a window of values
3. At each delay, verify read data matches the expected pattern
4. The "passing window" defines the valid range of gate delays
5. Center the DQS gate delay within the passing window

### 4.4 Read / Write DQ Deskew

Purpose: Align individual DQ bits within a byte lane to the center of the data eye.

Per-DQ process:
1. For each DQ bit in a byte lane:
   - Sweep DQ delay through programmable range (typically 0-63 taps)
   - For read: read known MPR pattern (0xAA, 0x55)
   - For write: write pattern, read back and compare
2. Find the left and right edges of the data eye for each DQ bit
3. Set each DQ delay to the center of its data eye
4. Optionally adjust VrefDQ to maximize eye opening

### 4.5 VrefDQ Calibration

VrefDQ (Data Voltage Reference) determines the threshold at which a DQ signal is
interpreted as 0 vs. 1. The optimal VrefDQ depends on:

- DRAM output driver strength (RTT_PU/RTT_PD)
- PCB trace impedance
- Signal termination (RTT_NOM/RTT_WR)
- Temperature and voltage variations

Calibration procedure:
1. Sweep VrefDQ from 60% to 92% VDDQ in 0.65% steps (DDR4 range)
2. At each step, perform a read pattern check
3. Identify the range of VrefDQ values where reads are error-free
4. Set VrefDQ to the center of the passing range

## 5. Memory Map Construction

After DRAM is initialized and tested, the firmware builds the *memory map* -- a
descriptor of all physical address regions that will be passed to the OS. The map
is the foundation of UEFI memory management.

### 5.1 UEFI Memory Types

| Type | Value | Description |
|------|-------|-------------|
| EfiReservedMemoryType | 0 | Not available for OS use |
| EfiLoaderCode | 1 | OS loader code -- can be reclaimed |
| EfiLoaderData | 2 | OS loader data -- can be reclaimed |
| EfiBootServicesCode | 3 | Firmware boot code -- reclaimed at ExitBootServices |
| EfiBootServicesData | 4 | Firmware boot data -- reclaimed at ExitBootServices |
| EfiRuntimeServicesCode | 5 | Firmware runtime code -- preserved |
| EfiRuntimeServicesData | 6 | Firmware runtime data -- preserved |
| EfiConventionalMemory | 7 | Free memory available for OS use |
| EfiUnusableMemory | 8 | Errors detected, must not be used |
| EfiACPIReclaimMemory | 9 | ACPI tables -- reclaimable after OS reads |
| EfiACPIMemoryNVS | 10 | ACPI NVS -- preserved across S4 |
| EfiMemoryMappedIO | 11 | MMIO range for PCI BARs |
| EfiMemoryMappedIOPortSpace | 12 | IO-mapped IO range (x86 port IO) |
| EfiPalCode | 13 | PAL code (Itanium only) |
| EfiPersistentMemory | 14 | NVDIMM / Optane persistent memory |

### 5.2 Memory Map Regions (Typical)

```
0x00000000 -- 0x000FFFFF  Reserved (BIOS/firmware, IVT, BDA)
0x00100000 -- 0x010FFFFF  LoaderCode (boot loader image)
0x01100000 -- 0x030FFFFF  BootServicesData (firmware data)
0x03100000 -- 0x1FFFFFFF  Conventional (free memory)
...
0x20000000 -- 0x3FFFFFFF  BootServicesCode (firmware image in DRAM)
0x40000000 -- 0xBFFFFFFF  Conventional (free memory)
...
0xE0000000 -- 0xEFFFFFFF  MMIO (PCI BAR space)
0xF0000000 -- 0xFE00FFFF  MMIO (more PCI BAR space, APIC)
0xFE010000 -- 0xFFFFFFFF  Reserved (BIOS flash, FWH)
```

### 5.3 Memory Hole (TOLUD)

Top of Low Usable DRAM (TOLUD) defines the boundary between DRAM and MMIO:
- Typically 3.0-3.5 GB for 32-bit systems
- System with 4 GB RAM loses ~0.5-1.0 GB to MMIO mapping
- OS uses PAE/64-bit to access memory above 4 GB (TOUUD)
- Remapped region (above 4 GB) contains the "stolen" DRAM

## 6. Memory Testing

Minimal memory testing during boot:

### 6.1 Quick Tests (PEI)
- **Data bus test**: Walking-1 pattern on each data lane
- **Address bus test**: Walking-1 pattern on address lines
- **Memory march test**: Simple march C- test for critical ranges:
  ~~~
  for addr in range: write 0x00000000, read 0x00000000
  for addr in range: write 0xFFFFFFFF, read 0xFFFFFFFF
  for addr in range: write 0x55555555, read 0x55555555
  for addr in range: write 0xAAAAAAAA, read 0xAAAAAAAA
  ~~~

### 6.2 BIST (Built-In Self Test)
- Intel Memory BIST (MBIST) engines in IMC
- Can run diagonal, checkerboard, and random patterns
- Results reported in MC_STATUS register

## 7. Error Handling

Common DRAM initialization failures and their handling:

| Error | Detection | Recovery |
|-------|-----------|----------|
| No DIMM detected | SPD read fails at all addresses | Halt or continue with empty slot |
| SPD CRC mismatch | CRC check fails | Retry 3 times, then mark slot empty |
| Training timeout | Training sequence times out | Retrain with relaxed timings |
| Data pattern mismatch | Read data ≠ Written data | Mark range as EfiUnusableMemory |
| ECC uncorrectable | MCi_STATUS.UC=1 | Map out the page, log error |
| Row/Column decode fail | Address uniqueness test | Disable the affected rank |

## 8. Simulation in mini-boot-process

The `memory_init.c` module simulates the complete flow:

```
mem_init_spd()          → Parse SPD bytes, extract timing and capacity
mem_init_controller()   → Configure channels and DIMM slots
mem_train_ddr()         → Write MRS registers, perform training sequences
mem_train_write_leveling() → Calibrate DQS-to-CK alignment
mem_train_read_dqs()    → Calibrate DQS gate delay
mem_build_map()         → Produce UEFI-compatible MemoryMap struct
mem_print_*()           → Console output of all gathered data
```

## 9. References

- JEDEC JESD79-4C: DDR4 SDRAM Specification
  https://www.jedec.org/standards-documents/docs/jesd79-4a
- JEDEC JESD79-5B: DDR5 SDRAM Specification
  https://www.jedec.org/standards-documents/docs/jesd79-5
- JEDEC JESD21-C: SPD Annex K (DDR4) and Annex L (DDR5)
- Intel Xeon Processor E5-2600 v3 Memory Subsystem Design Guide
- AMD BKDG for Family 17h Models 00h-0Fh Processors
- EDK II MRC Wrapper Design: https://github.com/tianocore/edk2-platforms
- Mastering DRAM Initialization, Maurice Steinman (Intel Press, 2014)
