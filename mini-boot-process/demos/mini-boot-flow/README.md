# mini-boot-flow -- UEFI PI Boot Flow Demonstration

> 参考规范: UEFI Platform Initialization Specification v1.8
> Intel Firmware Support Package (FSP) Boot Flow
> AMD AGESA PI Boot Architecture
> Tianocore EDK II Boot Sequence Implementation

## 1. Overview

The Unified Extensible Firmware Interface (UEFI) Platform Initialization (PI) boot flow governs
how an x86/x64 system transitions from cold power-on to hand-off to the operating system loader.
This document describes each PI-defined phase and shows how `mini-boot-process` simulates the
complete boot flow.

## 2. Reset Vector (Pre-SEC)

When power is applied or a reset is asserted, the x86 processor begins execution at the *reset
vector* (CS:IP = F000:FFF0 in real mode, physical address 0xFFFFFFF0). The firmware image
must place a far jump instruction at that address pointing to the SEC phase entry point.

At this stage, no DRAM is available. The processor caches are used as temporary storage via
the Cache-as-RAM (CAR) technique.

**Key events at reset vector:**
- CPU in real mode, 8086-compatible
- CR0.PE = 0 (real mode), CR0.PG = 0 (no paging)
- Cache disabled (CR0.CD = 1, CR0.NW = 1)
- Only 16-bit addressing available
- NMI, interrupts disabled
- Microcode loaded from BIOS flash

## 3. SEC Phase -- Security Phase (Phase 1)

The SEC phase is the first PI phase. Its primary responsibilities are:

### 3.1 Responsibilities
- **Cache-as-RAM setup**: Configure the CPU's cache into no-eviction mode to create a
  temporary data/stack area before DRAM is initialized. Typically up to 256 KB of L1/L2
  cache can be repurposed.
- **Microcode patching**: The BSP loads the latest microcode update for itself.
- **Temporary stack establishment**: A stack pointer is set inside the CAR area.
- **Hand-off to PEI**: Before leaving SEC, the firmware constructs a SEC-to-PEI Hand-Off
  Block (HOB) with at minimum:
  - The location and size of the PEI Foundation Firmware Volume (FV)
  - A temporary memory descriptor for the CAR area
  - Boot mode indication (normal boot, S3 resume, etc.)

### 3.2 SIMULATION in mini-boot-process
- `CARState` structure represents the Cache-as-RAM region
- `boot_sec_phase()` calls `car_enable()` then prepares HOB with:
  - FV at 0xFF000000 and 0xFE000000
  - Memory map entries for reserved, loader, and boot-service regions
- Console output shows each step of the SEC initialization

### 3.3 Hand-Off Block (SEC → PEI)
| Field | Value |
|-------|-------|
| FV Count | 2 |
| FV #0 Base | 0xFF000000 |
| FV #0 Size | 0x01000000 (16 MB) |
| FV #1 Base | 0xFE000000 |
| FV #1 Size | 0x01000000 (16 MB) |
| Memory Map Entries | 4 |
| Boot Mode | Normal |

## 4. PEI Phase -- Pre-EFI Initialization (Phase 2)

The Pre-EFI Initialization (PEI) phase has the most critical hardware responsibilities.
Execution is still in-place from flash (XIP), with CAR providing the stack and heap.

### 4.1 PEI Foundation
- Dispatches PEI Modules (PEIMs) from the firmware volume(s)
- PEIMs are discovered by scanning FV headers for PEIM GUIDs
- Each PEIM exports a PPIs (PEIM-to-PEIM Interfaces)
- PEI Core maintains the dependency graph and dispatches PEIMs in order

### 4.2 Key PEI Modules (PEIMs)
| PEIM | Dependency | Responsibility |
|------|-----------|----------------|
| **CpuPeim** | None | Initialize BSP, load microcode, set up MTRRs, enable caching. |
| **MemoryInit** | CpuPeim | Read SPD from DIMMs, initialize memory controller, train DDR, test memory. |
| **PciHostBridge** | MemoryInit | Initialize PCI host bridge, set up MMCFG space. |
| **PlatformInit** | MemoryInit | Platform-specific init (GPIO, silicon policy, etc.) |
| **DxeIpl** | All above | CAR teardown, load DXE Core, build PEI-to-DXE HOB list. |

