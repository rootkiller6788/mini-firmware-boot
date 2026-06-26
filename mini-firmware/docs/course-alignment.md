# Course Alignment — 课程对齐

> 将本 mini-firmware 项目映射到工业级固件架构文档和规范

---

## 1. Intel Firmware Architecture Mapping

| 本模块 | 对应 Intel 概念 | 说明 |
|--------|-----------------|------|
| `firmware_layout.h` - FlashDevice | BIOS Region in SPI Flash | Intel 平台使用 SPI Flash 存储固件，通过 Flash Descriptor 分区 |
| `firmware_layout.h` - FirmwareImage | Firmware Volume (FV) | UEFI PI 规范中 FV 是固件存储的基本容器 |
| `reset_vector.h` - ResetVector | Reset Vector at 0xFFFFFFF0 | 符合 Intel SDM Vol.3A §9.1.4 定义的复位行为 |
| `reset_vector.h` - CPUContext | Processor State after INIT# | 模拟 INIT# 或 RESET# 后的初始寄存器状态 |
| `mmio.h` - MMIOManager | I/O Mapped / Memory-Mapped I/O | Intel 平台通过 PCI 配置空间和 BAR 映射设备 |
| `smbios_fw.h` - SMBIOS | SMBIOS Reference Specification | DMTF SMBIOS 规范定义的固件表接口 |
| `spi_nor.h` - SPIFlash | SPI Flash Controller (ICH/PCH) | Intel PCH 中的 SPI 控制器负责固件闪存访问 |

### 1.1 Intel FSP (Firmware Support Package)

```
mini-firmware 模拟的启动流程:
  上电 -> 复位向量 -> 固件头部验证 -> 入口点跳转

Intel FSP 实际流程:
  Reset -> SEC (Security) -> PEI (Pre-EFI Init)
    -> FSP-T (Temp RAM Init / CAR)
    -> FSP-M (Memory Init / DDR Training)
    -> FSP-S (Silicon Init)
    -> DXE (Driver Execution) -> BDS (Boot Device Select)
```

### 1.2 Intel Flash Descriptor

Intel 平台 SPI Flash 由 Flash Descriptor 分为多个区域：

| 区域 | 偏移 | 描述 | 对应本模块 |
|------|------|------|-----------|
| Flash Descriptor | 0x0000 | 分区表、权限 | FlashDevice.sectors |
| Intel ME Region | 可变 | 管理引擎固件 | — |
| GbE Region | 可变 | 网络固件 | — |
| BIOS Region | 可变 | UEFI 固件本体 | FirmwareImage |
| PDR Region | 可变 | 平台数据 | — |

---

## 2. ARM Trusted Firmware-A Mapping

| 本模块 | 对应 ARM TF-A 概念 | 说明 |
|--------|-------------------|------|
| `reset_vector.h` - ResetVector | BL1 entry point | ARMv8 复位进入 EL3，起始执行 BL1 |
| `firmware_layout.h` - FirmwareImage | FIP (Firmware Image Package) | TF-A 使用 FIP 将 BL2/BL31/BL32/BL33 打包 |
| `reset_vector.h` - CPUContext | CPU state at EL3 | ARM 上电后处于最高异常级别 EL3 |
| `reset_vector.h` - cpu_switch_mode() | Exception level transitions | EL3 -> EL2 -> EL1 通过 ERET 指令切换 |
| `mmio.h` - MMIOManager | Memory-mapped peripherals | ARM SoC 中所有外设通过 MMIO 访问 |
| `spi_nor.h` - SPIFlash | Boot ROM SPI driver | BL1 使用内部 ROM 驱动从 SPI Flash 加载 BL2 |

### 2.1 ARM TF-A Boot Stages

```
BL1 (Boot ROM):
  - 最小化初始化 (cache, stack)
  - 从 SPI NOR 加载 BL2
  - 验证 BL2 签名
  ↓
BL2 (Trusted Boot Firmware):
  - 初始化 DDR
  - 加载 BL31 (EL3 Runtime) 和 BL32/BL33
  - 传递设备树或 ACPI 表
  ↓
BL31 (EL3 Runtime Firmware):
  - PSCI 电源管理服务
  - SDEI 软件委托异常接口
  - Secure Monitor
  ↓
BL33 (Non-trusted Firmware):
  - U-Boot 或 EDK2/UEFI
  - 最终启动操作系统内核
```

