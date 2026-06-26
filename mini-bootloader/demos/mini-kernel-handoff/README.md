# Mini Kernel Handoff — Linux 内核交接详解

> 交接过程（引导加载器将控制权移交给操作系统内核的精确时刻）是系统启动中最关键的时刻。本指南涵盖了从实模式切换到带分页的 32 位保护模式的完整过程。

---

## 1. 引导加载器 → 内核过渡概述

### 1.1 交接时的系统状态

| 寄存器 | 值 | 含义 |
|----------|-------|---------|
| **EAX** | `0x2BADB002` (Multiboot2) 或 `0x1BADB002` (Multiboot) | 魔法签名——内核验证引导加载器 |
| **EBX** | 指向多重引导信息结构的指针 | 指向内存映射、命令行、模块的指针 |
| **CS** | `0x08` (平坦代码段选择器) | 32 位受保护模式代码段，基址 = 0x00000000，限制 = 4GB |
| **DS/ES/FS/GS/SS** | `0x10` (平坦数据选择器) | 32 位受保护模式数据段，基址 = 0x00000000，限制 = 4GB |
| **ESP** | 内核栈顶 | 由引导加载器设置的临时栈 |
| **CR0** | `0x00000011` (PE=1, PG=0, 无分页) | 受保护模式已启用，分页已禁用 |
| **A20** | 已启用 | A20 地址线已由引导加载器打开 |

### 1.2 交接序列（时间轴）

```
[BIOS 阶段]
  POST → 初始化硬件 → INT 19h → 从 LBA 0 加载 MBR

[阶段 1 — MBR/VBR]
  重定位到 0x0600 → 扫描分区表 → 加载活动分区的第一个扇区

[阶段 2 — core.img / GRUB / 引导加载器]
  加载内核映像 → 设置多引导信息 → 切换到受保护模式
  → 启用 A20 → 设置 GDT → 跳转到内核入口点

[内核 — 早期启动]
  验证引导加载器 → 设置页表 → 启用分页 → 跳转到更高一半
  → 架构初始化 → start_kernel() → 用户空间 init
```

---

## 2. 受保护模式切换序列

### 2.1 GDT 设置

在内核入口点执行的第一条指令之前，引导加载器必须建立全局描述符表（GDT）：

```
; 典型的引导加载器 GDT
gdt:
    .null:  dq 0x0000000000000000   ; NULL 描述符（必需）
    .code:  dq 0x00CF9A000000FFFF   ; 32 位代码段：基址=0, 限制=4GB, DPL=0
    .data:  dq 0x00CF92000000FFFF   ; 32 位数据段：基址=0, 限制=4GB, DPL=0

gdt_desc:
    dw gdt_end - gdt - 1            ; 限制
    dd gdt                          ; 基址
```

**为什么基址 = 0？** 在平坦内存模型中，所有段描述符的基址都为 0。因此逻辑地址 = 线性地址——简化了分页设置。

### 2.2 切换到受保护模式

```asm
   cli                       ; 保护过渡期间禁用中断
   lgdt [gdt_desc]           ; 加载 GDT 寄存器

   mov eax, cr0
   or  eax, 0x01             ; 设置 PE（保护启用）位
   mov cr0, eax

   jmp 0x08:protected_entry  ; 远跳转以使用新代码段刷新 CS

[bits 32]
protected_entry:
   mov ax, 0x10              ; 使用平面数据段加载段寄存器
   mov ds, ax
   mov es, ax
   mov fs, ax
   mov gs, ax
   mov ss, ax

   mov esp, KERNEL_STACK_TOP ; 设置内核栈

   ; 准备内核参数
   mov eax, MULTIBOOT_MAGIC
   mov ebx, multiboot_info_ptr
   jmp KERNEL_ENTRY          ; 进入内核
```

---

## 3. 分页设置

