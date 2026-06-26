# mini-reset-vector — 复位向量与 CPU 初始化

> 参考 Intel 64 and IA-32 Architectures Software Developer's Manual, ARM Architecture Reference Manual

---

## 1. 概述 / Overview

复位向量 (Reset Vector) 是 CPU 在上电或复位后执行的第一条指令的地址。在 x86 架构中，复位向量固定为 0xFFFFFFF0（实模式寻址为 F000:FFF0）；在 ARM 架构中，复位向量是异常向量表的第一个入口。

This module covers the reset vector mechanism, CPU context initialization, GDT/IDT setup, and CPU mode switching (Real -> Protected -> Long), providing a simulated foundation for understanding the boot process.

---

## 2. x86 复位向量 / x86 Reset Vector

### 2.1 硬件复位流程 / Hardware Reset Sequence

x86 CPU 完全复位的步骤：

```
ASSERT RESET# pin
    |
    v
内部 BIST (Built-In Self Test)
    |
    v
微码加载初始状态
    |
    v
设置寄存器初始值:
  EIP      = 0x0000FFF0
  CS.base  = 0xFFFF0000
  CS.limit = 0xFFFF
  CR0      = 0x60000010  (PE=0, CD=1, NW=1)
  EFLAGS   = 0x00000002
    |
    v
首次取指: 物理地址 = CS.base + EIP = 0xFFFFFFF0
```

### 2.2 为什么是 0xFFFFFFF0？ / Why 0xFFFFFFF0?

1. **4 GB 地址空间顶部**: 复位向量位于 32 位地址空间的倒数第 16 字节
2. **兼容性**: 从 8086 时代起，1 MB 地址空间顶部 (0xFFFF0) 就是复位向量；在 386+ 上通过段基址扩展到 4 GB 顶部
3. **空间布局**: 1 MB 以下留给传统设备；3-4 GB 留给固件；顶部 64 KB 是 boot block

### 2.3 复位向量的实际内容 / Actual Content at Reset Vector

```asm
; 位于 0xFFFFFFF0 — 只有 16 字节空间!
F000:FFF0  EA 00 01 F0 FF    JMP FAR F000:0100   ; 远跳转到固件入口
F000:FFF5  00 00 00 00 00    ; 填充
F000:FFFA  00 00 00 00 00    ; 更多填充
```

16 字节不足以做任何有用事，所以这里通常只是一个远跳转指令。

---

## 3. CPU 上下文 / CPU Context

### 3.1 CPUContext 结构 / CPUContext Structure

```c
typedef struct {
    uint32_t eax, ebx, ecx, edx;  // 通用寄存器
    uint32_t esi, edi;            // 源/目的变址寄存器
    uint32_t esp, ebp;            // 栈指针/基址指针
    uint32_t eip;                 // 指令指针
    uint32_t eflags;              // 标志寄存器
    uint32_t cr0, cr3, cr4;       // 控制寄存器
    CPUMode  current_mode;        // 当前 CPU 模式
} CPUContext;
```

### 3.2 通用寄存器 / General Purpose Registers

| 寄存器 | 名称 | 常见用途 |
|--------|------|----------|
| EAX | Accumulator | 返回值、算术运算 |
| EBX | Base | 基址指针、DS 段数据 |
| ECX | Count | 计数器 (循环/移位) |
| EDX | Data | I/O 操作、乘除法扩展 |
| ESI | Source Index | 字符串操作源指针 |
| EDI | Destination Index | 字符串操作目的指针 |
| ESP | Stack Pointer | 栈顶指针 |
| EBP | Base Pointer | 栈帧基址指针 |

### 3.3 控制寄存器 / Control Registers

| 寄存器 | 关键位 | 功能 |
|--------|--------|------|
| CR0 | bit 0 (PE) | 保护模式使能 |
| CR0 | bit 16 (WP) | 写保护 |
| CR0 | bit 31 (PG) | 分页使能 |
| CR3 | — | 页目录基址寄存器 (PDBR) |
| CR4 | bit 5 (PAE) | 物理地址扩展 |
| CR4 | bit 20 (SMEP) | 管理模式执行保护 |

---

## 4. GDT 与段描述符 / GDT and Segment Descriptors

### 4.1 GDT 结构 / GDT Structure

GDT (Global Descriptor Table) 是保护模式下的核心数据结构：

```
+-------------------+
| Null Descriptor   |  选择子 0x00 — 必须为空
+-------------------+
| Kernel Code       |  选择子 0x08 — Ring 0, 可执行
+-------------------+
| Kernel Data       |  选择子 0x10 — Ring 0, 可读写
+-------------------+
| User Code         |  选择子 0x18 — Ring 3, 可执行
+-------------------+
| User Data         |  选择子 0x20 — Ring 3, 可读写
+-------------------+
| TSS               |  选择子 0x28 — 任务状态段
+-------------------+
```

### 4.2 段描述符格式 / Segment Descriptor Format (64-bit)

