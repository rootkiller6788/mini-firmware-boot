# mini-boot-process -- Firmware Boot Process (C Implementation)

> Reference: UEFI PI Spec, AMD AGESA, Intel Boot Flow, ACPI 6.5, SMBIOS 3.4.0

A comprehensive C-language teaching implementation of the complete UEFI PI
(Platform Initialization) boot flow. Covers all six PI phases from cold reset
to OS runtime, plus ACPI table construction, SMBIOS platform inventory,
Firmware Volume parsing, Boot Policy management, and SHA-256 firmware integrity.

**include/ + src/ = 4,616 lines** | **47 tests, 0 failures**

## Module Status: COMPLETE

- **L1-L6**: Complete — all core definitions, concepts, structures, theorems, algorithms, and canonical problems implemented
- **L7**: Complete — BGRT boot logo, NVRAM variable store, SMBIOS memory inventory, Boot Manager hotkey interface
- **L8**: Partial — SHA-256 firmware integrity (Measured Boot in TPM PCRs), FV measurements
- **L9**: Partial — TPM attestation, Secure Boot key infrastructure (documented)

---

## Module Table

| # | Module | Header | Source | Description |
|---|--------|--------|--------|-------------|
| 1 | Boot Phases | `include/boot_phases.h` | `src/boot_phases.c` | UEFI PI 6-phase state machine (SEC→PEI→DXE→BDS→TSL→RT) |
| 2 | CPU Init | `include/cpu_init.h` | `src/cpu_init.c` | BSP/AP init, MSRs (EFER, APIC_BASE, MTRR, STAR, LSTAR), microcode update, paging |
| 3 | Memory Init | `include/memory_init.h` | `src/memory_init.c` | SPD parsing (DDR4/DDR5), dual-channel controller, DDR training, UEFI memory map |
| 4 | Device Enum | `include/device_enum.h` | `src/device_enum.c` | PCI bus enumeration, device discovery, BAR resource assignment, class lookup |
| 5 | Cache-as-RAM | `include/cache_as_ram.h` | `src/cache_as_ram.c` | Pre-DRAM cache stack, no-eviction mode, CAR teardown to DRAM |
| 6 | ACPI Tables | `include/acpi_tables.h` | `src/acpi_tables.c` | RSDP, RSDT/XSDT, FADT, MADT, MCFG, HPET, BGRT — table construction + checksum validation |
| 7 | SMBIOS | `include/smbios.h` | `src/smbios.c` | SMBIOS 3.0 EPS, Type 0/1/2/4/7/17 structures, string table management |
| 8 | Firmware Volume | `include/firmware_volume.h` | `src/firmware_volume.c` | FV header parsing, FFS file scanning, GUID operations, SHA-256 integrity verification |
| 9 | Boot Policy | `include/boot_policy.h` | `src/boot_policy.c` | BootOption management, BootOrder priority queue, NVRAM variable store, Boot Manager |

---

## Knowledge Coverage (Nine-Level System)

### L1 — Core Definitions (Complete)
- `BootPhase` enum, `HandOffBlock`, `BootState` — UEFI PI phase definitions
- `CPUInitState`, `MSR` — Intel x86 model-specific register definitions
- `SPDData`, `MemoryController`, `MemoryMap` — JEDEC SPD and UEFI memory map
- `PCIDevice`, `PCIBus`, `PCIDeviceInfo` — PCI configuration space definitions
- `CARState` — Cache-as-RAM state machine
- `ACPIRSDP`, `ACPISDTHeader`, `ACPIFADT`, `ACPIMADT`, `ACPIMCFG`, `ACPIHPET`, `ACPIBGRT` — ACPI table structures
- `SMBIOSEPS`, `SMBIOS3EPS`, `SMBIOSType0/1/2/4/7/17` — SMBIOS record types
- `FVHeader`, `FFSFileHeader`, `FFSSectionHeader`, `EFI_GUID` — Firmware Volume definitions
- `BootOption`, `BootOrder`, `BootManager`, `NVRAMVariable` — Boot policy structures
- `SHA256Context` — SHA-256 hash context

