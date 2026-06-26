# ACPI 深度解析

## 1. ACPI 简介

ACPI (Advanced Configuration and Power Interface) 是 x86 平台和 ARM 服务器上的核心系统固件接口标准。它定义了操作系统与固件之间的交互方式，涵盖电源管理、设备枚举、热管理、NUMA 拓扑等多个方面。

与 Device Tree 不同，ACPI 使用字节码 (AML) 而非声明式数据来描述硬件，AML 由操作系统内的 ACPI 驱动程序解释执行。

### ACPI 在系统中的位置

```
Hardware Platform
      ↓
  Firmware (UEFI/BIOS)
      ↓  ACPI Tables in memory
  OS ACPI Driver (AML Interpreter)
      ↓
  ACPI Namespace  (DSDT + SSDTs)
      ↓
  OS Kernel (Device & Power Management)
```

---

## 2. ACPI 表层次结构

### RSDP → XSDT → Everything

```
RSDP (Root System Description Pointer)
 ├── revision=1 → RSDT (32-bit addresses)
 └── revision=2 → XSDT (64-bit addresses)
                    ├── FADT (Fixed ACPI Description Table)
                    ├── DSDT (Differentiated System Description Table)
                    ├── SSDT (Secondary System Description Table) ×N
                    ├── MADT (Multiple APIC Description Table)
                    ├── MCFG (PCI Memory-mapped Configuration)
                    ├── HPET (High Precision Event Timer)
                    ├── BGRT (Boot Graphics Resource Table)
                    ├── SPCR (Serial Port Console Redirection)
                    ├── DBG2 (Debug Port Table 2)
                    ├── SRAT (System Resource Affinity Table)
                    ├── SLIT (System Locality Information Table)
                    ├── DMAR (DMA Remapping Table)
                    ├── NHLT (Non-HD Audio Link Table)
                    └── ... (many more)
```

### 引导发现流程

1. **BIOS/UEFI** 在物理内存中构建 ACPI 表。
2. **RSDP 定位**: OS 搜索 EBDA (Extended BIOS Data Area) 或 0x000E0000-0x000FFFFF 范围，寻找签名 `"RSD PTR "`。
3. **验证 RSDP**: 校验 v1 的 20 字节校验和，或 v2+ 的 36 字节校验和。
4. **解析 XSDT/RSDT**: 从 RSDP 获取 XSDT 地址 (64-bit) 或 RSDT 地址 (32-bit)。
5. **枚举表**: XSDT/RSDT 包含指向其他 ACPI 表的指针数组。

---

## 3. RSDP 结构

### RSDP v1 (20 字节)

| 偏移 | 大小 | 字段 | 说明 |
|------|------|------|------|
| 0 | 8 | Signature | `"RSD PTR "` (末尾有空格) |
| 8 | 1 | Checksum | 前 20 字节校验和为 0 |
| 9 | 6 | OEMID | OEM 标识符 |
| 15 | 1 | Revision | 0=v1.0, 2=v2.0+ |
| 16 | 4 | RsdtAddress | RSDT 的 32 位物理地址 |

### RSDP v2 (36 字节, 扩展)

| 偏移 | 大小 | 字段 |
|------|------|------|
| 20 | 4 | Length |
| 24 | 8 | XsdtAddress (64-bit) |
| 32 | 1 | ExtendedChecksum |
| 33 | 3 | Reserved |

---

## 4. 关键 ACPI 表详解

### FADT (Fixed ACPI Description Table)

FADT 是电源管理和硬件寄存器访问的核心表。它定义了：

- **ACPI Hardware Register Blocks** (PM1a_EVT, PM1a_CNT, PM_TMR, GPE blocks)
- **SCI 中断号** (System Control Interrupt, 通常 IRQ 9)
- **SMI 命令端口** (System Management Interrupt port, 通常 0xB2)
- **Sleep 控制** (SLP_TYPa, SLP_EN bits)
- **RESET_REG** (硬件复位寄存器描述)
- **FACS/DSDT 地址**
- **Preferred PM Profile** (Desktop/Mobile/Workstation/Server/Tablet 等)

