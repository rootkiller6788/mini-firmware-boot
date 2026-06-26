# Mini GRUB Core — 启动核心映像详解

> 引导加载器核心 (core.img) 由多个组件精密编排而成，能够初始化硬件、理解文件系统，并最终将控制权交给操作系统内核。

---

## 1. GRUB core.img 组件编排

GRUB（Grand Unified Bootloader）的启动过程是一个多阶段过程，最终形成单一的 `core.img`，它包含了引导操作系统所需的所有模块和驱动程序。

### 1.1 组件序列

```
boot.img (446 bytes, MBR)
  → diskboot.img (LBA 1, sector-based reading)
    → lnxboot.img (Linux bootstrap)
      → kernel.img (GRUB 核心内核)
        → *.mod (模块：fs, disk, video, crypto...)
```

每个组件都扮演着特定的角色，并且都适合精确的边界：

| 组件 | 大小 | 位置 | 目的 |
|---------|------|--------|---------|
| **boot.img** | 446 字节 | MBR (LBA 0) | 加载第一个扇区。由 BIOS 固件执行。包含最小的 x86 实模式代码，用于加载存储在任何块设备上的 LBA 1。 |
| **diskboot.img** | 512 字节 | LBA 1 | 加载 core.img 的其余部分。从第一个扇区构建一个扇区列表，这些扇区可能不是连续的——理解核心映像的块列表格式。 |
| **lnxboot.img** | 512 字节 | LBA 2（可选） | 为 Linux 内核提供 Multiboot 规范头。伪装成内核映像，以便与兼容 Multiboot 的引导加载器进行链式加载。 |
| **kernel.img** | ~30KB | LBA 3+ | GRUB 核心：内存管理、磁盘 I/O、模块加载器、救援 shell、基本控制台。 |
| **fs.mod** | 变化 | 扇区列表 | 文件系统驱动程序：ext2, fat, ntfs, iso9660, xfs, btrfs, zfs。 |
| **disk.mod** | 变化 | 扇区列表 | 磁盘驱动程序：biosdisk, ata, ahci, usb, scsi。 |
| **video.mod** | 变化 | 扇区列表 | 视频驱动程序：vbe, efi_gop, vga, vga_text。 |
| **crypto.mod** | 变化 | 扇区列表 | 加密驱动程序：luks, luks2, pgp。 |

### 1.2 块列表格式

`diskboot.img` 理解一种称为 _块列表_ 的简单格式，用于定位构成 core.img 的扇区：

```
[sector_count][sector_list...]
```

每个条目指定要读取的扇区数量和起始 LBA：
```
8         ← 要读取的 8 个扇区
2048      ← 从 LBA 2048 开始
13        ← 接下来 13 个扇区
5120      ← 从 LBA 5120 开始
0         ← 终止符
```

这允许核心映像在物理上是非连续的——GRUB（`grub-install`）在安装时写入此信息。

---

## 2. 模块加载系统

GRUB 的核心将大部分功能推迟到 _模块_.这使得核心保持较小（~30KB），同时支持广泛的文件系统和硬件。

### 2.1 模块文件格式（`.mod`）

每个 `.mod` 文件是一个 ELF32 可重定位对象，布局如下：

```
[ELF Header]
[.text section]     — 可执行代码
[.rodata section]   — 只读数据（字符串、表）
[.data section]     — 可读写数据
[.bss section]      — 零初始化数据（在文件中不占用空间）
[.moddeps section]  — 模块依赖名称（以 null 结尾的字符串）
[.modname section]  — 模块名称（以 null 结尾的字符串）
[.symtab section]   — 导出的符号
```

模块被_重定位_到内核分配的运行时内存地址，类似于动态链接器为共享库工作的方式。任何未解析的符号都会根据内核的符号表进行解析。

### 2.2 模块依赖解析

模块可以声明对其他模块的依赖。加载顺序是通过 **拓扑排序** 解决的：

```
fat.mod 需要: disk.mod
ext2.mod 需要: disk.mod
ntfs.mod 需要: disk.mod, crypto.mod
luks.mod 需要: crypto.mod, disk.mod
```