### L2 — Core Concepts (Complete)
- UEFI PI boot flow: SEC→PEI→DXE→BDS→TSL→RT phase transitions
- Cache-as-RAM: using CPU cache as temporary memory before DRAM training
- DDR training: MRS programming, write leveling, read DQS gate training
- PCI resource management: BAR allocation, class code classification
- ACPI table namespace: RSDP→RSDT/XSDT→child tables discovery chain
- APIC interrupt routing: LAPIC, IOAPIC, GSI, interrupt source override
- SMBIOS platform inventory: BIOS, system, baseboard, processor, cache, memory devices
- Firmware Volume abstraction: FFS file system, section types, GUID-based lookup
- Boot Manager: BootOrder priority, BootNext override, hotkey detection
- Measured Boot: SHA-256 firmware integrity, TPM PCR extension

### L3 — Engineering Structures (Complete)
- HandOff Block (HOB) list: phase-to-phase data passing
- MTRR memory type range: WB/UC programming via MSRs
- Dual-channel memory controller: DIMM population tracking
- PCI BAR address space layout: MMIO vs IO space allocation
- RSDT/XSDT directory structure: flat array of physical addresses
- MADT variable-length entry serialization: type-length-value encoding
- SMBIOS string table: dual-section layout with 1-based index references
- FV file catalog: in-memory index for O(1) GUID lookup by DXE dispatcher
- NVRAM variable store: (name, vendor_guid) → data mapping
- BootOrder priority queue: ordered array traversal

### L4 — Standards/Theorems (Complete)
- **ACPI checksum theorem**: All table bytes must sum to 0 mod 256 (ACPI §5.2)
- **SMBIOS checksum**: Entry Point Structure 8-bit sum-to-zero (SMBIOS §5.2.1)
- **FV header checksum**: 16-bit word sum of header must be 0 (UEFI PI Vol 3 §2.3)
- **FFS 24-bit size encoding**: Compact little-endian storage (UEFI PI Vol 3 §3.2)
- **MADT entry format**: Type-length-value per ACPI §5.2.12
- **SMBIOS 3.0 anchor**: "_SM3_" 5-byte signature at 16-byte boundaries in F-segment
- **FADT PM register blocks**: PM1a_EVT, PM1a_CNT, PM_TMR (ACPI §5.2.9)
- **MCFG ECAM format**: Per-segment base address for PCIe config space (PCI FW §4.1)
- **SHA-256 padding**: Append '1' bit, pad to 448 mod 512, append 64-bit length (FIPS 180-4 §5)

### L5 — Algorithms/Methods (Complete)
- **ACPI table checksum**: O(n) single-pass 8-bit sum, complexity O(n)
- **MADT variable-length serialization**: Type-length-value linear encoding
- **RSDT/XSDT linear table lookup**: O(n) scan by 4-char signature
- **SMBIOS string table append**: O(k) per string, 1-based index return
- **Firmware Volume linear scan**: O(n) file discovery, building GUID catalog
- **SHA-256 hash**: 64-round Merkle-Damgard construction per FIPS 180-4, O(n) in message bits
- **BootOrder priority traversal**: O(n) sequential option enumeration
- **NVRAM variable linear search**: O(n) scan by (name, guid) pair
- **PCI class-code linear filter**: O(n) classification scan

### L6 — Canonical Problems (Complete)
- **Boot Device Selection** (UEFI BDS): BootOrder priority enumeration with BootNext override and hotkey detection
- **Platform Identification** (SMBIOS): Type 0 BIOS info, Type 1 system info, Type 4 processor info
- **Interrupt Topology Discovery** (ACPI MADT): LAPIC/IOAPIC enumeration for SMP OS initialization
- **Firmware Integrity Measurement** (Measured Boot): SHA-256 hash of FV content for TPM PCR extension
- **PCI Device Enumeration**: Bus scanning, device discovery, BAR resource assignment
- **Memory Discovery & Training**: SPD parsing, DDR MRS programming, UEFI memory map construction

### L7 — Applications (Complete: 7 applications)
1. **BGRT Boot Logo**: Windows 8+/systemd-boot use BGRT for flicker-free OEM logo transition
2. **SMBIOS Data Center Inventory**: `dmidecode` uses SMBIOS Types 0/1/4/17 for asset management
3. **NVRAM Persistent Boot Config**: `efibootmgr` reads/writes Boot#### and BootOrder UEFI variables
4. **HPET Multimedia Timer**: Linux/Windows use HPET as reliable clock when TSC is unstable
5. **PCIe ECAM via MCFG**: OS uses MCFG to locate PCIe configuration space for device drivers
6. **EFI Variable Runtime Access**: Linux `efivarfs` exposes NVRAM variables as filesystem
7. **BootNext One-Time Override**: Windows "Advanced Startup" uses BootNext for reboot-to-firmware

