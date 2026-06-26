# 课程规范对齐说明

本文档将 `mini-hardware-desc` 项目的各模块与其对应的工业规范章节进行映射，方便学习时对照规范原文理解实现。

---

## 1. Device Tree (FDT) — devicetree.org Specification

| 规范章节 | 内容 | 本项目实现 |
|----------|------|------------|
| §2.1 概述 | Device Tree 用途与设计理念 | `README.md` - "设备树简介" |
| §2.2 DTS 语法 | /dts-v1/, 节点, 属性语法 | `demos/mini-device-tree/README.md` §2 |
| §2.3 DTS 版本标记 | "/dts-v1/" 声明 | `examples/fdt_parse_demo.c` - 构建示例 DTB |
| §3 DTC 编译器 | dtc 命令、编译流程 | `demo/README.md` §3 |
| §5.2 FDT 结构 | 头部、内存预留块、结构块、字符串块 | `include/device_tree.h` - FDTHeader, FDTNode |
| §5.3 块对齐 | 4 字节对齐 | `src/device_tree.c` - `fdt_parse_struct()` 对齐逻辑 |
| §5.3.1 结构块 | BEGIN_NODE/END_NODE/PROP/NOP/END 令牌 | FDT_BEGIN_NODE(0x01), FDT_END_NODE(0x02), FDT_PROP(0x03), FDT_NOP(0x04), FDT_END(0x09) |
| §5.3.4 字符串块 | 属性名通过偏移量引用 | `src/device_tree.c` - `fdt_tring()` 函数 |
| §5.4 内存预留块 | (address, size) 对 | off_mem_rsvmap 字段, 解析中跳过 |
| §6 Device Node 要求 | compatible, reg, interrupts, #address-cells, #size-cells | `fdt_find_compatible()`, `fdt_read_prop_u32()` |
| §7 Interrupts/Interrupt Nexus | interrupt-parent, interrupt-map | 节点遍历支持 |

---

## 2. ACPI Tables — ACPI Specification 6.5

| 规范章节 | 内容 | 本项目实现 |
|----------|------|------------|
| §5.2.1 ACPI System Description Tables | 表架构总览: RSDP→XSDT→DSDT/SSDT | `include/acpi_tables.h` |
| §5.2.5 Root System Description Pointer (RSDP) | Signature "RSD PTR ", checksum, RSDT/XSDT 地址 | RSDP 结构体, `acpi_find_rsdp()` |
| §5.2.5.3 RSDP v2 | length, xsdt_address, extended_checksum | RSDP 完整结构体, 双版本校验和验证 |
| §5.2.7 Root System Description Table (RSDT) | 32 位条目数组 | `acpi_parse_rsdt()` |
| §5.2.8 Extended System Description Table (XSDT) | 64 位条目数组 | `acpi_parse_xsdt()` |
| §5.2.9 Fixed ACPI Description Table (FADT) | FACP signature, PM registers, SCI_INT, RESET_REG | `examples/acpi_tables_demo.c` - 构建 FADT 示例 |
| §5.2.11 Differentiated System Description Table (DSDT) | 主 AML 块 | 通过 XSDT 条目发现 |
| §5.2.11.1 Secondary System Description Table (SSDT) | 多个辅助 AML 块 | 通过 signature "SSDT" 查找 |
| §5.2.12 Multiple APIC Description Table (MADT) | APIC 条目 (LAPIC, IOAPIC, Override) | `examples/acpi_tables_demo.c` - 构建 4 核 + IOAPIC + Override |
| §5.2.13 MCFG | PCIe ECAM 配置空间描述 | MCFG 表结构 |
| §5.2.17 HPET | High Precision Event Timer | HPET 表识别 |
| §5.2.22 BGRT | Boot Graphics Resource Table | BGRT 表识别 |
| §5.2.30 SPCR | Serial Port Console Redirection | SPCR 表识别 |
| §19 ACPI Source Language (ASL) Reference | 源语言语法 | `demos/mini-acpi/README.md` §5 |
| §20 ACPI Machine Language (AML) Specification | 字节码操作码定义 | `include/acpi_aml.h` - 全部主要操作码 |
| §20.2 AML Byte Stream Byte Values | 操作码枚举 | AML_ZERO_OP ~ AML_ONES_OP (40+ 操作码) |
| §20.2.5 Method Invocation | Method 调用语义 | `aml_invoke_method()`, `aml_execute_method()` |
| §20.2.6.1 Store Operator | Store 操作码语义 | `aml_store_value()`, STORE_OP |
| §20.2.6.2 Arithmetic Operators | Add/Subtract/Multiply/Divide/Mod 等 | `aml_execute_op()` - 算术处理 |
| §20.2.6.3 Logical Operators | LAnd/LOr/LNot/LEqual/LGreater/LLess | `aml_execute_op()` - 逻辑处理 |
| §20.2.7.1 If/Else Operators | If/Else 条件执行 | AML_IF_OP/AML_ELSE_OP 分支处理 |
| §20.2.7.2 While Operator | While 循环 | AML_WHILE_OP |
| §21 System Description Table Architectures | DSDT vs SSDT 区别 | 概念覆盖 |

---

## 3. ACPI AML — ACPI Specification 6.5 §19-20

