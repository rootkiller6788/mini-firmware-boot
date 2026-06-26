# Legacy BIOS 深度解析

> 参考 Phoenix BIOS, IBM PC/AT Technical Reference, bios.h specification

## 1. 概述

Legacy BIOS (Basic Input/Output System) 是 x86 PC 平台上最早的固件标准，起源于 1981 年的 IBM PC。BIOS 以 16 位实模式运行，存储在 ROM/Flash 芯片中，映射到内存地址空间的高端区域（通常是 0xF000:0x0000 到 0xF000:0xFFFF，64KB）。

BIOS 的核心职责：
- **POST (Power-On Self Test)**：硬件初始化和自检
- **IVT (Interrupt Vector Table)**：提供标准化的软件中断服务接口
- **Bootstrap**：从引导设备加载并执行 MBR（Master Boot Record）
- **BDA (BIOS Data Area)**：维护系统硬件状态信息

## 2. 实模式内存布局

```
+---------------------------+ 0x100000 (1MB)
| Extended Memory           |
| (above 1MB, via A20 gate)|
+---------------------------+ 0x0F0000 (960KB)
| System BIOS ROM           | <--- 64KB BIOS 镜像
+---------------------------+ 0x0E0000
| Option ROMs (Video, NIC)  | <--- 附加设备 ROM
+---------------------------+ 0x0C0000
| Video Memory (128KB)      | <--- VGA 帧缓冲
+---------------------------+ 0x0A0000 (640KB)
| Conventional Memory       |
| (640KB, DOS 可用)          |
|                           |
| ... 0x7C00: Boot Sector   | <--- MBR 加载地址
| ... 0x0500: BIOS Stack    |
| ... 0x0400: BDA           | <--- BIOS Data Area (256 bytes)
| ... 0x0000: IVT           | <--- 中断向量表 (1024 bytes)
+---------------------------+ 0x000000
```

## 3. IVT (中断向量表 Interrupt Vector Table)

IVT 占据内存最底部的 1024 个字节（地址 0x00000–0x003FF），包含 256 个中断向量，每个向量 4 字节（段:偏移）。

### 3.1 结构

```
struct IVT {
    uint16_t offset;   // 偏移地址 (低 16 位 IP)
    uint16_t segment;  // 段地址 (CS)
};  // 共 256 个条目 = 1024 bytes
```

### 3.2 关键中断向量

| 中断号 | 名称 | 功能 |
|--------|------|------|
| 0x00 | 除零错误 | CPU 异常 |
| 0x08 | IRQ0 | 系统定时器 (18.2 Hz) |
| 0x09 | IRQ1 | 键盘中断 |
| 0x0E | IRQ6 | 软盘控制器 |
| **0x10** | **Video Services** | 显示模式设置、光标控制、字符输出 |
| **0x11** | **Equipment Check** | 返回设备列表字 |
| **0x12** | **Memory Size** | 返回基本内存大小 (KB) |
| **0x13** | **Disk Services** | 扇区读写、格式化 |
| **0x14** | Serial Services | 串口 I/O |
| **0x15** | System Services | 等待、内存移动、扩展内存 |
| **0x16** | Keyboard | 读取按键 |
| **0x17** | Printer | 打印输出 |
| **0x19** | **Bootstrap** | 从引导设备加载 MBR |
| **0x1A** | Time/RTC | 读取/设置实时时钟 |

### 3.3 INT 0x10 视频服务详解

```
AH = 0x00: Set Video Mode   (AL = mode: 0x03=80x25 text, 0x13=320x200)
AH = 0x01: Set Cursor Shape (CH=start scanline, CL=end scanline)
AH = 0x02: Set Cursor Pos   (BH=page, DH=row, DL=col)
AH = 0x06: Scroll Up        (AL=lines, BH=attribute)
AH = 0x09: Write Char+Attr  (AL=char, BH=page, BL=attr, CX=count)
AH = 0x0E: Teletype Output  (AL=char, BL=color)
```

### 3.4 INT 0x13 磁盘服务详解

```
输入:
  AH = 0x02 (读) / 0x03 (写) / 0x00 (复位)
  AL = 扇区数 (1-128)
  CH = 柱面号低 8 位
  CL = 柱面高 2 位 (bits 6-7) | 扇区号 (bits 0-5)
  DH = 磁头号
  DL = 驱动器号 (0x00=软盘A, 0x80=硬盘0)
  ES:BX = 数据缓冲区地址

输出:
  CF = 0 成功, CF = 1 失败 (AH=错误码)
```

CHS 寻址限制: 1024 柱面 × 256 磁头 × 63 扇区 ≈ 8GB。

## 4. BDA (BIOS Data Area)

BDA 位于 0x0040:0x0000（物理地址 0x00400），占 256 字节。包含：

