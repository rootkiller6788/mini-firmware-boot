# Course Alignment — mini-boot-process 与 UEFI/固件规范对照

## 1. 概述

本文档将 `mini-boot-process` 的各模块、函数和数据结构映射到对应的 UEFI Platform
Initialization (PI) 规范章节、Intel 固件支持包 (FSP)、AMD AGESA 启动流程以及
Tianocore EDK II 的源代码文件，作为学习参考对照表。

## 2. 模块与 UEFI PI 规范对照

| mini-boot-process 模块 | UEFI PI 规范章节 | 对应内容 |
|-------------------------|------------------|----------|
| `boot_phases.h/.c` | Vol 1, §5-10 — PI Phase Descriptions | SEC, PEI, DXE, BDS, TSL, RT 各阶段定义与状态转移 |
| `boot_phases.h/.c` | Vol 2, §2-3 — DXE Foundation | DXE Core, Boot Services, Runtime Services 的建立 |
| `cpu_init.h/.c` | Vol 1, §6 — PEI CPU Initialization | BSP/AP 初始化、MSR 编程、MTRR 设置、分页启用 |
| `cpu_init.h/.c` | Intel SDM Vol 3A, Ch 9 — Processor Management | xAPIC/x2APIC, SMM, microcode update |
| `memory_init.h/.c` | Vol 1, §7 — PEI Memory Discovery | SPD 读取、内存控制器配置、DDR 训练 |
| `memory_init.h/.c` | JEDEC JESD79-4C — DDR4 | MRS 寄存器、tCL/tRCD/tRP/tRAS 时序 |
| `device_enum.h/.c` | Vol 1, §8 — PCI Bus Enumeration | PCI 总线枚举、设备发现、BAR 资源分配 |
| `device_enum.h/.c` | PCI Local Bus Spec 3.0 — PCI Configuration | 配置空间读写、class code 分类 |
| `cache_as_ram.h/.c` | Vol 1, §5.3 — SEC CAR Setup | Cache-as-RAM 使用模式、CAR 拆卸流程 |

## 3. BootPhase 枚举与 PI 阶段对应

| mini-boot-process | PI Spec Phase | PI Vol 1 章节 |
|-------------------|---------------|---------------|
| `BOOT_PHASE_SEC (0)` | SEC Phase | §5 — Security Phase |
| `BOOT_PHASE_PEI (1)` | PEI Phase | §6 — Pre-EFI Initialization |
| `BOOT_PHASE_DXE (2)` | DXE Phase | §7 — Driver Execution Environment |
| `BOOT_PHASE_BDS (3)` | BDS Phase | §8 — Boot Device Selection |
| `BOOT_PHASE_TSL (4)` | TSL Phase | §9 — Transient System Load |
| `BOOT_PHASE_RT (5)` | RT Phase | §10 — Runtime Phase |

## 4. 数据结构对照

### 4.1 HandOffBlock → UEFI PI HOB

| mini-boot-process | UEFI PI 对应 |
|-------------------|--------------|
| `HandOffBlock.fv_count` | HOB 类型 `EFI_HOB_TYPE_FV` / `EFI_HOB_TYPE_FV2` 的数量 |
| `HandOffBlock.fv_bases[]` | `EFI_HOB_FIRMWARE_VOLUME.BaFseAddress` → `EFI_HOB_FIRMWARE_VOLUME2.BaFseAddress` |
| `HandOffBlock.fv_sizes[]` | `EFI_HOB_FIRMWARE_VOLUME.Length` |
| `HandOffBlock.memory_map[]` | `EFI_HOB_TYPE_RESOURCE_DESCRIPTOR` HOBs |

### 4.2 MemoryMap → UEFI Memory Map

| mini-boot-process | UEFI EFI_MEMORY_DESCRIPTOR |
|-------------------|---------------------------|
| `MemoryMapEntry.type` | `EFI_MEMORY_DESCRIPTOR.Type` (EfiXXXMemoryType) |
| `MemoryMapEntry.base` | `EFI_MEMORY_DESCRIPTOR.PhysicalStart` |
| `MemoryMapEntry.pages` | `EFI_MEMORY_DESCRIPTOR.NumberOfPages` |
| `MemoryMapEntry.attributes` | `EFI_MEMORY_DESCRIPTOR.Attribute` |