内核通常通过映射物理内存的前几个 MB 来设置初始页表。Linux 使用具有 2MB 大页面的临时页表（对于 PAE）或早期引导的 4KB 页面。

### 3.1 页表结构（32 位非 PAE）

```
CR3 → 页目录 (1024 个条目, 每条目 4 字节, 4096 字节)
       │
       ├── PDE[0]  → 页表 #0 (1024 个条目) → 4KB 页面 0..1023 (0-4MB)
       ├── PDE[1]  → 页表 #1 (1024 个条目) → 4KB 页面 1024..2047 (4-8MB)
       └── ...
```

### 3.2 分页设置代码

```c
void setup_paging(void) {
    // 页目录：身份映射前 4MB
    uint32_t *pgd = (uint32_t *)0x00100000;  // 页全局目录
    uint32_t *pte = (uint32_t *)0x00101000;  // 第一个页表

    for (int i = 0; i < 1024; i++) {
        pte[i] = (i * 0x1000) | 0x03;  // 存在, 可读写
    }

    pgd[0] = (uint32_t)pte | 0x03;     // 第一个 PDE 指向 PTE

    // 启用分页
    asm volatile (
        "mov %0, %%cr3\n"
        "mov %%cr0, %%eax\n"
        "or  $0x80000000, %%eax\n"     // 设置 PG 位
        "mov %%eax, %%cr0\n"
        : : "r" (pgd) : "eax"
    );
}
```

---

## 4. E820 内存映射

Linux 内核依赖于引导加载器通过 BIOS `INT 0x15, AX=0xE820` 收集的 E820 内存映射。

### 4.1 E820 条目格式

```
偏移    大小        字段       说明
0x00    8 字节     base       基址（64 位物理）
0x08    8 字节     length     长度（64 位物理）
0x10    4 字节     type       内存类型
0x14    4 字节     acpi       ACPI 3.0 扩展属性
```

### 4.2 内存类型

| 类型 | 值 | 含义 | 内核标签 |
|------|-------|---------|--------------|
| 1 | 可用内存 | 操作系统可自由使用 | `usable` |
| 2 | 保留 | BIOS 保留，不可用 | `reserved` |
| 3 | ACPI 回收 | 读取 ACPI 表后可回收 | `acpi reclaimable` |
| 4 | ACPI NVS | 非易失性存储，需保存 | `acpi nvs` |
| 5 | 不可用（坏内存） | 物理上不可用 | `unusable` |

### 4.3 典型内存映射（256MB 系统）

```
[基址]         [长度]         [类型]
0x000000000000 0x0009FC00 (639K)  可用 - 常规内存
0x000009FC00   0x00000400 (1K)    保留 - 扩展 BIOS 数据区域 (EBDA)
0x00000E8000   0x00018000 (96K)   保留 - ROM（视频、BIOS、SMM）
0x0000100000   0x0FEF0000 (254M)  可用 - 扩展内存
0x000FFF0000   0x00010000 (64K)   保留 - 内存映射 I/O
0x00FEC00000   0x00010000 (64K)   保留 - IOAPIC
0x00FEE00000   0x00010000 (64K)   保留 - 本地 APIC
0x00FFFC0000   0x00040000 (256K)  保留 - BIOS ROM
```

---

## 5. 内核参数传递

### 5.1 Linux 启动协议参数

