# mini-boot-attestation — 启动证明 (C 语言实现)

> 参考 TPM 2.0 Spec Part 1, TCG TAP (Trusted Attestation Protocol), IETF RATS

## Module Status: COMPLETE

- **L1-L6**: Complete
- **L7**: Complete (3 applications + 3 demos)
- **L8**: Partial (DICE layered attestation implemented)
- **L9**: Partial (documented in docs/)

## 模块表 (Module Overview)

| # | 模块 | 头文件 | 源文件 | 描述 |
|---|------|--------|--------|------|
| 1 | **TPM Quote** | `tpm_quote.h` | `tpm_quote.c` | TPM2_Quote 生成、签名、验证；PCR 复合摘要 |
| 2 | **AIK Identity** | `aik_identity.h` | `aik_identity.c` | EK 创建、AIK 创建、Privacy CA 证书颁发 |
| 3 | **Attest Protocol** | `attest_protocol.h` | `attest_protocol.c` | 挑战-响应远程证明协议 |
| 4 | **Verifier Service** | `verifier_service.h` | `verifier_service.c` | 设备注册、策略管理、机群证明验证 |
| 5 | **RATS** | `rats.h` | `rats.c` | IETF RATS 概念：证据生成、评估、依赖方接口 |
| 6 | **Event Log** | `eventlog.h` | `eventlog.c` | TCG Crypto Agile Event Log 处理和重放 |
| 7 | **Merkle PCR** | `merkle_pcr.h` | `merkle_pcr.c` | PCR Bank Merkle 树与包含证明 |
| 8 | **DICE** | `dice.h` | `dice.c` | DICE 分层证明：UDS/CDI/证书链 |

## 目录结构

```
mini-boot-attestation/
│
├── include/
│   ├── tpm_quote.h          # TPM2_Quote 数据结构 + 函数
│   ├── aik_identity.h       # EK / AIK / Privacy CA 类型
│   ├── attest_protocol.h    # 远程证明协议
│   ├── verifier_service.h   # 证明服务 (舰船管理)
│   └── rats.h               # IETF RATS 概念
│
├── src/
│   ├── tpm_quote.c          # Quote 创建/签名/验证 (180+ 行)
│   ├── aik_identity.c       # 密钥创建/凭证制作/激活 (180+ 行)
│   ├── attest_protocol.c    # 挑战/响应/验证/策略 (240+ 行)
│   ├── verifier_service.c   # 设备数据库/策略更新 (260+ 行)
│   └── rats.c               # 证据生成/评估/依赖方 (240+ 行)
│
├── examples/
│   ├── tpm_quote_demo.c     # Quote 创建、签名、事件日志重放
│   ├── attest_demo.c        # 完整挑战-响应证明流程
│   └── privacy_ca_demo.c    # Privacy CA 协议模拟
│
├── demos/
│   ├── mini-remote-attestation/
│   │   └── README.md        # 远程证明深入解析 (250+ 行)
│   └── mini-attestation-service/
│       └── README.md        # 证明服务与机群管理 (250+ 行)
│
├── docs/
│   ├── course-alignment.md  # TPM 2.0 / TCG TAP / IETF RATS / Keylime 对齐
│   └── attestation-fundamentals.md # 证明基础概念
│
├── README.md                # 本文件
└── Makefile                 # 构建 3 个 demo 示例
```

## 快速开始

```bash
# 构建所有示例
make

# 运行 TPM Quote 示例
make run-quote

# 运行证明示例
make run-attest

# 运行 Privacy CA 示例
make run-privacy-ca

# 清理
make clean
```

## 概念图

```
┌──────────────────────────────────────────────┐
│              制造商 CA (Manufacturer CA)       │
│               ● 签名 EK 证书                   │
└──────────────────┬───────────────────────────┘
                   │ EK 证书
                   ▼
┌──────────────────────────────────────────────┐
│              TPM 芯片 (TPM Chip)              │
│  ┌─────────────────────────────────────────┐ │
│  │  EK (Endorsement Key) — 唯一出厂密钥      │ │
│  │  SRK (Storage Root Key) — 平台拥有者密钥   │ │
│  │  AIK (Attestation Identity Key) — 证明密钥│ │
│  │  PCR[0..23] — 平台配置寄存器              │ │
│  └─────────────────────────────────────────┘ │
│                     │                         │
│       TPM2_Quote(pcr_select, nonce)           │
│       → 签名后的 PCR 状态证明                  │
└──────────────────┬───────────────────────────┘
                   │ Quote + EventLog
                   ▼
┌──────────────────────────────────────────────┐
│              证明服务 (Verifier Service)       │
│  ┌─────────────────────────────────────────┐ │
│  │  1. 验证 AIK 证书链                       │ │
│  │  2. 验证 Quote 签名                       │ │
│  │  3. 比较 PCR 值 vs 已知良好值              │ │
│  │  4. 重放事件日志                          │ │
│  │  5. 策略判断 → TRUSTED / UNTRUSTED        │ │
│  └─────────────────────────────────────────┘ │
│                     │                         │
│                AttestDB                       │
│  ┌─────────────────────────────────────────┐ │
│  │  device_id → ek_pub_hash                 │ │
│  │  device_id → expected_pcr_values[24]     │ │
│  │  device_id → fw_whitelist[]              │ │
│  │  device_id → policy_rules[]              │ │
│  └─────────────────────────────────────────┘ │
└──────────────────┬───────────────────────────┘
                   │ TRUSTED / UNTRUSTED
                   ▼
┌──────────────────────────────────────────────┐
│            依赖方 (Relying Party)              │
│  ● 基于证明结果做决策                           │
│  ● 网络访问控制、负载均衡、编排决策              │
└──────────────────────────────────────────────┘
```

