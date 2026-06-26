# UEFI 内部机制

> 深入 UEFI 固件的架构设计、数据结构和运行时行为

## 1. 固件镜像布局

UEFI 固件存储在 SPI Flash 中，典型布局：

```
+-----------------------------+  0xFFFFFFFF (4GB Flash 顶部)
| Firmware Volume (FV)        |
|  ┌───────────────────────┐  |
|  │ FV Header             │  |
|  ├───────────────────────┤  |
|  │ SEC Core              │  |  ← CPU reset vector
|  ├───────────────────────┤  |
|  │ PEI Core + PEIMs      │  |  ← 内存初始化模块
|  ├───────────────────────┤  |
|  │ DXE Core + Drivers    │  |  ← 驱动执行环境
|  ├───────────────────────┤  |
|  │ UEFI Applications     │  |  ← Boot Manager, Shell
|  └───────────────────────┘  |
+-----------------------------+
| NVRAM (UEFI Variables)      |  ← BootOrder, SecureBoot keys
+-----------------------------+
| Descriptor Region            |  ← Flash 布局描述符
+-----------------------------+  0xFF000000
```

### 1.1 Firmware Volume (FV)

FV 是固件文件的容器。包含：
- **FV Header**: 签名 `_FVH`, 卷大小, 属性 (可写/不可写)
- **Firmware File System (FFS)**: 每个文件有 GUID, 类型 (RAW, PEIM, DXE_DRIVER, APPLICATION)

## 2. Handle Database

Handle Database 是 UEFI 协议模型的核心数据结构：

```
HandleDatabase {
    List of IHandle {
        HandleKey (唯一标识)
        ProtocolList {
            GUID → ProtocolInterface*  (有序列表)
        }
        // 每个 Handle 可以有多个 Protocol
    }
}
```

### 2.1 Handle 操作

```
InstallProtocolInterface(Handle*, GUID, Interface):
    1. 如果 Handle==NULL → 创建一个新 Handle
    2. 将 (GUID, Interface) 添加到 Handle 的协议列表
    3. 通知所有注册了该 GUID 的 ProtocolNotify 事件

UninstallProtocolInterface(Handle, GUID, Interface):
    1. 从 Handle 上移除指定协议
    2. 如果 Handle 上没有其他协议 → 删除 Handle

LocateProtocol(GUID, &Interface):
    1. 遍历所有 Handle
    2. 对每个 Handle 检查协议 GUID
    3. 返回第一个匹配的 Interface

LocateHandleBuffer(ByProtocol, GUID, &HandleBuf):
    1. 返回所有安装了特定 GUID 协议的 Handle 数组
```

### 2.2 OpenProtocol / CloseProtocol

更复杂的协议访问模型，支持追踪协议使用关系：

```
OpenProtocol(Handle, GUID, &Interface, AgentHandle, ControllerHandle, Attributes):
    支持的 Attributes:
    - BY_HAND_PROTOCOL    : Agent 使用此协议
    - GET_PROTOCOL        : Agent 持有协议引用
    - TEST_PROTOCOL       : Agent 仅测试是否存在
    - BY_CHILD_CONTROLLER : Agent 通过子控制器使用
    - BY_DRIVER           : Agent 是此协议的驱动
    - EXCLUSIVE           : Agent 想要独占访问
```

## 3. DXE Driver 调度

### 3.1 DXE 调度器算法

```
Procedure DXE_DISPATCH(firmware_volumes):

    Queue = []   // 按优先级排序的就绪队列
    Insert(Queue, DXE_Core_internal_drivers)

    while not Queue.Empty():
        Driver = PopHighestPriority(Queue)
        
        // 检查依赖表达式 (Depex)
        if CanExecute(Driver.Depex):
            LoadDriver(Driver)      // PE/COFF loading
            StartDriver(Driver)     // Call EntryPoint()
            
            // 新安装的协议可能触发更多 driver
            for each newly_installed_protocol:
                for each pending_driver:
                    if pending_driver.Depex.satisfied():
                        Insert(Queue, pending_driver)
        else:
            // 尚未满足依赖，标记为 pending
            PendingDrivers.Add(Driver)
            
    // 所有可能的驱动都已加载
    Signal(EFI_EVENT_GROUP_READY_TO_BOOT)
```

### 3.2 依赖表达式 (Depex)

Depex 是一个二进制编码的 postfix 表达式：

```
// Driver 需要: BlockIo AND (SimpleFileSystem OR PartitionInfo)
Depex: PUSH(BlockIo_GUID) PUSH(SimpleFileSystem_GUID) PUSH(PartitionInfo_GUID) OR AND END
```

## 4. 内存类型系统

UEFI 将物理内存分为不同的类型，记录在 GetMemoryMap() 返回的表中：

| 类型 | 说明 | 何时释放 |
|------|------|---------|
| EfiReservedMemoryType | 不可用 | 永不 |
| EfiLoaderCode | OS 加载器代码 | ExitBootServices 后 |
| EfiLoaderData | OS 加载器数据 | ExitBootServices 后 |
| EfiBootServicesCode | 固件代码 | ExitBootServices |
| EfiBootServicesData | 固件数据 | ExitBootServices |
| EfiRuntimeServicesCode | 运行时固件代码 | 永久 (OS 可能虚拟化) |
| EfiRuntimeServicesData | 运行时固件数据 | 永久 (OS 可能虚拟化) |
| EfiConventionalMemory | 空闲可用 | AllocatePages 时分配 |
| EfiACPIReclaimMemory | ACPI 表 | OS 使用后可能回收 |
| EfiACPIMemoryNVS | ACPI NVS | 永远保留 (S3 睡眠) |

## 5. PE/COFF 重定位详解

