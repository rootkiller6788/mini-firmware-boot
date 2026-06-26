# Boot Phases Deep Dive — UEFI PI 每个阶段的详细解析

> 基于 UEFI PI Specification v1.8, Volume 1: Pre-EFI Initialization Core Interface
> 结合 Intel Firmware Support Package (FSP) 和 AMD AGESA 实现细节

## 1. SEC Phase — Security Phase (安全阶段)

### 1.1 阶段入口
- CPU 从复位向量 (0xFFFFFFF0) 开始执行
- 实模式, 16位寻址, 无分页, 无 DRAM
- 处理器处于最原始状态

### 1.2 核心任务

| 任务 | 说明 | 关键数据 |
|------|------|----------|
| 配置 CAR | 将 CPU 缓存设置为 no-eviction 模式作为临时 RAM | CAR_BASE, CAR_SIZE |
| 微码加载 | 为 BSP 加载最新的 microcode update | MSR 0x79 (MCG_CAP) |
| 临时栈建立 | 在 CAR 区域顶部设置栈指针 | CAR_STACK_TOP = CAR_BASE + CAR_SIZE - 16 |
| FV 发现 | 定位 PEI Foundation 的 Firmware Volume | 通过扫描 flash 头部 |
| 构建 SEC→PEI HOB | 准备包含 FV 位置的 hand-off block | HOB 格式 (PHIT + FV) |

### 1.3 CAR 技术细节
Cache-as-RAM 通过在 MTRR 中标记一个地址范围为 WB (Write-Back)，并将该区域对应的
缓存行标记为 "locked" (no-eviction mode)，从而使得对该区域的读写操作都命中缓存，
实现类 RAM 的效果，而无需实际的 DRAM。

```
CAR 内存映射 (典型):
  CAR_BASE      → [PEI临时堆]        (可增长向上)
                          ↓
  CAR_STACK_TOP → [PEI临时栈]        (可增长向下)
                          ↓
  CAR_END       → [预留空间]
```

### 1.4 SEC Hand-Off 数据结构
SEC 传递给 PEI 的数据:
```
SEC_HOB {
    PHIT (Phase Handoff Information Table)
        ├── PEI入口点
        ├── PEI堆栈信息
        └── PEI内存使用量

    FV_HOB (Firmware Volume)
        ├── BaseAddress: 0xFF000000
        ├── Size: 0x01000000
        └── AuthenticationStatus

    CAR_Descriptor (临时内存描述)
        ├── BaseAddress: CAR_BASE
        └── Size: CAR_SIZE
}
```

### 1.5 阶段退出条件
- CAR 成功启用并验证读写
- 微码更新应用成功
- 栈正常工作 (已验证)
- PEI Foundation FV 已定位
- HOB 准备完毕

## 2. PEI Phase — Pre-EFI Initialization (预 EFI 初始化)

### 2.1 PEI Foundation (PEI Core)

PEI Core 是 PEI 阶段的调度器，负责:
1. 解析 SEC HOB
2. 扫描 FV 寻找 PEIMs (PEI Modules)
3. 按依赖关系调度 PEIMs
4. 管理 PPI (PEIM-to-PEIM Interfaces) 数据库
5. 在阶段结束时构建 HOB 列表传给 DXE

### 2.2 PPI 数据库

PPI 是 PEIM 之间的接口机制:
```
PPI Database:
  { GUID: EFI_PEI_CPU_IO_PPI,                Instance → CPU_IO interface }
  { GUID: EFI_PEI_PCI_CFG2_PPI,              Instance → PCI config access }
  { GUID: EFI_PEI_STALL_PPI,                 Instance → microsecond delay }
  { GUID: EFI_PEI_READ_ONLY_VARIABLE2_PPI,   Instance → NVRAM read }
  { GUID: EFI_PEI_RESET_PPI,                 Instance → system reset }
  { GUID: EFI_PEI_LOAD_FILE_PPI,             Instance → file loading }
  { GUID: EFI_PEI_FV_FIND_PPI,               Instance → FV discovery }
```

