# mini-bios-uefi — BIOS 与 UEFI (C 语言实现)

> 参考 TianoCore EDK II, Phoenix BIOS, UEFI Specification 2.10

C99 实现的核心固件引导模块：Legacy BIOS (实模式 16-bit) 和 UEFI (64-bit PE/COFF 引导)。覆盖 IVT 中断向量表、POST 加电自检、UEFI 系统表、协议数据库、PE/COFF 加载器、GPT 分区和 Device Path。

## 模块

| 模块 | 头文件 | 源文件 | 说明 |
|------|--------|--------|------|
| **Legacy BIOS** | `include/legacy_bios.h` | `src/legacy_bios.c` | IVT/中断向量表, BDA (BIOS Data Area), POST, INT 0x10/0x13/0x19 |
| **UEFI Boot** | `include/uefi_boot.h` | `src/uefi_boot.c` | System Table, Boot/Runtime Services, 协议数据库, ExitBootServices |
| **UEFI Protocols** | `include/uefi_protocols.h` | `src/uefi_protocols.c` | LoadedImage, DevicePath, BlockIo, SimpleFileSystem, GOP |
| **PE/COFF Loader** | `include/pe_coff.h` | `src/pe_coff.c` | PE32+ 头解析, Section 加载, 基址重定位, PE 入口查找 |
| **GPT** | `include/gpt.h` | `src/gpt.c` | GPT Header/Partition 读写, Protective MBR, EFI System Partition 查找 |

## 构建

```bash
make all        # 构建全部 3 个演示程序
make bios_demo  # 仅构建 BIOS POST 演示
make uefi_demo  # 仅构建 UEFI Boot 演示
make gpt_demo   # 仅构建 GPT 演示
make clean      # 清理产物
```

## 运行演示

```bash
bin/bios_post_demo     # BIOS POST 加电自检模拟
bin/uefi_boot_demo     # UEFI 引导流程模拟 (DXE → ExitBootServices)
bin/gpt_demo           # GPT 分区表创建与解析
```

## 架构总览

```
┌──────────────────────────────────────────┐
│              Legacy BIOS                  │
│  IVT → INT 0x19 → MBR → Bootloader       │
│  POST: CPU → RAM → Video → Keyboard      │
│  BDA @ 0x400, EBDA @ 0x9FC0             │
└──────────────────────────────────────────┘

┌──────────────────────────────────────────┐
│                UEFI (PI Firmware Phases)  │
│  SEC → PEI → DXE → BDS → RT              │
│  SystemTable → BootServices → Protocols   │
│  PE/COFF .efi → StartImage() → Kernel    │
└──────────────────────────────────────────┘
```

## 文件列表

```
mini-bios-uefi/
├── include/
│   ├── legacy_bios.h       # Legacy BIOS 声明
│   ├── uefi_boot.h         # UEFI 引导服务声明
│   ├── uefi_protocols.h    # UEFI 协议声明
│   ├── pe_coff.h           # PE/COFF 加载器声明
│   └── gpt.h               # GPT 分区声明
├── src/
│   ├── legacy_bios.c       # Legacy BIOS 实现 (180+ 行)
│   ├── uefi_boot.c         # UEFI Boot 实现 (280+ 行)
│   ├── uefi_protocols.c    # UEFI 协议实现 (200+ 行)
│   ├── pe_coff.c           # PE/COFF 加载器实现 (210+ 行)
│   └── gpt.c               # GPT 实现 (220+ 行)
├── examples/
│   ├── bios_post_demo.c    # BIOS POST 演示 (170+ 行)
│   ├── uefi_boot_demo.c    # UEFI 引导演示 (180+ 行)
│   └── gpt_demo.c          # GPT 演示 (190+ 行)
├── demos/
│   ├── mini-legacy-bios/
│   │   └── README.md       # Legacy BIOS 深度解析 (250+ 行)
│   └── mini-uefi-walkthrough/
│       └── README.md       # UEFI 架构导览 (250+ 行)
├── docs/
│   ├── course-alignment.md # 课程对齐参考
│   └── uefi-internals.md   # UEFI 内部机制
├── Makefile
└── README.md
```