### 4.3 PCIDevice → PCI Config Space

| mini-boot-process | PCI Config Space Offset |
|-------------------|------------------------|
| `PCIDeviceInfo.vendor` | Offset 0x00 (Vendor ID) |
| `PCIDeviceInfo.device` | Offset 0x02 (Device ID) |
| `PCIDeviceInfo.class_code` | Offset 0x0B (Class Code: 3 bytes) |
| `PCIDeviceInfo.revision` | Offset 0x08 (Revision ID) |
| `PCIDevice.bar[]` | Offset 0x10-0x27 (BAR0-BAR5) |

## 5. 函数与 Intel FSP 对照

| mini-boot-process 函数 | Intel FSP API | 说明 |
|------------------------|---------------|------|
| `boot_init()` | `FspInitEntry()` | FSP 入口点 |
| `boot_sec_phase()` | `TempRamInit()` (FSP-S SEC phase) | 临时 RAM (CAR) 初始化 |
| `boot_pei_phase()` | `FspMemoryInit()` → `FspSiliconInit()` | 内存初始化 + 硅初始化 |
| `boot_dxe_phase()` | `NotifyPhase(EnumInitPhaseAfterPciEnumeration)` | DXE 阶段通知 |
| `cpu_init_bsp()` | FSP-M CPU initialization | BSP 初始化 |
| `cpu_init_ap()` | `CpuInit()` multiprocessor | AP 初始化 |
| `mem_train_ddr()` | Memory Reference Code (MRC) | DDR 训练 |
| `pci_enumerate_bus()` | `PciEnumeration()` | PCI 枚举 |

## 6. 函数与 AMD AGESA 对照

| mini-boot-process 函数 | AGESA Entry Point | 说明 |
|------------------------|-------------------|------|
| `boot_sec_phase()` | `AmdInitReset()` | 复位后初始化 |
| `cpu_init_bsp()` | `AmdInitEarly()` / `AmdInitPost()` | CPU 早期/后期初始化 |
| `cpu_init_ap()` | `AmdInitLate()` / `AmdS3LateRestore()` | AP 启动 |
| `mem_train_ddr()` | `AmdInitMid()` (MemoryInit) | 内存控制器初始化 |

## 7. Tianocore EDK II 源码对照

| mini-boot-process 源文件 | EDK II 源码路径 |
|--------------------------|-----------------|
| `src/boot_phases.c` — DXE phase | `MdeModulePkg/Core/Dxe/DxeMain/DxeMain.c` |
| `src/cpu_init.c` — CPU init, MSR, paging | `UefiCpuPkg/CpuDxe/CpuDxe.c` |
| `src/memory_init.c` — Memory map building | `MdeModulePkg/Core/Dxe/Mem/Page.c` |
| `src/memory_init.c` — SPD parsing | `UefiCpuPkg/CpuMpPei/CpuMpPei.c` (memory discovery) |
| `src/device_enum.c` — PCI enumeration | `MdeModulePkg/Bus/Pci/PciBusDxe/PciBus.c` |
| `include/boot_phases.h` — BootPhase enum | `MdePkg/Include/Pi/PiBootMode.h` |
| `include/boot_phases.h` — HandOffBlock | `MdePkg/Include/Pi/PiHob.h` |
| `include/device_enum.h` — PCI definitions | `MdePkg/Include/IndustryStandard/Pci*.h` |

## 8. Intel 64 & IA-32 架构对照

