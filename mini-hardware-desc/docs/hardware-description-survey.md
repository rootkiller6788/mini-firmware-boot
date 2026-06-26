# 硬件描述技术综述

## 1. 概述

硬件描述 (Hardware Description) 是固件与操作系统之间的关键桥梁。不同的 CPU 架构和平台类型使用不同的机制将硬件拓扑信息传递给操作系统。

本文档对比分析三种核心硬件描述技术：
- **Device Tree** — ARM / RISC-V / PowerPC 嵌入式系统的首选方案
- **ACPI** — x86 / ARM 服务器平台的标准接口
- **SMBIOS** — 跨平台系统资产管理标准

---

## 2. 为什么需要硬件描述？

### 不可发现硬件 (Non-Discoverable Hardware)

许多嵌入式外设没有标准化的硬件 ID 寄存器或枚举机制：

```
┌──────────────────────────────────────────────────┐
│  x86 平台 (可发现)                                │
│  CPU → PCI Host Bridge → PCIe 枚举 → 设备发现     │
│  通过 PCI Vendor/Device ID 自动识别               │
├──────────────────────────────────────────────────┤
│  ARM/RISC-V 平台 (不可发现)                       │
│  CPU → AMBA/AXI 总线 → 外设寄存器                 │
│  没有标准枚举机制，需要平台数据描述                │
└──────────────────────────────────────────────────┘
```

### 需求对比

| 需求 | Device Tree | ACPI | SMBIOS |
|------|-------------|------|--------|
| 设备枚举与驱动绑定 | 核心功能 | 核心功能 | 不支持 |
| 电源管理 | 有限 (通过驱动) | 核心功能 | 不支持 |
| 系统资产信息 | 有限 (compatible) | 有限 | 核心功能 |
| 热管理 | 通过驱动 | 核心功能 | 不支持 |
| 中断路由 | 完整支持 | 完整支持 (PRT) | 不支持 |
| NUMA 拓扑 | 有限 | 完整支持 (SRAT/SLIT) | 不支持 |
| 平台无关性 | 是 (ARM/RISC-V/PowerPC) | 是 (x86/ARM) | 是 (所有) |

---

## 3. Device Tree (FDT) 详解

### 使用场景

- **U-Boot**: 引导阶段的硬件初始化
- **Linux Kernel**: ARM/ARM64/RISC-V/PowerPC 的平台描述
- **FreeBSD**: ARM64 平台支持
- **Zephyr RTOS**: 嵌入式 RTOS 的硬件配置
- **Xen Hypervisor**: 虚拟机设备树传递

### 工作流程

```
┌──────────┐     ┌──────────┐     ┌──────────┐
│  硬件设计师 │────→│  固件工程师 │────→│  系统软件  │
└──────────┘     └──────────┘     └──────────┘
      │               │                  │
      ▼               ▼                  ▼
  定义内存映射   编写 DTS 文件       解析 DTB
  和中断连接    编译为 DTB         创建设备实例
      │               │                  │
      └───────────────┴──────────────────┘
           完整的硬件→驱动映射链
```

### 优势

1. **人可读格式**: DTS 是简洁的文本格式，易于审查和版本控制。
2. **编译时验证**: DTC 在编译阶段发现语法错误，而不是运行时崩溃。
3. **平台独立**: 不依赖任何特定固件实现或操作系统版本。
4. **动态覆盖**: Device Tree Overlay (DTBO) 支持运行时外设热插拔和 FPGA 重配置。
5. **轻量级**: 解析器仅需数千行 C 代码。

### 劣势

1. **有限的动态能力**: 不像 ACPI 那样支持复杂的运行时行为 (如 DPTF 热管理策略)。
2. **版本兼容性**: DTS 绑定的不兼容更改可能导致老的 DTB 在新内核上失败。
3. **复杂 SoC 的规模**: 高端 SoC (如现代手机芯片) 的 DTS 可达数万行。

### FDT 在 U-Boot 中的角色

```
U-Boot SPL → 初始化 DDR → 加载 U-Boot Proper + DTB
                ↓
        U-Boot Proper → 修改 DTB (内存/频率/bootargs) → 启动内核
```

U-Boot 使用 `fdt` 命令来检查和修改设备树：

```bash
fdt addr ${fdt_addr}              # 设置 FDT 地址
fdt set /memory reg <0x80000000 0x40000000>  # 修正内存大小
fdt chosen bootargs "console=ttyS0,115200"   # 设置内核参数
```

### FDT 在 Linux 内核中的角色

