# Course Alignment — 课程文档对照表

> minibootloader 项目参考的真实世界引导加载器及协议规范的映射。

---

## GRUB2 (GNU GRUB 2.12)

| 概念 | mini-bootloader 组件 | GRUB2 对应文件 |
|-----------|----------------------|---------------------|
| **阶段 1 (MBR)** | `include/stage1.h`, `src/stage1.c` | `grub-core/boot/i386/pc/boot.S` — 446 字节的 MBR，加载第一扇区，扫描分区表 |
| **阶段 2 (diskboot)** | `include/stage2.h`, `src/stage2.c` | `grub-core/boot/i386/pc/diskboot.S` — 加载核心映像的其余部分，理解块列表格式 |
| **lnxboot** | — | `grub-core/boot/i386/pc/lnxboot.S` — 为链式加载提供多引导头 |
| **内核映像** | `include/stage2.h`, `src/stage2.c` | `grub-core/kern/` — 内存管理（`mmap`, `malloc`）、磁盘 I/O（`disk`）、控制台、救援 shell |
| **多引导支持** | `include/stage2.h` (MultibootHeader, MultibootInfo) | `grub-core/loader/multiboot.c` — 解析多引导信息结构，跳转到内核 |
| **Linux 引导协议** | `include/linux_boot.h`, `src/linux_boot.c` | `grub-core/loader/i386/linux.c` — Linux 32 位和 64 位引导协议支持 |
| **模块系统** | `include/grub_modules.h`, `src/grub_modules.c` | `grub-core/kern/dl.c` — 动态模块加载器，ELF 重定位，依赖解析 |
| **FAT 驱动程序** | `include/filesys_boot.h` (BOOTFS_FAT32) | `grub-core/fs/fat.c` — FAT12/16/32 文件系统读取支持 |
| **EXT2 驱动程序** | `include/filesys_boot.h` (BOOTFS_EXT2) | `grub-core/fs/ext2.c` — ext2/3/4 文件系统读取支持 |
| **救援 shell** | — 文档化于 `demos/mini-grub-core/README.md` | `grub-core/normal/main.c`, `grub-core/commands/minicmd.c` — 用于恢复的最小 shell |
| **设备命名** | — | `(hd0,msdos1)` 磁盘/分区命名方案 |
| **块列表** | — | 用于将 core.img 嵌入为物理扇区列表的内部格式 |