| mini-boot-process 功能 | Intel SDM 章节 |
|------------------------|----------------|
| Reset vector (0xFFFFFFF0) | Vol 3A, §9.1.4 — First Instruction Executed |
| MSR_IA32_EFER (LME, NXE) | Vol 3A, §2.2.1 — Extended Feature Enable Register |
| MSR_IA32_APIC_BASE | Vol 3A, §10.4.4 — Local APIC Status and Location |
| MSR_IA32_MTRRCAP / MTRR | Vol 3A, §11.11 — Memory Type Range Registers |
| Cache control (CR0.CD, CR0.NW) | Vol 3A, §11.5 — Cache Control |
| Long mode enabling (LME, PAE, PG) | Vol 3A, §9.8 — Initializing IA-32e Mode |
| SYSENTER/SYSEXIT MSRs | Vol 2B, §4.3 — SYSENTER/SYSEXIT |
| PCI Configuration Space | Vol 3A, §16.2 — PCI Configuration Address Space |
| MTRR types (UC, WB, WC, WT, WP) | Vol 3A, §11.11.2.1 — Memory Type Range |

## 9. ACPI 规范对照

| mini-boot-process 内存类型 | ACPI 表 |
|----------------------------|---------|
| `MEMMAP_ACPI_RECLAIM` | ACPI Table storage (RSDT/XSDT), DSDT, SSDT, FADT |
| `MEMMAP_ACPI_NVS` | Non-Volatile Sleep region (S4 suspend) |
| Memory map attributes |= `MEMMAP_MMIO` | PCI/legacy device MMIO ranges |

## 10. 学习路径建议

```
第 1 步: SEC Phase — 阅读 cache_as_ram.h/.c
      ├── 理解: CPU 启动时无 DRAM 时如何工作
      ├── 参考: Intel SDM Vol 3A §11.5 Cache Control
      └── 实践: 运行 car_demo

第 2 步: CPU Init — 阅读 cpu_init.h/.c
      ├── 理解: BSP/AP 初始化、MSR、MTRR、paging
      ├── 参考: Intel SDM Vol 3A Ch 9, Ch 11
      └── 实践: 添加到 boot_sim_demo

第 3 步: Memory Init — 阅读 memory_init.h/.c
      ├── 理解: SPD 读取、DDR 训练、Memory Map 构建
      ├── 参考: JEDEC JESD79-4C, PI Spec Vol 1 §7
      └── 实践: 添加到 boot_sim_demo

第 4 步: PCI Enum — 阅读 device_enum.h/.c
      ├── 理解: PCI 总线扫描、class code 分类、BAR 分配
      ├── 参考: PCI Spec 3.0, PI Spec Vol 1 §8
      └── 实践: 运行 pci_enum_demo

第 5 步: Boot Flow — 阅读 boot_phases.h/.c
      ├── 理解: PI 各阶段依次推进、HOB 传递
      ├── 参考: UEFI PI Spec Vol 1 §5-10
      └── 实践: 运行 boot_sim_demo
```

## 11. 术语表

| 缩写 | 全称 | mini-boot-process 对应 |
|------|------|------------------------|
| PI | Platform Initialization | 整体项目架构 |
| SEC | Security Phase | `BOOT_PHASE_SEC` |
| PEI | Pre-EFI Initialization | `BOOT_PHASE_PEI` |
| DXE | Driver Execution Environment | `BOOT_PHASE_DXE` |
| BDS | Boot Device Selection | `BOOT_PHASE_BDS` |
| TSL | Transient System Load | `BOOT_PHASE_TSL` |
| RT | Runtime | `BOOT_PHASE_RT` |
| CAR | Cache-as-RAM | `cache_as_ram.h/.c` |
| HOB | Hand-Off Block | `HandOffBlock` struct |
| FV | Firmware Volume | `HandOffBlock.fv_bases[]` |
| PEIM | PEI Module | 阶段内可调度代码单元 |
| PPI | PEIM-to-PEIM Interface | PEIM 间接口 |
| MTRR | Memory Type Range Register | `cpu_init_mtrr()` |
| SPD | Serial Presence Detect | `SPDData` struct |
| MRS | Mode Register Set | `mem_train_ddr()` |
| BAR | Base Address Register | `PCIDevice.bar[]` |
| BSP | Bootstrap Processor | `cpu_init_bsp()` |
| AP | Application Processor | `cpu_init_ap()` |
| MMIO | Memory-Mapped IO | `MEMMAP_MMIO` type |
