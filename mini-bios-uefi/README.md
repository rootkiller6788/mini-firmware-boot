# mini-bios-uefi — BIOS 与 UEFI (C 语言实现)

> 参考 TianoCore EDK II, Phoenix BIOS, UEFI Specification 2.10, FIPS 180-4, ACPI 6.5, IEEE 802.3

## Module Status: COMPLETE ✅

| Level | Status | Coverage |
|-------|--------|----------|
| **L1** Definitions | Complete | struct/typedef/API for IVT, BDA, GPT, PE32+, UEFI ST/BS/RT, ACPI SDT, Secure Boot, Capsule |
| **L2** Core Concepts | Complete | POST flow, interrupt chaining, PE/COFF loading, UEFI DXE phase, ACPI enumeration |
| **L3** Engineering Structures | Complete | PCI config space, PIC 8259A cascade, PIT 8254 timer chain, RSDP→XSDT→MADT table walk |
| **L4** Standards/Theorems | Complete | CRC32 error detection (Peterson & Brown), SHA-256 (FIPS 180-4), ACPI checksum (mod 256), PCI enumeration (DFS) |
| **L5** Algorithms/Methods | Complete | CRC32 table-driven, SHA-256 Merkle-Damgard, BCD↔binary, PCI DFS enumeration, RSDP signature scan |
| **L6** Canonical Problems | Complete | BIOS POST (examples/bios_post_demo.c), UEFI Boot (uefi_boot_demo.c), GPT partitioning (gpt_demo.c) |
| **L7** Applications | Complete | Disk boot (INT 0x19), UEFI capsule update, Secure Boot image verification, PCI device discovery |
| **L8** Advanced Topics | Complete | Secure Boot PK/KEK/db/dbx chain, Capsule Update (persist-across-reset), ACPI MADT/IOAPIC parsing |
| **L9** Industry Frontiers | Partial | UEFI firmware attack surface documented in docs/; TPM measured boot referenced |

### Code Metrics
- `include/` + `src/` total: **4,026 lines** (target ≥ 3,000)
- 8 header files, 8 source files
- 56 test assertions, all passing
- **`make test` — PASS (56/0)**

## Modules

| 模块 | 头文件 | 源文件 | 行数 | 说明 |
|------|--------|--------|------|------|
| **Legacy BIOS** | `include/legacy_bios.h` | `src/legacy_bios.c` | 338 | IVT, BDA, POST, INT 0x10/0x13/0x19 |
| **UEFI Boot** | `include/uefi_boot.h` | `src/uefi_boot.c` | 646 | System Table, Boot/Runtime Services, Protocol DB, ExitBootServices |
| **UEFI Protocols** | `include/uefi_protocols.h` | `src/uefi_protocols.c` | 441 | LoadedImage, DevicePath, BlockIo, SimpleFileSystem, GOP |
| **PE/COFF Loader** | `include/pe_coff.h` | `src/pe_coff.c` | 449 | PE32+ header parsing, section loading, base relocation |
| **GPT** | `include/gpt.h` | `src/gpt.c` | 369 | GPT header/partition I/O, Protective MBR, ESP discovery |
| **BIOS Hardware** | `include/bios_hardware.h` | `src/bios_hardware.c` | 675 | CMOS RTC, 8042 KBC, A20 gate, PCI config, PIC 8259A, PIT 8254 |
| **UEFI Image Auth** | `include/uefi_image_auth.h` | `src/uefi_image_auth.c` | 560 | CRC32 (IEEE 802.3), SHA-256 (FIPS 180-4), Secure Boot, Capsule |
| **UEFI ACPI** | `include/uefi_acpi.h` | `src/uefi_acpi.c` | 548 | RSDP search, XSDT/RSDT walk, MADT/IOAPIC, MCFG/ECAM |

## 九层知识覆盖

### L1: 核心定义
- IVT (Interrupt Vector Table) — 256 x 4 byte entries
- BIOS Data Area (BDA) — real-mode 0x400 segment layout
- GPT Header / Partition Entry — UEFI spec 2.10 §5.3
- PE/COFF optional header PE32+ — Microsoft PE specification
- EFI System Table / Boot Services / Runtime Services
- EFI protocols: LoadedImage, BlockIo, GOP, SimpleFileSystem, DevicePath
- ACPI SDT header, RSDP, XSDT, MADT, FADT, MCFG structures
- SHA-256 context, CRC32 state, Secure Boot variable descriptors
- EFI Capsule Header, EFI Time, Variable Authentication descriptor