```c
// 常用 FADT 字段 (简化)
typedef struct {
    uint32_t SCI_INT;          // SCI 中断向量
    uint32_t SMI_CMD;          // SMI 命令端口
    uint8_t  ACPI_ENABLE;      // 写此值到 SMI_CMD 启用 ACPI
    uint8_t  ACPI_DISABLE;     // 写此值到 SMI_CMD 禁用 ACPI
    uint32_t PM1a_EVT_BLK;     // PM1 事件寄存器块
    uint32_t PM1a_CNT_BLK;     // PM1 控制寄存器块
    uint32_t PM_TMR_BLK;       // PM 定时器块
    uint32_t GPE0_BLK;         // 通用事件寄存器块 0
    uint32_t GPE1_BLK;         // 通用事件寄存器块 1
    uint8_t  PM1_EVT_LEN;      // PM1 事件寄存器长度
    uint8_t  PM1_CNT_LEN;      // PM1 控制寄存器长度
    uint8_t  PM_TMR_LEN;       // PM 定时器长度
} FADTFields;
```

### MADT (Multiple APIC Description Table)

MADT 描述系统中的中断控制器和 APIC 配置：

| 条目类型 | 类型 ID | 内容 |
|----------|---------|------|
| Processor Local APIC | 0x00 | ACPI ID, APIC ID, Flags |
| I/O APIC | 0x01 | APIC ID, Address, GSI Base |
| Interrupt Source Override | 0x02 | Bus, Source, GSI, Flags |
| NMI Source | 0x03 | Flags, GSI |
| Local APIC NMI | 0x04 | ACPI ID, Flags, LINT# |
| Local APIC Address Override | 0x05 | 64-bit address |
| I/O SAPIC | 0x06 | SAPIC ID, Address, GSI Base |
| Local SAPIC | 0x07 | ACPI ID, APIC ID/SAPIC EID |
| Platform Interrupt Sources | 0x08 | Flags, Type, ID, eid, iosapic vector, gsi, pis flags |
| Processor Local x2APIC | 0x09 | x2APIC ID, Flags, ACPI UID |
| GICC (ARM GIC CPU Interface) | 0x0B | CPU Interface Number, ACPI UID, Flags, Parking Protocol Version, GICC Address |
| GICD (ARM GIC Distributor) | 0x0C | Physical Address, System Vector Base |
| GIC MSI Frame | 0x0D | Frame ID, Physical Address, Flags, SPI Count/Base |
| GICR (GIC Redistributor) | 0x0E | Discovery Range Base/Size |
| GIC ITS | 0x0F | ITS ID, Physical Address |

### MCFG (PCI Express Memory-mapped Configuration)

MCFG 提供 PCIe ECAM (Enhanced Configuration Access Mechanism) 地址映射：

```c
typedef struct {
    uint64_t BaseAddress;      // PCIe 配置空间基址 (如 0xE0000000)
    uint16_t PciSegmentGroup;  // 段组号
    uint8_t  StartBusNumber;   // 起始总线号
    uint8_t  EndBusNumber;     // 终止总线号
    uint32_t Reserved;
} MCFGEntry;
```

### HPET (High Precision Event Timer)

```c
typedef struct {
    // Header
    uint32_t EventTimerBlockId;
    // bit[15:0] = Vendor ID
    // bit[31:16] = Legacy IRQ Routing Capable + Counter Size Capability + NumComparators + HardwareRevID
    uint8_t  AddressSpaceID;   // 0=System Memory, 1=System I/O
    uint8_t  RegisterBitWidth;
    uint8_t  RegisterBitOffset;
    uint64_t Address;          // HPET 寄存器基址 (MMIO)
    uint8_t  HpetNumber;
    uint16_t MinimumTick;
    uint8_t  PageProtection;
} HPETTable;
```

### BGRT (Boot Graphics Resource Table)

```c
typedef struct {
    uint16_t Version;
    uint8_t  Status;        // 0=Invalid, 1=Valid/Displayed
    uint8_t  ImageType;     // 0=Bitmap
    uint64_t ImageAddress;  // 图像数据的物理地址
    uint32_t ImageOffsetX;
    uint32_t ImageOffsetY;
} BGRTTable;
```

---

## 5. AML (ACPI Machine Language) 字节码

### AML 是什么

AML 是 ACPI 命名空间中的对象和控制方法的伪代码字节流。它是平台无关的，由平台固件编译，由操作系统解释。

### ASL → AML 编译流程

```bash
# 编写 ASL 源文件
# dsdt.asl
# DEFINE_BLOCK(DEF_DSDT, "DSDT.AML", 0x02, "NANO", "NANODSDT", 0x00000001)

# 编译为 AML
iasl dsdt.asl

# 生成 dsdt.aml (AML 二进制)

# 反编译
iasl -d dsdt.aml
# 生成 dsdt.dsl (ASL 源文件)
```