| 偏移 | 大小 | 内容 |
|------|------|------|
| 0x00 | 16B | COM 端口基地址 (4 个) |
| 0x08 | 16B | LPT 端口基地址 (4 个) |
| 0x10 | 2B | 设备列表 (Equipment List) |
| 0x13 | 2B | 基本内存大小 (KB) |
| 0x17 | 1B | 键盘 Shift 标志 |
| 0x1A | 2B | 键盘缓冲区头指针 |
| 0x1C | 2B | 键盘缓冲区尾指针 |
| 0x1E | 32B | 键盘环形缓冲区 (16 字) |
| 0x3F | 1B | 软盘电机状态 |
| 0x49 | 1B | 当前视频模式 |
| 0x4A | 2B | 屏幕列数 |
| 0x62 | 1B | 当前显示页 |

### Equipment List (0x10) 位定义

```
Bit 0:      IPL 磁盘驱动器已安装
Bit 1:      数学协处理器 (FPU) 存在
Bit 2:      PS/2 鼠标存在
Bit 3:      保留 (POST 中为 0)
Bit 4-5:    初始视频模式
             00=EGA/VGA, 01=40列CGA, 10=80列CGA, 11=MDA
Bit 6-7:    软盘驱动器数量 (00=1, 01=2, 10=3, 11=4)
Bit 8:      DMA 支持
Bit 9-11:   RS232 串口数量
Bit 12:     游戏端口存在
Bit 13:     串行打印机存在
Bit 14-15:  并口打印机数量
```

## 5. POST (Power-On Self Test) 序列

BIOS 上电后按以下顺序执行：

### Stage 1: 早期硬件初始化
1. **CPU 测试 (POST 0x01)**: 验证寄存器、标志位、基本指令
2. **CMOS 校验和 (POST 0x02)**: 检查实时时钟 RAM 完整性
3. **DMA 初始化 (POST 0x03)**: 编程 8237 DMA 控制器
4. **基本 64KB RAM 测试 (POST 0x04)**: 确保 BIOS 栈可用

### Stage 2: 内存测试
5. **基内存测试 (POST 0x10+)**: 从 0x1000 开始测试至 640KB，写入/读取模式 (AA, 55, FF, 00)
6. **扩展内存测试 (POST 0x2E)**: 测试 1MB 以上内存，通过 A20 门

### Stage 3: 设备枚举
7. **键盘初始化 (POST 0x05)**: 8042 控制器 BAT，设置键盘 LED
8. **视频 BIOS 调用 (POST 0x0C)**: 扫描 C000:0000 区域寻找 VGA ROM，执行其初始化代码
9. **其他 Option ROM 扫描**: 扫描 C800–F000 寻找网络卡、SCSI 卡等 Option ROM

### Stage 4: 系统 BIOS 初始化
10. **中断向量表设置 (POST 0x3F)**: 填充 IVT 的 256 个向量
11. **BDA 初始化 (POST 0x4F)**: 设置设备信息、内存大小、端口地址

### Stage 5: 引导
12. **Bootstrap (POST 0x7F)**: 调用 INT 0x19，从引导设备加载 MBR 到 0x7C00

## 6. MBR (Master Boot Record) Bootstrap

MBR 位于磁盘 LBA 0（CHS 0/0/1），共 512 字节：

```
偏移        大小    内容
0x000      446B    可执行引导代码
0x1BE      16B     分区表条目 1
0x1CE      16B     分区表条目 2
0x1DE      16B     分区表条目 3
0x1EE      16B     分区表条目 4
0x1FE      2B      引导签名 (0xAA55)
```

BIOS 加载 MBR 流程：
1. INT 0x19 调用 INT 0x13 AH=0x02，读取 CHS 0/0/1 到 ES:BX=0x0000:0x7C00
2. 检查 0x7DFE 处的字是否为 0xAA55
3. 是 → `jmp 0x0000:0x7C00` 跳转到 MBR 代码执行
4. 否 → 打印 "Missing operating system" 或尝试下一个启动设备

## 7. 实现要点

本项目的 legacy_bios 模块提供纯 C 语言的 BIOS 仿真：

- `bios_init_ivt()`: 用默认处理器 (0xF000:xxxx) 初始化 256 个 IVT 向量
- `bios_set_interrupt()`: 安装自定义中断处理器
- `bios_int10h_video()`: 模拟所有主要 INT 0x10 子功能
- `bios_int13h_disk()`: 模拟 CHS 寻址的磁盘读写操作
- `bios_int19h_bootstrap()`: 模拟从磁盘加载 MBR 的引导过程
- `bios_post()`: 执行完整的 POST 序列，初始化 IVT 和 BDA
- `bios_print_bda()`: 格式化转储 BIOS Data Area 内容

## 8. 参考资料

- IBM PC/AT Technical Reference Manual (1984)
- Phoenix BIOS 4.0 User's Manual
- "The Undocumented PC" by Frank van Gilluwe
- Ralf Brown's Interrupt List (RBIL)
- osdev.org: BIOS, IVT, BDA, POST
