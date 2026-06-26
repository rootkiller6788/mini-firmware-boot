# mini-firmware — 固件基础 (C 语言实现)

> 参考 Intel Firmware Architecture, ARM Trusted Firmware-A, UEFI PI Specification, JEDEC SPI NOR Standards

---

## 模块总览 / Module Overview

| # | 模块 | 头文件 | 核心功能 | 行数 |
|---|------|--------|----------|------|
| 1 | Firmware Layout | `firmware_layout.h` | Flash 设备管理, 固件镜像布局, 头部验证 | 100+ |
| 2 | Reset Vector | `reset_vector.h` | 复位向量初始化, CPU 上下文, 模式切换 | 100+ |
| 3 | MMIO Manager | `mmio.h` | 内存映射 I/O 管理, 虚拟设备 (UART/Timer/GPIO) | 100+ |
| 4 | SMBIOS Tables | `smbios_fw.h` | SMBIOS 表生成与查询, BIOS/系统/主板信息 | 100+ |
| 5 | SPI NOR Flash | `spi_nor.h` | SPI NOR 命令模拟, 擦除/编程/状态管理 | 100+ |

---

## 目录结构 / Directory Tree

```
mini-firmware/
├── include/
│   ├── firmware_layout.h     # 固件闪存布局
│   ├── reset_vector.h        # 复位向量与 CPU 初始化
│   ├── mmio.h                # 内存映射 I/O
│   ├── smbios_fw.h           # SMBIOS 固件表
│   └── spi_nor.h             # SPI NOR 闪存接口
├── src/
│   ├── firmware_layout.c     # 固件布局实现
│   ├── reset_vector.c        # 复位向量实现
│   ├── mmio.c                # MMIO 实现
│   ├── smbios_fw.c           # SMBIOS 实现
│   └── spi_nor.c             # SPI NOR 实现
├── examples/
│   ├── flash_boot_demo.c     # 启动流程模拟演示
│   ├── mmio_demo.c           # MMIO 设备访问演示
│   └── smbios_demo.c         # SMBIOS 表生成演示
├── demos/
│   ├── mini-firmware-layout/
│   │   └── README.md         # 固件闪存布局详解
│   └── mini-reset-vector/
│       └── README.md         # 复位向量详解
├── docs/
│   ├── course-alignment.md   # 课程对齐 (Intel/ARM/UEFI)
│   └── firmware-architecture.md  # 固件架构全景
├── Makefile
└── README.md
```

---

## 构建与运行 / Build & Run

### 前置要求 / Prerequisites

- GCC (MinGW or MSVC-compatible on Windows)
- GNU Make or compatible

### 构建所有演示 / Build All Demos

```bash
make all
```

### 运行各个演示 / Run Individual Demos

```bash
make run-flash-boot     # 模拟上电→固件加载→入口点跳转
make run-mmio           # MMIO 设备映射与读写
make run-smbios         # SMBIOS 表创建与查询
```

### 运行所有测试 / Run All Tests

```bash
make test
```

### 清理 / Clean

```bash
make clean
```

---

## 规范与设计 / Conventions & Design

- **C99**: 所有代码符合 C99 标准
- **依赖**: 仅使用 libc (`stdio.h, stdlib.h, string.h`)
- **命名**: `snake_case` 函数, `PascalCase` 类型, `UPPER_SNAKE_CASE` 常量
- **包含守卫**: 所有头文件使用 `#ifndef X_H` / `#define X_H` / `#endif`
- **布尔类型**: `#include <stdbool.h>`

---

## 参考资源 / References

- [Intel 64 and IA-32 Architectures Software Developer's Manual](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html)
- [ARM Architecture Reference Manual ARMv8-A](https://developer.arm.com/documentation/ddi0487/latest/)
- [ARM Trusted Firmware-A Documentation](https://trustedfirmware-a.readthedocs.io/)
- [UEFI Platform Initialization Specification](https://uefi.org/specifications)
- [DMTF SMBIOS Specification](https://www.dmtf.org/standards/smbios)
- [JEDEC JESD216: Serial Flash Discoverable Parameters](https://www.jedec.org/standards-documents/docs/jesd216b)
