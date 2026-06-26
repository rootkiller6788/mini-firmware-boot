# Firmware Architecture — 固件架构

> 硬件初始化 → 固件 → 引导加载程序 → 内核

---

## 1. 固件栈概览 / Firmware Stack Overview

```
+-----------------------------------------------------------+
|                     Operating System                       |
+-----------------------------------------------------------+
|                   Bootloader (GRUB / U-Boot)               |
+-----------------------------------------------------------+
|            DXE / BDS (UEFI Boot Manager)                   |
+-----------------------------------------------------------+
|    PEI / SEC (Pre-EFI / Security Phase)                    |
+-----------------------------------------------------------+
|  FSP / AGESA / SBI / TF-A  (Platform Init)                 |
+-----------------------------------------------------------+
|                     Hardware                               |
|  CPU | Chipset | Memory | SPI Flash | Peripherals           |
+-----------------------------------------------------------+
```

---

## 2. 硬件初始化阶段 / Hardware Init Phase

### 2.1 上电序列 / Power-On Sequence

1. **电源稳定**: 各电压轨达到稳定值 (Vcore, VIO, VDIMM)
2. **时钟稳定**: 晶振起振, PLL 锁定
3. **复位释放**: RESET# 信号从低变高
4. **CPU 初始化**: 微码执行内部初始化, 加载复位向量

### 2.2 CPU 早期初始化 / Early CPU Init

```
1. Cache-As-RAM (CAR) setup
   - 配置 MTRR 将一部分 cache 映射为可寻址空间
   - 在 DDR 初始化完成前提供临时栈

2. BIST (Built-In Self Test)
   - CPU 内部自检
   - 核心数检测

3. Microcode update
   - 从 Flash 加载最新微码补丁
   - 应用到所有核心
```

---

## 3. 平台初始化框架 / Platform Initialization Frameworks

### 3.1 Intel FSP

Intel Firmware Support Package 是 Intel 提供给 IBV/OEM 的二进制模块：

| 组件 | 功能 | 内存需求 |
|------|------|----------|
| FSP-T | 临时 RAM 初始化 (CAR) | ~16 KB |
| FSP-M | 内存控制器初始化, DDR 训练 | ~256 KB |
| FSP-S | 芯片组/硅片初始化 | ~128 KB |

#### FSP 集成流程:

```
Boot Loader
    |
    v
FSP-T entry (TempRamInit)
    | CAR 可用
    v
FSP-M entry (FspMemoryInit)
    | DDR 可用
    v
FSP-S entry (FspSiliconInit)
    | 芯片组初始化完成
    v
Boot Loader 继续 -> 加载 OS
```

### 3.2 AMD AGESA

AMD Generic Encapsulated Software Architecture (AGESA) 是 AMD 平台的初始化框架：

```
AGESA 组件:
  - AMD Reset Vector Module
  - AMD Core Module (CPU, HTT, NB, DRAM)
  - AMD PSP (Platform Security Processor) driver
  - AMD SMU (System Management Unit) firmware
```

#### AGESA 启动流程:

```
Reset -> PSP Boot -> AGESA Boot Loader (ABL)
    -> Memory Init (AGESA MemInit)
    -> PCIe Init
    -> SMU Init
    -> Handoff to UEFI (EDK2)
```

### 3.3 RISC-V SBI

RISC-V Supervisor Binary Interface (SBI) 运行在 M-mode：

```
M-mode (Machine):
  - SBI firmware (OpenSBI / RustSBI)
  - Timer, IPI, RFENCE, HSM
  - Physical memory protection (PMP)
  ↓
S-mode (Supervisor):
  - Linux / FreeBSD kernel
  - Uses ECALL for SBI services
```

| SBI Extension | EID | 功能 |
|---------------|-----|------|
| TIME | 0x54494D45 | 设置/获取定时器 |
| IPI | 0x735049 | 核间中断 |
| RFENCE | 0x52464E43 | 远程 TLB 刷新 |
| HSM | 0x48534D | Hart 状态管理 |

---

## 4. 引导加载程序 / Bootloader

### 4.1 U-Boot (ARM 平台常用)

```
U-Boot SPL (Secondary Program Loader):
  - 最小化初始化 (串口, DDR, 存储)
  - 从 SD/eMMC/SPI Flash 加载完整 U-Boot
  ↓
U-Boot Proper:
  - 完整设备树支持
  - 网络启动 (TFTP/NFS)
  - 加载 Linux 内核 (FIT Image 或 zImage)
```

### 4.2 GRUB2 (x86 平台常用)

```
GRUB2 模块化架构:
  kernel.img  -> core.img -> /boot/grub/*.mod
      |             |              |
   磁盘核心   文件系统驱动   菜单/网络/加密

启动命令:
  linux /boot/vmlinuz root=/dev/sda1
  initrd /boot/initrd.img
  boot
```

