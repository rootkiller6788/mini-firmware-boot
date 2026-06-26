# mini-hardware-desc — 硬件描述 (C 语言实现)

> 参考 devicetree.org Specification, ACPI Specification 6.5, SMBIOS Spec

`mini-hardware-desc` 是一个纯 C99 实现的硬件描述解析库，提供五种核心固件硬件描述机制的解析与操作能力：Device Tree (FDT)、ACPI Tables、ACPI AML 字节码解释器、SMBIOS 表，以及 UEFI PEI→DXE 阶段使用的 HOB (Hand-Off Block)。

该项目设计用于嵌入式系统、固件开发、Bootloader、模拟器以及系统编程教学场景。仅依赖 `libc` 和 `libm`，无需任何第三方库，可直接集成到 Bare-metal 或 RTOS 环境中。

---

## 模块概览

| # | 模块 | 头文件 | 源文件 | 对应规范 | 描述 |
|---|------|--------|--------|----------|------|
| 1 | Device Tree (FDT) | `include/device_tree.h` | `src/device_tree.c` | devicetree.org v0.4 | 扁平化设备树解析、节点遍历、属性读取、兼容性查找 |
| 2 | ACPI Tables | `include/acpi_tables.h` | `src/acpi_tables.c` | ACPI 6.5 §5.2 | RSDP/XSDT/RSDT 解析、表查找、校验和验证、FADT/MADT/MCFG/HPET/BGRT |
| 3 | ACPI AML 解释器 | `include/acpi_aml.h` | `src/acpi_aml.c` | ACPI 6.5 §19-20 | AML 字节码解析、Method 调用、Store 操作、算术/逻辑运算、If/While 控制流、_CRS/_STA 解析 |
| 4 | SMBIOS | `include/smbios.h` | `src/smbios.c` | SMBIOS 3.7 | BIOS/System/Baseboard/Processor/Memory/Cache 信息打印，32/64 位入口点兼容 |
| 5 | HOB (Hand-Off Block) | `include/hob.h` | `src/hob.c` | PI Spec v1.8 Vol 3 §4 | PEI→DXE 握手机制，PHIT/Memory Alloc/Resource Desc/FV/CPU HOB 构建与查询 |

---

## 编译与运行

### 需求
- GCC (MinGW on Windows, or native on Linux)
- GNU Make
- 仅依赖 libc (`stdlib.h`, `stdio.h`, `string.h`) 和 libm

### 构建

```bash
make clean && make all
```

输出在 `bin/` 目录下：
- `fdt_parse_demo.exe` — 设备树解析演示
- `acpi_tables_demo.exe` — ACPI 表解析与校验和验证演示
- `smbios_demo.exe` — SMBIOS 系统信息打印演示

### 运行演示

```bash
make run-fdt     # 运行设备树演示
make run-acpi    # 运行 ACPI 演示
make run-smbios  # 运行 SMBIOS 演示
```

---

## API 速览

### Device Tree (FDT)

```c
#include "device_tree.h"

FDTTree tree;
uint8_t *dtb = load_dtb_from_flash();

if (fdt_parse(&tree, dtb, dtb_size)) {
    // 按路径查找节点
    FDTNode *serial = fdt_find_node_by_path(&tree, "/soc/serial@1000");

    // 按 compatible 查找
    FDTNode *uart = fdt_find_compatible(tree.root, "ns16550a");

    // 打印整棵树
    fdt_print_tree(tree.root, 0);

    fdt_free_tree(&tree);
}
```

### ACPI Tables

```c
#include "acpi_tables.h"

ACPITableList list;
uint8_t *bios = (uint8_t *)0xE0000;

if (acpi_find_rsdp(&list, bios, 0x20000)) {
    acpi_parse_xsdt(&list, bios);
    acpi_print_tables(&list);

    ACPITableEntry fadt;
    if (acpi_find_table(&list, "FACP", &fadt)) {
        // 使用 FADT 进行电源管理配置
    }
}
```

### SMBIOS

```c
#include "smbios.h"

SMBIOSTable table;
uint8_t *smbios_data = (uint8_t *)0xF0000;

if (smbios_parse(&table, smbios_data, 0x10000)) {
    smbios_print_all(&table);      // 打印全部信息
    smbios_free_table(&table);
}
```

### HOB

```c
#include "hob.h"

HOBList hob_list;
hob_init(&hob_list, 0x100000000, 0x0, 0x100000000, 0x100000);
hob_add_memory_alloc(&hob_list, 0x100000, 0x10000, 0x07);
hob_add_resource_desc(&hob_list, HOB_RESOURCE_SYSTEM_MEMORY, 0, 0x0, 0x100000000);
hob_add_firmware_volume(&hob_list, 0xFF000000, 0x1000000);
hob_add_cpu(&hob_list, 64, 16);
hob_finalize(&hob_list);
hob_print_list(&hob_list);
```

---

## 目录结构

```
mini-hardware-desc/
├── include/
│   ├── device_tree.h       # FDT 解析器接口
│   ├── acpi_tables.h       # ACPI 表结构与查找
│   ├── acpi_aml.h          # AML 字节码解释器
│   ├── smbios.h            # SMBIOS 表解析与打印
│   └── hob.h               # UEFI HOB 构建接口
├── src/
│   ├── device_tree.c       # FDT 实现 (160+ 行)
│   ├── acpi_tables.c       # ACPI 表实现 (170+ 行)
│   ├── acpi_aml.c          # AML 解释器实现 (300+ 行)
│   ├── smbios.c            # SMBIOS 实现 (260+ 行)
│   └── hob.c               # HOB 实现 (200+ 行)
├── examples/
│   ├── fdt_parse_demo.c    # 设备树完整演示
│   ├── acpi_tables_demo.c  # ACPI 表完整演示
│   └── smbios_demo.c       # SMBIOS 完整演示
├── demos/
│   ├── mini-device-tree/
│   │   └── README.md       # 设备树深度解析 (250+ 行)
│   └── mini-acpi/
│       └── README.md       # ACPI 深度解析 (250+ 行)
├── docs/
│   ├── course-alignment.md         # 规范对齐说明
│   └── hardware-description-survey.md # 硬件描述技术综述
├── Makefile
└── README.md
```

---

## 规范覆盖

| 规范 | 版本 | 覆盖章节 |
|------|------|----------|
| Device Tree Specification | devicetree.org v0.4 | §2-5: DTS format, FDT binary structure, interrupts, memory |
| ACPI Specification | 6.5 (2022) | §5.2.5: RSDP, §5.2.7-8: RSDT/XSDT, §5.2.9: FADT, §5.2.12: MADT, §19-20: AML |
| SMBIOS Specification | 3.7 (2023) | §3: Entry Point, §7: Structure format, Type 0-17 definitions |
| UEFI PI Specification | 1.8 (2023) | Vol 3 §4: HOB definitions, types, usage |

---

## 代码规范

- **C99** 标准
- 仅依赖 `libc` (`stdlib.h`, `stdio.h`, `string.h`, `stdbool.h`, `stdint.h`) 和 `libm`
- 函数命名: `snake_case` (如 `fdt_parse`, `acpi_find_rsdp`)
- 类型命名: `PascalCase` (如 `FDTHeader`, `ACPITableList`)
- 常量: `UPPER_SNAKE_CASE` (如 `FDT_MAGIC`, `AML_IF_OP`)
- 头文件保护: `#ifndef X_H` / `#define X_H` / `#endif`
- 所有头文件包含 `<stdbool.h>`

## 许可证

本项目的代码和文档仅供学习、研究和教学用途。