UEFI 镜像可能在不同于其首选基址的地址加载。固件应用重定位修正：

### 5.1 重定位类型

| Type | Value | 操作 |
|------|-------|------|
| IMAGE_REL_BASED_ABSOLUTE | 0 | 无操作 (对齐填充) |
| IMAGE_REL_BASED_HIGH | 1 | `*Fixup += (uint16_t)(Delta >> 16)` |
| IMAGE_REL_BASED_LOW | 2 | `*Fixup += (uint16_t)(Delta & 0xFFFF)` |
| IMAGE_REL_BASED_HIGHLOW | 3 | `*Fixup += (uint32_t)Delta` |
| IMAGE_REL_BASED_DIR64 | 10 | `*Fixup += Delta` |

### 5.2 .reloc 段结构

```
.reloc Section:
  [BaseRelocBlock]     ← 4KB 页大小块
    PageRVA: uint32    ← 此块修正的目标页 RVA
    BlockSize: uint32  ← 块总大小 (包括此头)
    [Entry] × N        ← 每项 2 字节 (type:4, offset:12)
  [BaseRelocBlock]
    ...
  [BaseRelocBlock]     ← PageRVA=0 表示结束
```

## 6. Device Path 设备路径

Device Path 是一种二进制编码的、描述设备拓扑的结构：

```
格式: Type + SubType + Length + Data

示例 — 从 GUID Partition Table 上的文件启动:
/ACPI(0x0A0341D0,0x0)/PCI(0x1,0x0)/SATA(0x0,0xFFFF,0x0)/HD(1,GPT,<guid>,0x800,0x40000)/File(\EFI\BOOT\BOOTX64.EFI)

逐层解析:
  ACPI(0x0A0341D0,0x0)  — 根 PCI 桥
  PCI(0x1,0x0)           — PCI 设备 1, 功能 0 (SATA 控制器)
  SATA(0x0,0xFFFF,0x0)   — SATA 端口 0
  HD(1,GPT,<guid>,...)   — GPT 分区 1 (EFI System Partition)
  File(\EFI\BOOT\BOOTX64.EFI) — 文件路径
```

## 7. 配置表 (Configuration Table)

System Table 中的 Configuration Table 是一个 GUID→Table 的映射，用于向 OS 传递系统信息：

| Table GUID | 内容 |
|-----------|------|
| ACPI_TABLE_GUID | ACPI RSDP 指针 |
| ACPI_20_TABLE_GUID | ACPI 2.0+ RSDP 指针 |
| SMBIOS_TABLE_GUID | SMBIOS 入口点结构 |
| SMBIOS3_TABLE_GUID | SMBIOS 3.0 入口点 |
| SAL_SYSTEM_TABLE_GUID | IA64 SAL 表 |
| DTB_TABLE_GUID | Device Tree Blob (ARM/RISC-V) |
| MEMORY_ATTRIBUTES_TABLE_GUID | 内存属性表 |
| IMAGE_SECURITY_DATABASE_GUID | Secure Boot db/dbx |

## 8. TPL (Task Priority Level)

UEFI 使用 TPL 来管理中断优先级和同步：

```
TPL_APPLICATION  (4)   ← 普通应用程序
TPL_CALLBACK     (8)   ← 回调函数
TPL_NOTIFY       (16)  ← 通知函数
TPL_HIGH_LEVEL   (31)  ← 不可屏蔽中断
```

RaiseTPL(Tpl) → 返回原 Tpl。只有在 Tpl 以上级别的事件不会被抢占。
RestoreTPL(OldTpl) → 恢复原 Tpl。

## 9. NVRAM 和 UEFI 变量

UEFI 变量存储在 NVRAM (非易失性 RAM) 中：

### 9.1 全局变量

```
Boot####        — 启动选项 (Boot0000, Boot0001, ...)
BootOrder       — 启动选项顺序
BootNext        — 下次启动覆盖选项
Timeout         — 引导菜单超时 (秒)
ConIn / ConOut  — 控制台设备路径
Lang            — 语言代码
PlatformLang    — 平台语言
Key####         — Secure Boot Key Exchange Keys (KEK)
PK              — Platform Key
db / dbx        — Allowed / Forbidden 签名数据库
```

### 9.2 GetVariable / SetVariable

```
GetVariable(VariableName, VendorGuid, &Attributes, &DataSize, Data)
SetVariable(VariableName, VendorGuid, Attributes, DataSize, Data)
```

Attributes 控制:
- NV: 非易失 (写入 Flash)
- BS: Boot Services 期间可访问
- RT: Runtime 期间可访问 (OS 可用)
- AT: 带时间戳的认证变量
- AW: 需要认证才能写入

## 10. 安全启动 (Secure Boot)

```
启动流程中的签名验证:

1. 固件校验 Boot Manager (如果 PK 已注册)
2. 加载 bootx64.efi 时:
   a. 提取 PE/COFF 镜像的 Authenticode 签名
   b. 计算镜像的 SHA-256 摘要
   c. 检查 db (Allowed Database):
      - 如果有匹配的证书/摘要 → 允许
   d. 检查 dbx (Forbidden Database):
      - 如果有匹配 → 拒绝
   e. 检查时间戳证书链:
      - 验证 KEK → db 或 PK → KEK 的信任链
3. 启动加载器同样递归验证后续加载的组件
```

## 11. 参考

- UEFI Specification 2.10 — Chapters 2, 4, 6, 7, 10
- Intel PI Specification 1.8 — Volume 1 (DXE), Volume 3 (Firmware Volumes)
- EDK II: MdePkg/Include/Uefi/ — Header reference
- EDK II: MdeModulePkg/Core/Dxe/ — DXE Core reference