### 4.3 Memory Discovery
The MemoryInit PEIM performs the sequence:
1. **SPD Reading**: Via SMBus/I2C/I3C per DIMM slot
2. **DDR Training**: Write leveling, read DQS gate training, DQ deskew, Vref calibration
3. **Memory Testing**: Basic write/readback patterns on critical ranges
4. **Memory Map Construction**: Produce a UEFI-compatible memory map

### 4.4 Hand-Off Block (PEI → DXE)
The HOB list (Hand-Off Block list) is the primary data structure that PEI passes to DXE.
HOB types include:
- **PHIT HOB**: Always first, contains the PEI memory usage report
- **Memory Allocation HOB**: Describes free/to-be-preserved memory ranges
- **Resource Descriptor HOB**: Physical memory and MMIO resource ranges
- **Firmware Volume HOB**: Location of all FVs the DXE dispatcher should load from
- **CPU HOB**: CPU capabilities, cache sizes, reset vector location
- **FPDT HOB**: Firmware Performance Data Table pointers

## 5. DXE Phase -- Driver Execution Environment (Phase 3)

DXE is the most feature-rich PI phase. It provides the environment in which DXE drivers
execute and produce EFI Protocols. Unlike PEI, DXE can load drivers into DRAM and does
not need XIP.

### 5.1 DXE Foundation
- DXE Core provides Boot Services, Runtime Services, and DXE Services
- The DXE Dispatcher loads DXE drivers from firmware volumes
- Drivers register *Protocols*, which other drivers can consume
- The dispatcher resolves dependency expressions (DEPEX) to determine load order

### 5.2 Standard DXE Drivers
| Driver | Produced Protocols |
|--------|-------------------|
| CpuArch | EFI_CPU_ARCH_PROTOCOL |
| Metronome | EFI_METRONOME_ARCH_PROTOCOL |
| Timer | EFI_TIMER_ARCH_PROTOCOL |
| RealTimeClock | EFI_RTC_ARCH_PROTOCOL |
| ResetSystem | EFI_RESET_ARCH_PROTOCOL |
| Runtime | EFI_RUNTIME_ARCH_PROTOCOL |
| SecurityStub | EFI_SECURITY_ARCH_PROTOCOL |
| DataHub | EFI_DATA_HUB_PROTOCOL |
| Variable | EFI_VARIABLE_ARCH_PROTOCOL |
| WatchdogTimer | EFI_WATCHDOG_TIMER_ARCH_PROTOCOL |
| PciHostBridge | EFI_PCI_HOST_BRIDGE_RESOURCE_ALLOCATION |
| PciBus | EFI_PCI_IO_PROTOCOL, EFI_PCI_ROOT_BRIDGE_IO |
| IdeController / SataController | EFI_IDE_CONTROLLER / Block I/O |
| UsbBus | EFI_USB_IO_PROTOCOL |
| Network (SNP → MNP → ARP → IP4 → UDP → TCP) | EFI_SIMPLE_NETWORK through EFI_TCP4_PROTOCOL |
| GraphicsOutput | EFI_GRAPHICS_OUTPUT_PROTOCOL |
| ConsoleSplitter | EFI_SIMPLE_TEXT_IN/OUT_PROTOCOL |
| DiskIo | EFI_DISK_IO_PROTOCOL, EFI_BLOCK_IO_PROTOCOL |
| Partition (GPT/MBR) | Block I/O children for each partition |
| Fat / SimpleFs | EFI_SIMPLE_FILE_SYSTEM_PROTOCOL |

### 5.3 DXE Phase Termination
DXE ends when the BDS phase is entered. The EFI_BDS_ARCH_PROTOCOL is installed, and
all architectural protocols must be present before BDS can begin.

## 6. BDS Phase -- Boot Device Selection (Phase 4)

The Boot Device Selection phase implements the platform's boot policy.

### 6.1 BDS Operations
1. **Boot Manager Initialization**: Loads the platform boot manager
2. **Console Device Connection**: Connects keyboard, display, and serial
3. **Boot Option Processing**: Iterates through ordered boot options
4. **Boot Device Enumeration**: Identifies bootable media via:
   - GPT/MBR partition discovery
   - File system detection (FAT32 for ESP)
   - Boot loader location: `\EFI\BOOT\BOOT{ARCH}.EFI`
     (e.g., `\EFI\BOOT\BOOTX64.EFI` for x86_64)
5. **Secure Boot Verification**: If enabled, verifies the EFI image signature
   against db/dbx databases
6. **Image Loading**: Calls `EFI_BOOT_SERVICES.LoadImage()` then
   `EFI_BOOT_SERVICES.StartImage()`

