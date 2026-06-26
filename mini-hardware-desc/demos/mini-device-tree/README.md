# Device Tree 深度解析

## 1. 设备树简介

Device Tree 是 ARM、RISC-V、PowerPC 等架构中描述硬件配置的核心数据结构。它将"板级硬件描述"从操作系统内核中分离出来，以平台无关的数据格式传递硬件拓扑。

### 设计哲学

在 x86 平台上，硬件信息通过 ACPI 和 PCI 枚举在运行时动态发现。但在嵌入式 SoC 系统中，许多外设是不可探测的（non-discoverable）。设备树在 Boot 阶段由 Bootloader (U-Boot, coreboot 等) 传递给操作系统内核。

```
DTS (文本) → DTC 编译器 → DTB (二进制) → FDT 解析器 → 内核设备模型
```

---

## 2. DTS (Device Tree Source) 语法

### 基本结构

```dts
/dts-v1/;

/ {
    model = "My Nano Board";
    compatible = "my-vendor,my-board";
    #address-cells = <1>;
    #size-cells = <1>;

    memory@80000000 {
        device_type = "memory";
        reg = <0x80000000 0x40000000>;
    };

    soc {
        compatible = "simple-bus";
        #address-cells = <1>;
        #size-cells = <1>;
        ranges;

        serial@1000 {
            compatible = "ns16550a";
            reg = <0x1000 0x100>;
            interrupts = <1>;
            clock-frequency = <50000000>;
            status = "okay";
        };

        interrupt-controller@2000 {
            compatible = "arm,cortex-a9-gic";
            reg = <0x2000 0x1000>;
            interrupt-controller;
            #interrupt-cells = <3>;
        };
    };

    chosen {
        bootargs = "console=ttyS0,115200";
    };
};
```

### 关键语法元素

| 语法 | 说明 | 示例 |
|------|------|------|
| `/dts-v1/;` | DTS 版本声明 | 必须出现在文件首行 |
| `/ {...};` | 根节点 | 整个设备树的起始 |
| `node@address { };` | 带地址节点 | `serial@1000 {}` |
| `property = <value>;` | 属性赋值 | `reg = <0x1000 0x100>;` |
| `&label { };` | 节点引用/覆盖 | `&uart1 { status = "disabled"; };` |
| `#include` | 预处理 | `#include "skeleton.dtsi"` |
| `/include/` | 二进制包含 | 包含 DTSI 片段 |
| `phandle = <&label>;` | 节点间引用 | `interrupt-parent = <&intc>;` |

### 数据类型

```dts
string-prop = "hello";           // 字符串 (null-terminated)
cells-prop  = <0xDEAD 0xBEEF>;   // 32-bit cells (大端)
bytestring  = [00 11 22 33 FF];  // 原始字节
bool-prop;                        // 布尔属性 (存在即为真)
combo-prop  = "str", <1>, [02];  // 混合类型
```

---

## 3. DTC 编译器

```bash
# DTS → DTB
dtc -I dts -O dtb -o output.dtb input.dts

# DTB → DTS (反编译)
dtc -I dtb -O dts -o output.dts input.dtb

# 输出预处理结果
dtc -E -o output.pp.dts input.dts

# 检查语法
dtc -I dts -O dtb -o /dev/null input.dts
```

DTC 编译过程：
1. 预处理 (DTS → DTS.i) — 展开 `#include`、`/include/`
2. 解析 (DTS.i → 语法树) — 构建内存语法树
3. 展平 (语法树 → FDT) — 生成二进制 Blob

---

## 4. FDT 二进制格式 (大端字节序)

```
+------------------+
| FDT Header       |   40 字节
+------------------+
| Memory Reserve   |   8 字节对齐
| Map              |   (address, size) 对
+------------------+
| Structure Block  |   4 字节对齐
| (节点/属性树)     |
+------------------+
| Strings Block    |
| (属性名字符串表)  |
+------------------+
```

### FDT Header (40 字节)