### L8 — Advanced Topics (Partial: 3/5 implemented)
- **SHA-256 Measured Boot** ✓ — FV content hashing for TPM PCR extension
- **Firmware Volume GUID-based lookup** ✓ — O(1) catalog for DXE DEPEX resolution
- **Multi-segment PCIe via MCFG** ✓ — Per-segment ECAM base address configuration
- **TPM 2.0 PCR Extension** (documented, not implemented) — `PCR_new = SHA-256(PCR_old || measurement)`
- **Secure Boot Key Management** (documented) — PK/KEK/db/dbx variable hierarchy

### L9 — Industry Frontiers (Partial: documented)
- **TPM Remote Attestation** — PCR quotes signed by TPM AIK for cloud confidential computing
- **Intel TXT / AMD SEV** — DRTM measured launch for trusted execution environments
- **UEFI Secure Boot Advanced** — Certificate chains, timestamp counters, revocation databases
- **Firmware Update Capsules** — UEFI UpdateCapsule() runtime service for field-upgradable firmware

---

## Nine-School Curriculum Mapping

| School | Key Course | mini-boot-process Mapping |
|--------|-----------|--------------------------|
| **MIT** | 6.004 Computation Structures | Boot phases state machine (FSM), CAR (cache hierarchy), PCI enumeration (bus topology) |
| **MIT** | 6.858 Computer Security | SHA-256 measured boot, FV integrity verification, secure key hierarchy |
| **Stanford** | CS 144 Networking | PCIe topology enumeration, MCFG ECAM address assignment |
| **Berkeley** | CS 162 Operating Systems | UEFI memory map (loader/boot/runtime/ACPI types), paging setup, long mode transition |
| **CMU** | 15-410 Operating Systems | Firmware→OS handoff (HOB list → EFI System Table → ExitBootServices) |
| **CMU** | 15-418 Parallel Systems | AP startup via INIT-SIPI-SIPI, MP initialization, APIC/x2APIC |
| **UT Austin** | CS 380D Distributed Systems | SMBIOS platform inventory for data center asset management |
| **ETH** | 263-0006 Computer Architecture | MTRR programming, cache control (CR0.CD/NW), CAR no-eviction mode |
| **Cambridge** | Part II Concurrent Systems | SMP boot: BSP/AP synchronization, MWAIT state, local APIC timer |
| **Tsinghua** | Operating Systems | Complete UEFI PI boot flow: SEC→PEI→DXE→BDS→TSL→RT, ACPI table construction |
| **Georgia Tech** | CS 6210 Advanced OS | Hardware-reduced ACPI (HW_REDUCED flag), UEFI Runtime Services, NVRAM variable architecture |

---

## Core Definitions

| Structure | Fields | Spec Reference |
|-----------|--------|---------------|
| `BootPhase` | SEC(0), PEI(1), DXE(2), BDS(3), TSL(4), RT(5) | UEFI PI Vol 1 §5-10 |
| `HandOffBlock` | FV bases/sizes, memory map entries | UEFI PI Vol 1 §6.3 (HOB) |
| `MADTLAPIC` | ACPI processor ID, APIC ID, flags | ACPI 6.5 §5.2.12.1 |
| `MADTIOAPIC` | IOAPIC ID, address, GSI base | ACPI 6.5 §5.2.12.2 |
| `MADTIntSrcOverride` | Bus, IRQ source, GSI mapping, polarity/trigger flags | ACPI 6.5 §5.2.12.5 |
| `ACPIFADT` | PM1a_EVT/CNT, PM_TMR, SCI, boot_arch, feature flags | ACPI 6.5 §5.2.9 |
| `SMBIOS3EPS` | "_SM3_" anchor, 64-bit table address, checksum | SMBIOS 3.4.0 §5.2.2 |
| `FFSFileHeader` | GUID name, type, attributes, 24-bit size, state | UEFI PI Vol 3 §3.2 |
| `BootOption` | Attributes, description, device path, option number | UEFI 2.10 §3.1.3 |
| `BootOrder` | uint16_t[] priority-ordered Boot#### list | UEFI 2.10 §3.3 |

## Core Theorems

