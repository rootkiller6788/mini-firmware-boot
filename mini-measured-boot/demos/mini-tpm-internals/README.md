# mini-tpm-internals — TPM 2.0 Internals

> 参考 TPM 2.0 Library Specification Parts 1-4, Trusted Computing Group

## 目录

1. [架构概览](#架构概览)
2. [PCR 银行](#pcr-银行)
3. [授权与会话](#授权与会话)
4. [NV 存储](#nv-存储)
5. [层级体系](#层级体系)
6. [命令与响应](#命令与响应)
7. [审计与时钟](#审计与时钟)
8. [字典攻击防护](#字典攻击防护)

---

## 架构概览

TPM 2.0 是一个硬件安全芯片，提供密钥生成、平台度量、远程证明和密封存储等
功能。其核心架构分为以下几个子系统：

```
┌───────────────────────────────────────────────────────────┐
│                      TPM 2.0 Subsystem                     │
├───────────────────────────────────────────────────────────┤
│  I/O Buffer          │  Command Processing                │
├──────────────────────┼────────────────────────────────────┤
│  Authorization (Auth) │  HMAC / Session / Policy Auth      │
├──────────────────────┼────────────────────────────────────┤
│  PCR Banks            │  Platform Configuration Registers   │
├──────────────────────┼────────────────────────────────────┤
│  NV Storage           │  Non-Volatile Memory                │
├──────────────────────┼────────────────────────────────────┤
│  Object Management    │  Keys, Data, Sequences               │
├──────────────────────┼────────────────────────────────────┤
│  Power / Reset        │  Self-test, PCR initialization       │
├──────────────────────┼────────────────────────────────────┤
│  Hierarchies          │  Platform / Storage / Endorsement   │
├──────────────────────┼────────────────────────────────────┤
│  Dictionary Attack    │  Lockout logic, failure counting     │
├──────────────────────┼────────────────────────────────────┤
│  Audit / Clock        │  Command auditing, internal clock    │
└──────────────────────┴────────────────────────────────────┘
```

TPM 2.0 与 TPM 1.2 的关键区别：
- TPM 1.2 仅支持 SHA-1 PCR，TPM 2.0 支持多算法 PCR banks
- TPM 2.0 引入了增强授权 (Enhanced Authorization, EA) 策略
- TPM 2.0 支持更灵活的密钥层级和 NV 索引管理
- TPM 2.0 支持 HMAC 会话和 Policy 会话两种授权方式

---

## PCR 银行

### 什么是 PCR？

PCR (Platform Configuration Register) 是 TPM 内部的易失性寄存器，用于
记录平台的完整性度量。每个 PCR 存储一个哈希值，用于追踪平台状态变化。

PCR 扩展操作定义为：

```
PCR_new = Hash(PCR_old || Digest)
```

这个链式哈希设计确保：
1. 无法回退 PCR 到之前的状态
2. 测量的顺序被密码学绑定
3. 通过事件日志可以重演 PCR 值

### TPM 2.0 PCR Bank 概念

TPM 2.0 引入了 PCR Bank 的概念，允许为不同的哈希算法维护独立的 PCR 集合。
同一个测量事件会被同时扩展到所有启用的 PCR bank 中。

常见的 PCR Bank 算法包括：
- SHA-1 PCR Bank (20 字节 digest)
- SHA-256 PCR Bank (32 字节 digest)
- SHA-384 PCR Bank (48 字节 digest)
- SHA-512 PCR Bank (64 字节 digest)

### PCR 分配

| PCR | 用途                     | 静态/动态 |
|-----|-------------------------|-----------|
| 0   | CRTM, BIOS 度量         | Static    |
| 1   | 平台配置                 | Static    |
| 2   | 扩展 ROM                 | Static    |
| 3   | ROM 配置                | Static    |
| 4   | 启动管理器代码           | Static    |
| 5   | 启动管理器配置           | Static    |
| 6   | 休眠状态                 | Static    |
| 7   | 安全启动策略             | Static    |
| 8   | OS Loader (SRTM)        | Static    |
| 9   | OS Loader (DRTM)        | Static    |
| 10  | (未使用)                 | -         |
| 11  | Boot Manager 数据       | Static    |
| 12  | 平台数据 (PCR 0-7 的组合)| Static    |
| 13  | (未使用)                 | -         |
| 14  | 权威机构证书             | Static    |
| 15-16 | (未使用)              | -         |
| 17  | DRTM - SINIT ACM        | Dynamic   |
| 18  | DRTM - MLE              | Dynamic   |
| 19  | DRTM - Trusted OS       | Dynamic   |
| 20  | DRTM - OS Kernel        | Dynamic   |
| 21  | DRTM - Application      | Dynamic   |
| 22  | DRTM - Application Data | Dynamic   |
| 23  | (未使用)                 | -         |

静态 PCR (0-16) 只能在平台复位时清零。
动态 PCR (17-22) 可以通过 TPM2_PCR_Reset 命令清零而不需要平台复位，
这是 DRTM (Intel TXT / AMD SKINIT) 的关键特性。

### PCR 算法敏捷性

TPM2_PCR_Allocate 命令可以配置哪些 PCR bank 是启用的。在平台的整个生命
周期内通常只设置一次（由平台固件在制造或资源调配期间设置）。

---

## 授权与会话

### 三种授权类型

TPM 2.0 支持三种授权机制：

1. **密码授权 (Password Authorization)**
   - 最简单的授权方式
   - 在命令中明文传递授权值
   - 仅用于低安全需求场景

2. **HMAC 授权 (HMAC Authorization)**
   - 使用 HMAC 对命令参数进行签名
   - 防止重放攻击（通过 nonce）
   - 需要 HMAC session

3. **策略授权 (Policy Authorization)**
   - TPM 2.0 的增强授权机制
   - 通过构建 policy digest 来控制访问
   - 支持 PCR 绑定、口令、物理存在等条件

### Session 类型

**HMAC Session:**
- 用于 HMAC 授权
- 加解密命令参数
- 提供防重放保护
- 占用 TPM 内部 session 槽位（通常 3 个）

**Policy Session:**
- 用于策略授权
- 可以是 Trial Session (计算 policy digest 但不执行)
- Policy 命令构建 digest: 每步 Hash(previous_policy || command_params)
- 最后与对象的 authPolicy 比较

### 增强授权策略命令

| 命令                  | 功能                                |
|-----------------------|-------------------------------------|
| TPM2_PolicyPCR        | 绑定到当前 PCR 状态                  |
| TPM2_PolicySecret     | 要求外部实体提供授权值                |
| TPM2_PolicyPassword   | 要求提供密码                        |
| TPM2_PolicyOr         | 满足多个条件之一即可                  |
| TPM2_PolicyLocality   | 要求特定的 locality                  |
| TPM2_PolicyNV         | 要求 NV 索引满足特定条件              |
| TPM2_PolicySigned     | 要求外部签名                        |
| TPM2_PolicyAuthorize  | 允许委托授权                        |
| TPM2_PolicyCounterTimer| 基于 TPM 内部定时器/计数器          |
| TPM2_PolicyCpHash     | 基于命令参数哈希                    |

---

## NV 存储

TPM 的非易失性存储 (NV RAM) 用于持久化存储密钥、证书、策略等数据。

### NV 索引属性

每个 NV 索引可以有如下属性：
- **TPMA_NV_PPWRITE**: 需要平台授权才能写入
- **TPMA_NV_OWNERWRITE**: 需要 Owner 授权才能写入
- **TPMA_NV_AUTHWRITE**: 需要索引的 authValue 才能写入
- **TPMA_NV_POLICYWRITE**: 需要满足索引的 authPolicy 才能写入
- **TPMA_NV_PPREAD**: 需要平台授权才能读取
- **TPMA_NV_OWNERREAD**: 需要 Owner 授权才能读取
- **TPMA_NV_AUTHREAD**: 需要索引的 authValue 才能读取
- **TPMA_NV_POLICYREAD**: 需要满足索引的 authPolicy 才能读取
- **TPMA_NV_WRITEDEFINE**: 写入后锁定
- **TPMA_NV_WRITE_STCLEAR**: Shutdown 时清除
- **TPMA_NV_GLOBALLOCK**: 在 TPM2_NV_GlobalWriteLock 后锁定

### NV 索引范围

- `0x01...` 范围属于 TPM 管理的 NV 索引
- 平台特定 NV 索引由平台设计决定
- 每个索引有独立的大小、属性和授权

### NV 常用操作

| 命令                  | 功能            |
|-----------------------|-----------------|
| TPM2_NV_DefineSpace   | 定义一个 NV 索引 |
| TPM2_NV_UndefineSpace | 删除一个 NV 索引 |
| TPM2_NV_Read          | 读取 NV 数据     |
| TPM2_NV_Write         | 写入 NV 数据     |
| TPM2_NV_ReadLock      | 锁定 NV 读取     |
| TPM2_NV_WriteLock     | 锁定 NV 写入     |
| TPM2_NV_Certify       | 证明 NV 内容     |

---

## 层级体系

TPM 2.0 定义了三个持久化层级：

### 1. 平台层级 (Platform Hierarchy)

- **Handle**: `0x4000000C`
- **Auth**: platformAuth
- **用途**: 平台固件使用，控制 PCR、NV、电源管理等
- **特点**: 平台复位时此层级的授权值被重置
- **控制**: TPM2_ChangeEPS (改变 Endorsement Primary Seed)

### 2. 存储层级 (Storage Hierarchy)

- **Handle**: `0x40000001`
- **Auth**: ownerAuth
- **用途**: 用户和管理员使用，管理存储密钥
- **特点**: 相当于 TPM 1.2 的 owner
- **控制**: TPM2_Clear, TPM2_HierarchyControl

### 3. 背书层级 (Endorsement Hierarchy)

- **Handle**: `0x4000000B`
- **Auth**: endorsementAuth
- **用途**: 隐私敏感操作，证明 TPM 是真实的
- **特点**: 包含 EK (Endorsement Key)
- **控制**: TPM2_ChangeEPS, TPM2_CreatePrimary

### 空层级 (NULL Hierarchy)

- **Handle**: `0x40000007`
- **Auth**: 无
- **用途**: 临时会话密钥，非持久化
- **特点**: 重启后所有对象消失

### 层级控制

每个层级都可以独立启用或禁用：
- TPM2_HierarchyControl 可以禁用某个层级
- 禁用后，该层级的所有授权值被清除
- 平台层级与固件 TPM 状态关联

---

## 命令与响应

### 命令格式

```
┌──────────┬──────────┬──────────┬───────────────┬──────────┐
│  Tag     │  Size    │  CmdCode │  Parameters   │ Auth     │
│  2 bytes │  4 bytes │  4 bytes │  variable     │ variable │
└──────────┴──────────┴──────────┴───────────────┴──────────┘
```

### 响应格式

```
┌──────────┬──────────┬──────────┬───────────────┬──────────┐
│  Tag     │  Size    │  RspCode │  Parameters   │ Auth     │
│  2 bytes │  4 bytes │  4 bytes │  variable     │ variable │
└──────────┴──────────┴──────────┴───────────────┴──────────┘
```

### 常用 Tag 值

- `0x8001` TPM_ST_NO_SESSIONS: 无授权
- `0x8002` TPM_ST_SESSIONS: 有授权区域

### 命令分类

| 类别       | 示例命令                                    |
|------------|---------------------------------------------|
| PCR        | PCR_Extend, PCR_Read, PCR_Reset, PCR_Allocate|
| NV         | NV_DefineSpace, NV_Read, NV_Write           |
| 对象       | CreatePrimary, Load, Unseal                  |
| 会话       | StartAuthSession, FlushContext               |
| 证明       | Quote, Certify, GetTime                      |
| 层级       | HierarchyControl, Clear, ChangeEPS           |
| 测试       | SelfTest, GetTestResult                      |
| 字典攻击   | DictionaryAttackLockReset                    |

---

## 审计与时钟

### 审计

TPM 2.0 支持命令审计功能：
- TPM2_SetCommandCodeAuditStatus 开启审计
- 被审计的命令记录在 audit digest 中
- TPM2_GetAuditDigest 获取当前审计摘要
- 审计摘要跟踪所有被审计的命令（类似 PCR 的链式结构）

### TPM 时钟

TPM 内部维护以下时钟值：
- **TPM Clock**: 自上次开机以来的计数值
- **TPM Reset Count**: TPM 复位次数
- **TPM Restart Count**: TPM 重启次数
- **TPM Safe**: 指示时钟是否安全（未溢出）

时钟用于：
- 限制授权尝试次数
- NV 写入速率限制
- 防重放保护（ticket 有效期）

---

## 字典攻击防护

TPM 2.0 实现了字典攻击防护机制：

### 原理

当授权失败时，TPM 内部记录失败计数。达到阈值后进入 lockout 状态。

### 参数

- **maxTries**: 最大失败尝试次数（默认 5-32）
- **recoveryTime**: 锁定恢复时间（默认几分钟到几小时）
- **lockoutRecovery**: 进入 lockout 后的恢复时间

### 相关命令

| 命令                             | 功能                    |
|----------------------------------|-------------------------|
| TPM2_DictionaryAttackLockReset   | 重置锁定计数器          |
| TPM2_DictionaryAttackParameters  | 读取/设置防护参数       |

### Lockout 层级

- **Handle**: `0x4000000A`
- **Auth**: lockoutAuth
- 用于管理字典攻击防护参数
- 需要一个单独的授权值

---

## 启动与自检

### 自检模式

- `TPM2_SelfTest(fullTest=YES)`: 完整自检
- `TPM2_SelfTest(fullTest=NO)`: 增量自检（仅未测试的算法）
- 自检覆盖所有已加载的算法（RNG, SHA, HMAC, RSA, ECC, AES）

### 启动顺序

1. 上电 / 复位
2. TPM 硬件初始化
3. PCR 全部归零
4. 加载 NV 存储
5. 恢复保存的会话上下文（如果有）
6. 自检完成
7. TPM 进入就绪状态

---

## 参考

- TPM 2.0 Library Specification Part 1: Architecture
- TPM 2.0 Library Specification Part 2: Structures
- TPM 2.0 Library Specification Part 3: Commands
- TPM 2.0 Library Specification Part 4: Supporting Routines
- TCG PC Client Platform TPM Profile (PTP) Specification
- TCG EFI Platform Specification