```c
// 驱动通过 Open Firmware (OF) API 访问设备树
struct device_node *np = pdev->dev.of_node;
int irq = platform_get_irq(pdev, 0);
struct resource *res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
struct clk *clk = devm_clk_get(&pdev->dev, NULL);
```

关键 API：
- `of_find_node_by_path()` / `of_find_compatible_node()`
- `of_property_read_u32()` / `of_property_read_string()`
- `of_iomap()` / `of_ioremap()`
- `irq_of_parse_and_map()`
- `of_clk_get_by_name()`

---

## 4. ACPI 详解

### 使用场景

- **x86/x86_64 PC**: 自 1996 年以来的标准固件接口
- **ARM 服务器**: ARM SBBR (Server Base Boot Requirements) 要求 ACPI
- **RISC-V 服务器**: RISC-V BRS (Boot and Runtime Services) 推荐 ACPI
- **虚拟化**: QEMU/KVM 生成 ACPI 表给 Guest OS

### ACPI 在 UEFI 中的角色

```
UEFI Firmware (DXE Phase)
         │
         ▼
  ACPI Table Construction
  ├── Build RSDP → XSDT
  ├── Build FADT (PM register info)
  ├── Build DSDT/SSDT (AML bytecodes)
  ├── Build MADT (CPU/APIC topology)
  ├── Build MCFG (PCIe ECAM)
  └── Build other tables (HPET, SRAT, SLIT, DMAR, etc.)
         │
         ▼
  Install tables via UEFI ConfigurationTable
         │
         ▼
  OS Loader → Locate RSDP → Parse tables
```

### 关键子系统

**电源管理** (Through ACPI):
```
ACPI Sleep States:
  S0   - Working
  S0ix - Modern Standby (Intel/AMD)
  S1   - CPU Stop Grant (legacy)
  S3   - Suspend to RAM (Sleep)
  S4   - Suspend to Disk (Hibernate)
  S5   - Soft Off

Device Power States: D0 (full on) → D3hot → D3cold
Processor Power States: C0 → C1 → C2 → ... → C10
Performance States: P0 (max) → P1 → ... → Pn (min)
```

**中断路由** (Through MADT + PRT):

ACPI 提供三项中断路由机制：
1. **MADT APIC 条目**: 描述 Local/IO APIC 和中断重定向
2. **_PRT (PCI Routing Table)**: PCI 设备 INT A-D → GSI 的映射
3. **Interrupt Source Override**: 修正传统 PIC 中断到 IOAPIC GSI 的映射

**设备枚举** (Through DSDT/SSDT + _HID/_CID):

ACPI 使用以下命名空间对象枚举设备：
- `_HID` (Hardware ID): "PNP0A08" 等 PNP/ACPI ID
- `_CID` (Compatible ID): 备用兼容性 ID
- `_UID` (Unique ID): 实例标识符
- `_ADR` (Address): 设备在父总线上的地址
- `_CRS` (Current Resource Settings): IO/MMIO/IRQ 资源
- `_STA` (Status): 设备存在性/功能性

### 优势

1. **字节码可编程**: AML 提供运行时决策能力，比声明式 DTS 更灵活
2. **完整的电源管理**: 定义标准化的睡眠/唤醒/性能状态模型
3. **热管理**: DPTF (Dynamic Platform and Thermal Framework) 支持
4. **企业级特性**: NUMA (SRAT/SLIT)、RAS (HEST/BERT/EINJ)、IOMMU (DMAR/IVRS)

### 劣势

1. **复杂度**: AML 解释器实现复杂，完整的 ACPICA 子系统约 100K 行代码。
2. **安全性**: AML 字节码在 Ring-0 执行，恶意/有缺陷的 AML 可导致系统崩溃 (ACPI 曾被称为 "Advanced Configuration and Pain Interface")。
3. **平台依赖**: ACPI 表通常由主板/BIOS 供应商生成，不同平台间的质量不一。
4. **ARM 社区争议**: 一些嵌入式 ARM 开发者认为 ACPI 过度设计且不符合嵌入式哲学。

---

## 5. SMBIOS 详解

### 设计目标

SMBIOS 不负责设备枚举或电源管理，而是提供**系统级资产管理信息**：
- 主机型号和制造商
- 内存条规格 (插槽位置、类型、速度、序列号)
- 处理器信息 (型号、核心数、缓存配置)
- 机箱类型和标识

### 信息流

