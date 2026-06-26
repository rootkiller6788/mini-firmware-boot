# UEFI 架构导览

> 参考 UEFI Specification 2.10, Intel Platform Innovation (PI) 1.8, TianoCore EDK II

## 1. 概述

UEFI (Unified Extensible Firmware Interface) 是替代传统 BIOS 的现代固件接口标准，由 UEFI Forum 维护。与 BIOS 的关键区别：

| 特性 | Legacy BIOS | UEFI |
|------|------------|------|
| 运行模式 | 16-bit 实模式 | 64-bit (或 32-bit) 保护模式/长模式 |
| 驱动模型 | Option ROM (实模式回调) | DXE 驱动 (PE/COFF 格式) |
| 启动方式 | MBR (446B 引导代码) | GPT + EFI System Partition + .efi 文件 |
| 内存模型 | 1MB 地址空间, 分段 | 平坦地址空间, 分页 |
| 网络支持 | PXE (通过 Option ROM) | 原生 TCP/IP 网络栈 |
| 扩展性 | 256 中断向量 | Protocol-based 组件模型 |
| Secure Boot | 不支持 | 签名验证 PE/COFF |

## 2. PI Firmware 阶段

Intel PI (Platform Initialization) 规范定义了固件执行的严格阶段：

```
┌─────────┐     ┌─────────┐     ┌─────────┐     ┌─────────┐     ┌─────────┐
│  SEC    │────▶│  PEI    │────▶│  DXE    │────▶│  BDS    │────▶│   RT    │
│ Security│     │Pre-EFI  │     │ Driver  │     │ Boot    │     │ Runtime │
│         │     │ Init    │     │ Exec    │     │ Device  │     │Services │
└─────────┘     └─────────┘     └─────────┘     └─────────┘     └─────────┘
```

### 2.1 SEC (Security Phase)
- 处理 CPU 上电后的第一条指令
- 初始化临时缓存 (Cache-as-RAM)
- 验证 PEI 基础代码 (Firmware Volume)
- 将控制权移交给 PEI Core

### 2.2 PEI (Pre-EFI Initialization)
- 极简的环境初始化
- 发现内存并初始化主内存控制器
- 使用 HOB (Hand-Off Block) 将硬件信息传递给 DXE
- **PPI (PEIM-to-PEIM Interface)**: PEI 阶段的协议机制

### 2.3 DXE (Driver Execution Environment)
- **这是 UEFI 的核心阶段**，也是本项目的主要聚焦点
- 加载并执行 DXE 驱动程序（PE/COFF 格式）
- 建立系统服务：
  - **Boot Services**: 引导阶段可用的服务
  - **Runtime Services**: 在 OS 运行后仍然可用的服务
- 安装 Protocol（协议）到 Protocol Database
- 构建 EFI System Table

#### DXE 调度器工作流程:
```
1. 读取 Firmware Volume 中的 DXE 驱动列表
2. 创建 EFI System Table
3. 初始化 Boot Services 和 Runtime Services
4. 按依赖关系加载 DXE 驱动:
   - Architectural Protocols (APs) 优先
   - 其他 DXE 驱动按 DEPEX (Dependency Expression) 加载
5. 调用每个 DXE 驱动的入口点 (EntryPoint)
6. 驱动安装自己提供的协议
7. BDS 阶段被触发
```

### 2.4 BDS (Boot Device Selection)
- 执行 UEFI Boot Manager
- 枚举启动设备 (根据 NVRAM 中的 BootOrder 变量)
- 加载 EFI System Partition 中的启动加载器
- 调用 LoadImage() → StartImage()
- 或者进入 UEFI Shell / 固件设置界面

### 2.5 Runtime (RT)
- 在 ExitBootServices() 被调用后
- Boot Services 不再可用
- Runtime Services (GetTime, SetVariable, ResetSystem 等) 持续可用
- OS 内核接管硬件控制

## 3. EFI System Table

