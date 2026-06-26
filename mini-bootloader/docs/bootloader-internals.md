# Bootloader Internals — 引导加载器内部原理

> Bootloaders 是计算机系统的入口。它们桥接了裸露的硬件初始化（由固件执行）和完全成熟的操作系统内核之间的鸿沟。本文档探讨了引导加载器设计的架构、协议和内部数据结构。

---

## 1. 多阶段架构

引导加载器几乎都是**多阶段的**，以一个严重受大小限制（< 512 字节）的最小初始阶段开始，并逐渐扩大范围。

### 1.1 为什么是多阶段？

| 阶段 | 大小限制 | 环境 | 功能 |
|-------|-------------|-------------|--------|
| **阶段 1** | 446 字节（MBR）/ 512 字节（VBR） | 实模式 | 加载阶段 2；无 FS 知识；仅 BIOS INT 13h 磁盘 I/O |
| **阶段 1.5**（GRUB） | 一个扇区 | 实模式 | 理解一个 FS；直接从 FS 加载阶段 2，绕过块列表 |
| **阶段 2** | ~30KB+ | 实模式 → 受保护模式 | FS 驱动程序，内核解析，多引导/引导协议支持 |
| **阶段 3**（某些设计） | 变化 | 受保护模式 / 32 位 | 完整模块系统，GUI/图形模式，网络引导 |

**阶段 1 的限制：** MBR 只有 446 个字节的可执行代码（剩余 64 个字节用于 4 个分区条目 + 2 个字节用于 `0xAA55` 签名）。不足以实现 ASCII 字符串，更不用说文件系统驱动程序了。

### 1.2 典型控制流

```
固件 (BIOS / UEFI / CoreBoot)
    │
    ▼
[阶段 1: MBR / VBR] （实模式，< 512 字节）
    │ 通过 INT 13h 加载阶段 1.5 或阶段 2
    ▼
[阶段 1.5: FS-aware 映像] （可选 — GRUB 特定）
    │ 直接从文件系统加载阶段 2
    ▼
[阶段 2: 完整引导加载器] （实模式 → 受保护模式）
    │ 加载内核、initrd、DTB
    ▼
[操作系统内核] （受保护模式 / 长模式）
```

---

## 2. 文件系统驱动程序

引导加载器必须嵌入文件系统驱动程序才能从磁盘读取内核映像。与操作系统内核不同，它们使用极简的只读实现。

### 2.1 支持的常见文件系统

| FS | 引导加载器 | 说明 |
|----|------------|-------------|
| **FAT32** | 通用（GRUB, U-Boot, systemd-boot） | 用于 EFI 系统分区（ESP）。简单，几乎在所有地方都受支持。 |
| **EXT2/3/4** | GRUB, U-Boot, syslinux | Linux 默认根 FS。读取超级块和 inode 表以解析文件路径。 |
| **NTFS** | GRUB | Windows 引导配置数据。只读支持。 |
| **ISO 9660** | GRUB, isolinux | CD/DVD 引导。包含 El Torito 引导目录。 |
| **Btrfs** | GRUB | 写时复制 FS。支持压缩和子卷。 |
| **ZFS** | GRUB | 具有高级池/数据集结构的 Solaris/BSD FS。 |
| **XFS** | GRUB | SGI 的日志 FS。用于 RHEL/CentOS 服务器。 |
| **SquashFS** | GRUB | 压缩的只读 FS。常用于 live CD。 |

### 2.2 FAT32 内部原理

```
引导扇区 (扇区 0):
  [跳转指令: 3 字节] [OEM 名称: 8 字节] [BPB: 53 字节]
  BPB（BIOS 参数块）:
      偏移 11: bytes_per_sector         (通常为 512)
      偏移 13: sectors_per_cluster      (通常 8, 16, 32, 64)
      偏移 14: reserved_sectors         (通常 32)
      偏移 16: fat_count                (通常 2)
      偏移 36: fat_size_sectors         (每个 FAT 的扇区)
      偏移 44: root_dir_first_cluster   (通常 2)

布局:
  [保留区域: 32 个扇区] [FAT #1] [FAT #2] [数据区域]
  数据区域 = 保留 + (fat_count × fat_size)
  第一个数据扇区 = 数据区域 + ((root_cluster - 2) × sectors_per_cluster)
```

**文件读取算法：**
```
1. 从目录条目中获取起始簇号
2. 虽然簇 != 0x0FFFFFF8（文件结束链）:
     a. 从数据区域读取簇数据
     b. 在 FAT 中查找下一个簇：FAT[当前簇] & 0x0FFFFFFF
     c. 如果 FAT[当前簇] == 0x0FFFFFF7：坏的簇（错误）
       如果 FAT[当前簇] >= 0x0FFFFFF8：结束
```

### 2.3 EXT2 内部原理

```
布局: [引导块] [块组 0] [块组 1] ...

每个块组:
  [超级块] [组描述符] [块位图] [inode 位图] [inode 表] [数据块]

超级块:
  s_inodes_count         inode 总数
  s_blocks_count         块总数
  s_log_block_size       块大小 = 1024 << s_log_block_size
  s_blocks_per_group     每个组的块数
  s_inodes_per_group     每个组的 inode 数
  s_magic                0xEF53

Inode:
  i_mode                  文件类型和权限
  i_size                  文件大小，以字节为单位
  i_block[0..11]          直接块指针
  i_block[12]             单间接块指针（块 → 块的数组）
  i_block[13]             双间接块指针
  i_block[14]             三重间接块指针

目录条目:
  [inode 编号] [条目长度] [名称长度] [文件类型] [名称...]
```

