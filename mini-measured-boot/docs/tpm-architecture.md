# tpm-architecture — TPM 2.0 架构

> 参考 TPM 2.0 Library Specification Part 1: Architecture

## 1. 概述

TPM (Trusted Platform Module) 是一个密码学协处理器，提供以下核心功能：
- **平台度量与证明**: 使用 PCR 记录平台状态
- **安全密钥生成**: 基于 TPM 内部层级生成密钥
- **密封存储**: 将数据加密绑定到特定平台状态
- **远程证明**: 向远程方证明平台状态

## 2. 组件架构

```
                          ┌──────────────────┐
                          │   External Bus    │
                          └────────┬─────────┘
                                   │
                          ┌────────▼─────────┐
                          │   I/O Buffer      │
                          └────────┬─────────┘
                                   │
          ┌────────────────────────┼────────────────────────┐
          │                        │                        │
┌─────────▼────────┐  ┌────────────▼─────────┐  ┌──────────▼──────────┐
│ Authorization    │  │  Session Manager     │  │  Command Dispatcher │
│ - Password       │  │  - HMAC Sessions     │  │  - Command Parsing   │
│ - HMAC           │  │  - Policy Sessions   │  │  - Response Build    │
│ - Policy         │  │  - Context Mgmt      │  │  - Error Handling    │
└─────────┬────────┘  └────────────┬─────────┘  └──────────┬──────────┘
          │                        │                        │
          └────────────────────────┼────────────────────────┘
                                   │
          ┌────────────────────────┼────────────────────────┐
          │                        │                        │
┌─────────▼────────┐  ┌────────────▼─────────┐  ┌──────────▼──────────┐
│ PCR Manager      │  │  Object Manager       │  │  NV Storage       │
│ - PCR Banks      │  │  - Key Hierarchy      │  │  - NV Indices     │
│ - PCR Extend     │  │  - Data Objects        │  │  - NV Read/Write   │
│ - PCR Read       │  │  - Sequences          │  │  - NV Lock        │
│ - PCR Reset      │  │  - Transient Objects  │  │  - NV Counters     │
└─────────┬────────┘  └────────────┬─────────┘  └──────────┬──────────┘
          │                        │                        │
          └────────────────────────┼────────────────────────┘
                                   │
          ┌────────────────────────┼────────────────────────┐
          │                        │                        │
┌─────────▼────────┐  ┌────────────▼─────────┐  ┌──────────▼──────────┐
│ Crypto Engine    │  │  Power / Reset       │  │  Clock / Timer     │
│ - SHA-1          │  │  - Startup           │  │  - TPM Clock        │
│ - SHA-256/384/512│  │  - Shutdown          │  │  - Reset Counter   │
│ - HMAC           │  │  - Self Test         │  │  - Safe Flag       │
│ - RSA            │  │  - PCR Init          │  │  - Time Attest     │
│ - ECC            │  │  - NV Restore        │  │                    │
│ - RNG            │  │                      │  │                    │
└──────────────────┘  └──────────────────────┘  └────────────────────┘
```

## 3. 层级体系 (Hierarchies)

TPM 2.0 定义了四个层级，每个层级独立管理：

### 3.1 平台层级 (Platform Hierarchy)

- **Handle**: `0x4000000C`
- **角色**: 由平台固件控制
- **控制**:
  - PCR 分配 (`TPM2_PCR_Allocate`)
  - 平台 NV 索引管理
  - TPM 电源状态控制
  - EPS (Endorsement Primary Seed) 变更

### 3.2 存储层级 (Storage Hierarchy)

- **Handle**: `0x40000001`
- **角色**: 由平台 Owner 控制
- **用途**:
  - 存储根密钥 (SRK)
  - 创建和管理用户密钥
  - 密封和解封数据
  - TPM2_Clear 控制

### 3.3 背书层级 (Endorsement Hierarchy)

- **Handle**: `0x4000000B`
- **角色**: 隐私管理员控制
- **用途**:
  - 背书密钥 (EK)
  - 证明身份密钥 (AIK)
  - 证明 (Quote, Certify)
  - 隐私保护和证书管理

### 3.4 空层级 (NULL Hierarchy)

- **Handle**: `0x40000007`
- **特性**: 非持久化，重启后清除
- **用途**: 临时性密钥和会话

### 层级关系

```
           TPM
            │
  ┌─────────┼─────────┐
  │         │         │
  ▼         ▼         ▼
Platform  Storage  Endorsement   NULL
(0x0C)    (0x01)   (0x0B)       (0x07)
  │         │         │             │
  ▼         ▼         ▼             ▼
EPS       SPS       EPS          (none)
  │         │         │
  ▼         ▼         ▼
Primary   Primary   Primary
Key       Key       Key
```

每个层级的 Primary Seed 都不同，因此从不同层级派生的密钥在密码学上
是不相关的。

## 4. PCR 架构

### 4.1 核心设计

- TPM 2.0 必须支持至少一个 PCR Bank (SHA-256)
- 每个 PCR Bank 有 24 个 PCR
- PCR 通过 `TPM2_PCR_Extend` 操作更新
- PCR 只能扩展，不能直接写入
- 扩展操作使 PCR 值依赖于所有之前的扩展

### 4.2 扩展操作数学定义

```
digest_context = H(algorithm, current_PCR_value || data_hash)
```

其中:
- `algorithm` 是 PCR Bank 的哈希算法标识符
- `current_PCR_value` 是 PCR 的当前值
- `data_hash` 是被测量数据的哈希
- `H` 是 PCR Bank 的哈希算法

### 4.3 PCR 索引与用途