```
BIOS/UEFI → 收集硬件信息 → 构建 SMBIOS 表 → 写入物理内存
                                              ↓
OS/Linux → /sys/class/dmi/id/ → 导出为 sysfs 文件
         → dmidecode 工具 → 人类可读输出
```

### dmidecode 示例输出

```
# dmidecode -t system
Handle 0x0001, DMI type 1, 27 bytes
System Information
    Manufacturer: NanoHardware Inc.
    Product Name: NanoBoard Pro
    Version: Rev C
    Serial Number: SN-000001
    UUID: 55010203-0405-0607-0809-0a0b0c0d0e0f
    Wake-up Type: Power Switch
    SKU Number: SKU-001
    Family: Nano Family

# dmidecode -t memory
Handle 0x0007, DMI type 17, 40 bytes
Memory Device
    Array Handle: 0x0000
    Error Information Handle: Not Provided
    Total Width: 64 bits
    Data Width: 64 bits
    Size: 8192 MB
    Form Factor: SODIMM
    Set: None
    Locator: DIMM 0
    Bank Locator: Channel A
    Type: DDR4
    Type Detail: Synchronous
    Speed: 3200 MT/s
    Manufacturer: NanoMemory
    Serial Number: MEM-SN-67890
    Part Number: NMD-32GB-3200
```

### Linux sysfs 接口

```bash
ls /sys/class/dmi/id/
# bios_date  bios_vendor  bios_version  board_asset_tag
# board_name  board_serial  board_vendor  board_version
# chassis_asset_tag  chassis_serial  chassis_type  chassis_vendor
# chassis_version  modalias  product_family  product_name
# product_serial  product_sku  product_uuid  product_version
# sys_vendor  uevent
```

### 版本演进

- **SMBIOS 2.x (1995-2020)**: 32 位入口点结构 (_SM_ anchor), 32-bit 物理地址
- **SMBIOS 3.x (2015-present)**: 64 位入口点结构 (_SM3_ anchor), 64-bit 物理地址, 支持 >4GB 表

---

## 6. 对比总结

### Device Tree vs ACPI

| 维度 | Device Tree | ACPI |
|------|-------------|------|
| **主要架构** | ARM, RISC-V, PowerPC | x86, ARM 服务器, RISC-V 服务器 |
| **描述形式** | 声明式数据 (DTS → DTB) | 字节码 (ASL → AML) |
| **发现机制** | 引导参数 (寄存器 r0-r3 on ARM) | RSDP 搜索 (0xE0000-0xFFFFF) |
| **动态行为** | 静态 + Overlay (DTBO) | 运行时解释 (AML) |
| **电源管理** | 有限 (驱动实现) | 完整的 S0-S5/C-state/P-state |
| **热管理** | 通过热驱动 | DPTF 框架 |
| **NUMA** | 有限 (distance-map) | 完整 (SRAT + SLIT) |
| **编译工具** | dtc | iasl |
| **复杂度** | 低 (~5K lines parser) | 高 (~100K lines ACPICA) |
| **可移植性** | 高 | 中 (依赖平台 ACPI 实现) |
| **固件责任** | 提供 DTB | 提供全部表 + AML 解释 |

### 技术选型矩阵

| 平台类型 | 推荐方案 | 原因 |
|----------|----------|------|
| 嵌入式 ARM SoC (IoT/工业) | Device Tree | 轻量、确定性、社区广泛支持 |
| ARM SBC (树莓派等) | Device Tree | Overlay 支持扩展板 |
| ARM 服务器 (Ampere, Graviton) | ACPI | SBBR 要求、企业级管理 |
| x86 笔记本/台式机 | ACPI + SMBIOS | 传统标准 |
| x86 服务器 | ACPI + SMBIOS | 电源/热/资产全面管理 |
| RISC-V 嵌入式 | Device Tree | 轻量级 |
| RISC-V 服务器 | ACPI + SMBIOS | BRS 规范要求 |
| 虚拟机 (KVM) | Device Tree 或 ACPI | 取决于 Guest OS |
| 微控制器 (MCU) | 无 (直接寄存器访问) | 内存映射外设直接在代码中定义 |

---

## 7. HOB: UEFI PI 的握手机制

### HOB 在固件阶段中的位置