### 2.3 PEIM 调度顺序

```
阶段1 PEIMs (无依赖):
  ├── CpuPeim           → 安装 EFI_PEI_CPU_IO_PPI
  └── PciCfgPeim        → 安装 EFI_PEI_PCI_CFG2_PPI

阶段2 PEIMs (依赖 CPU→PciCfg→Memory):
  ├── MemoryInitPeim    → 安装 EFI_PEI_MEMORY_DISCOVERED_PPI
  ├── PciHostBridgePeim → 安装 EFI_PEI_PCI_HOST_BRIDGE_PPI
  └── StallPeim         → 安装 EFI_PEI_STALL_PPI

阶段3 PEIMs (依赖 Memory→Stall):
  ├── SmmAccessPeim     → 安装 EFI_PEI_SMM_ACCESS_PPI
  ├── BootModePeim      → 安装 EFI_PEI_MASTER_BOOT_MODE_PPI
  └── PlatformPeim      → 平台特定初始化

阶段4 (DXE IP):
  └── DxeIplPeim        → 安装 EFI_PEI_DXE_IPL_PPI, 载入DXE Core
```

### 2.4 Memory Discovery PEIM 详细流程

```
1. Read SPD from SMBus
   │
   ├── For each SMBus address (0x50-0x53):
   │   ├── Issue START + I2C address
   │   ├── Send SPD byte offset
   │   ├── REPEATED START + read command
   │   └── Read 256 bytes + STOP
   │
2. Parse SPD Data
   │
   ├── Memory type (DDR3/DDR4/DDR5)
   ├── Module size (from density + banks)
   ├── Speed grade (from tCKmin)
   ├── Timing parameters (tCL, tRCD, tRP, tRAS, tRFC)
   └── Organization (ranks, channel population)
   │
3. Configure DDR Controller
   │
   ├── Set DRAM clock frequency
   ├── Program MRS registers via MR commands
   ├── Configure ODT (RTT_NOM, RTT_WR, RTT_PARK)
   └── Enable ZQ calibration
   │
4. DDR Training
   │
   ├── Write leveling (align DQS to CK per-byte-lane)
   ├── Read DQS gate training
   ├── Read DQ deskew
   ├── Write DQ deskew
   └── VrefDQ calibration
   │
5. Test Memory
   │
   ├── Walking-1 data bus test
   ├── Address uniqueness test
   ├── March C- pattern test
   └── ECC scrub
   │
6. Build Memory Map
   │
   ├── TOLUD calculation (Top of Low Usable DRAM)
   ├── TOUUD calculation (Top of Upper Usable DRAM)
   ├── Memory hole handling (3-4 GB MMIO gap)
   ├── Reserved regions (SMM, TSEG, Gfx stolen, ME/UMA)
   └── ACPI reclaim/NVS reservation
```

## 3. DXE Phase — Driver Execution Environment (驱动执行环境)

### 3.1 DXE Core 初始化序列

```
DXE Core Entry
  ├── Initialize DXE Core global data
  ├── Initialize memory services → CoreAddMemorySpace()
  ├── Initialize the HOB list → CoreInitHobList()
  ├── Create EFI System Table (gST) ← pointer to global tables
  ├── Create EFI Boot Services Table (gBS) ← 所有Boot Services函数
  ├── Create EFI Runtime Services Table (gRT) ← 所有Runtime函数
  ├── Create EFI DXE Services Table (gDS)
  ├── Initialize DXE Dispatcher
  ├── Load Arch Protocol drivers first
  └── Load remaining DXE drivers
```

### 3.2 Architecture Protocols (必须最先加载)