| 规范章节 | 内容 | 本项目实现 |
|----------|------|------------|
| §19.6.48 Control Method Types | Method (Serialized/NotSerialized) | AMLMethod 结构体 |
| §19.6.100 Resource Template Macro | ResourceTemplate() 宏 | aml_parse_crs() - 资源描述符解析 |
| §20.2.1 Data Objects Encoding | Int/Str/Buf/Pkg 编码 | AMLValue, AML_VAL_* 枚举 |
| §20.2.2 Name Objects Encoding | NameString, NameSeg | aml_eval_name() |
| §20.2.3 Package Length Encoding | PkgLength 可变长度编码 | If/Else 的 PkgLength 解析 |
| §20.2.5.2 Method Object | MethodFlags, ArgCount, SerializeRule | AMLMethod.arg_count, needs_package |
| §20.2.5.3 Predefined Methods | _CRS, _STA, _PRT, _HID, _UID 等 | aml_parse_crs(), aml_parse_sta(), aml_parse_prt() |

---

## 4. SMBIOS — SMBIOS Specification 3.7

| 规范章节 | 内容 | 本项目实现 |
|----------|------|------------|
| §3.1 SMBIOS Entry Point Structure | _SM_ anchor, checksum, structure table addr | SMBIOSEntryPoint32 结构体 |
| §3.2 SMBIOS 3.0 Entry Point Structure | _SM3_ anchor, 64-bit addr | SMBIOSEntryPoint64 结构体 |
| §6.1 Structure Header Format | type, length, handle | SMBIOSStructure, SMBIOSParsedStructure |
| §7.1 Type 0 - BIOS Information | vendor, version, date, characteristics | smbios_print_bios() |
| §7.2 Type 1 - System Information | manufacturer, product, UUID, wakeup | smbios_print_system() |
| §7.3 Type 2 - Baseboard Information | manufacturer, product, version | smbios_print_baseboard() |
| §7.4 Type 3 - System Enclosure | manufacturer, type, version | smbios_print_chassis() |
| §7.5 Type 4 - Processor Information | socket, manufacturer, speed, core count | smbios_print_processor() |
| §7.8 Type 7 - Cache Information | socket, max/installed size, type | smbios_print_cache() |
| §7.18 Type 17 - Memory Device | device locator, size, speed, type | smbios_print_memory() |
| §6.2 Text Strings | 字符串表在结构化段之后，以双 null 结束 | smbios_parse_structure() - 字符串解析 |

---

## 5. HOB — UEFI PI Specification 1.8

| 规范章节 | 内容 | 本项目实现 |
|----------|------|------------|
| PI 1.8 Vol 3 §4.1 HOB Overview | HOB 是 PEI→DXE 握手机制 | `include/hob.h` - 模块概述 |
| PI 1.8 Vol 3 §4.2 HOB List | HOB 列表结构，必须以 PHIT 开始，以 End of List 结束 | hob_init(), hob_finalize() |
| PI 1.8 Vol 3 §4.3 Phase Handoff Information Table (PHIT) | boot mode, memory ranges | HOBPHIT 结构体, hob_get_phit() |
| PI 1.8 Vol 3 §4.4 Memory Allocation HOB | base, length, memory type (EFI_MEMORY_TYPE) | HOBMemoryAlloc, hob_add_memory_alloc() |
| PI 1.8 Vol 3 §4.5 Resource Descriptor HOB | resource type, attributes, physical addr, length | HOBResourceDesc, hob_add_resource_desc() |
| PI 1.8 Vol 3 §4.6 Firmware Volume HOB | base address, length | HOBFirmwareVolume, hob_add_firmware_volume() |
| PI 1.8 Vol 3 §4.7 CPU HOB | address space widths | HOBCPU, hob_add_cpu() |
| PI 1.8 Vol 3 §4.8 GUID Extension HOB | GUID + 自定义数据 | HOBGuidExt |
| PI 1.8 Vol 3 §4.9 Firmware Volume 2/3 HOB | 扩展 FV 信息 | HOB_TYPE_FIRMWARE_VOLUME2/3 |
| PI 1.8 Vol 3 §4.11 End of HOB List | 0xFFFF 类型标记 | hob_finalize() 添加 |

---

## 6. 规范一致性声明

本项目是一个教学实现，旨在说明各规范的核心概念，而非生产级实现。具体限制包括：

| 方面 | 限制 |
|------|------|
| **Device Tree** | 不支持 property value 内嵌解析 (phandle 解析需要完整 DTB context)，大端字节序假设为编译时主机序 |
| **ACPI Tables** | 仅支持精简表集合 (FADT/MADT/MCFG/HPET/BGRT)，不支持完整的表关系图 (DSDT 链) |
| **ACPI AML** | 简化解释器，不支持完整命名空间、递归 Method 调用、OperationRegion 访问、Notify 分发、Mutex/Sleep 同步 |
| **SMBIOS** | 字符串编码假设 ASCII/UTF-8，不支持多字节语言，不支持所有扩展表类型 (>Type 46) |
| **HOB** | 不允许添加后修改 PHIT，硬限制 HOB_MAX_COUNT=256 |

---

## 引用

- devicetree.org Devicetree Specification v0.4: https://www.devicetree.org/specifications/
- ACPI Specification 6.5: https://uefi.org/specifications
- SMBIOS Specification 3.7: https://www.dmtf.org/standards/smbios
- UEFI PI Specification 1.8: https://uefi.org/specifications