```c
struct boot_params {
    uint8_t  setup_sects;        // 0x01F1: 设置扇区大小（0=4 个设置扇区）
    uint16_t root_flags;         // 0x01F2: root_rw, 只读等。
    uint32_t syssize;            // 0x01F4: 系统大小，以 16 字节段落为单位
    uint16_t ram_size;           // 0x01F8: 扩展内存大小，以 KB 为单位
    uint16_t vid_mode;           // 0x01FA: 视频模式
    uint16_t root_dev;           // 0x01FC: 根文件系统设备
    uint16_t boot_flag;          // 0x01FE: 必须为 0xAA55

    /* 偏移 0x0200 — "HdrS"（header） */
    uint8_t  jump[2];            // 0x0200: 跳转到启动代码（EB xx）
    uint8_t  header[4];          // 0x0202: "HDrS" - 引导加载器 ID 标志
    uint16_t version;            // 0x0206: 启动协议版本

    /* 偏移 0x0208+ — 加载器特定 */
    uint32_t realmode_swtch;     // 0x0208: 实模式切换钩子
    uint16_t start_sys_seg;      // 0x020C: 实模式内核段
    uint16_t kernel_version;     // 0x020E: 指向内核版本字符串的指针
    uint8_t  type_of_loader;     // 0x0210: 引导加载器 ID
    uint8_t  loadflags;          // 0x0211: 引导协议标志
    uint16_t setup_move_size;    // 0x0212: 从实模式移至受保护模式
    uint32_t code32_start;       // 0x0214: 32 位受保护模式入口点
    uint32_t ramdisk_image;      // 0x0218: initrd 加载地址
    uint32_t ramdisk_size;       // 0x021C: initrd 大小
    uint32_t bootsect_kludge;    // 0x0220: 已过时，必须为零
    uint16_t heap_end_ptr;       // 0x0224: 设置代码堆的结束
    uint8_t  ext_loader_ver;     // 0x0226: 扩展引导加载器版本
    uint8_t  ext_loader_type;    // 0x0227: 扩展引导加载器 ID
    uint32_t cmd_line_ptr;       // 0x0228: 32 位命令行指针
    uint32_t initrd_addr_max;    // 0x022C: 最高的 initrd 加载地址
    // ... 更多字段（请参阅 linux/bootparam.h）
};
```

### 5.2 引导加载器类型 ID

| ID | 引导加载器 |
|----|------------|
| 0x00 | 旧版（版本 < 2.00） |
| 0x01 | Loadlin |
| 0x10 | LILO |
| 0x11 | SYSLINUX |
| 0x20 | pxelinux |
| 0x30 | Etherboot |
| 0x40 | ELILO |
| 0x50 | kexec-tools |
| 0x70 | GRUB |
| 0x71 | GRUB2 |
| 0x72 | Das U-Boot |
| 0xFF | 未知 |

---

## 6. 内核入口点流程

### 6.1 从引导加载器到内核的交接

```
引导加载器入口点 → 32 位内核代码 (startup_32)
```

**x86 上 `startup_32` 的作用：**

```asm
startup_32:
    cld                     ; 清除方向标志（字符串操作向上）
    cli                     ; 禁用中断
    movl $(stack_top), %esp ; 设置栈
    pushl $0                ; 重置 EFLAGS
    popfl

    /* 保存 Multiboot 魔法和信息指针 */
    movl %eax, multiboot_magic
    movl %ebx, multiboot_info

    /* 设置页表（身份映射 + 高地址映射） */
    call setup_paging

    /* 启用分页 */
    movl $swapper_pg_dir, %eax
    movl %eax, %cr3
    movl %cr0, %eax
    orl  $0x80000000, %eax
    movl %eax, %cr0

    /* 跳转到虚拟地址空间 */
    ljmp $BOOT_CS, $1f
1:
    movl %eax, %cr4          ; 在需要时设置 PAE

    /* 调用 C 入口点 */
    call i386_start_kernel   ; 进入 C 代码
```

### 6.2 内核初始化序列

```
startup_32 (汇编)              — 架构引导
  → i386_start_kernel (C)      — ISA 初始化
    → setup_arch()              — 架构特定设置
      → setup_memory()          — 页面初始化
      → paging_init()           — 页表初始化
    → trap_init()               — 中断/异常
    → mm_init()                 — 内存管理
    → sched_init()              — 调度器
    → init_IRQ()                — 中断请求
    → init_timers()             — 定时器
    → console_init()            — 控制台
    → rest_init()               — 启动 init 进程
```

