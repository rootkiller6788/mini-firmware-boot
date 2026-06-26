# mini-firmware — 固件基础 (Firmware Fundamentals)

> Intel Firmware Architecture · ARM Trusted Firmware-A · UEFI PI · JEDEC SPI NOR · DMTF SMBIOS · ACPI · PSCI · TCG TPM

---

## Module Status: COMPLETE ✅

- **L1-L6**: Complete
- **L7**: Complete (3+ applications)
- **L8**: Complete (2 advanced topics)
- **L9**: Partial (documented, not implemented)

| Metric | Value |
|--------|-------|
| `include/` + `src/` total lines | **5,209** |
| Header files | 11 |
| Source files | 11 |
| Test cases | 41 (all passing) |
| Demo programs | 3 |
| Knowledge layers | L1-L9 covered |

---

## 九层知识覆盖 / Nine-Layer Knowledge Coverage

| Level | Name | Status | Key Artifacts |
|-------|------|--------|---------------|
| **L1** | Definitions | ✅ Complete | 80+ structs, 30+ enums, 100+ API declarations |
| **L2** | Core Concepts | ✅ Complete | Verified boot, CRC32, ACPI tables, memory training, PSCI power mgmt, A/B slot updates |
| **L3** | Engineering Structures | ✅ Complete | Flash descriptor, hash chain, ACPI table pipeline, DDR training FSM, capsule validation |
| **L4** | Standards/Theorems | ✅ Complete | CRC32 error detection theorem, SHA-256 collision resistance, RSA correctness, Amdahl's Law |
| **L5** | Algorithms/Methods | ✅ Complete | CRC32 (Sarwate), SHA-256 (FIPS 180-4), RSA sig verify, FDT walker, hash chain extension, SPD parsing |
| **L6** | Canonical Problems | ✅ Complete | Verified boot chain, firmware update with A/B slots, recovery mode, flash wear leveling |
| **L7** | Applications | ✅ Complete | TPM PCR measurement, ACPI for OS enumeration, PSCI CPU hotplug, FIT multi-platform boot |
| **L8** | Advanced Topics | ✅ Complete | Anti-rollback monotonic counters, ACPI vs FDT comparison |
| **L9** | Industry Frontiers | ⚠️ Partial | Confidential computing, post-quantum signatures (documented only) |

---

## 模块总览 / Module Overview

| # | Module | Header | Key Knowledge |
|---|--------|--------|---------------|
| 1 | Firmware Layout | `firmware_layout.h` | CRC32, flash descriptor, wear leveling, FW volume |
| 2 | Reset Vector | `reset_vector.h` | CPU reset, mode switching, register dump |
| 3 | MMIO Manager | `mmio.h` | Memory-mapped I/O, UART/Timer/GPIO |
| 4 | SMBIOS Tables | `smbios_fw.h` | BIOS/System/Baseboard/Processor tables |
| 5 | SPI NOR Flash | `spi_nor.h` | JEDEC commands, sector erase, page program |
| 6 | Bootblock | `bootblock.h` | SHA-256, RSA-2048, hash chain, PCR log |
| 7 | ACPI Tables | `acpi_fw.h` | RSDP, XSDT, FADT, MADT (x86+ARM), DSDT |
| 8 | FIT Image | `fit_image.h` | FDT header parser, image/config nodes |
| 9 | Memory Init | `meminit.h` | SPD parsing, DDR4 training FSM, Amdahl |
| 10 | PSCI Firmware | `psci_fw.h` | CPU_ON/OFF/SUSPEND, system power |
| 11 | Firmware Update | `firmware_update.h` | A/B slots, capsule, recovery |

---

## 核心定理列表 / Core Theorems (L4)