**参考链接：**
- [GNU GRUB 手册](https://www.gnu.org/software/grub/manual/grub/grub.html)
- [GRUB 开发者文档](https://www.gnu.org/software/grub/manual/grub-dev/grub-dev.html)
- [GRUB 源代码 (git)](https://git.savannah.gnu.org/cgit/grub.git/)

---

## Linux x86 引导协议

| 概念 | mini-bootloader 组件 | 内核文档对应部分 |
|-----------|----------------------|-----------------------------|
| **设置头 (HdrS)** | `include/linux_boot.h` (SetupHeader) | `Documentation/x86/boot.rst` §2 — 设置头结构，偏移量 0x01F1 |
| **bzImage 格式** | `linux_load_kernel()` — setup.bin + vmlinux.bin | `boot.rst` §6 — bzImage 是 setup.bin 后跟压缩内核（vmlinux.bin） |
| **Boot flag (0xAA55)** | `LINUX_BOOT_SIGNATURE` | `boot.rst` §2.1 — 必须验证的签名 |
| **Header magic (HdrS)** | `LINUX_HEADER_MAGIC` | `boot.rst` §2.2 — 偏移量 0x0202 的 4 字节魔术 |
| **Loadflags** | `LOADFLAGS_LOADED_HIGH`, `LOADFLAGS_CAN_USE_HEAP` | `boot.rst` §2.5 — 引导协议标志 |
| **Initrd** | `linux_load_initrd()`, `linux_set_initrd_addr()` | `boot.rst` §3 — 初始 RAM 磁盘加载和支持 |
| **命令行** | `linux_set_cmdline()` + `cmd_line_ptr` | `boot.rst` §3 — 传递给内核的以 null 结尾的字符串 |
| **内存映射 (E820)** | `linux_setup_e820()` + `E820Entry` | `boot.rst` §11.2 — E820 内存映射，已取代 E801/E88 |
| **视频模式** | `vid_mode` 字段 | `boot.rst` §2.4 — 请求的视频模式 |
| **代码 32 启动** | `code32_start` 字段 | `boot.rst` §2.6 — 32 位入口点地址 |
| **协议版本** | `version` 字段 (0x020C = 2.12) | `boot.rst` §2.3 — 引导协议版本 |
| **Loader 类型** | `type_of_loader` 字段 | `boot.rst` §2.3 — 引导加载器标识符（0x71 = GRUB2） |
| **setup_move_size** | `setup_move_size` 字段 | `boot.rst` §2.5 — 要从 0x90000 复制的设置大小 |
| **实模式切换** | `realmode_swtch` 字段 | `boot.rst` §2.6 — 实模式切换约定 |
| **initrd_addr_max** | 最大 initrd 加载地址 | `boot.rst` §3 — initrd 可在物理地址空间中的放置位置上限 |

**参考链接：**
- [Linux x86 引导协议 v2.15](https://www.kernel.org/doc/html/latest/x86/boot.html)
- [Linux 内核 source/arch/x86/boot/](https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/arch/x86/boot)
- [linux/bootparam.h](https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/arch/x86/include/uapi/asm/bootparam.h)

---

## Das U-Boot (通用引导加载器)

| 概念 | mini-bootloader 组件 | U-Boot 对应部分 |
|-----------|----------------------|----------------|
| **阶段 1 (SPL)** | `include/stage1.h` — MBR 仿真 | `arch/arm/cpu/armv7/start.S` — 次级程序加载器（TCM/内部 SRAM 中的 ~16KB） |
| **阶段 2 (U-Boot proper)** | `include/stage2.h` — 多引导阶段 | `common/board_r.c` — 板级初始化，`init_sequence_r[]` 函数阵列 |
| **设备树支持** | — | `arch/arm/dts/` — 扁平设备树（FDT），通过 `bootm` 传递给内核 |
| **FIT 镜像** | — | `common/image-fit.c` — 扁平镜像树（内核 + DTB + initrd 组合） |
| **bootm 命令** | `stage2_jump_to_kernel()` — 交接 | `arch/arm/lib/bootm.c` — 引导内核，解压，安装 ATAG 或 FDT |
| **命令行传递** | `stage2_set_cmdline()` | `common/bootm.c:bootm_start_linux()` — 从环境变量传递 `bootargs` |
| **环境变量** | — | `env/` 目录 — 从闪存/MMC 存储和访问的变量 |
| **文件系统** | `include/filesys_boot.h` — FAT32/EXT2 | `fs/fat/`, `fs/ext4/` — FAT 和 ext4 支持，以及其他文件系统 |
| **自动引导** | `mbr_emulate_boot()` | `common/autoboot.c` — 倒计时 + 中断自动引导 |
| **DFU (设备固件升级)** | — | `drivers/usb/gadget/f_dfu.c` — 用于固件刷新的 USB DFU 模式 |
| **Fastboot** | — | `drivers/fastboot/` — 用于批量分区刷新的 Android Fastboot 协议 |
| **MMC/SD 支持** | — | `drivers/mmc/` — 用于 SD/eMMC 存储的 MMC 子系统和块设备 |
| **网络引导 (TFTP)** | — | `net/tftp.c` — 通过 TFTP 基于网络的引导，用于嵌入式开发 |
| **串行控制台** | — | `drivers/serial/` — 用于调试输出的 NS16550 UART |

**参考链接：**
- [Das U-Boot 项目](https://docs.u-boot.org/en/latest/)
- [U-Boot 源代码 (git)](https://source.denx.de/u-boot/u-boot)
- [U-Boot 设计原则](https://docs.u-boot.org/en/latest/develop/designprinciples.html)