```
┌──────┬────────────────────────┬─────────┬───────────┐
│ PCR  │ Purpose                │ Reset   │ Extendible│
├──────┼────────────────────────┼─────────┼───────────┤
│ 0-7  │ SRTM Only              │ TPM Reset│ Until     │
│ 8-15 │ SRTM + Application     │ TPM Reset│ OS boots   │
│ 16   │ Debug                  │ Loc4/DRTM│ Always    │
│ 17-22│ DRTM                   │ Loc4/DRTM│ Locality 4 │
│ 23   │ Vendor Specific        │ Variable │ Variable   │
└──────┴────────────────────────┴─────────┴───────────┘
```

### 4.4 PCR Bank 类型

```
TPM 可以同时拥有多个 PCR bank:

Bank 0: SHA-1 PCR Bank   → 24 PCRs × 20 bytes = 480 bytes
Bank 1: SHA-256 PCR Bank → 24 PCRs × 32 bytes = 768 bytes
Bank 2: SHA-384 PCR Bank → 24 PCRs × 48 bytes = 1152 bytes
Bank 3: SHA-512 PCR Bank → 24 PCRs × 64 bytes = 1536 bytes
                           ─────────────────────────────
                           Total: 3936 bytes
```

算法敏捷性: 当系统执行 TPM2_PCR_Extend 时，输入数据被同时哈希到
所有启用的 PCR bank 中。

## 5. 会话管理架构

### 5.1 会话生命周期

```
         StartAuthSession
              │
              ▼
         ┌──────────┐
         │  ACTIVE  │◄────── policy commands
         └────┬─────┘
              │
     ┌────────┼────────┐
     │        │        │
     ▼        ▼        ▼
  Flush   Context   Session
  Context Save      timeout
```

### 5.2 会话槽位

TPM 内部维护有限数量的会话槽位：
- 通常 3 个 HMAC/PIN 槽位
- 通常 1 个 trial policy 槽位
- 总计约 4-8 个活动会话

## 6. 对象管理架构

### 6.1 对象类型

```
Objects
├── Transient Objects (易变对象)
│   ├── Keys (密钥)
│   │   ├── Symmetric (AES)
│   │   └── Asymmetric (RSA, ECC)
│   ├── Data (密封数据)
│   │   └── Keyed Hash
│   └── Sequences (哈希序列)
│
├── Persistent Objects (持久化对象)
│   ├── Primary Keys
│   └── Stored Objects via NV
│
└── Sequence Objects
    └── Hash Sequence
```

### 6.2 密钥层级

```
Primary Seed (来自层级)
    │
    ▼
Primary Key (模板定义的模板)
    │
    ▼
Child Keys (可无限派生)
    │
    ├── Storage Keys (用于加密其他密钥)
    ├── Signing Keys (用于签名)
    ├── Attestation Keys (AIK)
    └── Sealed Data Objects
```

## 7. NV 存储架构

### 7.1 NV 内存区域

```
TPM NV RAM
├── 保留区域 (TPM 内部使用)
│   ├── 层级 Seeds
│   ├── Lockout 参数
│   └── 内部计数器
│
└── 用户 NV 索引 (TPM2_NV_DefineSpace)
    ├── Index 0x01xxxxxx
    │   ├── Platform 数据
    │   ├── EK 证书
    │   └── 平台固件数据
    │
    └── Index 0x01yyyyyy
        ├── 用户密钥
        ├── 密封数据
        └── 自定义数据
```

## 8. 命令分发与处理

### 8.1 命令处理流程

```
1. Receive command bytes
      │
2. Parse command header (tag, size, code)
      │
3. Determine if authorization is needed
      │
4. Process authorization (if any)
      │
5. Execute command
      │
6. Build response
      │
7. Send response bytes
```

### 8.2 常见命令响应码

| 代码   | 名称        | 含义                    |
|--------|-------------|-------------------------|
| 0x000  | SUCCESS     | 命令成功                 |
| 0x100  | INITIALIZE  | TPM 未启动               |
| 0x101  | FAILURE     | 命令执行失败              |
| 0x128  | PCR_CHANGED | PCR 值不匹配             |
| 0x12F  | BAD_AUTH    | 授权失败                 |
| 0x138  | LOCKOUT     | 字典攻击锁定              |

## 9. 字典攻击防护

```
每次授权失败 → lockoutCounter++

if lockoutCounter >= maxTries:
    TPM 进入 lockout 模式
    需要 lockoutAuth 才能恢复

lockout 期间:
    不允许任何需要授权的操作
    只能执行有限的管理命令
```

配置参数:
- `maxTries`: 最大失败次数
- `recoveryTime`: 恢复时间窗口
- `lockoutRecovery`: 锁定后的额外恢复时间

## 10. 内部时钟

TPM 维护以下时间/计数值:

```
┌──────────────────┬──────────────────────┐
│  Field           │  Description          │
├──────────────────┼──────────────────────┤
│  Clock           │  自启动以来的计数值    │
│  Reset Count     │  总 TPM 复位次数       │
│  Restart Count   │  总 TPM 重启次数       │
│  Safe            │  时钟是否可信           │
└──────────────────┴──────────────────────┘
```

时钟用于:
- 限制 TPM2_PolicySigned 中 nonce 的有效期
- 实现速率限制
- 字典攻击防护的计时

## 参考

- TPM 2.0 Library Specification Part 1: Architecture, Rev 1.59
- TPM 2.0 Library Specification Part 2: Structures, Rev 1.59
- TPM 2.0 Library Specification Part 3: Commands, Rev 1.59
- TPM 2.0 Library Specification Part 4: Supporting Routines, Rev 1.59
