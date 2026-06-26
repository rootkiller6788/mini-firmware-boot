# mini-measured-boot — 度量启动 (C 语言实现)

> 参考 TPM 2.0 Specification, TCG PC Client Platform, Intel TXT Specification

mini-measured-boot 是一个纯 C99 实现的 TPM 2.0 度量启动教学库，
包含 PCR 银行管理、事件日志、静态/动态信任根和 TPM locality 授权
等核心模块。

## 代码目录

```
include/
  tpm2_structs.h    TPM 2.0 核心结构体定义
  pcr_bank.h        PCR 银行管理 API
  event_log.h       事件日志 API (TCG EFI 格式)
  crtm_drtm.h       静态/动态信任根 API
  tpm_locality.h    TPM Locality 授权 API
  sha256.h          SHA-256 哈希实现 (自包含)

src/
  tpm2_structs.c    TPM 结构体辅助函数
  pcr_bank.c        PCR 扩展/读取/复位实现 (含 SHA-256)
  event_log.c       事件日志管理实现
  crtm_drtm.c       SRTM/DRTM 启动流模拟
  tpm_locality.c    会话/授权/策略管理

examples/
  tpm_extend_demo.c  PCR 扩展链式哈希演示
  event_log_demo.c   事件日志记录/重演/验证演示
  txt_demo.c         Intel TXT DRTM 启动演示

demos/
  mini-tpm-internals/     TPM 2.0 内部机制详解 (250+ lines)
  mini-measured-boot-flow/ 度量启动端到端流程 (250+ lines)

docs/
  course-alignment.md   规范章节参考映射
  tpm-architecture.md   TPM 2.0 架构参考
```

## 模块总览

| 模块 | 文件 | 说明 |
|------|------|------|
| TPM 2.0 Structures | `include/tpm2_structs.h`, `src/tpm2_structs.c` | TPM2B、算法 ID、命令/响应码、证明结构 |
| PCR Bank | `include/pcr_bank.h`, `src/pcr_bank.c` | PCR 银行 (SHA-256)、扩展操作、静态/动态 PCR 管理 |
| Event Log | `include/event_log.h`, `src/event_log.c` | TCG EFI 事件日志、重演验证、EFI 事件类型 |
| SRTM / DRTM | `include/crtm_drtm.h`, `src/crtm_drtm.c` | CRTM 初始化、SRTM 度量链、Intel TXT DRTM 模拟 |
| TPM Locality | `include/tpm_locality.h`, `src/tpm_locality.c` | Locality 0-4、授权会话、策略命令、字典攻击防护 |

## 构建与运行

### 环境要求

- GCC (C99)
- GNU Make
- 无外部依赖 (仅 libc + libm)

### 构建所有演示程序

```bash
make all
```

### 运行示例

```bash
make run-extend    # PCR 扩展演示
make run-event     # 事件日志演示
make run-txt       # Intel TXT DRTM 演示
```

### 构建产物

```
bin/
  tpm_extend_demo    PCR 扩展演示
  event_log_demo     事件日志演示
  txt_demo           Intel TXT 演示
```

### 清理

```bash
make clean
```

## 核心概念

### PCR 扩展操作

```
PCR_new = SHA256(PCR_old || measurement_hash)
```

链式哈希设计确保 PCR 值的不可逆性，每个测量事件都密码学地绑定到
所有之前的测量。

### 静态信任根 (SRTM)

在平台复位时启动的度量链：
CRTM → BIOS → Option ROM → MBR → Bootloader → OS (PCR0-7)

### 动态信任根 (DRTM)

运行时由 CPU 指令 GETSEC[SENTER] 或 SKINIT 触发的度量链：
SINIT ACM → MLE → Trusted OS (PCR17-22)

动态 PCR 可通过 PCR_Reset 清零（无需平台复位）。

### 事件日志验证

1. 从 TPM Quote 获得签名的 PCR 值
2. 从事件日志重演 PCR 计算
3. 比较重演值 vs 引用值
4. 逐一审核事件日志条目
5. 根据策略判断平台可信状态

### TPM 授权

TPM 2.0 支持三种授权：
- 密码授权 (Password)
- HMAC 授权 (防重放)
- 策略授权 (Policy: PCR 绑定、locality、口令)

## 参考规范

- TPM 2.0 Library Specification (Parts 1-4), Rev 1.59
- TCG PC Client Platform TPM Profile (PTP), Rev 1.05
- TCG PC Client Platform Firmware Profile, Rev 1.05
- TCG EFI Platform Specification, Rev 1.06
- Intel TXT MLE Developer's Guide, Rev 013
- AMD64 APM Vol. 2: System Programming (SKINIT)