| Theorem | Formula/Statement | Module |
|---------|------------------|--------|
| **ACPI Checksum** | `Σ_{i=0}^{n-1} byte[i] mod 256 = 0` | ACPI Tables §5.2 |
| **SHA-256 Compression** | `H_i = H_{i-1} + Compress(M_i, H_{i-1})` | Firmware Volume (FIPS 180-4) |
| **SMBIOS String Encoding** | `string_ref = 1-based index into null-terminated string table` | SMBIOS §6.1.3 |
| **MTRR Memory Type** | `PhysicalAddr[51:12] & MTRR_MASK = MTRR_BASE[51:12]` | Intel SDM Vol 3A §11.11 |
| **PCI BAR Sizing** | `BAR_size = ~(BAR & ~0xF) + 1` (write all-1s, read back) | PCI 3.0 §6.2.5.1 |
| **FFS Alignment** | `file_start_addr mod 8 = 0` | UEFI PI Vol 3 §3.2 |

## Core Algorithms

| Algorithm | Complexity | Module |
|-----------|-----------|--------|
| ACPI table checksum computation | O(n) | `acpi_checksum()` |
| RSDT/XSDT linear table lookup | O(n) | `acpi_find_table_*()` |
| MADT variable-length serialization | O(n) | `acpi_madt_build()` |
| SMBIOS string table construction | O(k·m) | `smbios_add_string()` |
| Firmware Volume linear file scan | O(n) | `fv_scan_files()` |
| SHA-256 Merkle-Damgard hash | O(n) | `sha256_hash()` / `sha256_update()` |
| BootOrder priority traversal | O(n) | `boot_manager_select()` |
| NVRAM variable linear search | O(n) | `nvram_get_variable()` |

## Directory Tree

```
mini-boot-process/
├── Makefile                    # make all / make test / make clean
├── README.md                   # Knowledge coverage report (this file)
├── include/                    # Headers (9 files, 1,401 lines)
│   ├── boot_phases.h           # BootPhase, HandOffBlock, BootState
│   ├── cpu_init.h              # CPUInitState, MSR, feature flags
│   ├── memory_init.h           # SPDData, MemoryController, MemoryMap
│   ├── device_enum.h           # PCIDevice, PCIBus, PCI registers
│   ├── cache_as_ram.h          # CARState, CAR modes
│   ├── acpi_tables.h           # RSDP, RSDT/XSDT, FADT, MADT, MCFG, HPET, BGRT
│   ├── smbios.h                # SMBIOS 2.1/3.0 EPS, Types 0/1/2/4/7/17
│   ├── firmware_volume.h       # FVHeader, FFSFileHeader, EFI_GUID, SHA256Context
│   └── boot_policy.h           # BootOption, BootOrder, BootManager, NVRAMVariable
├── src/                        # Sources (9 files, 3,215 lines)
│   ├── boot_phases.c           # PI phase implementation with HOB propagation
│   ├── cpu_init.c              # BSP/AP init, MSR programming, MTRR, paging
│   ├── memory_init.c           # SPD parse, controller config, DDR training, memory map
│   ├── device_enum.c           # PCI bus enumeration, BAR assignment, device lookup
│   ├── cache_as_ram.c          # CAR init, read/write, block ops, teardown
│   ├── acpi_tables.c           # All ACPI table builders + checksum validation
│   ├── smbios.c                # SMBIOS structure builders + string table management
│   ├── firmware_volume.c       # FV parsing, FFS scanning, SHA-256 + integrity verification
│   └── boot_policy.c           # Boot option management, NVRAM variables, Boot Manager
├── tests/                      # Test suite (47 tests, all pass)
│   └── test_all.c              # Comprehensive assert-based tests for all 9 modules
├── examples/                   # 3 end-to-end demos
│   ├── boot_sim_demo.c         # Full SEC→PEI→DXE→BDS→TSL→RT simulation
│   ├── pci_enum_demo.c         # PCI enumeration + BAR allocation demo
│   └── car_demo.c              # Cache-as-RAM read/write/teardown demo
├── benches/                    # Performance benchmarks (directory prepared)
├── demos/                      # Detailed documentation
│   ├── mini-boot-flow/README.md
│   └── mini-memory-init/README.md
└── docs/                       # In-depth documentation
    ├── course-alignment.md     # Module ↔ UEFI PI / Intel FSP / AMD AGESA mapping
    └── boot-phases-detail.md   # Deep dive into all 6 PI phases
```

## Build & Run

```bash
make all          # Build all 3 demo executables
make test         # Build and run comprehensive test suite (47 tests)
make clean        # Clean all build artifacts
```

Demo executables:
```bash
bin/boot_sim_demo.exe    # Full boot flow simulation
bin/pci_enum_demo.exe    # PCI device enumeration
bin/car_demo.exe         # Cache-as-RAM operation
```

## License

MIT