```c
struct EFISystemTable {
    uint64_t  signature;       // "IBI SYST" (0x5453595320494249)
    uint32_t  revision;        // 固件规范版本
    uint32_t  header_size;
    uint32_t  crc32;
    void     *firmware_vendor; // 厂商名称 (Unicode)
    uint32_t  firmware_revision;

    // 控制台 I/O
    EFIHandle console_in_handle;
    EFISimpleTextInputProtocol  *con_in;   // 键盘输入
    EFIHandle console_out_handle;
    EFISimpleTextOutputProtocol *con_out;  // 屏幕输出
    EFIHandle standard_error_handle;
    EFISimpleTextOutputProtocol *std_err;

    // 服务表
    EFIRuntimeServices *runtime_services;
    EFIBootServices    *boot_services;

    // 配置表（ACPI, SMBIOS, SAL 等）
    uint64_t num_table_entries;
    EFIConfigurationTable *config_table;
};
```

### 关键设计思想
System Table 是 UEFI 应用程序和驱动程序的**唯一全局入口**。从 DXE 阶段开始，每个加载的 .efi 镜像在其入口点 (EntryPoint(ImageHandle, SystemTable)) 接收 System Table 的指针。

## 4. Boot Services 详解

Boot Services 是 UEFI 最核心的服务组，仅在 ExitBootServices() 之前可用：

### 4.1 内存分配
```
AllocatePages()  — 按页 (4KB) 分配物理内存
FreePages()     — 释放页
AllocatePool()  — 按字节分配内存 (来自 EfiConventionalMemory)
FreePool()      — 释放池
GetMemoryMap()  — 获取完整物理内存映射 (传递给 OS)
```

### 4.2 协议管理 (Protocol Database)
```
InstallProtocolInterface()      — 在句柄上安装协议
UninstallProtocolInterface()    — 从句柄上卸载协议
HandleProtocol()                — 在特定句柄上查找协议 (原始)
LocateProtocol()                — 在数据库中全局查找协议 (GUID)
LocateHandleBuffer()            — 查找所有安装了某协议的句柄
OpenProtocol() / CloseProtocol() — 带属性的协议访问 (EXCLUSIVE, GET_PROTOCOL 等)
```

### 4.3 镜像管理
```
LoadImage()   — 将 PE/COFF .efi 文件加载到内存
StartImage()  — 调用镜像的入口点
UnloadImage() — 卸载镜像
Exit()        — 从当前镜像退出
```

### 4.4 事件系统
```
CreateEvent() / CloseEvent()
WaitForEvent() / SignalEvent()
SetTimer()
```

### 4.5 过渡到运行时
```
ExitBootServices(ImageHandle, MapKey)  — 终止 Boot Services，将系统控制权交给 OS Loader
```

## 5. Protocol 模型

UEFI 使用 **GUID + Protocol Interface** 的组合而非传统的 vtable 继承：

```
EFIHandle (不透明指针)
   └── 安装 Protocol → (GUID → Interface*)
   
示例:
Handle_A
  ├── EFI_LOADED_IMAGE_PROTOCOL → { ImageBase, ImageSize, ... }
  ├── EFI_DEVICE_PATH_PROTOCOL  → { /PciRoot(0)/Pci(1,0)/HD(1,GPT,...) }
  └── EFI_BLOCK_IO_PROTOCOL     → { ReadBlocks(), WriteBlocks(), ... }
```

### 5.1 为什么使用 GUID?
- 全局唯一性 — 两个独立开发的驱动程序不会产生协议冲突
- 版本演进 — 新协议可以有不同的 GUID
- 发现性 — LocateProtocol(GUID) 不需要知道句柄

### 5.2 核心协议一览