---

## 5. 安全启动链 / Secure Boot Chain

### 5.1 x86 安全启动

```
Hardware Root of Trust (Intel Boot Guard):
  1. CPU 微码验证 ACM (Authenticated Code Module) 签名
  2. ACM 测量并验证 IBB (Initial Boot Block)
  3. IBB 验证后续阶段 (PEI -> DXE -> Bootloader)
  4. Bootloader 验证 OS 内核签名

完整性度量链:
  PCR[0] = hash(ACM)
  PCR[1] = hash(IBB)
  PCR[2] = hash(PEI)
  PCR[4] = hash(Bootloader)
  PCR[7] = hash(UEFI Secure Boot Policy)
```

### 5.2 ARM 安全启动

```
ARM Trusted Boot Flow:
  BL1 (in ROM, immutable) -> verify BL2 signature
  BL2 -> verify BL31, BL32, BL33 signatures
  BL31 -> EL3 runtime, PSCI
  BL33 (U-Boot/EDK2) -> verify kernel FIT image

信任根:
  - ROTPK (Root of Trust Public Key) 存储在 eFuse
  - nv-counters 用于防回滚保护
```

---

## 6. 内存映射 / Memory Map

### 6.1 典型 x86 物理内存映射

```
0x00000000 - 0x000FFFFF  传统区域 (IVT, BDA, EBDA)
  0x000A0000 - 0x000BFFFF  VGA framebuffer
  0x000C0000 - 0x000FFFFF  BIOS ROM 映射

0x00100000 - 0x7FFFFFFF  DRAM (低 2 GB)

0x80000000 - 0xDFFFFFFF  MMIO 空间
  0xFED00000  HPET
  0xFEC00000  IOAPIC
  0xFEE00000  LAPIC

0xE0000000 - 0xFFFFFFFF  Firmware (if 4 GB RAM)
  or DRAM (if > 4 GB RAM installed)

> 4 GB 区域通过 remap 映射到 MMIO 之上
```

### 6.2 ARM 系统内存映射

```
0x00000000 - 0x7FFFFFFF  DRAM
0x08000000 - 0x0800FFFF  Boot ROM / On-chip Flash

0x40000000 - 0x5FFFFFFF  AHB/APB 外设总线
  0x40000000  Timer
  0x40001000  UART
  0x40002000  GPIO
  0x40003000  SPI Controller

0xE0000000 - 0xFFFFFFFF  Vendor-specific (debug, ETM)
```

---

## 7. 本项目的架构位置 / Where This Project Fits

```
mini-firmware 覆盖范围:
  +---------------------------------------------------+
  |     OS Kernel                                     |
  +---------------------------------------------------+
  |     Bootloader (未来扩展)                          |
  +---------------------------------------------------+
  |     DXE/BDS  (未来扩展)                            |
  +---------------------------------------------------+
  | --> PEI/SEC  mini-firmware (当前)              <-- |
  |  - 复位向量处理 (reset_vector)                     |
  |  - Flash 驱动 (spi_nor)                            |
  |  - 固件布局管理 (firmware_layout)                  |
  |  - MMIO 设备抽象 (mmio)                            |
  |  - SMBIOS 表生成 (smbios_fw)                       |
  +---------------------------------------------------+
  |     Hardware (模拟)                                |
  +---------------------------------------------------+
```

---

## 8. 术语对照 / Glossary

| 英文 | 中文 | 说明 |
|------|------|------|
| Firmware | 固件 | 存储在非易失性存储器中的底层软件 |
| Bootloader | 引导加载程序 | 加载并启动操作系统内核 |
| Reset Vector | 复位向量 | CPU 上电后第一条指令地址 |
| SPI NOR Flash | SPI NOR 闪存 | 用于存储固件的非易失性存储器 |
| GDT | 全局描述符表 | x86 保护模式内存分段 |
| IDT | 中断描述符表 | x86 中断处理入口 |
| MMIO | 内存映射 I/O | 通过内存地址访问外设 |
| SMBIOS | 系统管理 BIOS | 固件提供的硬件信息表 |
| FSP | 固件支持包 | Intel 平台初始化二进制模块 |
| AGESA | AMD 通用封装软件架构 | AMD 平台初始化框架 |
| SBI | 监管者二进制接口 | RISC-V M-mode 到 S-mode 接口 |
| TF-A | ARM 可信固件-A | ARM 平台参考固件实现 |
| EL3 | 异常级别 3 | ARM 最高特权级别 |
| PSCI | 电源状态协调接口 | ARM 电源管理标准接口 |
| UEFI | 统一可扩展固件接口 | 现代固件接口标准 |
| PI | 平台初始化 | UEFI 固件启动阶段规范 |
