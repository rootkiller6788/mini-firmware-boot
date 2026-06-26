# mini-firmware-layout — 固件闪存布局详解

> 参考 Intel Firmware Support Package (FSP), ARM Trusted Firmware-A (TF-A), UEFI PI specification

---

## 1. 概述 / Overview

固件闪存布局是底层固件开发的核心概念。本模块定义了嵌入式系统和 x86/ARM 平台中固件镜像在 NOR Flash 上的组织方式。

The firmware flash layout defines how firmware images are organized on NOR Flash memory, covering everything from sector/page geometry to the logical sections within a firmware image.

---

## 2. SPI NOR Flash 基础 / SPI NOR Flash Fundamentals

### 2.1 存储结构 / Storage Structure

SPI NOR Flash 是固件存储的首选介质：

| 属性 | 典型值 | 说明 |
|------|--------|------|
| 容量 (Capacity) | 16 MB ~ 128 MB | 通过 JEDEC ID 识别 |
| 扇区大小 (Sector Size) | 4 KB ~ 64 KB | 最小擦除单元 |
| 页大小 (Page Size) | 256 bytes | 最小编程单元 |
| 擦除次数 | ~100,000 次 | 每扇区生命周期 |

### 2.2 操作约束 / Operation Constraints

SPI NOR Flash 有三条关键规则：

1. **擦除优先**: 只能将 bit 从 0 写为 1 通过擦除操作；编程只能将 1 写为 0
2. **按扇区擦除**: 擦除操作以整个扇区为单位（通常 4 KB）
3. **按页编程**: 编程操作以页为单位（通常 256 bytes），且不能跨越页边界

### 2.3 命令集 / Command Set

| 命令 | 字节码 | 描述 |
|------|--------|------|
| READ (03h) | 0x03 | 读取数据，需要 3 字节地址 |
| WREN (06h) | 0x06 | 写使能，设置 WEL 位 |
| SE (D8h) | 0xD8 | 扇区擦除（4 KB 扇区） |
| RDSR (05h) | 0x05 | 读状态寄存器 |

### 2.4 状态寄存器 / Status Register

```
Bit 0 (BUSY):  1 = 设备忙（正在执行擦除/编程）
Bit 1 (WEL):   1 = 写使能锁存已置位
Bit 2-7:       保留
```

在执行任何写入/擦除操作前，固件必须：
1. 发送 WREN 命令设置 WEL 位
2. 发送擦除/编程命令
3. 轮询 BUSY 位直到操作完成

---

## 3. 固件镜像布局 / Firmware Image Layout

### 3.1 FirmwareImage 结构 / FirmwareImage Structure

在 `firmware_layout.h` 中定义的 `FirmwareImage` 结构体对应典型的固件镜像：

```c
typedef struct {
    uint32_t base_addr;      // 基地址，例如 0xFFF00000 (x86) 或 0x08000000 (ARM)
    uint32_t entry_point;    // 入口点，CPU 上电后跳转的地址
    FirmwareSection text_section;    // .text  — 可执行代码
    FirmwareSection rodata_section;  // .rodata — 只读数据（字符串、常量）
    FirmwareSection data_section;    // .data — 已初始化全局变量
    FirmwareSection bss_section;     // .bss — 未初始化全局变量（运行时清零）
} FirmwareImage;
```

### 3.2 各段功能 / Section Functions

#### .text（代码段）
- 包含固件的可执行机器码
- 通常映射到 ROM/Flash 的只读区域
- 在 x86 中，.text 段通常从 4 GB 地址空间的顶部向下增长
- ARM TF-A 中，BL1 代码从 0x08000000 开始

#### .rodata（只读数据段）
- 字符串常量、查表、配置数据
- 与 .text 段紧密相邻，共享相同的只读保护属性
- 可放在单独的区域以支持 flash 的读保护

#### .data（已初始化数据段）
- 初始值非零的全局/静态变量
- 在 Flash 中存储初始值，启动时复制到 RAM
- 这称为"数据重定位" (data relocation)

#### .bss（未初始化数据段）
- 初始值为零的全局/静态变量
- 不在 Flash 中占用实际空间，只有大小信息
- 启动时由 CRT 清零

---

## 4. 固件验证 / Firmware Validation

### 4.1 头部验证 / Header Validation

`fw_validate_header()` 执行以下检查：

1. 基地址不能为零
2. 入口点必须在基地址之后
3. 各段不能重叠
4. 段偏移必须按升序排列 (.text < .rodata < .data)

实际系统中通常使用更严格的验证：

- **CRC32/CRC16**: 校验整个固件映像完整性
- **RSA 签名**: 使用 OEM 公钥验证固件签名（安全启动）
- **版本检查**: 确保固件版本 >= 最低要求的版本（防回滚保护）