解析顺序（一次可能的拓扑排序）：
1. `disk.mod` （无依赖）
2. `crypto.mod` （无依赖或依赖于 disk）
3. `fat.mod` （disk 现在可用）
4. `ext2.mod` （disk 现在可用）
5. `luks.mod` （disk, crypto 现在可用）
6. `ntfs.mod` （disk, crypto 现在可用）

### 2.3 常用模块

**文件系统驱动程序：**
- `ext2.mod` — ext2/3/4 文件系统支持
- `fat.mod` — FAT12/16/32 和 exFAT
- `ntfs.mod` — NTFS 只读支持
- `iso9660.mod` — ISO 9660 / UDF (CD/DVD)
- `xfs.mod` — XFS 文件系统
- `btrfs.mod` — Btrfs（包括压缩和子卷）
- `zfs.mod` — ZFS（压缩和 RAID-Z）
- `squash4.mod` — SquashFS 只读
- `hfsplus.mod` — HFS+ (macOS)
- `ufs2.mod` — UFS2 (FreeBSD)

**磁盘驱动程序：**
- `biosdisk.mod` — 传统 BIOS INT 13h 磁盘 I/O
- `ata.mod` — ATA/IDE 控制器访问
- `ahci.mod` — SATA AHCI 控制器
- `usb.mod` + `usbms.mod` — USB 大容量存储
- `scsi.mod` — SCSI 磁盘
- `memdisk.mod` — RAM 磁盘支持

**视频/控制台驱动程序：**
- `vbe.mod` — VESA BIOS 扩展（高分辨率图形）
- `efi_gop.mod` — UEFI 图形输出协议
- `vga.mod` — VGA 文本和图形
- `video_fb.mod` — 通用线性帧缓冲区
- `gfxterm.mod` — 图形模式终端
- `bitmap.mod` — 位图图像支持
- `font.mod` — 字体渲染

---

## 3. 救援 Shell

当 GRUB 无法找到其配置文件（`grub.cfg`）或遇到引导错误时，它会丢弃用户的 **救援 shell**.这是一个最小化的 shell，用于诊断和恢复。

### 3.1 救援 Shell 命令

```
grub rescue> ls                          # 列出设备和分区
(hd0) (hd0,msdos1) (hd0,msdos2) (cd0)

grub rescue> ls (hd0,msdos1)             # 列出分区内容
lost+found/ boot/ etc/ usr/

grub rescue> ls (hd0,msdos1)/boot/       # 列出 /boot 目录
vmlinuz-5.15.0 initrd.img-5.15.0 grub/

grub rescue> set                         # 显示所有变量
prefix=(hd0,msdos1)/boot/grub
root=hd0,msdos1

grub rescue> set prefix=(hd0,msdos1)/boot/grub
grub rescue> set root=hd0,msdos1

grub rescue> insmod normal               # 加载 normal.mod
grub rescue> insmod linux                # 加载 linux 引导模块

grub rescue> linux /boot/vmlinuz-5.15.0 root=/dev/sda1
grub rescue> initrd /boot/initrd.img-5.15.0
grub rescue> boot                        # 启动内核
```

### 3.2 救援 Shell 能力

救援 shell 提供：
- **设备枚举**：`ls` 扫描所有 BIOS/UEFI 已知的磁盘设备
- **分区识别**：读取分区表（MBR、GPT）
- **文件系统读取**：如果加载了 FS 模块，则列出和读取文件
- **变量管理**：`set` / `unset` 修改 GRUB 内部变量
- **模块加载**：`insmod` 从任何发现的 FS 模块加载额外的 `.mod` 文件
- **内核引导**：直接 `linux`、`initrd` 和 `boot` 命令

### 3.3 常见救援 Shell 场景

| 问题 | 症状 | 修复 |
|-------|----------|------|
| `prefix` 错误 | `error: file '/boot/grub/i386-pc/normal.mod' not found` | `set prefix=(hd0,msdos1)/boot/grub`，然后是 `insmod normal` |
| 磁盘移动 | GRUB 启动但找不到 `grub.cfg` | 救援 shell 中 `configfile (hd1,msdos1)/boot/grub/grub.cfg` |
| 损坏的 MBR | `Error: no such disk` | 从 live ISO 启动并运行 `grub-install /dev/sda` |
| 加密的 /boot | LUKS 需要 crypto 模块 | `insmod luks`，`cryptomount (hd0,msdos2)` |
| 缺少内核 | `error: file '/boot/vmlinuz-*' not found` | 使用救援 shell 从 live ISO 通过 `linux` + `initrd` 手动引导 |

