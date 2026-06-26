# mini-measured-boot-flow — 度量启动端到端流程

> 参考 TCG PC Client Platform Firmware Profile, Intel TXT Specification, AMD SKINIT Specification

## 目录

1. [概述](#概述)
2. [SRTM: 静态启动度量链](#srtm-静态启动度量链)
3. [DRTM: 动态启动度量链](#drtm-动态启动度量链)
4. [事件日志 (Event Log)](#事件日志)
5. [PCR 映射详解](#pcr-映射详解)
6. [远程证明 (Remote Attestation)](#远程证明)
7. [Sealed Storage (密封存储)](#密封存储)
8. [完整启动流](#完整启动流)

---

## 概述

度量启动 (Measured Boot) 是可信计算的关键概念。在系统启动过程中，
每个阶段都在将控制权传递给下一个阶段之前测量 (hash) 下一个阶段的
代码和数据。测量值被扩展到 TPM 的 PCR 中，同时记录在事件日志中。

```
   Power-On / Reset
        │
        ▼
┌─────────────────────────┐
│  CRTM (Immutable)       │  ◄── 平台信任根
│  Measure BIOS           │
│  Extend PCR0            │
└───────────┬─────────────┘
            ▼
┌─────────────────────────┐
│  BIOS / UEFI Firmware   │
│  Measure Option ROMs     │
│  Extend PCR2             │
└───────────┬─────────────┘
            ▼
┌─────────────────────────┐
│  Option ROM Execution   │
│  Measure Boot Manager   │
│  Extend PCR4             │
└───────────┬─────────────┘
            ▼
┌─────────────────────────┐
│  Boot Manager / MBR     │
│  Measure Bootloader      │
│  Extend PCR4/8           │
└───────────┬─────────────┘
            ▼
┌─────────────────────────┐
│  Bootloader (GRUB/SD)   │
│  Measure OS Kernel       │
│  Extend PCR8             │
└───────────┬─────────────┘
            ▼
┌─────────────────────────┐
│  OS Kernel Loaded       │
│  PCR 0-8 包含完整       │
│  SRTM 度量链             │
└─────────────────────────┘
```

---

## SRTM: 静态启动度量链

SRTM (Static Root of Trust for Measurement) 从平台复位开始，
CRTM (Core Root of Trust for Measurement) 作为第一个被执行的代码。

### CRTM

CRTM 是最为基础的信任锚点，通常实现为：
- 存储在 BIOS 芯片中的不可变代码
- 在 CPU 复位后最先执行
- 负责度量剩余的 BIOS 代码并扩展到 PCR0
- 自身必须被认为是可信的 (Trusted Computing Base)

### SRTM 度量链步骤

| 步骤 | 执行组件           | 度量对象        | PCR |
|------|-------------------|----------------|------|
| 1    | CRTM              | BIOS 固件      | 0   |
| 2    | BIOS              | 平台配置       | 1   |
| 3    | BIOS              | Option ROM     | 2   |
| 4    | BIOS              | ROM 配置       | 3   |
| 5    | BIOS              | Boot Manager   | 4   |
| 6    | Boot Manager      | Boot Config    | 5   |
| 7    | Boot Manager      | Bootloader     | 4,8 |
| 8    | Bootloader        | OS Kernel      | 8   |
| 9    | Bootloader        | Kernel cmdline | 9   |

### 关键 PCR 的语义

**PCR0**: CRTM + 平台固件度量
- 包含系统中所有平台固件代码的度量
- 如果 BIOS 被修改（如刷入恶意固件），PCR0 的值将改变

**PCR4/8**: Boot Manager 和 OS Loader
- PCR4: 启动管理器代码（bootmgr, MBR）
- PCR8: OS Loader 代码（GRUB stage2, SD-boot）
- 这两个 PCR 是远程证明最常引用的 PCR

**PCR7**: 安全启动策略
- 包含 UEFI Secure Boot 配置
- PK, KEK, db, dbx 的度量

### SRTM 的限制

1. **有限的 PCR 空间**: 只有 PCR0-7 是 SRTM 专用的
2. **扩大的 TCB**: 整个 BIOS + Option ROM 都是 TCB，攻击面大
3. **不可中断**: 必须在系统启动时完成，无法在运行时重新度量
4. **平台复位依赖性**: 所有 PCR 在平台复位时重置

---

## DRTM: 动态启动度量链

DRTM (Dynamic Root of Trust for Measurement) 允许在任何时间点发起
一个"被度量启动"，不需要平台复位。这是 Intel TXT 和 AMD SKINIT 的
核心技术。

### Intel TXT 流程

```
  Normal OS Running
        │
        ▼ 执行 GETSEC[SENTER]
┌────────────────────────────────────┐
│  1. CPU 进入 SENTER 状态           │
│     - 停止所有逻辑处理器            │
│     - 禁用 SMI/中断                │
│     - 清空 CPU 缓存               │
│     - 启用 CRTM 验证               │
└──────────────┬─────────────────────┘
               ▼
┌────────────────────────────────────┐
│  2. 加载 SINIT ACM                 │
│     - Intel 签名的 AC 模块         │
│     - 验证平台配置                  │
│     - 度量到 PCR17                 │
└──────────────┬─────────────────────┘
               ▼
┌────────────────────────────────────┐
│  3. 加载 MLE (Measured Launch Env) │
│     - MLE Header 验证              │
│     - 度量到 PCR18                 │
│     - 跳转到 MLE entry point      │
└──────────────┬─────────────────────┘
               ▼
┌────────────────────────────────────┐
│  4. MLE 执行                       │
│     - 验证自身完整性               │
│     - 度量 OS 到 PCR19            │
│     - 建立信任环境                 │
└────────────────────────────────────┘
```

### AMD SKINIT 流程

```
  Normal OS Running
        │
        ▼ 执行 SKINIT (Secure Loader + SLB)
┌────────────────────────────────────┐
│  1. SKINIT 指令启动                │
│     - 验证 SL 签名 (SPI flash)     │
│     - 清空 TLB/缓存               │
│     - 禁用中断和调试               │
└──────────────┬─────────────────────┘
               ▼
┌────────────────────────────────────┐
│  2. Secure Loader (SL) 执行        │
│     - 度量 SLB 到 PCR17           │
│     - 加载并验证 SLB              │
└──────────────┬─────────────────────┘
               ▼
┌────────────────────────────────────┐
│  3. SLB (Secure Loader Block) 执行 │
│     - 度量 MLE 到 PCR18           │
│     - 初始化安全环境               │
└──────────────┬─────────────────────┘
               ▼
┌────────────────────────────────────┐
│  4. MLE 执行                       │
│     - 后续度量到 PCR19-22         │
└────────────────────────────────────┘
```

### DRTM PCR 分配

| PCR  | DRTM 度量内容          |
|------|----------------------|
| 17   | SINIT ACM / Secure Loader 度量 |
| 18   | MLE (Measured Launch Environment) 度量 |
| 19   | MLE 启动的 Trusted OS 度量 |
| 20   | OS Kernel 度量        |
| 21   | Application 度量      |
| 22   | Application Data 度量 |

### DRTM 与 SRTM 对比

| 特性         | SRTM                    | DRTM                           |
|-------------|-------------------------|--------------------------------|
| 触发时机     | 平台复位时              | 运行时（任意时刻）              |
| PCR          | 0-7                     | 17-22                          |
| 信任根       | CRTM (BIOS 不可变代码)   | CPU 微码 + ACM/SL               |
| TCB 大小     | 大 (BIOS + Option ROM)  | 小 (ACM + MLE)                  |
| 重置         | 平台复位清零            | PCR_Reset 可单独清零            |
| 技术实现     | 平台固件                | Intel TXT / AMD SKINIT          |
| 中断影响     | 无法中断                | 可中断恢复                      |

---

## 事件日志 (Event Log)

### 为什么需要事件日志？

PCR 只能存储固定长度的哈希值。当 PCR 值不匹配时，仅知道"有东西变了"。
事件日志记录每次测量的具体内容，使得验证者可以：
1. 重演 PCR 计算以验证 PCR 值
2. 识别哪些组件被修改
3. 判断修改是否为预期的（如合法更新）

### 事件日志结构 (TCG EFI 格式)

```
┌──────────────────────────────────────────┐
│  TCG_PCR_EVENT2 Header                   │
├──────────────────────────────────────────┤
│  PCR Index (4 bytes)                     │
│  Event Type  (4 bytes)                   │
│  Digest Count (4 bytes)                  │
│  - Hash Alg ID                           │
│  - Digest value                          │
│  Event Size (4 bytes)                    │
├──────────────────────────────────────────┤
│  Event Data (variable length)            │
│  - 如 "CRTM", "BIOS", "GRUB" 等描述     │
└──────────────────────────────────────────┘
```

### 事件类型

| 事件类型                  | 说明                    |
|-------------------------|-------------------------|
| EV_S_CRTM_CONTENTS      | CRTM 度量                  |
| EV_POST_CODE            | POST 代码                |
| EV_EFI_PLATFORM_FIRMWARE_BLOB | UEFI 固件卷         |
| EV_EFI_BOOT_SERVICES_DRIVER | UEFI 引导服务驱动程序 |
| EV_EFI_BOOT_SERVICES_APP | UEFI 引导服务应用程序     |
| EV_EFI_ACTION           | EFI 动作                      |
| EV_EFI_VARIABLE_AUTHORITY | 安全启动变量          |
| EV_IPL                  | 初始程序加载                 |

### 事件日志验证

```
验证者收到:
  - 引用:  PCR 值 + TPM 签名
  - 事件日志: 所有测量事件的记录

验证步骤:
  1. 验证 TPM 签名 (证明 Quote 来自真实 TPM)
  2. 从事件日志重演 PCR 计算
  3. 比较重演的 PCR 值与引用中的 PCR 值
  4. 逐一审查事件日志中的每个组件
  5. 根据策略决定是否信任平台
```

---

## PCR 映射详解

### 完整的 PCR 分配表 (TCG PC Client)

```
PCR Index   | 分配                              |  重置条件
------------|-------------------------------------------|--------------
PCR0        | SRTM: CRTM + BIOS Firmware                | 平台复位
PCR1        | SRTM: Platform Config (CMOS, ACPI)        | 平台复位
PCR2        | SRTM: Option ROMs / UEFI Drivers          | 平台复位
PCR3        | SRTM: ROM Configuration                  | 平台复位
PCR4        | SRTM: Boot Manager Code                   | 平台复位
PCR5        | SRTM: Boot Manager Config / GPT           | 平台复位
PCR6        | SRTM: Platform Manufacturer Specific      | 平台复位
PCR7        | SRTM: Secure Boot Policy                  | 平台复位
------------|-------------------------------------------|--------------
PCR8        | SRTM: OS Loader (GRUB, Windows Boot Mgr)  | 平台复位
PCR9        | SRTM: Kernel Command Line / Config        | 平台复位
------------|-------------------------------------------|--------------
PCR10       | IMA: OS Integrity Measurement (baseline)  | 平台复位
PCR11       | 平台特定                                  | 平台复位
PCR12       | 平台特定                                  | 平台复位
------------|-------------------------------------------|--------------
PCR17       | DRTM: SINIT ACM / Secure Loader           | Locality 4 或 PCR_Reset
PCR18       | DRTM: MLE                                | Locality 4 或 PCR_Reset
PCR19       | DRTM: Trusted OS                          | Locality 4 或 PCR_Reset
PCR20       | DRTM: OS Kernel                           | Locality 4 或 PCR_Reset
PCR21       | DRTM: Application                        | Locality 4 或 PCR_Reset
PCR22       | DRTM: Application Data                    | Locality 4 或 PCR_Reset
------------|-------------------------------------------|--------------
PCR23       | 平台特定 / 保留                           | 平台特定
```

---

## 远程证明 (Remote Attestation)

### TPM2_Quote 流程

```
  Verifier (远程服务器)                         Attester (被测平台)
       │                                              │
       │  1. 发送 nonce + PCR 选择                   │
       ├─────────────────────────────────────────────►│
       │                                              │
       │                       2. TPM2_Quote 执行     │
       │                          - 读取所选 PCR      │
       │                          - 用 AIK 签名       │
       │                          - 生成 Quote 结构   │
       │                                              │
       │  3. 返回 Quote + 事件日志                    │
       │◄─────────────────────────────────────────────┤
       │                                              │
       │  4. 验证 AIK 证书链                            │
       │  5. 验证 Quote 签名                         │
       │  6. 重演事件日志                             │
       │  7. 比较 PCR 值与已知良好值                  │
       │  8. 返回验证结果                              │
       │                                              │
```

Quote 结构包含:
- TPM_ST_ATTEST_QUOTE (TPM 生成的签名结构)
- 选定的 PCR 选择
- 选定的 PCR 的复合摘要
- Nonce (防重放)
- TPM 时钟信息
- TPM 签名

---

## Sealed Storage (密封存储)

密封存储允许数据仅在特定的平台状态下才能被解密。

### TPM2_Create / TPM2_Unseal

```
创建密封对象:
  TPM2_Create(
    parentHandle,       // 父密钥
    authValue,          // 数据授权
    sensitive,          // 要密封的数据
    publicTemplate: {
      type: KEYEDHASH,
      authPolicy: <policy_digest>,  // 基于 PCR 状态的策略
      seedValue: ...
    }
  ) -> sealed_object

解封:
  TPM2_Unseal(
    itemHandle,         // 密封对象的句柄
    authSession: <policy_session>  // 满足 authPolicy 的会话
  ) -> sensitive_data
```

### Policy 示例: 仅在安全启动时解封

```
PolicySession_Start(trial)
PolicyPCR(pcr7, expected_digest)  // PCR7 = Secure Boot Policy
PolicyGetDigest() -> policy_digest

创建对象时使用此 policy_digest 作为 authPolicy
解封时需要 PCR7 匹配（即安全启动已启用）
```

---

## 完整启动流

```
时间轴 ─────────────────────────────────────────────────────►

复位 ──► CRTM ──► BIOS ──► OptionROM ──► BootMgr ──► Bootloader ──► OS
  │       │        │          │            │            │          │
  │       ▼        ▼          ▼            ▼            ▼          ▼
  │     PCR0     PCR0       PCR2         PCR4         PCR4      PCR8
  │              PCR1       PCR3         PCR5         PCR8      PCR9
  │                                       PCR8
  │
  │  .......................... SRTM Chain .........................
  │  (PCR 0-9)
  │
  │                                                SENTER/SKINIT
  │                                                    │
  │                                                    ▼
  │                                                  PCR17
  │                                                  PCR18
  │                                                  PCR19
  │                                                  PCR20
  │                                                  PCR21
  │                                                  PCR22
  │
  │                    ˋˋˋˋˋˋˋˋˋˋˋˋˋ DRTM Chain ˋˋˋˋˋˋˋˋˋˋˋˋˋˋ
  │                                 (PCR 17-22)
  ▼
时间 ─►
```

---

## 参考

- TCG PC Client Platform Firmware Profile Specification, Family "2.0"
- TCG EFI Protocol Specification
- Intel TXT (Trusted Execution Technology) Measured Launched Environment Developer's Guide
- AMD64 Architecture Programmer's Manual, Vol. 2: System Programming (SKINIT)
- TPM 2.0 Library Specification Part 3: Commands (Sections on PCR, Quote, Create, Unseal)
- TCG Infrastructure Working Group: TPM Keys for Platform Identity