| A Priori File | Arch Protocol |
|---------------|--------------|
| CpuArchDxe | EFI_CPU_ARCH_PROTOCOL (FlushCache, EnableInterrupt, ...) |
| MetronomeDxe | EFI_METRONOME_ARCH_PROTOCOL (wait for tick) |
| TimerDxe | EFI_TIMER_ARCH_PROTOCOL (timer services) |
| RealTimeClockDxe | EFI_RTC_ARCH_PROTOCOL (get/set time) |
| ResetDxe | EFI_RESET_ARCH_PROTOCOL (system reset) |
| RuntimeDxe | EFI_RUNTIME_ARCH_PROTOCOL (runtime image pointers) |
| SecurityDxe | EFI_SECURITY_ARCH_PROTOCOL (file authentication) |
| DataHubDxe | EFI_DATA_HUB_PROTOCOL (logging, data hub) |
| VariableDxe | EFI_VARIABLE_ARCH_PROTOCOL (NVRAM variable access) |
| WatchdogTimerDxe | EFI_WATCHDOG_TIMER_ARCH_PROTOCOL |
| MonotonicCounterDxe | EFI_MONOTONIC_COUNTER_ARCH_PROTOCOL |
| BdsDxe | EFI_BDS_ARCH_PROTOCOL (boot device selection) |

### 3.3 DXE 调度器 (Dispatcher)

DXE dispatcher 的工作方式:
```
while (more FVs to scan):
  1. Open Firmware Volume
  2. Read FFS (Firmware File System) headers
  3. For each FFS file:
     a. Read DEPEX section (optional)
     b. If DEPEX is satisfied:
        - Load PE/COFF+ image into memory
        - Relocate image (apply fix-ups)
        - Call image's entry point
     c. Image entry point RegisterProtocol()
  4. After loading, protocols become available
  5. Repeat — newly available protocols may satisfy more DEPEX
```

### 3.4 DEPEX (Dependency Expression)

DEPEX 定义了驱动加载的前置条件:
```
// PciBusDxe 的 DEPEX 示例:
BEFORE  PciHostBridge    // 在 PciHostBridge 之前不需要 — 实际上 AFTER
         &
         gEfiPciHostBridgeResourceAllocationProtocolGuid
         &
         gEfiPciRootBridgeIoProtocolGuid
```

DEPEX 操作码:
| Opcode | 含义 |
|--------|------|
| `BEFORE` | 如果 GUID 协议已安装，则在它之前加载 |
| `AFTER` | 在所有 GUID 协议安装后才加载 |
| `PUSH`  | 将协议 GUID 推入求值栈 |
| `AND`   | 栈上所有条件都必须满足 |
| `OR`    | 栈上至少一个条件满足 |
| `NOT`   | 栈顶条件必须不满足 |
| `TRUE`  | 总是满足 |
| `FALSE` | 永远不满足 |
| `END`   | 求值结束 |

### 3.5 设备驱动绑定模型

```
Driver Binding 协议:
  1. Supported()    — 驱动是否支持此控制器？
  2. Start()        — 启动驱动，安装子协议
  3. Stop()         — 停止驱动，卸载子协议

示例: SataController 驱动
  1. ConnectController(SataControllerHandle)
       → SataController.Supported() 检查 AHCI class code
       → Returns EFI_SUCCESS
  2. SataController.Start()
       → 初始化 AHCI 寄存器
       → 扫描端口，发现连接的设备
       → 为每个端口创建子句柄
       → 在子句柄上安装 EFI_BLOCK_IO_PROTOCOL
  3. 后续驱动可连接到子句柄
```

## 4. BDS Phase — Boot Device Selection (启动设备选择)

### 4.1 Boot Manager 架构

