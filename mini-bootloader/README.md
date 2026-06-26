# mini-bootloader — 引导加载器 (C 语言实现)

> 参考 GRUB2, Das U-Boot, Linux Boot Protocol

一个简化的 bootloader 系统，以教育风格模拟多阶段引导加载器功能，涵盖 MBR / VBR、Multiboot 协议、Linux x86 引导协议、GRUB 模块系统和简化的文件系统驱动程序。

---

## 模块总览

| 模块 | 头文件 | 源文件 | 说明 |
|--------|-----------|--------|-------------|
| **Stage 1** | `include/stage1.h` | `src/stage1.c` | MBR / VBR 引导阶段：分区表、活动分区检测、引导仿真 |
| **Stage 2** | `include/stage2.h` | `src/stage2.c` | Multiboot 阶段：MultibootHeader / MultibootInfo、内存映射、内核交接 |
| **Linux Boot** | `include/linux_boot.h` | `src/linux_boot.c` | Linux x86 引导协议：bzImage 解析、E820 内存映射、initrd 支持 |
| **GRUB Modules** | `include/grub_modules.h` | `src/grub_modules.c` | GRUB 模块系统：模块注册表、依赖排序、FS 驱动程序注册 |
| **Filesys Boot** | `include/filesys_boot.h` | `src/filesys_boot.c` | 引导文件系统：FAT32 和 EXT2 支持、目录列表、文件读取 |

---

## 构建与运行

```bash
make
```

这将编译 `bin/` 中的 3 个演示程序：

| 二进制 | 来源 | 演示内容 |
|--------|-------|-------------|
| `bin/mbr_boot_demo` | `examples/mbr_boot_demo.c` | MBR 创建、分区扫描、阶段 1 引导模拟 |
| `bin/multiboot_demo` | `examples/multiboot_demo.c` | Multiboot 头验证、内存映射、内核交接模拟 |
| `bin/linux_boot_demo` | `examples/linux_boot_demo.c` | Linux 引导协议、bzImage 设置头、E820 映射 |

单独运行：
```bash
./bin/mbr_boot_demo
./bin/multiboot_demo
./bin/linux_boot_demo
```

---

## 目录结构

```
mini-bootloader/
├── include/
│   ├── stage1.h
│   ├── stage2.h
│   ├── linux_boot.h
│   ├── grub_modules.h
│   └── filesys_boot.h
├── src/
│   ├── stage1.c
│   ├── stage2.c
│   ├── linux_boot.c
│   ├── grub_modules.c
│   └── filesys_boot.c
├── examples/
│   ├── mbr_boot_demo.c
│   ├── multiboot_demo.c
│   └── linux_boot_demo.c
├── demos/
│   ├── mini-grub-core/
│   │   └── README.md
│   └── mini-kernel-handoff/
│       └── README.md
├── docs/
│   ├── course-alignment.md
│   └── bootloader-internals.md
├── Makefile
└── README.md
```
