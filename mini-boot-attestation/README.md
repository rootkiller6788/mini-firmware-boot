# mini-boot-attestation — 启动证明 (C 语言实现)

> 参考 TPM 2.0 Spec Part 1, TCG TAP (Trusted Attestation Protocol), IETF RATS

## 模块表 (Module Overview)

| # | 模块 | 头文件 | 源文件 | 描述 |
|---|------|--------|--------|------|
| 1 | **TPM Quote** | `tpm_quote.h` | `tpm_quote.c` | TPM2_Quote 生成、签名、验证；PCR 复合摘要 |
| 2 | **AIK Identity** | `aik_identity.h` | `aik_identity.c` | EK 创建、AIK 创建、Privacy CA 证书颁发 |
| 3 | **Attest Protocol** | `attest_protocol.h` | `attest_protocol.c` | 挑战-响应远程证明协议 |
| 4 | **Verifier Service** | `verifier_service.h` | `verifier_service.c` | 设备注册、策略管理、机群证明验证 |
| 5 | **RATS** | `rats.h` | `rats.c` | IETF RATS 概念：证据生成、评估、依赖方接口 |

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

## License

MIT — 仅供教育和参考使用。