### L2: 核心概念
- BIOS POST sequence: CPU→CMOS→DMA→RAM→KBD→Video→IVT→Boot
- Interrupt vector chaining (IVT → handler → BIOS service)
- PE/COFF image loading: DOS header → PE signature → optional header → sections → relocations
- UEFI DXE phase protocol installation and discovery
- ACPI table hierarchy: RSDP → XSDT → [MADT, FADT, MCFG, ...]
- Secure Boot signing chain: PK → KEK → db/dbx
- PCI bus enumeration (bus/device/function scan)
- BCD ↔ binary number format conversion
- Memory-mapped vs. port-mapped I/O

### L3: 工程结构
- IBM PC/AT chipset: 8259A PIC cascade (master IRQ2→slave), 8254 PIT channel assignment
- PCI Type 1 configuration mechanism (0xCF8/0xCFC)
- UEFI protocol database (linear array with GUID-based lookup)
- PE base relocation block parsing (.reloc section)
- ACPI MADT entry parser (type/length dispatch loop)
- CMOS RTC register bank (status A/B/C/D, century register)
- GPT primary/alternate header mirroring
- Keyboard controller 8042 command/data port protocol

### L4: 标准/定理 (含代码验证)
| 定理 | 来源 | 实现 |
|------|------|------|
| **CRC error detection properties** | Peterson & Brown (1961), IEEE 802.3-2018 §3.2.8 | `crc32_compute()` — table-driven, polynomial 0xEDB88320 |
| **SHA-256 collision resistance** | FIPS PUB 180-4 §4.1.2 | `sha256_hash()` — 64-round Merkle-Damgard, test vector validated |
| **ACPI table integrity** | ACPI Spec 6.5 §5.2.6 | `acpi_sdt_checksum()` — 8-bit sum mod 256 = 0 |
| **GPT protective MBR** | UEFI Spec 2.10 §5.2 | `gpt_build_protective_mbr()` — type 0xEE spanning disk |
| **PCI device discovery** | PCI Local Bus 3.0 §3.2.2.3.2 | `pci_enumerate_bus()` — DFS with multi-function detection |
| **RTC time encoding** | MC146818A datasheet | `bcd_to_bin()` — BCD→binary decoder, leap year validation |

### L5: 算法/方法
| 算法 | 复杂度 | 位置 |
|------|--------|------|
| **CRC32 table-driven** | O(n), table: O(256 words) | `uefi_image_auth.c:crc32_update()` |
| **SHA-256 Merkle-Damgard** | O(n) per block | `uefi_image_auth.c:sha256_compress_block()` |
| **RSDP signature scan** | O(search_range/16) | `uefi_acpi.c:acpi_find_rsdp()` |
| **PCI DFS enumeration** | O(buses × devices × functions) | `bios_hardware.c:pci_enumerate_bus()` |
| **GPT partition type lookup** | O(n) linear GUID comparison | `gpt.c:gpt_find_partition_by_type()` |
| **PE base relocation application** | O(num_reloc_entries) | `pe_coff.c:pecoff_relocate()` |
| **CMOS checksum (16-bit sum)** | O(CMOS range = 30 bytes) | `bios_hardware.c:cmos_verify_checksum()` |
| **Unix timestamp conversion** | O(1) day counting | `bios_hardware.c:cmos_rtc_to_timestamp()` |

### L6: 经典工程问题
- `examples/bios_post_demo.c` — 完整的 BIOS POST 流程 (6 阶段)
- `examples/uefi_boot_demo.c` — UEFI DXE 加载 → ExitBootServices (9 步骤)
- `examples/gpt_demo.c` — GPT 分区表创建/解析/ESP 查找 (7 步骤)

### L7: 应用
1. **Disk Boot**: INT 0x19 引导流程 (MBR 加载 → 签名验证 → 跳转)
2. **UEFI Capsule Update**: 固件在线更新 (EFI_CAPSULE_HEADER 解析验证)
3. **Secure Boot 镜像验证**: PE hash + db/dbx 查找
4. **PCI 设备发现**: 按 class code 搜索 VGA/网卡/存储控制器
5. **ACPI 中断路由**: MADT IOAPIC/ISO 解析 + LAPIC 枚举

### L8: 进阶主题
1. **Secure Boot PK/KEK 证书链** (`uefi_image_auth.c`) — 3 层签名验证链
2. **Capsule Update Persist-Across-Reset** (`uefi_image_auth.c`) — 固件更新事务
3. **ACPI 中断源覆盖 (ISO)** (`uefi_acpi.c`) — ISA IRQ→GSI 重映射
4. **Fast A20 Gate** (`bios_hardware.c`) — System Control Port A (0x92)
5. **PCI 多函数设备扫描** (`bios_hardware.c`) — Header Type bit 7 递归