### ASL 基本语法

```asl
DefinitionBlock ("dsdt.aml", "DSDT", 2, "NANO", "NANODSDT", 0x00000001)
{
    // Scope: 命名空间容器
    Scope (\_SB)
    {
        // Device: 设备对象
        Device (PCI0)
        {
            Name (_HID, EisaId ("PNP0A08"))  // PCI Host Bridge
            Name (_CID, EisaId ("PNP0A03"))
            Name (_ADR, 0x00000000)

            // _CRS: 当前资源设置
            Method (_CRS, 0, Serialized)
            {
                Name (BUF0, ResourceTemplate ()
                {
                    WordBusNumber (ResourceConsumer, MinFixed, MaxFixed, PosDecode,
                        0x0000,
                        0x0000,
                        0x00FF,
                        0x0000,
                        0x0100)
                    DWordMemory (ResourceProducer, PosDecode, MinFixed, MaxFixed,
                        Cacheable, ReadWrite,
                        0x00000000,
                        0x000A0000,
                        0x000BFFFF,
                        0x00000000,
                        0x00020000)
                })
                Return (BUF0)
            }

            Method (_STA, 0, NotSerialized)
            {
                Return (0x0F)  // Present, Enabled, Shown, Functional
            }
        }
    }
}
```

### 核心 AML 操作码分类

| 类别 | 操作码 | 说明 |
|------|--------|------|
| **数据** | Zero, One, Ones, BytePrefix, WordPrefix, DWordPrefix, QWordPrefix, StringPrefix | 立即数据类型 |
| **命名** | Name, Scope, Device, Processor, PowerRes, ThermalZone | 命名空间对象声明 |
| **Method** | Method, Return, Break, Continue | 控制方法定义 |
| **算术** | Add, Subtract, Multiply, Divide, Mod, ShiftLeft, ShiftRight, And, Nand, Or, Nor, Xor, Not, FindSetLeftBit, FindSetRightBit | 算术和位运算 |
| **逻辑** | LAnd, LOr, LNot, LEqual, LGreater, LLess | 逻辑比较 |
| **类型转换** | ToInteger, ToBuffer, ToDecimalString, ToHexString, ToString, CopyObject, Mid | 类型与字符串转换 |
| **控制流** | If, Else, While, BreakPoint, Stall, Sleep | 条件和循环 |
| **Store** | Store, RefOf, DerefOf, Index | 赋值与引用 |
| **包操作** | Package, VarPackage | 创建包 (类似数组) |
| **缓冲操作** | Buffer, CreateDWordField, CreateWordField, CreateByteField, CreateBitField, CreateQWordField | 缓冲区与字段访问 |
| **字符串** | Concat, ConcatRes, SizeOf, ObjectType | 字符串操作 |
| **资源** | IRQ, IO, FixedIO, DMA, Memory24, Memory32, FixedMemory32, DWordIO, DWordMemory, QWordIO, QWordMemory, ExtendedIRQ, StartDependentFn, EndDependentFn | 硬件资源描述符 |

---

## 6. AML 解释器设计

### 关键数据结构

```c
// AML 上下文
AMLContext {
    bytecode, pos          // 字节码指针和当前位置
    scope_stack, var_stack // 作用域和变量堆栈
    method_stack           // 方法调用栈
    return_pending         // RETURN 操作码标志
}

// 命名空间作用域
AMLScope {
    name                   // 作用域名称
    variables[256]         // 局部变量
    methods[64]            // 方法定义
}
```

### 解释器主循环

```
while pos < bytecode_size:
    opcode = read_byte()
    switch opcode:
        case IF_OP:
            eval_condition()
            if true: parse_body() else: skip_to_else_or_end()
        case WHILE_OP:
            while eval_condition(): parse_body()
        case METHOD_OP:
            define_method()  // 注册方法，不执行
        case STORE_OP:
            eval_source(); eval_destination(); store_value()
        case ADD_OP ... XOR_OP:
            pop_b(); pop_a(); push(a op b)
        case RETURN_OP:
            set return_pending = true; break
        case NAME_OP:
            read_name(); parse_data_object(); register_variable()
```

### 特殊对象解析

**_CRS (Current Resource Settings)**:
AML 在 CRT 缓冲区中编码硬件资源 (IO 端口、MMIO 范围、IRQ、DMA 通道) 作为字节序列。解释器必须识别 `ResourceTemplate()` 中的资源描述符类型 (0x47=IO Port, 0x81=Memory24, 0x89=ExtIRQ 等)。