```
BootManager
  ├── ProcessBootOptions()
  │   ├── Boot0000: UEFI Hard Drive     → BBS_TABLE entry
  │   ├── Boot0001: UEFI CD/DVD Drive
  │   ├── Boot0002: UEFI USB Drive
  │   ├── Boot0003: UEFI PXE Network
  │   └── Boot0004: UEFI Internal Shell
  │
  ├── ConnectBootDevice(DevicePath)
  │   ├── 递归 ConnectController()
  │   ├── 直到 DevicePath 上的所有节点都连接
  │   └── 在最末端安装 EFI_SIMPLE_FILE_SYSTEM_PROTOCOL
  │
  └── LoadBootImage()
      ├── 打开文件: \EFI\BOOT\BOOTX64.EFI
      ├── LoadImage() → 创建 Image Handle
      ├── StartImage() → 移交控制权
      └── 返回 EFI_SUCCESS → 进入 TSL 阶段
```

### 4.2 Boot Option 数据结构

```
EFI_LOAD_OPTION {
    Attributes     UINT32  // LOAD_OPTION_ACTIVE (bit 0)
    FilePathListLength UINT16
    Description    CHAR16[] // "UEFI Hard Drive"
    FilePathList   EFI_DEVICE_PATH_PROTOCOL[]
    //   PciRoot(0)/Pci(0x1F,0x2)/Sata(0x0)/HD(1,GPT,...)
}

// 存储在 UEFI 变量:
// VariableName: Boot#### (#### = 0000-FFFF)
// GUID: {EFI_GLOBAL_VARIABLE}
// NV+BS+RT 属性
```

### 4.3 平台 BDS 策略

Platform BDS 库实现以下回调:
- `PlatformBootManagerBeforeConsole()` — 连接静默设备
- `PlatformBootManagerAfterConsole()` — 连接键盘/显示设备
- `PlatformBootManagerWaitCallback()` — 超时回调 (按 F2/F12 等)
- `PlatformBootManagerUnableToBoot()` — 所有启动选项失败后的行为

### 4.4 Device Path 协议

Device Path 描述了设备在系统中的拓扑位置:
```
// 示例: SATA SSD 上的 ESP 分区
PciRoot(0x0)
  /Pci(0x1F, 0x2)          // SATA controller at bus 0, device 0x1F, func 2
    /Sata(0x0, 0xFFFF, 0x0) // SATA port 0
      /HD(1, GPT, UUID, 0x800, 0x100000) // Partition 1
        /\EFI\BOOT\BOOTX64.EFI
```

## 5. TSL Phase — Transient System Load (瞬时系统加载)

### 5.1 TSL 流程

```
1. Boot Loader 以 UEFI 应用程序形式运行
   └── EFI_IMAGE_SUBSYSTEM_EFI_APPLICATION

2. Boot Loader 调用 UEFI Boot Services:
   ├── gBS->AllocatePages()           内核/initrd 分配内存
   ├── gBS->GetMemoryMap()            获取当前内存映射
   ├── gBS->LocateProtocol()          查找 ACPI/Graphics 等协议
   └── gBS->Stall()                   微秒级延迟

3. Boot Loader 准备内核启动:
   ├── 构建内核启动参数 (command line)
   ├── 加载 initrd/initramfs 到内存
   ├── 设置内核页表
   └── 设置启动协议 (Linux Boot Protocol, Multiboot, etc.)

4. 调用 ExitBootServices():
   ├── 传入当前 Memory Map Key
   ├── 固件回收 Boot Services 内存
   ├── 仅保留 Runtime Services 表和 EFI 配置表
   └── 控制权转移给操作系统内核
```

### 5.2 Linux Boot Flow (EFI_STUB)

```
linux/arch/x86/boot/header.S (EFI handover entry)
  → efi_stub_entry()
    → efi_main()
      ├── setup_boot_services()
      ├── handle_cmdline()
      ├── handle_ramdisk()
      ├── allocate_e820()  // 构建 BIOS E820 兼容内存映射
      ├── exit_boot()
      │   └── ExitBootServices()
      └── efi_stub_entry() → startup_64 (内核入口)

// 内核接管后:
linux/arch/x86/kernel/head_64.S
  → startup_64()
    ├── 解压内核 (如果使用 bzImage)
    ├── 设置新的页表
    ├── 初始化中断控制器 (APIC/IOAPIC)
    ├── 解析 ACPI 表
    └── start_kernel() → 进入 Linux 内核主循环
```