### L9: 工业前沿 (文档)
- UEFI 固件攻击面 (SMM, DXE, PEI 各阶段)
- TPM 2.0 measured boot (PCR extends)
- Confidential Computing (AMD SEV / Intel TDX ACPI tables)
- 参考: `docs/uefi-internals.md`

## 九校课程映射

| 学校 | 课程 | 对应模块 |
|------|------|----------|
| **MIT** | 6.004 Computation Structures | BIOS interrupt architecture, PIT timing |
| **MIT** | 6.858 Computer Security | Secure Boot, capsule authentication |
| **Stanford** | CS 144 Networking (CRC) | CRC32 implementation |
| **Berkeley** | CS 162 OS (booting) | BIOS/UEFI boot flow |
| **CMU** | 15-410 OS (firmware) | PE/COFF loader, ACPI hardware discovery |
| **CMU** | 15-418 Parallel (hashing) | SHA-256 Merkle-Damgard construction |
| **ETH** | 263-0006 Computer Architecture | PCI enumeration, PIC/PIT chipset |
| **清华** | 操作系统 | BIOS POST, UEFI runtime/boot services |
| **Georgia Tech** | CS 6210 Advanced OS | Secure Boot chain, capsule update |

## 构建与测试

```bash
make all        # 构建全部 3 个演示程序
make test       # 编译 + 运行全量测试 (56 assertions)
make bios_demo  # 仅构建 BIOS POST 演示
make uefi_demo  # 仅构建 UEFI Boot 演示
make gpt_demo   # 仅构建 GPT 演示
make clean      # 清理产物
```

## 运行演示

```bash
bin/bios_post_demo     # BIOS POST 加电自检模拟
bin/uefi_boot_demo     # UEFI 引导流程模拟 (DXE → ExitBootServices)
bin/gpt_demo           # GPT 分区表创建与解析
bin/test_all.exe       # 全量测试套件
```

## 架构总览

```
┌──────────────────────────────────────────┐
│              Legacy BIOS                  │
│  IVT → INT 0x19 → MBR → Bootloader       │
│  POST: CPU → CMOS → DMA → RAM → KBD →    │
│         Video → IVT → Bootstrap          │
│  BDA @ 0x400, EBDA @ 0x9FC0             │
│  CMOS RTC (MC146818A), 8042 KBC, 8259A  │
│  PIC, 8254 PIT, PCI config space         │
└──────────────────────────────────────────┘

┌──────────────────────────────────────────┐
│                UEFI (PI Firmware Phases)  │
│  SEC → PEI → DXE → BDS → RT              │
│  SystemTable → BootServices → Protocols   │
│  PE/COFF .efi → StartImage() → Kernel    │
│  Secure Boot: PK → KEK → db/dbx          │
│  Capsule Update                          │
│  ACPI: RSDP → XSDT → MADT/FADT/MCFG      │
└──────────────────────────────────────────┘
```

## 文件列表

```
mini-bios-uefi/
├── include/
│   ├── bios_hardware.h       # PC hardware: CMOS, KBC, A20, PCI, PIC, PIT
│   ├── legacy_bios.h         # Legacy BIOS declarations
│   ├── uefi_boot.h           # UEFI boot services declarations
│   ├── uefi_protocols.h      # UEFI protocol declarations
│   ├── uefi_image_auth.h     # SHA-256, CRC32, Secure Boot, Capsule
│   ├── uefi_acpi.h           # ACPI table definitions (RSDP, XSDT, MADT...)
│   ├── pe_coff.h             # PE/COFF loader declarations
│   └── gpt.h                 # GPT partition declarations
├── src/
│   ├── bios_hardware.c       # PC hardware implementation (442 lines)
│   ├── legacy_bios.c         # Legacy BIOS implementation (204 lines)
│   ├── uefi_boot.c           # UEFI Boot implementation (397 lines)
│   ├── uefi_protocols.c      # UEFI protocol implementation (214 lines)
│   ├── uefi_image_auth.c     # CRC32 + SHA-256 + Secure Boot (414 lines)
│   ├── uefi_acpi.c           # ACPI table parsing (323 lines)
│   ├── pe_coff.c             # PE/COFF loader implementation (280 lines)
│   └── gpt.c                 # GPT implementation (254 lines)
├── examples/
│   ├── bios_post_demo.c      # BIOS POST demonstration (153 lines)
│   ├── uefi_boot_demo.c      # UEFI boot demonstration (201 lines)
│   └── gpt_demo.c            # GPT demonstration (184 lines)
├── tests/
│   └── test_all.c            # Full test suite — 56 assertions (397 lines)
├── demos/
├── docs/
│   ├── course-alignment.md   # Course alignment reference
│   └── uefi-internals.md     # UEFI internals documentation
├── Makefile                  # Build + test (make test)
└── README.md                 # This file
```