```
┌──────────────────────────────────┐
│ SEC (Security)                   │ ← CPU 初始化, CAR (Cache-As-RAM)
├──────────────────────────────────┤
│ PEI (Pre-EFI Initialization)     │ ← 内存初始化, HOB 构建
│   ├── 发现内存 + 资源             │
│   ├── 构建 HOB 列表               │
│   └── 传递 HOB 给 DXE            │
├──────────────────────────────────┤
│ DXE (Driver Execution Environment)│ ← 消费 HOB, 构建完整的 ACPI/SMBIOS 表
│   ├── 读取 HOB 中的内存映射       │
│   ├── 根据资源描述初始化驱动       │
│   └── 构建 ACPI 表 (基于 HOB)    │
├──────────────────────────────────┤
│ BDS (Boot Device Selection)      │ ← 引导 OS Loader
├──────────────────────────────────┤
│ TSL/RT (Transient System Load /  │
│         Runtime)                 │ ← OS 运行时
└──────────────────────────────────┘
```

### HOB 类型与用途

| HOB 类型 | PEI 提供什么 | DXE 如何使用 |
|----------|-------------|-------------|
| **PHIT** | 内存范围 (整体/空闲) | 确定可用内存大小和位置 |
| **Memory Allocation** | 每段内存的类型 (EfiConventionalMemory/AcpiNVS 等) | 构建 GCD (Global Coherency Domain) 内存映射 |
| **Resource Descriptor** | MMIO 范围, IO 端口, 固件设备 | 注册 MMIO/IOPort 空间, 发现硬件 |
| **Firmware Volume** | FV 在内存/Flash 中的位置 | 加载 DXE 驱动和应用程序 |
| **CPU** | 地址空间宽度 | 确定寻址模式 |
| **GUID Extension** | 任意 PEIM→DXE Driver 的自定义数据 | PEIM 和 DXE 驱动间的私有协议 |

---

## 8. 未来趋势

### 1. ACPI 向 ARM/RISC-V 扩张
- ARM SBBR (Server Base Boot Requirements) 要求 ACPI
- RISC-V 社区正在标准化 ACPI 支持
- 服务器级功能 (RAS, NUMA) 增加 ACPI 的必要性

### 2. Device Tree 的持续进化
- 更好的 schema 验证 (dtschema/dt-validate 工具)
- YAML 绑定取代 TXT 绑定
- DTBO 在嵌入式 Linux 中广泛应用

### 3. 系统资源描述统一化
- DMTF Redfish 作为管理 API 的上层抽象
- CXL (Compute Express Link) 引入新的拓扑描述需求
- 异构计算 (CPU+GPU+NPU+FPGA) 的复杂拓扑需要新的描述方法

### 4. 安全硬件描述
- ARM FF-A (Firmware Framework for Arm A-profile) 定义 secure/non-secure 分区
- Device Tree 引入 `/firmware/optee` 等安全节点
- ACPI 引入安全相关表 (ASPT, HEST)

---

## 9. 结论

硬件描述是固件与操作系统之间最古老的接口之一，也是最具活力的技术领域。不同的使用场景需要不同的技术方案：

- **Device Tree** 为嵌入式系统提供了简洁、可移植的硬件描述方案，特别适合 ARM/RISC-V 的单板计算机和 IoT 设备。
- **ACPI** 是企业级 x86 和 ARM 服务器的必需品，提供完整的电源管理、热管理和 RAS (Reliability, Availability, Serviceability) 支持。
- **SMBIOS** 补充了资产管理维度的信息，使系统管理员无需物理接触设备即可查询硬件配置。
- **HOB** 在 UEFI PI 框架内扮演 "胶水" 角色，连接 PEI 和 DXE 阶段。

`mini-hardware-desc` 项目通过约 1200 行的 C99 实现，提供了这四种技术的可运行参考实现，适合学习和集成到更大型的固件项目中。

---

## 引用

- **Device Tree**: [https://www.devicetree.org/](https://www.devicetree.org/)
- **ACPI 6.5**: [https://uefi.org/specifications](https://uefi.org/specifications)
- **SMBIOS 3.7**: [https://www.dmtf.org/standards/smbios](https://www.dmtf.org/standards/smbios)
- **UEFI PI 1.8**: [https://uefi.org/specifications](https://uefi.org/specifications)
- **ACPICA**: [https://www.acpica.org/](https://www.acpica.org/)
- **Device Tree for Dummies**: [https://elinux.org/Device_Tree_Reference](https://elinux.org/Device_Tree_Reference)
- **Linux DMI sysfs**: [https://www.kernel.org/doc/Documentation/ABI/testing/sysfs-firmware-dmi](https://www.kernel.org/doc/Documentation/ABI/testing/sysfs-firmware-dmi)
- **ARM SBBR**: [https://developer.arm.com/architectures/platform-design/server-systems](https://developer.arm.com/architectures/platform-design/server-systems)