---

## 3. UEFI PI Specification Mapping

| 本模块 | UEFI PI 概念 | 卷/章节 |
|--------|-------------|---------|
| `firmware_layout.h` | Firmware Volume (FV) | PI Vol.3: Shared Architectural Elements |
| `firmware_layout.h` - fw_validate_header() | Firmware File System (FFS) | PI Vol.3 §3: FFS 完整性校验 |
| `reset_vector.h` | SEC Phase | PI Vol.1 §2.4: Security Phase |
| `mmio.h` | PEI/DXE I/O Services | PI Vol.1 §5: PEI Services, Vol.2 §7: DXE Services |
| `smbios_fw.h` | SMBIOS DXE Driver | PI Vol.2 §10: DXE Drivers |
| `spi_nor.h` | SPI I/O Protocol | PI Vol.2 §13: I/O Protocols |

### 3.1 UEFI PI Boot Phases

```
SEC (Security):
  - 处理复位向量
  - 切换到保护模式
  - CAR (Cache-As-RAM) 设置临时栈
  ↓
PEI (Pre-EFI Initialization):
  - 最小化 CPU/芯片组初始化
  - 内存初始化 (通过 FSP-M)
  - 发现并启动 DXE 核心
  ↓
DXE (Driver Execution Environment):
  - 加载设备驱动
  - 构建 SMBIOS/ACPI 表
  - 初始化控制台
  ↓
BDS (Boot Device Selection):
  - 枚举启动设备
  - 加载 OS Boot Loader
  ↓
TSL (Transient System Load):
  - 运行 OS Boot Loader (GRUB/Windows Boot Manager)
  - ExitBootServices() — 控制权转交内核
```

---

## 4. SMBIOS Specification Mapping

| 本模块 | DMTF SMBIOS Spec | 版本 |
|--------|-----------------|------|
| SMBIOSHeader | Structure Header Format (§3.1) | 3.0+ |
| SMBIOSBIOSInfo | Type 0 — BIOS Information (§3.3.1) | 3.0+ |
| SMBIOSSystemInfo | Type 1 — System Information (§3.3.2) | 3.0+ |
| SMBIOSBaseboard | Type 2 — Baseboard Information (§3.3.3) | 3.0+ |
| SMBIOSProcessor | Type 4 — Processor Information (§3.3.5) | 3.0+ |
| SMBIOSEntryPoint | Entry Point Structure (§2.1) | 3.0+ |

---

## 5. JEDEC SPI NOR Flash Mapping

| 本模块 | JEDEC Standard | 描述 |
|--------|---------------|------|
| `spi_init()` | JESD216 (SFDP) | Serial Flash Discoverable Parameters |
| `spi_read_jedec_id()` | JESD216 §6: READ ID (9Fh) | 制造商和设备 ID |
| `spi_read_status()` | RDSR (05h) Command | 状态寄存器读取 |
| `spi_sector_erase()` | SE (D8h) Command | 4 KB 扇区擦除 |
| `spi_page_program()` | PP (02h) / QPP (32h) | 页编程 |
| `spi_write_enable()` | WREN (06h) Command | 设置写使能锁存 |

---

## 6. 参考文档 / Reference Documents

| 文档 | 版本 | 说明 |
|------|------|------|
| Intel 64 and IA-32 Architectures SDM | December 2023 | Intel CPU 架构手册 |
| Intel FSP External Architecture Spec | v2.4 | FSP 接口定义 |
| ARM Architecture Reference Manual ARMv8-A | G.a | ARMv8 架构手册 |
| ARM TF-A Documentation | v2.10 | Trusted Firmware-A 设计文档 |
| UEFI PI Specification | v1.8 | UEFI 平台初始化规范 |
| DMTF SMBIOS Specification | v3.7.0 | SMBIOS 表结构规范 |
| JEDEC JESD216F (SFDP) | Rev. F | 串行 Flash 可发现参数 |