| Protocol | GUID | 用途 |
|----------|------|------|
| Loaded Image | 5B1B31A1... | 镜像基址、大小、卸载函数 |
| Device Path | 09576E91... | 设备路径 (PciRoot → PCI → HD → File) |
| Block I/O | 964E5B21... | 磁盘扇区读写 |
| Simple File System | 0964E5B2... | 文件系统访问 (OpenVolume → File Protocol) |
| Graphics Output | 9042A9DE... | 帧缓冲访问、分辨率切换 |
| Simple Text Input | 387477C1... | 键盘输入 |
| Simple Text Output | 387477C2... | 文本输出 (Unicode) |
| PCI Root Bridge I/O | 2F707EBB... | PCI 配置空间访问 |
| Disk I/O | CE345171... | 原始磁盘 I/O (不通过 Block I/O) |

## 6. PE/COFF 镜像加载

UEFI 可执行文件使用 PE/COFF (Portable Executable / Common Object File Format) 格式，与 Windows 使用相同的格式，但有特定约束：

### 6.1 UEFI 对 PE 的特殊要求
- **Subsystem** 必须为 `EFI_APPLICATION (10)`, `EFI_BOOT_SERVICE (11)`, 或 `EFI_RUNTIME_DRIVER (12)`
- **Machine** 类型与平台匹配 (x64 = 0x8664)
- DLL Characteristics 中无特殊标志
- .reloc 段必须存在 (因为固件可以选择加载到非首选的基址)

### 6.2 LoadImage() 处理流程
```
1. 打开并读取 .efi 文件 (通过 Simple File System)
2. 验证 DOS 头 → PE 签名 ("PE\0\0")
3. 读取 COFF 头和 Optional Header
4. 验证子系统 (必须为 EFI 子系统)
5. 为镜像分配内存 (AllocatePages)
6. 将各 Sections 复制到内存 (.text, .data, .rdata 等)
7. 处理基址重定位 (如果加载到非首选基址)
8. 创建 ImageHandle 并安装 EFI_LOADED_IMAGE_PROTOCOL
9. 返回 EFI_SUCCESS + ImageHandle
```

### 6.3 StartImage() 流程
```
1. 获取 EFI_LOADED_IMAGE_PROTOCOL
2. 计算入口地址 = ImageBase + AddressOfEntryPoint
3. 调用: EntryPoint(ImageHandle, SystemTable)
4. 入口函数返回 EFI_STATUS
```

## 7. ExitBootServices 与内核交接

这是 UEFI 和 OS 之间最关键的时刻：

```
OS Loader 调用流程:

1. GetMemoryMap(&map_size, memory_map, &map_key, ...)
   // 获取当前系统内存映射
   
2. ExitBootServices(image_handle, map_key)
   // 固件释放所有 Boot Services 资源
   // map_key 验证内存映射未被修改
   // 仅 Runtime Services 继续可用
   
3. SetVirtualAddressMap(map_size, desc_size, descriptor_version,
                         virtual_map)
   // 将物理地址转换为虚拟地址
   
4. 跳转到内核入口
```

## 8. 本项目实现

`src/uefi_boot.c` + `src/uefi_protocols.c` 提供：
- `uefi_init_system_table()`: 初始化 System Table，填充签名/版本/控制台句柄
- `uefi_install_protocol()`: 在 Boot Services 协议数据库中注册 (GUID, Interface)
- `uefi_locate_protocol()`: 通过 GUID 在数据库中查找协议
- `uefi_exit_boot_services()`: 终止 Boot Services，标记签名无效
- `uefi_block_io_read/write()`: 模拟扇区读写
- `uefi_gop_set_mode()`: 模拟图形模式切换
- `uefi_init_device_path()`: 构造设备路径

## 9. 参考资料

- UEFI Specification Version 2.10 (uefi.org)
- Intel Platform Innovation Framework for EFI (PI) 1.8
- TianoCore EDK II Source Code (github.com/tianocore/edk2)
- "Beyond BIOS" by Vincent Zimmer, Michael Rothman, Suresh Marisetty
- UEFI Forum Whitepapers
- osdev.org: UEFI