---

## 4. core.img 构建过程

### 4.1 grub-mkimage 调用

`grub-mkimage` 工具将原始内核与模块组装成最终的 `core.img`：

```bash
grub-mkimage \
  --output=/boot/grub/i386-pc/core.img \
  --prefix="(hd0,msdos1)/boot/grub" \
  --format=i386-pc \
  --compression=xz \
  biosdisk ext2 fat ntfs part_msdos normal search
```

这将产生一个压缩的映像，其中包含指定目录中的所有必要模块。

### 4.2 grub-install 步骤

`grub-install` 编排整个过程：

1. **检测平台**：i386-pc（BIOS）、x86_64-efi（UEFI）、arm64-efi 等。
2. **创建目录**：`/boot/grub/` 或 `\EFI\grub\`
3. **构建 core.img**：带有预装模块的内核映像
4. **计算块列表**：确定 core.img 在磁盘上的哪些扇区
5. **写入 MBR**：`boot.img` 进入设备的前 446 字节
6. **嵌入块列表**：将物理扇区列表写入 `boot.img` 和 `diskboot.img` 之间的空隙（LBA 1-62）
7. **写入 core.img**：将映像写入 `grub-install` 选择的扇区位置

### 4.3 磁盘布局（最终）

```
LBA 0:    [MBR + boot.img (446 bytes)] [partition table (64 bytes)] [0xAA55]
LBA 1:    [diskboot.img (512 bytes)] 
           ← 包含指向 kernel.img 扇区的块列表
LBA 2:    [lnxboot.img (512 bytes, 可选)] 
           ← 多引导头
LBA 3-62: [kernel.img, 压缩的模块] 
           ← 核心映像负载 (~30KB 未压缩)
```

---

## 5. mini-grub-core 实现

我们的 `mini-bootloader` 项目以简化的形式模拟了这些概念：

### 5.1 阶段 1（`stage1.h / .c`）

在 `stage1.c` 中实现，代表 MBR/ VBR 阶段：
- 零 LBA 的磁盘 I/O
- 分区表解析
- 活动分区选择
- 向阶段 2 的切换

### 5.2 阶段 2（`stage2.h / .c`）

在 `stage2.c` 中实现，代表 core.img：
- 多引导头验证（魔术 `0x1BADB002`）
- 内存映射设置（通过 e820 的 BIOS INT 15h）
- 内核加载到受保护模式内存（0x100000+）
- 命令行和模块传递给内核

### 5.3 模块系统（`grub_modules.h / .c`）

在 `grub_modules.c` 中实现，代表 GRUB 的模块加载器：
- 模块注册表（最多 32 个模块）
- 依赖图构建
- 拓扑排序解析
- 文件系统驱动程序注册

### 5.4 文件系统（`filesys_boot.h / .c`）

在 `filesys_boot.c` 中实现，代表 FS 驱动程序：
- FAT32 支持：引导扇区、FAT 链遍历、目录结构
- EXT2/EXT4 支持：超级块、inode 表、直接/间接块
- 文件读取原语：按路径打开/读取

---

## 6. 构建和测试

```bash
cd mini-bootloader
make

# 运行 MBR 仿真
./bin/mbr_boot_demo

# 运行多引导内核交接
./bin/multiboot_demo

# 运行 Linux 启动协议仿真
./bin/linux_boot_demo
```

---

## 7. 进一步阅读

- [GNU GRUB 手册](https://www.gnu.org/software/grub/manual/grub/grub.html)
- [GRUB 2 内部原理 (PDF)](https://www.gnu.org/software/grub/manual/)
- [core.img 块列表实现](https://git.savannah.gnu.org/cgit/grub.git/tree/grub-core/boot/i386/pc/diskboot.S)
- [GRUB 模块格式](https://www.gnu.org/software/grub/manual/grub/grub.html#Modules)
- [救援 Shell 文档](https://www.gnu.org/software/grub/manual/grub/grub.html#GRUB-only-offers-a-rescue-shell)