| Theorem | Statement | Verified In |
|---------|-----------|-------------|
| **CRC32 Error Detection** | Detects all single/double/odd-bit errors; all burst errors ≤ 32 bits; 99.99999995% of longer bursts (Koopman, 2002) | `crc32_compute()` |
| **SHA-256 Collision Resistance** | Finding m1 ≠ m2 with H(m1)=H(m2) requires ~2^128 operations (birthday bound) | `sha256_hash()` |
| **SHA-256 Preimage Resistance** | Given H(m), finding m requires ~2^256 operations | `sha256_hash()` |
| **RSA Correctness** | (m^e)^d ≡ m (mod n) where ed ≡ 1 (mod φ(n)) | `vb_verify_signature()` |
| **Hash Chain Security** | Modifying any stage breaks all subsequent composites (avalanche property) | `vb_extend_chain()` |
| **Anti-Rollback Safety** | Monotonic counter prevents downgrade attacks without physical tampering | `vb_check_anti_rollback()` |
| **Amdahl's Law** | Speedup = 1/((1-P) + P/S); memory training: P≈0.95, S=4 → 3.48x | `memctrl_train_all()` |

---

## 核心算法列表 / Core Algorithms (L5)

| Algorithm | Complexity | Implementation | Reference |
|-----------|------------|----------------|-----------|
| CRC32 (Sarwate) | O(n), 1KB table | `crc32_compute()` | IEEE 802.3 |
| SHA-256 (FIPS 180-4) | O(n), O(1) space | `sha256_hash()` | FIPS PUB 180-4 |
| RSA-2048 Sig Verify | O(log e · log² n) | `vb_verify_signature()` | PKCS#1 v2.2, RFC 8017 |
| Verified Boot Chain Walk | O(n·m) | `vb_verify_chain()` | Android AVB, Chrome OS VBoot |
| Hash Chain Extension | O(1) per stage | `vb_extend_chain()` | TCG PC Client Spec |
| FDT Structure Walker | O(n) | `fdt_walk_structure()` | Devicetree Spec v0.4 |
| ACPI Table Checksum | O(n) per table | `acpi_set_checksum()` | ACPI Spec §5.2 |
| SPD Parsing | O(1) | `memctrl_parse_spd()` | JESD400-5 |
| DDR Training FSM | O(states) per ch | `memctrl_train_channel()` | JESD79-4C |

---

## 经典问题列表 / Canonical Problems (L6)

| Problem | Solution | Module |
|---------|----------|--------|
| **Verified Boot** | Hash chain + RSA signature chain from immutable ROM | `bootblock` |
| **Firmware Update Safety** | A/B slots with boot attempt counters, auto-fallback | `firmware_update` |
| **Firmware Recovery** | Golden image in WP region, recovery mode with reflash | `firmware_update` |
| **Flash Wear Leveling** | Static wear leveling with relocation threshold | `firmware_layout` |
| **Multi-Platform Firmware** | FIT image with per-config kernel/dtbs | `fit_image` |
| **Hardware Enumeration** | ACPI table construction (FADT, MADT, DSDT) | `acpi_fw` |

---

## 九校课程映射 / Course Alignment

