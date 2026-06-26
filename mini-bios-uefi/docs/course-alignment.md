# 课程对齐参考

本文档将本项目模块映射到业界主流固件实现和规范标准。

## 参考源

| 编号 | 名称 | 版本 | 类型 |
|------|------|------|------|
| R1 | TianoCore EDK II | Latest (edk2-stable) | 开源实现 |
| R2 | UEFI Specification | 2.10 | 行业标准 |
| R3 | Phoenix SecureCore BIOS | 4.0+ | 商业实现 |
| R4 | Intel PI Specification | 1.8 | 行业标准 |
| R5 | ACPI Specification | 6.5 | 行业标准 |
| R6 | IBM PC/AT Technical Reference | 1984 | 历史参考 |
| R7 | GNU-EFI | 3.0.15 | 开源工具链 |

## 模块对齐表

| 本项目模块 | 对应标准/实现 | 覆盖率 | 备注 |
|-----------|-------------|--------|------|
| **legacy_bios.c — IVT** | R6 Ch.5, R3 BIOS INT 13h | 核心 | 中断向量表管理 (256 entries) |
| **legacy_bios.c — POST** | R6 POST Sequence, R3 Power-On | 核心 | CPU→RAM→VGA→KB→Boot 序列 |
| **legacy_bios.c — BDA** | R6 Memory Map, R3 BDA Layout | 完整 | 0x400 处 256 字节 BDA |
| **legacy_bios.c — INT 0x13** | R3 Disk Services | AH 0x00-0x05 | 磁盘读写、复位、格式化 |
| **legacy_bios.c — INT 0x10** | R3 Video Services | AH 0x00-0x0F | 文本模式视频服务 |
| **uefi_boot.c — SystemTable** | R2 §4, R1 MdePkg/Include/UefiSpec.h | 完整 | gST 签名/修订/控制台 |
| **uefi_boot.c — BootServices** | R2 §6, R1 MdeModulePkg/Core/Dxe | 30+ 服务 | 内存/协议/镜像/事件 |
| **uefi_boot.c — Protocol DB** | R2 §10, R1 Core/Dxe/Hand | 核心 | GUID 查找 + 句柄注册 |
| **uefi_protocols.c — DevicePath** | R2 §10.5, R1 MdePkg DevicePath | Text/Media/HW | 设备拓扑描述 |
| **uefi_protocols.c — BlockIo** | R2 §13.4, R1 MdePkg BlockIo | Media+LBA | 扇区级 I/O 接口 |
| **uefi_protocols.c — GOP** | R2 §12.9, R1 MdePkg GraphicsOutput | Query/SetMode | 帧缓冲和分辨率 |
| **uefi_protocols.c — SimpleFS** | R2 §13.4, R1 MdePkg FileSystem | OpenVolume | 文件系统访问 |
| **pe_coff.c** | R2 §2 (PE/COFF), R1 BaseTools/GenFw | PE32+ 完整 | 头解析/重定位/入口 |
| **gpt.c** | R2 §5 (GPT), R4 Disk I/O | 128 Partitions | 分区表 + Protective MBR |

## 术语对照

| 本项目 | TianoCore EDK II | UEFI Spec |
|--------|-----------------|-----------|
| `uefi_init_system_table()` | `DxeMain.c` → `CoreInitializeSystemTable()` | §4.3.3 System Table |
| `uefi_install_protocol()` | `InstallProtocolInterface.c` | §7.3 Handle Protocol |
| `uefi_locate_protocol()` | `Locate.c` | §7.3 Handle Protocol |
| `uefi_exit_boot_services()` | `DxeMain.c` → `CoreExitBootServices()` | §7.6 ExitBootServices |
| `uefi_block_io_read()` | `DiskIo.c` + `BlockIo.c` | §13.4 Block I/O Protocol |
| `pecoff_load_image()` | `Image.c` → `CoreLoadPeImage()` | §2 PE/COFF Image |
| `pecoff_relocate()` | `Image.c` → `CoreProcessRelocation()` | §2.3.1 Relocation |
| `gpt_read_header()` | `Partition.c` → `PartitionInstallGptChildHandles()` | §5.3 GPT |
| `bios_post()` | N/A (EDK II 不包含 BIOS) | N/A |
| `bios_int19h_bootstrap()` | N/A | N/A |

## TianoCore EDK II 关键目录对照

| EDK II 路径 | 功能 | 本项目对应 |
|-------------|------|-----------|
| `MdePkg/Include/Uefi/UefiSpec.h` | System Table + Boot/Runtime Services | `uefi_boot.h` |
| `MdePkg/Include/Protocol/BlockIo.h` | Block I/O | `uefi_protocols.h` |
| `MdePkg/Include/Protocol/GraphicsOutput.h` | GOP | `uefi_protocols.h` |
| `MdePkg/Include/Protocol/DevicePath.h` | Device Path | `uefi_protocols.h` |
| `MdePkg/Include/Protocol/LoadedImage.h` | Loaded Image | `uefi_protocols.h` |
| `MdePkg/Include/IndustryStandard/PeImage.h` | PE/COFF 定义 | `pe_coff.h` |
| `MdeModulePkg/Core/Dxe/DxeMain.c` | DXE Core 入口 | `uefi_boot.c` |
| `MdeModulePkg/Core/Dxe/Hand/Handle.c` | Handle 管理 | `uefi_boot.c` |
| `MdeModulePkg/Core/Dxe/Image/Image.c` | 镜像加载 | `pe_coff.c` |
| `MdeModulePkg/Universal/Disk/PartitionDxe/` | 分区检测 | `gpt.c` |
| `MdeModulePkg/Universal/Disk/DiskIoDxe/` | 磁盘 I/O | `uefi_protocols.c` |

## 学习路线建议

```
Phase 1: Legacy BIOS 基础
├── 理解实模式内存布局和分段
├── 掌握 IVT 中断向量表机制
├── 运行 bios_post_demo 观察 POST 流程
└── 阅读 demos/mini-legacy-bios/README.md

Phase 2: UEFI 核心概念
├── 理解 PI 固件阶段 (SEC→PEI→DXE→BDS→RT)
├── 掌握 System Table 和 Boot Services 架构
├── 运行 uefi_boot_demo 观察协议数据库
└── 阅读 demos/mini-uefi-walkthrough/README.md

Phase 3: 深入 TianoCore EDK II
├── 克隆 github.com/tianocore/edk2
├── 对比本项目的结构 vs EDK II 的 MdePkg + MdeModulePkg
├── 研究 DXE Core (MdeModulePkg/Core/Dxe/)
└── 构建 OVMF (Open Virtual Machine Firmware) for QEMU

Phase 4: 高级主题
├── Secure Boot / PK/KEK/db/dbx 变量体系
├── ACPI 表 (由 BDS 阶段安装到配置表)
├── SMM (System Management Mode) 与 Runtime Drivers
└── Capsule Update (固件更新机制)
```