### 6.2 Boot Options
Boot options are stored as UEFI variables (typically in NVRAM):
```
Boot0000: "UEFI Hard Drive"    – HD(1,GPT,...)
Boot0001: "UEFI CD/DVD Drive"  – CDROM(...)
Boot0002: "UEFI USB Drive"     – USB(...)
Boot0003: "UEFI PXE IPv4"      – MAC(...)
Boot0004: "UEFI Shell"         – MemoryMapped(...)
```

## 7. TSL Phase -- Transient System Load (Phase 5)

The TSL phase starts when the boot loader image is started via `StartImage()` and persists
until `ExitBootServices()` is called.

### 7.1 During TSL
- The OS boot loader runs as a UEFI application
- UEFI Boot Services are still available
- The loader:
  1. Reads the OS kernel from disk
  2. Retrieves the UEFI memory map via `GetMemoryMap()`
  3. Allocates pages for kernel/setup data
  4. Sets up the kernel command line, initrd location
  5. Calls `ExitBootServices()` with the memory map key
  6. Transfers control to the OS kernel

### 7.2 ExitBootServices Transition
Calling `ExitBootServices()` is a one-way transition:
- Boot Services memory is reclaimed
- Only Runtime Services remain available
- The OS takes ownership of the memory map, PCI resources, and interrupt controllers

## 8. RT Phase -- Run Time (Phase 6)

After `ExitBootServices()`, the firmware enters the Runtime phase.

### 8.1 Available Services
- **EFI Runtime Services Table**: Contains function pointers for:
  - `GetTime()`, `SetTime()`, `GetWakeupTime()`, `SetWakeupTime()`
  - `GetVariable()`, `GetNextVariableName()`, `SetVariable()`
  - `GetNextHighMonotonicCount()`
  - `ResetSystem()`
  - `UpdateCapsule()`, `QueryCapsuleCapabilities()`
- All Boot Services functions become invalid

### 8.2 OS Use of UEFI Runtime
- Linux: `efivarfs` for NVRAM variables (`/sys/firmware/efi/efivars/`)
- Windows: NtSetSystemEnvironmentValueEx for UEFI variables
- SMM/ACPI interaction for power management

## 9. Boot Flow Summary Table

| Step | Phase | Key Action | Memory State |
|------|-------|-----------|-------------|
| 0 | Reset | CPU at 0xFFFFFFF0, real mode | No DRAM, no cache |
| 1 | SEC | CAR enabled, microcode loaded | Cache-as-RAM active |
| 2 | SEC→PEI | HOB published | CAR only |
| 3 | PEI | Memory init, CPU init, PCI enum | CAR + DRAM |
| 4 | PEI→DXE | CAR teardown, HOB list passed | DRAM only |
| 5 | DXE | Drivers loaded, protocols installed | DRAM |
| 6 | BDS | Boot option selected, loader started | DRAM |
| 7 | TSL | Boot services active, kernel loaded | DRAM |
| 8 | TSL→RT | ExitBootServices called | DRAM (runtime only) |
| 9 | RT | OS running | DRAM |

## 10. Hand-Off Block Data Flow

```
SEC HOB
  ├── FV list (2 entries)
  ├── Memory map (CAR descriptor)
  └── Boot mode
       │
       ▼
PEI HOB List
  ├── PHIT HOB (PEI memory usage)
  ├── Memory Allocation HOBs
  ├── Resource Descriptor HOBs
  ├── FV HOBs (3 entries)
  ├── CPU HOB (BSP/AP info)
  └── GUID Extension HOBs
       │
       ▼
DXE Services
  ├── EFI System Table
  ├── Boot Services Table
  ├── Runtime Services Table
  ├── DXE Services Table
  └── Configuration Tables (ACPI, SMBIOS, etc.)
       │
       ▼
BDS → Boot Loader
  ├── Loaded Image Protocol
  ├── Device Path Protocol
  └── Memory Map (from GetMemoryMap)
       │
       ▼
OS Kernel
  ├── Runtime Services Table
  └── Configuration Tables
```

## 11. References

- UEFI PI Specification v1.8: https://uefi.org/specifications
- Intel Firmware Support Package (FSP) Integration Guide
- AMD Platform Initialization (PI) Architecture Guide
- Tianocore EDK II Boot Flow: https://github.com/tianocore/edk2
- UEFI Driver Writer's Guide, Intel Press
- Beyond BIOS, 2nd Edition, Vincent Zimmer et al.