| 偏移 | 大小 | 字段 | 说明 |
|------|------|------|------|
| 0x00 | 4 | magic | 0xD00DFEED (大端) |
| 0x04 | 4 | totalsize | DTB 总大小 |
| 0x08 | 4 | off_dt_struct | 结构块偏移 |
| 0x0C | 4 | off_dt_strings | 字符串块偏移 |
| 0x10 | 4 | off_mem_rsvmap | 内存预留映射偏移 |
| 0x14 | 4 | version | FDT 版本 (17) |
| 0x18 | 4 | last_comp_version | 最低兼容版本 (16) |
| 0x1C | 4 | boot_cpuid_phys | 引导 CPU 物理 ID |
| 0x20 | 4 | size_dt_strings | 字符串块大小 |
| 0x24 | 4 | size_dt_struct | 结构块大小 |

### 结构块令牌

| 令牌值 | 名称 | 格式 |
|--------|------|------|
| 0x00000001 | FDT_BEGIN_NODE | + name (null-terminated) |
| 0x00000002 | FDT_END_NODE | 无附加数据 |
| 0x00000003 | FDT_PROP | + len (u32) + nameoff (u32) + value |
| 0x00000004 | FDT_NOP | 无附加数据 (跳过) |
| 0x00000009 | FDT_END | 结构块结束 |

### 示例解析

```
Offset  Hex                                     Token
------  ------------------------------------    ------------
0x0000  00000001                                FDT_BEGIN_NODE
0x0004  00000000                                name = "" (root)
0x0008  00000003                                FDT_PROP
0x000C  00000010                                len = 16
0x0010  00000000                                nameoff = 0 ("model")
0x0014  4D79204E61 6E6F2042 6F61726420           "My Nano Board" + padding
0x0024  76312E30 00
0x0028  00000003                                FDT_PROP
...                                            ...
0x0100  00000002                                FDT_END_NODE
0x0104  00000009                                FDT_END
```

---

## 5. Device Tree Overlay (DTBO)

Overlay 允许在运行时动态修改设备树，常用于 FPGA 动态重配置、外设热插拔等场景。

### Fragment 语法

```dts
/dts-v1/;
/plugin/;

&{/} {
    /* 目标节点修改 */
};

&uart1 {
    status = "okay";
};
```

带 fragment 的版本：

```dts
/dts-v1/;
/plugin/;

/ {
    fragment@0 {
        target = <&fpga_region>;
        __overlay__ {
            #address-cells = <1>;
            #size-cells = <1>;

            firmware-name = "my-design.bit.bin";

            gpio@40000000 {
                compatible = "my-fpga-gpio";
                reg = <0x40000000 0x1000>;
            };
        };
    };
};
```

### 编译 Overlay

```bash
dtc -@ -I dts -O dtb -o overlay.dtbo overlay.dts
```

`-@` 标志在编译时保留符号表，使 overlay 能够解析 `&label` 引用。

---

## 6. Linux 内核中的使用

### FDT 解析流程

```
start_kernel() → setup_arch() → unflatten_device_tree()
  → __unflatten_device_tree()
    → unflatten_dt_nodes()  // FDT → 内存树
      → of_platform_populate()  // 创建 platform device
```

### `/proc/device-tree/`

```bash
ls /proc/device-tree/
# model  compatible  #address-cells  #size-cells  memory@80000000  soc/  chosen/

cat /proc/device-tree/model
# My Nano Board

cat /proc/device-tree/compatible
# my-vendor,my-board
```

每个属性都是一个文件，节点是目录。值以大端二进制形式存储。

### 驱动端 API

```c
// 匹配 compatible
static const struct of_device_id my_driver_of_match[] = {
    { .compatible = "my-vendor,my-device" },
    {}
};

// 驱动中读取属性
struct device_node *np = pdev->dev.of_node;
u32 reg[2];
of_property_read_u32_array(np, "reg", reg, 2);
int irq = irq_of_parse_and_map(np, 0);
```

---

## 7. U-Boot 中的设备树

### 编译到 U-Boot 中

```bash
make myboard_defconfig
# CONFIG_OF_CONTROL=y
# CONFIG_OF_SEPARATE=y (外部 DTB) 或 CONFIG_OF_EMBED=y (嵌入)
make
```

### U-Boot 中的选择逻辑

```
U-Boot SPL → 加载 FIT Image → 选择 kernel DTB → 传递给 Kernel
```

常用命令：

```
uboot> fdt addr ${fdt_addr_r}
uboot> fdt resize
uboot> fdt set /soc/serial@1000 status "disabled"
uboot> fdt print /soc/serial@1000
uboot> bootm ${kernel_addr_r} - ${fdt_addr_r}
```