**_STA (Status)**:
简单的 Bitmap: bit[0]=Present, bit[1]=Enabled, bit[2]=Shown in UI, bit[3]=Functional。通常返回 `0x0F` (全功能)。

**_PRT (PCI Routing Table)**:
由 PCI 设备 (INT A/B/C/D) 到中断控制器输入的映射包数组。

---

## 7. IASL 编译器

Intel ASL Compiler (IASL) 是 ACPICA 项目的一部分：

### 常用命令

```bash
# 编译 ASL → AML
iasl -ve dsdt.asl          # -ve: verbose errors only

# 反编译 AML → ASL
iasl -d dsdt.aml

# 编译时生成 C 头文件 (嵌入固件)
iasl -tc dsdt.asl

# 查看 AML 统计
iasl -l dsdt.aml

# 将 AML 嵌入为 C 数组
iasl -oa dsdt.aml          # 生成 dsdt.hex
```

### 生成示例

```c
// iasl -tc dsdt.asl 生成
unsigned char AmlCode[] =
{
    0x44,0x53,0x44,0x54,0x90,0x00,0x00,0x00,  /* DSDT.... */
    0x02,0xA0,0x4E,0x41,0x4E,0x4F,0x00,0x00,  /* ..NANO.. */
    0x4E,0x41,0x4E,0x4F,0x44,0x53,0x44,0x54,  /* NANODSDT */
    ...
};
```

---

## 8. 校验和验证

ACPI 所有表都使用简单的 8 位反码校验和：

```c
bool acpi_validate_checksum(const uint8_t *table, uint32_t length) {
    uint8_t sum = 0;
    for (uint32_t i = 0; i < length; i++) sum += table[i];
    return sum == 0;  // 所有字节之和的低 8 位必须为 0
}
```

**重要**:
- RSDP v1 校验和覆盖 20 字节
- RSDP v2 增加扩展校验和 (覆盖全部 36 字节)
- RSDT/XSDT 各自有独立的校验和字段
- 所有 ACPI 表头 (ACPISDTHeader) 都包含校验和字段

---

## 9. ACPI 命名空间

```
\                       // 根命名空间
├── _PR                 // 处理器命名空间
│   ├── CPU0            // 处理器 0
│   │   ├── _PCT        // 性能控制
│   │   ├── _PSS        // 性能状态
│   │   └── _CST        // C 状态
│   └── CPU1 ... CPUn
├── _SB                 // 系统总线
│   ├── PCI0            // PCI 总线 0
│   │   ├── _HID        // PNP0A08 (PCI Host Bridge)
│   │   ├── _CRS        // 总线资源
│   │   ├── _PRT        // PCI IRQ 路由表
│   │   └── ...         // 下游设备
│   └── ...
├── _TZ                 // 热区
├── _SI                 // 系统指示器
└── _GPE                // 通用事件
    ├── _Lxx            // Level-triggered 事件方法
    └── _Exx            // Edge-triggered 事件方法
```

---

## 10. mini-hardware-desc ACPI 实现要点

### acpi_tables.c

- **acpi_find_rsdp()**: 在 BIOS 内存区域 (0xE0000-0xFFFFF) 每 16 字节扫描 `"RSD PTR "` 签名，验证 v1 或 v2 校验和。
- **acpi_parse_xsdt()**: 从 64-bit XSDT 地址读取条目，解析每个条目的 ACPI 表头 (signature, length, revision)。
- **acpi_validate_checksum()**: 对任意 ACPI 表进行 8-bit 总和校验。
- **acpi_print_tables()**: 格式化输出所有已发现表及其大小和描述。

### acpi_aml.c

- 实现精简的 AML 字节码解释器，支持约 40+ 个操作码。
- 核心循环: 逐字节读取、解码操作码、执行语义动作。
- 支持 If/Else 条件分支 (读取 PkgLength 计算正确的跳转偏移)。
- 实现算术/逻辑运算栈机模型。
- Store 操作将值写入命名变量或方法局部/参数。
- 提供 _CRS、_STA、_PRT 的简化占位实现。

---

## 引用

- [ACPI Specification 6.5](https://uefi.org/specifications) (UEFI Forum)
- [ACPICA (ACPI Component Architecture)](https://www.acpica.org/)
- [IASL Compiler User Guide](https://acpica.org/documentation/)
- [Linux ACPI Documentation](https://www.kernel.org/doc/html/latest/firmware-guide/acpi/)
- [UEFI ACPI 指南](https://uefi.org/acpi)
