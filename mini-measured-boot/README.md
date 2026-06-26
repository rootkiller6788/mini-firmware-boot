# mini-measured-boot — 度量启动 (C 语言实现)

> 参考 TPM 2.0 Specification, TCG PC Client Platform, Intel TXT Specification

mini-measured-boot 是一个纯 C99 实现的 TPM 2.0 度量启动教学库，
覆盖 PCR 银行管理、事件日志、SRTM/DRTM 信任根、TPM Locality 授权、
NV 存储、密钥管理和远程证明等核心模块。

## Module Status: COMPLETE ✅

- **include/ + src/**: 3,147 行 C 代码 (≥ 3,000 底线)
- **Tests**: 77 项测试全部通过 (0 失败)
- **L1-L6**: Complete
- **L7**: Complete (4 applications: UEFI variables, measured boot, auth sessions, PCR benchmark)
- **L8**: Partial+ (3 advanced topics: formal attestation properties, DA protection, NV global lock)
- **L9**: Partial (documented: Intel TXT, AMD SKINIT, TPM 2.0 specification compliance)

## 九层知识覆盖摘要

| Level | 名称 | 状态 | 条目数 |
|-------|------|------|--------|
| L1 | Definitions | Complete | 22 struct/typedef/enum |
| L2 | Core Concepts | Complete | 8 核心概念 |
| L3 | Engineering Structures | Complete | 8 数据结构+操作 |
| L4 | Standards/Theorems | Complete | 8 定理/标准验证 |
| L5 | Algorithms/Methods | Complete | 11 算法实现 |
| L6 | Canonical Problems | Complete | 6 经典工程问题 |
| L7 | Applications | Complete | 4 应用示例 |
| L8 | Advanced Topics | Partial+ | 3 进阶主题 |
| L9 | Industry Frontiers | Partial | 文档化 |

## 核心定理列表

| 定理 | 公式/陈述 | 验证位置 |
|------|----------|---------|
| PCR 链式哈希不可逆 | PCR_new = H(PCR_old \|\| H(data)) | `src/pcr_bank.c` |
| NV 计数器单调性 | C_{n+1} > C_n (anti-rollback) | `src/nv_storage.c` |
| KDFa PRF 安全性 | HMAC is PRF → KDFa is PRF (Bellare 2006) | `src/tpm_keys.c` |
| 证明协议新鲜性 | Pr[replay succeeds] = 2^{-256} | `src/attestation.c` |
| FIXEDTPM→FIXEDPARENT | ∀k: FIXEDTPM(k) ⇒ FIXEDPARENT(k) | `src/tpm_keys.c` |

## 核心算法列表

| 算法 | 复杂度 | 来源 | 实现 |
|------|--------|------|------|
| SHA-256 | O(n) | FIPS 180-4 | `src/pcr_bank.c` |
| HMAC-SHA256 | O(n) | RFC 2104 | `src/tpm_keys.c` |
| KDFa (SP800-108) | O(n) | NIST SP 800-108 | `src/tpm_keys.c` |
| PCR Extend | O(1) | TPM 2.0 Part 3 §18 | `src/pcr_bank.c` |
| Event Log Replay | O(m·n) | TCG EFI Spec §5 | `src/event_log.c` |
| NV Counter Inc | O(1) atomic | TCG PTP §8.5.1 | `src/nv_storage.c` |
| Key Seal/Unseal | O(n) | TPM 2.0 Part 3 §23 | `src/tpm_keys.c` |
| Challenge-Response | O(1) | TCG TAP §7 | `src/attestation.c` |

## 经典问题列表

| 问题 | 解决方案 | 示例 |
|------|---------|------|
| PCR 扩展链式哈希演示 | 多次扩展同一 PCR 展示不可逆性 | `examples/tpm_extend_demo.c` |
| 事件日志记录/重演/验证 | 完整的事件日志生命周期 | `examples/event_log_demo.c` |
| Intel TXT DRTM 启动流 | SRTM vs DRTM 对比 | `examples/txt_demo.c` |
| 端到端远程证明 | 挑战→引用→验证→属性检查 | `src/attestation.c` |

## 代码目录

```
include/
  tpm2_structs.h    TPM 2.0 核心结构体定义 (119 行)
  pcr_bank.h        PCR 银行管理 API (61 行)
  event_log.h       事件日志 API (TCG EFI 格式) (60 行)
  crtm_drtm.h       静态/动态信任根 API (70 行)
  tpm_locality.h    TPM Locality 授权 API (92 行)
  sha256.h          SHA-256 哈希实现 (22 行)
  nv_storage.h      NV 存储 API (92 行)
  tpm_keys.h        密钥管理 API (163 行)
  attestation.h     远程证明 API (129 行)
  hmac_tpm.h        HMAC-SHA256 API (38 行)

src/
  tpm2_structs.c    TPM 结构体辅助函数 (63 行)
  pcr_bank.c        PCR 扩展/读取/复位 + SHA-256 (277 行)
  event_log.c       事件日志管理实现 (119 行)
  crtm_drtm.c       SRTM/DRTM 启动流模拟 (199 行)
  tpm_locality.c    会话/授权/策略管理 (245 行)
  nv_storage.c      NV 存储完整实现 (417 行)
  tpm_keys.c        密钥管理 + KDFa + 密封 (577 行)
  attestation.c     远程证明协议实现 (404 行)

tests/
  test_all.c        77 项综合测试 (543 行)

examples/
  tpm_extend_demo.c  PCR 扩展链式哈希演示 (94 行)
  event_log_demo.c   事件日志记录/重演/验证演示 (105 行)
  txt_demo.c         Intel TXT DRTM 启动演示 (91 行)

benches/
  bench_pcr.c       PCR 性能基准 (78 行)

demos/
  mini-tpm-internals/     TPM 2.0 内部机制详解
  mini-measured-boot-flow/ 度量启动端到端流程

docs/
  course-alignment.md   规范章节参考映射
  tpm-architecture.md   TPM 2.0 架构参考
  knowledge-graph.md    九层知识覆盖表
  coverage-report.md    知识覆盖评估
  gap-report.md         缺失知识点列表
  course-tree.md        前置依赖树
```

## 九校课程映射

| 学校 | 课程 | 对应知识点 |
|------|------|-----------|
| MIT | 6.004 Computation Structures | 硬件→固件信任链, PCR 链式哈希 |
| MIT | 6.858 Computer Security | 信任根, 安全启动, 远程证明 |
| Stanford | CS 255 Cryptography | SHA-256, HMAC, KDFa |
| Berkeley | CS 161 Computer Security | TPM 架构, 授权模型 |
| CMU | 15-410 Operating Systems | 安全启动, 度量启动流程 |
| CMU | 15-445 Database Systems | NV 存储, 单调计数器 |
| UT Austin | CS 380D Distributed Systems | 远程证明协议 |
| ETH | 263-3501 Parallel Programming | 安全硬件信任根 |
| Cambridge | Part II: Security | TPM 规范, 平台安全 |
| 清华 | 操作系统 | 可信计算, 安全启动 |
| Georgia Tech | CS 6262 Network Security | 远程证明, 平台完整性验证 |

## 构建与运行

### 环境要求

- GCC (C99)
- GNU Make
- 无外部依赖 (仅 libc + libm)

### 运行测试 (一键)

```bash
make test
```

输出: `=== Results: 77 passed, 0 failed ===`

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

### 性能基准

```bash
make bench
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

### NV 存储

TPM NV 存储支持四种索引类型：
- **ORDINARY**: 标准读写数据块
- **COUNTER**: 单调递增计数器 (anti-rollback)
- **BITFIELD**: 位集字段 (单调 OR)
- **EXTEND**: PCR-like 链式哈希

### 密钥层级

```
Primary Seed → Primary Key (EK/SRK) → Child Keys → Sealed Data
   (platform)      (endorsement)      (storage)      (application)
```

### 远程证明

挑战-应答协议确保平台状态的可验证性：
1. Verifier 发送 challenge (含 nonce + 请求 PCR)
2. Prover 用 AIK 签名 PCR 值生成 quote
3. Verifier 验证: nonce 新鲜性 + 签名有效性 + PCR 对账

## 参考规范

- TPM 2.0 Library Specification (Parts 1-4), Rev 1.59
- TCG PC Client Platform TPM Profile (PTP), Rev 1.05
- TCG PC Client Platform Firmware Profile, Rev 1.05
- TCG EFI Platform Specification, Rev 1.06
- Intel TXT MLE Developer's Guide, Rev 013
- AMD64 APM Vol. 2: System Programming (SKINIT)
- NIST SP 800-108: Recommendation for Key Derivation
- RFC 2104: HMAC — Keyed-Hashing for Message Authentication
- RFC 4231: Identifiers and Test Vectors for HMAC-SHA-224/256/384/512