| School | Course | Mapped Modules |
|--------|--------|----------------|
| **MIT** | 6.004 Computation Structures | reset_vector, mmio |
| **MIT** | 6.858 Computer Security | bootblock (verified boot, secure chain) |
| **Stanford** | CS 144 Networking | SPI NOR (flash storage for PXE boot) |
| **Berkeley** | CS 162 OS | PSCI (power mgmt), ACPI (HW enumeration) |
| **CMU** | 15-410 OS | firmware_layout, memory init |
| **CMU** | 15-418 Parallel | meminit (Amdahl's Law, parallel training) |
| **UT Austin** | CS 380D Distributed | firmware_update (A/B slots, capsule) |
| **ETH** | 263-0006 Computer Architecture | reset_vector (CPU modes), meminit (DDR) |
| **Cambridge** | Part II: OS | All modules (full firmware boot flow) |
| **清华** | 操作系统 | ACPI tables, SMBIOS, PSCI |
| **Georgia Tech** | CS 6290 HPCA | meminit (DDR4 timing, SPD parsing) |

---

## 目录结构 / Directory Tree

```
mini-firmware/
├── Makefile                    # make test: 41/41 passed
├── README.md                   # Knowledge coverage report (this file)
├── include/ (11 headers, 1,705 lines)
│   ├── firmware_layout.h       # Flash descriptor, CRC32, wear leveling
│   ├── reset_vector.h          # CPU reset, mode switches
│   ├── mmio.h                  # Memory-mapped I/O abstraction
│   ├── smbios_fw.h             # SMBIOS table definitions
│   ├── spi_nor.h               # SPI NOR flash interface
│   ├── bootblock.h             # Verified boot, SHA-256, RSA, PCR
│   ├── acpi_fw.h               # ACPI tables (RSDP, FADT, MADT, DSDT)
│   ├── fit_image.h             # FIT image parser
│   ├── meminit.h               # DDR training, SPD parsing
│   ├── psci_fw.h               # ARM PSCI power management
│   └── firmware_update.h       # UEFI capsule, A/B slots, recovery
├── src/ (11 files, 3,504 lines)
│   ├── firmware_layout.c       # CRC32 Sarwate, flash desc, volume validation
│   ├── reset_vector.c          # CPU state machine
│   ├── mmio.c                  # MMIO dispatch with UART/TIMER/GPIO
│   ├── smbios_fw.c             # Table management
│   ├── spi_nor.c               # JEDEC command simulation
│   ├── bootblock.c             # FIPS 180-4 SHA-256, RSA verify, chain walk
│   ├── acpi_fw.c               # ACPI table construction with checksums
│   ├── fit_image.c             # FDT walker, image/config management
│   ├── meminit.c               # DDR4 training FSM, Amdahl's analysis
│   ├── psci_fw.c               # PSCI v1.2 functions, CPU hotplug
│   └── firmware_update.c       # A/B boot, capsule validation, recovery
├── tests/
│   └── test_firmware.c         # 41 assert-based test cases
├── examples/
│   ├── flash_boot_demo.c       # Full boot sequence: power-on->firmware entry
│   ├── mmio_demo.c             # UART/Timer/GPIO MMIO demo
│   └── smbios_demo.c           # SMBIOS table generation demo
├── demos/
│   ├── mini-firmware-layout/   # Flash layout documentation
│   └── mini-reset-vector/      # Reset vector documentation
└── docs/
    ├── course-alignment.md     # Intel/ARM/UEFI course mapping
    └── firmware-architecture.md # Complete firmware architecture doc
```

---

## 构建与运行 / Build & Run

```bash
# Build all targets (demos + tests)
make all

# Run test suite (41 tests + 3 demos)
make test

# Run individual demos
make run-flash-boot
make run-mmio
make run-smbios

# Clean build artifacts
make clean
```

---

## 规范与设计 / Conventions

- **C99**: ISO C99 standard, `-Wall -Wextra` clean
- **Dependencies**: libc only (`stdio.h, stdlib.h, string.h`)
- **Naming**: `snake_case` functions, `PascalCase` types, `UPPER_SNAKE_CASE` constants
- **Safety**: All APIs validate null pointers, boundary conditions, OOM
- **Documentation**: Every function has theorem source + complexity annotation

---

## 参考资源 / References

| Document | Version | Topic |
|----------|---------|-------|
| Intel 64 and IA-32 SDM | Dec 2023 | CPU architecture, reset vector |
| ARM TF-A Documentation | v2.10 | Trusted boot, BL1-BL33 stages |
| UEFI PI Specification | v1.8 | Firmware volumes, SEC/PEI/DXE phases |
| DMTF SMBIOS Specification | v3.7.0 | System management tables |
| JEDEC JESD216F (SFDP) | Rev. F | Serial flash parameters |
| JEDEC JESD79-4C (DDR4) | 2020 | DDR4 SDRAM specification |
| JEDEC JESD400-5 (SPD) | 2020 | DDR4 SPD contents |
| ACPI Specification | v6.5 | ACPI table structures |
| ARM PSCI Specification | v1.2 (DEN 0022E) | Power state coordination |
| TCG PC Client Firmware Profile | v1.05 | Measured boot, PCR operations |
| FIPS PUB 180-4 | 2015 | SHA-256 specification |
| PKCS#1 v2.2 / RFC 8017 | 2016 | RSA cryptography |
| Android Verified Boot 2.0 | — | A/B updates, hash tree |
| Chrome OS Firmware Design | — | Recovery, slot selection |