---

## 7. initrd / initramfs 交接

### 7.1 什么是 initrd？

**Initial RAM Disk** 是在内核引导过程中挂载的临时根文件系统，提供在挂载实际根 FS 之前加载模块和运行启动脚本的能力。

### 7.2 传递 initrd 给内核

引导加载器必须提供 initrd 的物理地址和大小：

```
在 setup_header 中:
ramdisk_image = 0x02000000（32 位物理地址）
ramdisk_size  = 0x01000000（以字节为单位的大小，例如 16 MB）
```

### 7.3 内核侧的恢复过程

```c
// arch/x86/kernel/setup.c (简化版)
void __init setup_arch(char **cmdline_p) {
    // ...
    if (boot_params.hdr.ramdisk_image) {
        phys_addr_t ramdisk_phys =
            boot_params.hdr.ramdisk_image;
        unsigned long ramdisk_size =
            boot_params.hdr.ramdisk_size;

        reserve_bootmem(ramdisk_phys, ramdisk_size,
                        BOOTMEM_DEFAULT);
        initrd_start = ramdisk_phys;
        initrd_end   = ramdisk_phys + ramdisk_size;
    }
}
```

然后内核解压 `initramfs` 并将内容填充到 `rootfs`（tmpfs 实例）中，该实例成为初始 `/`。`/init` 从 initramfs 中运行。

---

## 8. mini-kernel-handoff 实现

我们的 `mini-bootloader` 项目以简化的形式演示了这些概念：

### 8.1 Linux 引导上下文（`linux_boot.h / .c`）

`LinuxBootContext` 结构包含：
- 设置头指针（`setup_data`）
- 内核二进制指针（`kernel_data`）
- initrd 数据和大小（`initrd_data`，`initrd_size`）
- E820 内存映射（`e820_map[128]`）
- 内核加载地址和入口点

### 8.2 引导模拟序列（`linux_boot_demo.c`）

1. 初始化上下文（`linux_boot_init`）
2. 构建设置头（魔法 `HdrS`，启动标志 `0xAA55`）
3. 解析并验证设置头（`linux_parse_setup_header`）
4. 加载内核映像（`linux_load_kernel` — 模拟）
5. 设置内核命令行（`linux_set_cmdline`）
6. 配置 E820 内存映射
7. 跳转到内核（`linux_boot_kernel` — 带日志的模拟）

### 8.3 阶段 2 交接（`stage2.h / .c`）

`stage2_jump_to_kernel` 函数模拟：
- EAX = `0x2BADB002`（Multiboot2 魔法）
- EBX = 多引导信息结构指针
- CR0 寄存器设置（PE=1 表示受保护模式）
- 用于代码/数据段的 GDT 条目
- ESP = `0x0009FC00` 的栈设置

---

## 9. 构建和测试

```bash
cd mini-bootloader
make

# 模拟完整的 Linux 引导交接
./bin/linux_boot_demo

# 模拟带有内存映射的多引导交接
./bin/multiboot_demo
```

输出演示了完整的引导序列——从 MBR 到内核入口——包含解释每个步骤的日志消息。

---

## 10. 进一步阅读

- [Linux x86 引导协议](https://www.kernel.org/doc/html/latest/x86/boot.html) — 官方内核文档
- [Multiboot 规范](https://www.gnu.org/software/grub/manual/multiboot/multiboot.html) — 来自 GNU GRUB 的标准多重引导接口
- [Linux 内核初始化](https://0xax.gitbooks.io/linux-insides/content/Initialization/) — 来自 Linux 深入解析的深入指南
- [Intel 64 和 IA-32 架构软件开发人员手册](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html)，第 3A 卷：系统编程指南
- [E820 内存映射规范](https://www.uefi.org/specs/ACPI/6.4/15_System_Address_Map_Interfaces/uefi-getmemorymap-boot-services-function.html)