### 4.2 入口点定位 / Entry Point Location

`fw_find_entry_point()` 返回固件的入口点地址。在不同架构中：

- **x86**: 入口点通常是 16 位实模式代码，复位向量 (0xFFFFFFF0) 跳转至此
- **ARM**: 入口点是异常向量表的第一个入口（Reset vector at offset 0x00）
- **RISC-V**: 入口点在 `mtvec` CSR 指定的地址

---

## 5. Intel FSP 布局 / Intel FSP Layout

### 5.1 FSP 组件 / FSP Components

Intel Firmware Support Package (FSP) 是固件栈中的关键组件：

```
+---------------------------+  <- Flash Top
| FSP-T (Temp RAM Init)     |  初始化 CAR (Cache-As-RAM)
+---------------------------+
| FSP-M (Memory Init)       |  DDR 内存训练和初始化
+---------------------------+
| FSP-S (Silicon Init)      |  芯片组/CPU 初始化
+---------------------------+
| UEFI PI Firmware Volume   |  PEI/DXE 阶段
+---------------------------+
| UEFI Boot Manager         |  BDS 阶段
+---------------------------+
```

### 5.2 FSP 头部 / FSP Header

FSP 二进制镜像在开头有一个标准化头部：

| 偏移 | 字段 | 说明 |
|------|------|------|
| 0x00 | Signature | "FSPH" |
| 0x0C | ImageBase | 加载基地址 |
| 0x10 | ImageSize | 镜像总大小 |
| 0x28 | FspMemoryInitEntry | FSP-M 入口点偏移 |
| 0x30 | TempRamInitEntry | FSP-T 入口点偏移 |
| 0x38 | FspSiliconInitEntry | FSP-S 入口点偏移 |

---

## 6. ARM Trusted Firmware-A 布局 / ARM TF-A Layout

### 6.1 TF-A 启动流程 / TF-A Boot Flow

```
BL1 (ROM) -> BL2 (Flash) -> BL31 (EL3 Runtime) -> BL33 (UEFI/Uboot)
                |
                +--> BL32 (TEE/OP-TEE at S-EL1)
```

### 6.2 内存布局 / Memory Layout

```
0x00000000  +-------------------+
            | BL1 (boot ROM)    |  4-64 KB, 来自 SoC 内部 ROM
            +-------------------+
            | BL2               |  ~48 KB, 镜像验证和加载
            +-------------------+
            | BL31              |  运行时服务 (PSCI, SDEI)
            +-------------------+
            | BL32 (optional)   |  TEE OS, OP-TEE
            +-------------------+
            | BL33              |  Non-secure 固件 (U-Boot/EDK2)
            +-------------------+
```

### 6.3 FIP (Firmware Image Package)

ARM TF-A 使用 FIP 格式打包多个镜像：

```
+-------------------+
| FIP Header        |  UUID, flags, size
+-------------------+
| ToC Entry 0       |  BL2 image descriptor
+-------------------+
| ToC Entry 1       |  BL31 image descriptor
+-------------------+
| ...               |
+-------------------+
| BL2 Payload       |  实际的 BL2 镜像数据
+-------------------+
| BL31 Payload      |  实际的 BL31 镜像数据
+-------------------+
```

---

## 7. 启动流程模拟 / Boot Flow Simulation

### 7.1 简化启动序列 / Simplified Boot Sequence

本项目的 `flash_boot_demo.c` 模拟了以下流程：

1. **上电 (Power-On)**: Flash 设备初始化, CPU 被复位
2. **读取复位向量**: CPU 从 0xFFFFFFF0 读取跳转指令
3. **加载固件头部**: 从 Flash 读取 FirmwareImage 结构
4. **验证头部**: 调用 fw_validate_header()
5. **模式切换**: CPU 从实模式切换到保护模式再到长模式
6. **跳转到入口点**: 固件开始执行

### 7.2 实际硬件差异 / Real Hardware Differences

| 步骤 | 模拟 | 实际硬件 |
|------|------|----------|
| CPU 复位 | 调用 cpu_reset() 设置寄存器 | 硬件 RESET# 引脚触发微码 |
| Flash 读取 | 内存访问 flash.data 数组 | I/O 映射或 SPI 控制器 |
| 模式切换 | 直接设置 CR0/CR4 | 需要设置 GDT/IDT，远跳转指令 |
| 缓存 | 不实现 | 需要启用/禁用 cache、设置 MTRR |

---

## 8. 参考资源 / References

- Intel Firmware Support Package (FSP) Integration Guide
- ARM Trusted Firmware-A Design Document
- UEFI Platform Initialization Specification v1.8
- JEDEC JESD216: Serial Flash Discoverable Parameters (SFDP)
- coreboot Documentation: flashmap layout format