```
63    56 55 52 51    48 47     40 39   32
+--------+----+--------+---------+------+
| Base   |Flags| Limit  | Access  | Base |
| 31:24  |     | 19:16  | Byte    | 23:16|
+--------+----+--------+---------+------+
31                                   0
+--------------------------------------+
| Base 15:0           | Limit 15:0     |
+--------------------------------------+
```

### 4.3 实模式 vs 保护模式寻址 / Real vs Protected Mode Addressing

| 模式 | 地址计算 | 最大内存 |
|------|----------|----------|
| 实模式 (Real) | Physical = Segment * 16 + Offset | 1 MB (20-bit) |
| 保护模式 (Protected) | Linear = Segment Base + Offset (via GDT) | 4 GB (32-bit) |
| 长模式 (Long) | Linear = RIP + displacement (flat model) | 2^48 (256 TB) |

---

## 5. IDT 与中断处理 / IDT and Interrupt Handling

### 5.1 IDT 结构 / IDT Structure

IDT (Interrupt Descriptor Table) 包含 256 个门描述符：

| 类型 | 描述 | 示例 |
|------|------|------|
| 中断门 (Interrupt Gate) | 硬件中断，禁用 IF | IRQ0 (Timer) |
| 陷阱门 (Trap Gate) | 软件中断，保持 IF | INT 3 (Breakpoint) |
| 任务门 (Task Gate) | 硬件任务切换 | 双故障处理 |

### 5.2 常见中断向量 / Common Interrupt Vectors

| 向量 | 名称 | 描述 |
|------|------|------|
| 0x00 | #DE | 除法错误 |
| 0x03 | #BP | 断点 |
| 0x06 | #UD | 无效操作码 |
| 0x08 | #DF | 双故障 |
| 0x0D | #GP | 通用保护故障 |
| 0x0E | #PF | 页故障 |
| 0x20 | IRQ0 | 定时器中断 |

---

## 6. CPU 模式切换 / CPU Mode Switching

### 6.1 实模式 -> 保护模式 / Real -> Protected Mode

切换步骤：

1. 禁用中断 (`CLI`)
2. 用 `LGDT` 加载 GDT 寄存器
3. 设置 CR0 的 PE 位 (bit 0)
4. 执行远跳转 (`JMP FAR`) 以刷新 CS 寄存器
5. 加载各段选择子 (DS, ES, FS, GS, SS)
6. 用 `LIDT` 加载 IDT（可选）
7. 设置栈指针 ESP

### 6.2 保护模式 -> 长模式 / Protected -> Long Mode

切换步骤：

1. 禁用分页 (清除 CR0.PG)
2. 设置 CR4.PAE = 1
3. 设置 IA32_EFER.LME = 1
4. 加载 CR3 指向 PML4 表
5. 启用分页 (设置 CR0.PG)
6. 远跳转到 64 位代码段

### 6.3 模式切换模拟 / Simulated Mode Switch

本项目的 `cpu_switch_mode()` 函数模拟了模式切换：

```c
bool cpu_switch_mode(CPUContext *ctx, CPUMode target_mode) {
    if (ctx->current_mode == CPU_MODE_REAL && target_mode == CPU_MODE_PROTECTED) {
        ctx->cr0 |= 0x00000001;          // 设置 PE 位
        ctx->current_mode = CPU_MODE_PROTECTED;
        return true;
    }
    if (ctx->current_mode == CPU_MODE_PROTECTED && target_mode == CPU_MODE_LONG) {
        ctx->cr4 |= 0x00000020;          // 设置 PAE 位
        ctx->current_mode = CPU_MODE_LONG;
        return true;
    }
    return false;
}
```

---

## 7. ARM 异常向量 / ARM Exception Vectors

### 7.1 ARMv8 异常向量表 / ARMv8 Exception Vector Table

ARM 的异常向量与 x86 不同，位于内存低地址：

```
偏移    异常类型        说明
0x000   Reset           上电/复位
0x004   Undefined Inst  未定义指令
0x008   SVC (Supervisor) 系统调用
0x00C   Prefetch Abort  指令预取异常
0x010   Data Abort      数据访问异常
0x014   (Reserved)      ARM 保留
0x018   IRQ             外部中断请求
0x01C   FIQ             快速中断请求
```

### 7.2 ARM vs x86 复位对比 / ARM vs x86 Reset Comparison

| 特性 | x86 | ARM |
|------|-----|-----|
| 复位向量地址 | 0xFFFFFFF0 | 0x00000000 / 0xFFFF0000 |
| 初始模式 | 实模式 (16-bit) | EL3 (最高特权级) |
| 地址空间 | 按段寻址 | 平坦地址空间 |
| 固件入口 | 远跳转指令 | 异常向量表第一条指令 |
| 向量重定位 | 无 (固定硬件) | VBAR_EL3 寄存器可重定位 |

---

## 8. 参考资源 / References

- Intel 64 and IA-32 Architectures Software Developer's Manual, Volume 3A: System Programming Guide
- ARM Architecture Reference Manual ARMv8, for ARMv8-A architecture profile
- AMD64 Architecture Programmer's Manual, Volume 2: System Programming
- Intel Firmware Support Package (FSP) External Architecture Specification
- UEFI Platform Initialization (PI) Specification, Volume 2: DXE