## 6. RT Phase — Runtime (运行时)

### 6.1 运行时服务

ExitBootServices() 后仍可调用的服务:
| 函数 | 说明 | 中断上下文安全? |
|------|------|----------------|
| `GetTime()` | 获取当前日期时间 | 否 |
| `SetTime()` | 设置日期时间 | 否 |
| `GetWakeupTime()` | 获取唤醒时间 | 否 |
| `SetWakeupTime()` | 设置唤醒时间 | 否 |
| `GetVariable()` | 读取 UEFI 变量 | 否 |
| `GetNextVariableName()` | 枚举 UEFI 变量 | 否 |
| `SetVariable()` | 写入 UEFI 变量 | 否 |
| `GetNextHighMonotonicCount()` | 高精度单调计数器 | 是 |
| `ResetSystem()` | 重启/关机系统 | 是 |
| `UpdateCapsule()` | 更新固件胶囊 | 否 |
| `QueryCapsuleCapabilities()` | 查询胶囊支持 | 否 |

### 6.2 运行时调用模型

操作系统调用 UEFI Runtime Services 时:
1. OS 必须调用 `SetVirtualAddressMap()` 将运行时内存区域映射到虚拟地址
2. 运行时服务以 16 字节对齐映射; 必须在使用前映射所有运行时区域
3. 调用约定: OS 保存所有寄存器; UEFI 使用 MS ABI (Microsoft x64 calling convention)
4. 在调用运行时服务期间必须禁用中断; UEFI 内部可能自旋等待

### 6.3 Linux 的 efivarfs

```
用户空间访问 UEFI 变量:
  /sys/firmware/efi/
    ├── efivars/             (旧版, 每个变量一个文件)
    │   ├── Boot0000-...
    │   ├── BootOrder-...
    │   ├── Lang-...
    │   └── ...
    └── vars/                 (新版, efivarfs 文件系统)
        ├── Boot0000/
        │   ├── raw_var       (二进制变量数据)
        │   ├── attributes    (变量属性)
        │   └── size          (变量大小)
        └── ...

用户空间工具: efibootmgr, fwupdmgr, systemd-boot
```

## 7. Hand-Off 结构演变总结

```
SEC
  └── HOB: PHIT + FV 位置
      │
      ▼
PEI
  └── HOB List: PHIT + Memory Allocation + Resource Descriptor + FV + CPU + extensions
      │
      ▼
DXE
  └── EFI System Table + Configuration Tables (ACPI, SMBIOS, etc.)
      │
      ▼
BDS
  └── Loaded Image Protocol + Device Path + Memory Map
      │
      ▼
TSL → RT
  └── Runtime Services Table + Configuration Tables (still accessible to OS)
```

## 8. mini-boot-process 的模拟实现映射

| 实际 PI 元素 | mini-boot-process 模拟 |
|--------------|------------------------|
| PEI Foundation PEI Core | `boot_pei_phase()` — 集中式 PEIM 调度 |
| PEIMs | `cpu_init_*()`, `mem_init_*()`, `pci_*()` 函数调用序列 |
| PPI 数据库 | 无显式模拟; 函数间直接调用 |
| DEPEX | `boot_transition()` 中的 conditional dispatch |
| Hand-Off Blocks | `HandOffBlock` + `BootState.phase_handoff_blocks[]` |
| DXE Dispatcher | `boot_dxe_phase()` — 循序加载驱动列表 |
| DRAM 训练 | `mem_train_ddr()` — 简化的 MRS + write leveling + read DQS |
| PCI BAR 分配 | `pci_assign_resources()` — 线性的 MMIO/IO 地址分配 |
| Memory Map | `mem_build_map()` → `MemoryMap` → `mem_print_map()` |