## 证明流程 (Attestation Flow)

```
1. 挑战 (Challenge)
   Verifier → Attester: { nonce[32], pcr_selection_mask }

2. 报价 (Quote)
   Attester: TPM2_Quote(AIK, nonce, pcr_selection)
   → 签名证明结构 + PCR 复合摘要

3. 响应 (Response)
   Attester → Verifier: { Quote, EventLog, AIK_Cert }

4. 验证 (Verification)
   Verifier:
   ├── 检查 nonce 匹配
   ├── 验证 AIK 证书链 (MFR CA → EK → Privacy CA → AIK)
   ├── 验证 Quote 签名
   ├── 比较 PCR 值 vs 预期值
   ├── 重放事件日志
   └── 策略判断 (ALLOW / DENY)

5. 结果 (Result)
   Verifier → Relying Party: TRUSTED / UNTRUSTED / UNKNOWN
```

## 技术规范

| 规范 | 版本 | 相关章节 |
|------|------|---------|
| TPM 2.0 Part 1 | rev. 01.83 | §16 Attestation, §28 Remote Attestation |
| TPM 2.0 Part 3 | rev. 01.83 | TPM2_Quote, TPM2_Certify, TPM2_MakeCredential |
| TCG TAP | v1.0 r1.16 | Trusted Attestation Protocol |
| IETF RATS | RFC 9334 | Remote ATtestation procedureS |
| NIST SP 800-155 | Dec 2011 | BIOS Integrity Measurement Guidelines |
| TCG PC Client | v1.05 | PCR allocation, event log format |
| Keylime | v7+ | Linux IMA remote attestation |

## 编码规范

- **C99 标准**, 仅依赖 libc + libm
- **类型**: `PascalCase` (如 `TPMQuote`, `AttestVerifier`)
- **函数**: `snake_case` (如 `tpm_quote_create`, `attest_verify`)
- **常量**: `UPPER_SNAKE_CASE` (如 `TPM_GENERATED_VALUE`, `AIK_KEY_SIZE`)
- **头文件保护**: `#ifndef X_H` / `#define X_H` / `#endif`
- 所有头文件包含 `#include <stdbool.h>`

## 模拟说明

该项目使用**模拟密码学** (伪哈希、伪RSA) — 这些不是密码学安全的实现。
它们仅用于演示证明协议的数据流和控制流。生产环境应使用真正的 TPM 库
（如 `tpm2-tss`）和真实密钥。

## 九层知识覆盖 (Knowledge Coverage)

| Level | 名称 | 状态 | 覆盖内容 |
|-------|------|------|---------|
| **L1** | Definitions | **Complete** | 8 头文件: struct/typedef/enum/API 声明全覆盖 |
| **L2** | Core Concepts | **Complete** | TPM Quote, AIK Identity, Attestation, Verifier, RATS, Event Log, Merkle PCR, DICE |
| **L3** | Engineering Structures | **Complete** | Crypto Agile Log 格式, Merkle Tree 数据结构, DICE 链式状态机, PCR Bank 位图选择器 |
| **L4** | Standards/Theorems | **Complete** | Hash Chain Integrity 定理, Merkle 1979 包含证明, DICE Transitive Trust, PUF Uniqueness, 事件日志完整性定理 |
| **L5** | Algorithms/Methods | **Complete** | TPM2_PCR_Extend 哈希链, Merkle Tree 构建/验证/证明, DICE KDF/CDI 派生, 事件日志重放算法, TPM2_Hash 序列 |
| **L6** | Canonical Problems | **Complete** | 远程证明协议 (Challenge-Response), 证书链验证 (DICE), 固件完整性度量 (EventLog Replay) |
| **L7** | Applications | **Complete** | TPM Quote Demo, Attestation Demo, Privacy CA Demo |
| **L8** | Advanced Topics | **Partial** | DICE 分层证明 (硬件-软件链), Alias Key 密钥隔离 |
| **L9** | Industry Frontiers | **Partial** | 文档覆盖: Confidential Computing, Keylime IMA, Intel TXT/AMD SKINIT |

## 核心定义列表 (L1)

