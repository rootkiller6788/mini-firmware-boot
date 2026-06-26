# mini-boot-process -- 启动流程 (C 语言实现)

> 参考 UEFI PI Spec, AMD AGESA, Intel Boot Flow

mini-boot-process 是 UEFI PI (Platform Initialization) 启动流程的 C 语言教学实现。
它模拟 x86_64 平台上从冷启动到操作系统 Runtime 的全部六个 PI 阶段, 包含 CPU 初始化、
内存 (DDR) 训练、PCI 设备枚举、Cache-as-RAM 等关键固件子系统的简化实现。

## 模块表

| # | 模块 | 头文件 | 源文件 | 说明 |
|---|------|--------|--------|------|
| 1 | Boot Phases | `include/boot_phases.h` | `src/boot_phases.c` | UEFI PI 六个阶段的定义与控制 |
| 2 | CPU Init | `include/cpu_init.h` | `src/cpu_init.c` | BSP/AP 初始化, MSR, MTRR, paging |
| 3 | Memory Init | `include/memory_init.h` | `src/memory_init.c` | SPD 解析, DDR 训练, 内存映射构建 |
| 4 | Device Enum | `include/device_enum.h` | `src/device_enum.c` | PCI 总线枚举, 设备发现, BAR 分配 |
| 5 | Cache-as-RAM | `include/cache_as_ram.h` | `src/cache_as_ram.c` | 预 DRAM 缓存栈, CAR 拆卸 |

## 目录树

```
mini-boot-process/
├── include/
│   ├── boot_phases.h       # BootPhase 枚举, BootState, HandOffBlock 结构
│   ├── cpu_init.h          # CPUInitState, MSR 结构, CPU 初始化函数
│   ├── memory_init.h       # SPDData, MemoryController, MemoryMap
│   ├── device_enum.h       # PCIDevice, PCIBus, PCI 配置寄存器定义
│   └── cache_as_ram.h      # CARState, CAR_BASE/CAR_SIZE 宏
├── src/
│   ├── boot_phases.c       # 启动阶段实现 (300+ 行)
│   ├── cpu_init.c          # CPU 初始化实现 (200+ 行)
│   ├── memory_init.c       # 内存初始化实现 (250+ 行)
│   ├── device_enum.c       # PCI 设备枚举实现 (250+ 行)
│   └── cache_as_ram.c      # Cache-as-RAM 实现 (200+ 行)
├── examples/
│   ├── boot_sim_demo.c     # 完整启动仿真: SEC→PEI→DXE→BDS→TSL→RT
│   ├── pci_enum_demo.c     # PCI 总线枚举 + BAR 资源分配演示
│   └── car_demo.c          # Cache-as-RAM: 写入、读回、拆卸到 DRAM
├── demos/
│   ├── mini-boot-flow/
│   │   └── README.md       # UEFI 启动流程详细文档 (300+ 行)
│   └── mini-memory-init/
│       └── README.md       # DRAM 初始化深入解析 (300+ 行)
├── docs/
│   ├── course-alignment.md # 各模块与 UEFI PI 规范的对照表
│   └── boot-phases-detail.md # 每个 PI 阶段的详细解析
├── Makefile                # 构建三个 demo 程序
└── README.md               # 本文件
```

## 构建

```bash
make all          # 编译所有 demo
make boot_sim     # 仅编译启动仿真 demo
make pci_enum     # 仅编译 PCI 枚举 demo
make car_demo     # 仅编译 Cache-as-RAM demo
make clean        # 清理编译产物
```

编译产物输出到 `bin/` 目录。

## 运行

```bash
bin/boot_sim_demo    # 完整启动流程仿真
bin/pci_enum_demo    # PCI 设备枚举演示
bin/car_demo         # Cache-as-RAM 读写与拆卸演示
```

## 实现概览

### Boot Phases (启动阶段)
`BootPhase` 枚举定义六个 PI 阶段: `SEC(0)`, `PEI(1)`, `DXE(2)`, `BDS(3)`, `TSL(4)`, `RT(5)`。
`BootState` 在每个阶段之间维护状态, `HandOffBlock` 在阶段之间传递 FV 位置和内存映射。
`boot_transition()` 处理阶段之间的转换逻辑。

### CPU Init
`cpu_init_bsp()` 初始化为 Bootstrap Processor (BSP), 设置 APIC/x2APIC/SMX/VMX/NX/SMEP 等
feature flags。 `cpu_init_ap()` 通过 INIT-SIPI-SIPI 序列启动 Application Processor。
`cpu_init_msrs()` 编程 EFER, APIC_BASE, MISC_ENABLE, FEATURE_CTRL, STAR/LSTAR/FMASK 等 MSR。
`cpu_init_caches()` 启用 L1/L2/L3 缓存, `cpu_init_mtrr()` 设置 MTRR 内存类型。

### Memory Init
`mem_init_spd()` 解析 DDR4/DDR5 SPD (Serial Presence Detect) EEPROM 数据, 提取容量、
速度、时序参数。 `mem_init_controller()` 配置双通道内存控制器, 统计总内存。
`mem_train_ddr()` 模拟 MRS 寄存器编程和 DDR 训练序列 (write leveling, read DQS gate training)。
`mem_build_map()` 构建 UEFI 兼容的内存映射, 包含 Reserved, LoaderCode, BootServices,
Runtime, ACPI, MMIO 区域。

### Device Enum (PCI)
`pci_enumerate_bus()` 扫描模拟的 PCI 总线, 发现 VGA, SATA, USB, NIC 等设备。
`pci_assign_resources()` 为发现的设备分配 MMIO 和 IO 地址空间的 BAR (Base Address Register)。
`pci_find_device()` 按 vendor/device ID 查找设备, `pci_find_class()` 按 class code 查找。
`pci_enable_bus_mastering()` 启用所有设备的 bus mastering 能力。

### Cache-as-RAM (CAR)
`car_init()` 初始化 CAR 数据结构和内部缓存线数组。 `car_enable()` 启用 no-eviction 模式,
让 CPU 缓存充当临时 RAM。 `car_read()`/`car_write()` 提供 CAR 区域的读写访问。
`car_teardown()` 在 DRAM 初始化后将 CAR 内容刷新到 DRAM, 并恢复正常的缓存操作。
`car_is_addr_in_car()` 判断地址是否落在 CAR 范围内。

## License

MIT