---

## 3. 内核映像格式

引导加载器支持多种内核映像格式。每种都有一种独特的结构，引导加载器在将控制权移交给内核之前必须对其进行解析。

### 3.1 bzImage（大 zImage — Linux x86）

```
偏移      内容
0x0000    实模式设置代码（setup.bin）
              引导扇区 / 设置头
              16 位实模式代码
0x0200    "HdrS" 魔术（如果协议 >= 2.00）
0x0202    引导协议版本
0x0206    加载标志、代码 32 启动等。
...
setup_sects * 512        设置代码结尾
setup_sects * 512 + 1    压缩内核（vmlinux.bin）
                          （以 gzip, bzip2, LZMA, xz, LZO, LZ4 等压缩）
```

**引导序列：**
1. 将设置的 `setup_sects` 复制到 `0x90000`
2. 跳转到设置代码（`0x90200`）
3. 设置代码收集硬件信息（内存、视频、APM）
4. 切换到受保护模式
5. 解压 vmlinux.bin（内联）
6. 跳转到解压后的内核入口点

### 3.2 ARM Image / zImage

```
Image（原始二进制）:
  [32 字节头] [ARM 机器代码]
  入口点：偏移 0x00

zImage（压缩）:
  [压缩的内核] [解压 stub (piggy.o)] [zImage 头]
  在运行时自解压
```

### 3.3 vmlinux（ELF 内核）

由一些引导加载器（例如，通过 `kexec`）直接使用。是一个 ELF 文件：
- `.text` 段 — 可执行内核代码
- `.data` — 初始化的数据
- `.bss` — 零初始化的数据
- ELF 头中的入口点

### 3.4 U-Boot FIT 镜像（扁平镜像树）

引导加载器和内核之间的一种多功能多组件格式：

```
FIT 镜像:
  [FDT 头] [镜像树 (.its 编译的)]
       ├── kernel@1  — 压缩的内核二进制文件 (gzip, lzma...)
       ├── fdt@1     — 扁平设备树
       ├── ramdisk@1 — initrd / initramfs
       └── config@1  — 默认引导配置
```

U-Boot 的 `bootm` 命令：
1. 验证 FIT 映像完整性（SHA / RSA）
2. 将内核提取到 RAM
3. 将 FDT 和 initrd 加载到正确的地址
4. 跳转到内核入口点，传递 FDT 指针

---

## 4. Initrd / Initramfs

### 4.1 概念

**initrd**（初始 RAM 磁盘）：加载到 RAM 中的一个压缩的、基于块的文件系统映像。在内核引导过程中挂载，以提供早期用户空间功能——加载驱动程序、运行启动脚本、解密根文件系统，以及设置可移动的根 FS。

### 4.2 引导加载器的职责

```
1. 从磁盘加载 initrd.img 到 RAM
2. 将物理地址传递给内核（通过引导参数或设备树）
3. 确保地址不冲突：
   — initrd 不得覆盖内核
   — initrd 不得在指向目标的范围内（0x37FFFFFF）
   — 不得与 ACPI / 内存映射 IO 范围冲突
```

### 4.3 实际示例

一个典型的 Linux `grub.cfg` 条目：
```
menuentry 'Ubuntu' {
    set root='(hd0,msdos1)'
    linux   /boot/vmlinuz-5.15.0 root=/dev/sda1
    initrd  /boot/initrd.img-5.15.0
}
```

引导加载器：
1. 加载 vmlinuz-5.15.0 到 0x00100000
2. 加载 initrd.img-5.15.0 到 0x02000000
3. 设置内核命令行：`root=/dev/sda1`
4. 通过多引导或 Linux 引导协议跳转到内核

---

## 5. mini-bootloader 项目结构

我们的教育项目以一种简化的、类似于模拟的方法来模拟这些系统：

```
include/
├── stage1.h           MBR / VBR 阶段 1 结构和函数
├── stage2.h           多引导阶段 2 结构和函数
├── linux_boot.h       Linux x86 引导协议（bzImage 格式）
├── grub_modules.h     GRUB 模块系统和依赖解析
└── filesys_boot.h     简化文件系统（FAT32, EXT2）

src/
├── stage1.c           MBR 创建、分区扫描、引导模拟
├── stage2.c           多引导头验证、内存映射设置、内核交接
├── linux_boot.c       bzImage 解析、E820 内存映射、initrd 处理
├── grub_modules.c     模块注册表、依赖排序、FS 驱动程序
└── filesys_boot.c     文件系统挂载、目录列表、文件读取

examples/
├── mbr_boot_demo.c    MBR + 分区表演示
├── multiboot_demo.c   多引导内核交接演示
└── linux_boot_demo.c  Linux 引导协议演示
```

每个模块的详细说明，请参阅 `include/` 目录中的头文件以及 `src/` 目录中对应的实现文件。