### FIT Image 结构

```
FIT Image
├── kernel@1
│   ├── description = "Linux Kernel"
│   ├── data = <...>
│   └── type = "kernel"
├── fdt@1
│   ├── description = "Board DTB"
│   ├── data = <...>
│   └── type = "flat_dt"
├── config@1
│   ├── description = "Default Configuration"
│   ├── kernel = "kernel@1"
│   └── fdt = "fdt@1"
```

---

## 8. 标准属性参考

| 属性 | 节点 | 说明 | 示例 |
|------|------|------|------|
| `compatible` | 任意 | 硬件标识符列表 | `"vendor,device"` |
| `reg` | 设备 | MMIO 地址范围 | `<0x1000 0x100>` |
| `interrupts` | 中断消费者 | 中断说明符 | `<0 42 4>` |
| `interrupt-parent` | 设备 | 指向中断控制器 | `<&intc>` |
| `#address-cells` | 总线 | `reg` 中地址字段数量 | `<1>` 或 `<2>` |
| `#size-cells` | 总线 | `reg` 中大小字段数量 | `<1>` 或 `<2>` |
| `ranges` | 总线 | 地址翻译表 | `<0 0 0x10000000 0x1000>` |
| `status` | 设备 | 设备状态 | `"okay"`, `"disabled"` |
| `dma-ranges` | 总线 | DMA 地址翻译 | 类似 ranges |
| `clocks` | 设备 | 时钟引用 | `<&clk 0>` |
| `resets` | 设备 | 复位引用 | `<&rst 1>` |
| `phandle` | 任意 | 唯一 ID | `<1>` |
| `#interrupt-cells` | 中断控制器 | 中断说明符大小 | `<1>` 到 `<4>` |
| `interrupt-map` | 中断 Nexus | 中断路由表 | 嵌套中断映射 |
| `model` | 根节点 | 板卡型号字符串 | `"Nano Board"` |

---

## 9. 最佳实践

1. **compatible 顺序**: 从具体到通用，例如 `"my-board,uart", "ns16550a"`
2. **reg 编码**: `#address-cells` + `#size-cells` 决定每个 reg 条目大小
3. **状态管理**: 使用 `status = "disabled"` 禁用未使用设备，引导时 overlay 可重新启用
4. **引用代替复制**: 使用 `&label` 引用共享资源（时钟、中断、GPIO）
5. **使用 *.dtsi**: 将 SoC 级定义放在 `.dtsi` 文件中，板级 `.dts` 文件中 include 并覆盖
6. **验证 DTS**: 运行 `dtc` 编译检查语法，使用 `dt-validate` 验证 Schema
7. **大端意识**: DTB 中所有 `u32` 值均以大端序存储，解析器必须正确字节交换

---

## 10. mini-hardware-desc 实现要点

本项目的 `device_tree.c` 实现了精简但完整的 FDT 解析器：

- **fdt_parse()**: 从 DTB 二进制数据中解析 Header，验证魔数 (0xD00DFEED) 和版本兼容性 (>=16)，然后递归遍历 Structure Block。
- **令牌处理**: 解析器识别 FDT_BEGIN_NODE/FDT_END_NODE/FDT_PROP/FDT_NOP/FDT_END 令牌，构建树形结构。
- **字符串表**: 属性名通过 off_dt_strings 中的偏移量引用，避免重复存储。
- **内存预留**: off_mem_rsvmap 区域标记固件占用的物理内存区域。
- **fdt_find_node_by_path()**: 按 `/soc/serial@1000` 格式的路径遍历节点树。
- **fdt_find_compatible()**: 递归搜索 compatible 属性匹配的节点。

---

## 引用

- [devicetree.org Specification](https://www.devicetree.org/specifications/)
- [Device Tree Usage](https://elinux.org/Device_Tree_Usage)
- [Linux Kernel Device Tree Bindings](https://www.kernel.org/doc/Documentation/devicetree/bindings/)
- [Device Tree Compiler (DTC)](https://git.kernel.org/pub/scm/utils/dtc/dtc.git)
- [U-Boot FIT Image](https://u-boot.readthedocs.io/en/latest/usage/fit.html)