| 类型 | 文件 | 关键定义 |
|------|------|---------|
| TPMHash | tpm_quote.h | SHA-256 摘要 (32 bytes) |
| TPMPcrComposite | tpm_quote.h | 多 PCR 选择 + 摘要值 |
| TPMQuote | tpm_quote.h | TPM2_Quote 签名结构 |
| EKCertificate | aik_identity.h | 背书密钥证书 |
| AIKCredential | aik_identity.h | 证明身份密钥凭证 |
| AttestChallenge | attest_protocol.h | 随机数挑战 (nonce + PCR mask) |
| AttestVerdict | attest_protocol.h | 证明判定 (8 个标志位) |
| RATSEvidence | rats.h | IETF RATS 证据结构 |
| TCGEventLogSHA256 | eventlog.h | SHA-256 事件日志 |
| MerkleTree | merkle_pcr.h | Merkle 树 (最多 32 叶) |
| MerkleProof | merkle_pcr.h | 包含证明 (最多 6 层) |
| DICELayerState | dice.h | DICE 层状态 (DeviceID + AliasKey) |
| DICECertChain | dice.h | DICE 证书链 (最多 8 层) |

## 核心定理列表 (L4)

| 定理 | 公式 / 声明 | 文件 |
|------|------------|------|
| Hash Chain Integrity | 若 H 抗碰撞, 则 PCR_k = H(...H(0\|\|m_1)...\|\|m_k) 唯一 | eventlog.c |
| Merkle Proof Integrity | Verifier(root, leaf, siblings, index) → {0,1} in O(log n) | merkle_pcr.c |
| DICE Transitive Trust | cert_i 有效 under DeviceID_{i-1} ⟹ 全链路可信 | dice.c |
| PUF Uniqueness | P[uds_A = uds_B] ≤ 2^{-256} for ideal PUF | dice.c |
| Nonce Freshness | Challenge 时间戳 vs max_age 确保重放保护 | attest_protocol.c |

## 核心算法列表 (L5)

| 算法 | 复杂度 | 知识点 | 文件 |
|------|--------|--------|------|
| TPM2_PCR_Extend | O(1) | PCR_new = H(PCR_old \|\| digest) | tpm_quote.c |
| Event Log Replay | O(n·d) | 重放 n 个事件，验证 PCR 一致性 | eventlog.c |
| Merkle Tree Build | O(n) | 构建 n 个叶子的 Merkle 树 | merkle_pcr.c |
| Merkle Proof Verify | O(log n) | 验证包含证明 | merkle_pcr.c |
| DICE CDI Chain | O(n) | CDI_i = KDF(CDI_{i-1}, H(code_i, config_i)) | dice.c |
| DICE Key Derivation | O(1) | DeviceID + Alias Key from CDI | dice.c |
| TCG Hash Sequence | O(n) | hash_start → update → end | eventlog.c |

## 经典问题列表 (L6)

| 问题 | 解决方案 | 示例 |
|------|---------|------|
| Remote Attestation | 挑战-响应协议 + Quote 签名 | attest_demo.c |
| Certificate Chain Validation | DICE 链式验证 (自底向上) | dice.c |
| Firmware Integrity Measurement | Event Log 重放 + PCR 比较 | tpm_quote_demo.c |
| Device Identity Provisioning | Privacy CA 协议 | privacy_ca_demo.c |

## 九校课程映射

| 学校 | 课程 | 映射内容 |
|------|------|---------|
| **MIT** | 6.858 Computer Security | TPM 远程证明, 信任链 |
| **Stanford** | CS 144 Networking Security | 证明协议设计 |
| **CMU** | 15-410 OS | 安全启动, 度量启动 |
| **Cambridge** | Part II: Concurrent Systems | 可信启动, DICE 架构 |
| **清华** | 操作系统 | 可信计算基础 (TPM/TCM) |
| **Berkeley** | CS 162 OS | 内核完整性验证 |
| **ETH** | 263-3501 Parallel Programming | 并行 PCR Bank 验证 |

## 快速开始

```bash
# 构建并运行全部测试
make test

# 构建所有示例
make

# 运行 TPM Quote 示例
make run-quote

# 运行证明示例
make run-attest

# 运行 Privacy CA 示例
make run-privacy-ca

# 清理
make clean
```

## 测试覆盖

| 测试类别 | 数量 | 说明 |
|---------|------|------|
| TPM Quote | 2 | 创建/签名/验证 + 篡改检测 |
| AIK Identity | 1 | 完整 EK→AIK→Credential 流程 |
| Attest Protocol | 1 | 挑战-响应验证 |
| Verifier Service | 1 | 设备注册与查询 |
| RATS | 1 | 证据生成 + 评估 + RP 接口 |
| Event Log | 3 | 添加/重放/完整性检查 |
| PCR Extend | 1 | 确定性/差异属性验证 |
| Merkle Tree | 3 | 构建/包含证明/PCR Bank 集成 |
| DICE | 3 | UDS/CDI/层密钥/证书链 |
| 其他 | 3 | PCR Bank 比较/哈希序列/空指针安全 |
| **总计** | **19** | 全部 PASS |

## License

MIT — 仅供教育和参考使用。
