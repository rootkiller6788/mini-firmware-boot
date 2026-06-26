# mini-secure-boot chain — 完整安全启动链

> 本文演示从硬件信任根到操作系统内核的完整安全启动链实现，包含证书链验证、反回滚保护和认证变量管理。

## 1. 安全启动链总览

```
┌──────────────────────────────────────────────────────────────────┐
│                        SECURE BOOT CHAIN                        │
│                                                                 │
│  ┌─────────┐    ┌─────────┐    ┌──────────┐    ┌────────────┐  │
│  │ Root of │───▶│  SPL /  │───▶│  U-Boot  │───▶│   Linux    │  │
│  │ Trust   │    │  TPL    │    │  (FIT)   │    │   Kernel   │  │
│  │ (ROM)   │    │         │    │          │    │            │  │
│  └─────────┘    └─────────┘    └──────────┘    └────────────┘  │
│       │              │               │                │         │
│       ▼              ▼               ▼                ▼         │
│  ┌─────────┐    ┌─────────┐    ┌──────────┐    ┌────────────┐  │
│  │ PubKey  │    │  Signed │    │   FIT    │    │  dm-verity │  │
│  │  Hash   │    │  Image  │    │  Image   │    │   rootfs   │  │
│  │ in ROM  │    │ (RSA)   │    │ (RSA)    │    │            │  │
│  └─────────┘    └─────────┘    └──────────┘    └────────────┘  │
└──────────────────────────────────────────────────────────────────┘
```

## 2. Root of Trust (信任根)

### 2.1 类型

| 类型 | 描述 | 安全性 |
|------|------|--------|
| ROM_BASED | 不可变 Boot ROM，密钥哈希硬编码在 Mask ROM | 最高 |
| eFUSE | 一次性可编程熔丝，烧录公钥哈希 | 高 |
| PUF_BASED | 物理不可克隆函数，设备唯一密钥 | 中高 |

### 2.2 初始化流程

```
1. 芯片上电 → Boot ROM 执行
2. ROM 读取 OTP/eFUSE 中的公钥哈希
3. 验证 SPL 签名 (RSA-2048 + SHA-256)
4. 反回滚计数器检查 (version >= anti_rollback_counter)
5. SPL 通过 → 跳转到 SPL 执行
6. 失败 → 进入恢复模式 / 停止启动
```

### 2.3 代码示例

```c
RootOfTrust rot;
rot_init(&rot, ROT_TYPE_ROM_BASED);

/* ROM 中硬编码的 trusted key hash */
rot_lock_device(&rot, rom_hardcoded_pk_hash);

/* 验证第一级启动代码 */
RoTVerifyResult result = rot_verify_first_stage(
    &rot, spl_image, spl_size, spl_signature, sig_size, version);

if (result == ROT_VERIFY_OK) {
    jump_to_spl();
} else {
    enter_recovery();
}
```

## 3. UEFI Secure Boot

### 3.1 变量数据库架构

```
┌─────────────────────────────────────────────────────────────────┐
│                       UEFI VARIABLE STORE                       │
│                                                                 │
│   ┌──────────────────────────────────────────────────────┐      │
│   │ PK (Platform Key) — 最多 1 个                        │      │
│   │  ▸ 所有者的公钥 (X.509 证书)                         │      │
│   │  ▸ 控制 KEK/db/dbx 的更新                           │      │
│   └──────────────────────────────────────────────────────┘      │
│                          │ 授权                                  │
│   ┌──────────────────────────────────────────────────────┐      │
│   │ KEK (Key Exchange Key) — 多个                        │      │
│   │  ▸ OS 厂商和平台所有者的密钥                         │      │
│   │  ▸ 控制 db/dbx 的更新                               │      │
│   └──────────────────────────────────────────────────────┘      │
│                    │                    │                        │
│   ┌────────────────────────┐  ┌────────────────────────┐        │
│   │ db (Authorized)        │  │ dbx (Forbidden)        │        │
│   │ ▸ 允许的签名/哈希      │  │ ▸ 禁止的签名/哈希      │        │
│   │ ▸ 白名单               │  │ ▸ 黑名单 (吊销)        │        │
│   └────────────────────────┘  └────────────────────────┘        │
│                                                                 │
│   ┌──────────────────────────────────────────────────────┐      │
│   │ dbt (Timestamp-based authorization)                  │      │
│   │ dbr (Recovery authorization)                         │      │
│   └──────────────────────────────────────────────────────┘      │
└─────────────────────────────────────────────────────────────────┘
```

### 3.2 状态机

```
                  ┌──────────┐
                  │ Platform │
          ┌──────▶│  Reset   │
          │       └────┬─────┘
          │            │
          │       ┌────▼─────┐
          │       │  Setup   │◀──────────────┐
          │       │  Mode    │               │
          │       └────┬─────┘               │
          │            │ Enroll PK           │
          │       ┌────▼─────┐               │
          │       │   User   │       Delete PK
          │       │   Mode   │───────────────┘
          │       └────┬─────┘
          │            │ Set SecureBoot=1
          │       ┌────▼──────────┐
          │       │  Deployed     │
          └───────│  Mode         │
           Audit  │               │
           Mode   └───────────────┘
```

### 3.3 镜像验证流程

```
启动镜像加载请求
    │
    ▼
┌──────────────┐    是    ┌──────────────┐
│ Setup Mode?  │────────▶│ 允许启动     │
└──────┬───────┘         └──────────────┘
       │ 否
       ▼
┌──────────────┐    是    ┌──────────────┐
│ Hash in dbx? │────────▶│ 拒绝启动     │
└──────┬───────┘         └──────────────┘
       │ 否
       ▼
┌──────────────┐    是    ┌──────────────┐
│ Hash in db?  │────────▶│ 允许启动     │
└──────┬───────┘         └──────────────┘
       │ 否
       ▼
┌──────────────┐
│  拒绝启动    │
└──────────────┘
```

## 4. 证书链验证

### 4.1 X.509 证书链结构

```
┌─────────────────────────────────────────────────────────────────┐
│                     CERTIFICATE CHAIN                           │
│                                                                 │
│   Root CA (自签名)                                              │
│   ├─ Subject: CN=Root CA                                       │
│   ├─ Issuer:  CN=Root CA                                       │
│   ├─ Basic Constraints: CA=TRUE                                │
│   └─ Public Key: [RSA-2048]                                    │
│           │                                                     │
│           │ 签名                                                │
│           ▼                                                     │
│   Intermediate CA                                               │
│   ├─ Subject: CN=Intermediate CA                               │
│   ├─ Issuer:  CN=Root CA                                       │
│   ├─ Basic Constraints: CA=TRUE, PathLen=0                     │
│   └─ Public Key: [RSA-2048]                                    │
│           │                                                     │
│           │ 签名                                                │
│           ▼                                                     │
│   Leaf Certificate (终端证书)                                    │
│   ├─ Subject: CN=bootloader.efi                                │
│   ├─ Issuer:  CN=Intermediate CA                               │
│   ├─ Basic Constraints: CA=FALSE                               │
│   ├─ Extended Key Usage: Code Signing                          │
│   └─ Public Key: [RSA-2048]                                    │
└─────────────────────────────────────────────────────────────────┘
```

### 4.2 验证步骤

```
1. 解析证书 DER 编码
2. 检查证书有效期 (not_before ≤ current_time ≤ not_after)
3. 检查 CA 标志 (Basic Constraints)
4. 验证签名链 (每个证书用其颁发者的公钥验证)
5. 根 CA 必须匹配信任锚 (trusted root)
6. 关键扩展检查 (Key Usage, Extended Key Usage)
```

## 5. 反回滚保护

### 5.1 机制

| 机制 | 位置 | 原理 |
|------|------|------|
| Anti-rollback Counter | eFUSE / Secure Storage | 单调递增，拒绝低版本固件 |
| Version Checks | Image Header | 固件镜像包含版本号 |
| Timestamp Checks | UEFI Variables | PK/KEK/db 更新记录时间戳 |
| Hardware Fuse Bits | OTP / eFUSE | 物理不可逆，防止降级 |

### 5.2 流程图

```
新固件版本 V_new
    │
    ▼
┌──────────────────────────┐
│ V_new >= lowest_         │    否     ┌──────────────┐
│ supported?               │──────────▶│  拒绝更新    │
└──────────┬───────────────┘          └──────────────┘
           │ 是
           ▼
┌──────────────────────────┐
│ V_new >= anti_rollback_  │    否     ┌──────────────┐
│ counter?                 │──────────▶│  拒绝回滚    │
└──────────┬───────────────┘          └──────────────┘
           │ 是
           ▼
┌──────────────────────────┐
│ 应用更新                  │
│ anti_rollback_counter =  │
│ V_new                     │
└──────────────────────────┘
```

## 6. 认证变量管理

### 6.1 EFI_VARIABLE_AUTHENTICATION_2 描述符

```
┌─────────────────────────────────────────┐
│  EFI_VARIABLE_AUTHENTICATION_2          │
│  ┌─────────────────────────────────────┐│
│  │  EFI_TIME        Timestamp         ││
│  ├─────────────────────────────────────┤│
│  │  WIN_CERTIFICATE_UEFI_GUID         ││
│  │  ┌─────────────────────────────────┐││
│  │  │ Hdr.dwLength                    │││
│  │  │ Hdr.wRevision                  │││
│  │  │ CertType (EFI_CERT_TYPE_PKCS7) │││
│  │  │ PKCS#7 SignedData              │││
│  │  │   ├─ SignerInfo                │││
│  │  │   │   ├─ Issuer + Serial       │││
│  │  │   │   └─ Encrypted Digest      │││
│  │  │   └─ ContentInfo               │││
│  │  │       └─ New Variable Value    │││
│  │  └─────────────────────────────────┘││
│  └─────────────────────────────────────┘│
└─────────────────────────────────────────┘
```

### 6.2 写保护规则

| 变量 | 更新条件 | 签名要求 |
|------|----------|----------|
| PK | SetupMode 或 PK 签名 | PK 所有者签名 |
| KEK | SetupMode 或 PK 签名 | PK 所有者签名 |
| db | SetupMode 或 KEK 签名 + 时间戳 | KEK 所有者签名 |
| dbx | SetupMode 或 KEK 签名 + 时间戳 | KEK 所有者签名 |

## 7. 完整启动流程示例

```
1. ROM Boot
   ├── 验证 SPL 签名
   ├── 反回滚计数器检查
   └── 跳转到 SPL

2. SPL (Secondary Program Loader)
   ├── 初始化 DDR
   ├── 加载 TPL/U-Boot (FIT Image)
   ├── 验证 FIT 签名
   └── 跳转到 U-Boot

3. U-Boot
   ├── 初始化 UEFI Runtime
   ├── 设置 SecureBoot=1
   ├── 加载 shim.efi
   └── UEFI 验证: db 查询

4. shim.efi (可选)
   ├── 加载 MOK Manager (Machine Owner Key)
   ├── 验证 GRUB 签名
   └── 跳转到 GRUB

5. GRUB
   ├── 加载 Linux Kernel + initrd
   ├── 验证签名 (shim_lock)
   └── 跳转到 Kernel

6. Linux Kernel
   ├── dm-verity (rootfs 完整性)
   ├── IMA (Integrity Measurement Architecture)
   └── EVM (Extended Verification Module)
```

## 8. 安全威胁模型

| 威胁 | 对策 |
|------|------|
| 固件替换 | RSA-2048 签名验证 |
| 回滚攻击 | 反回滚计数器 (eFUSE) |
| 中间人更新 | 签名验证 + TLS |
| 冷启动攻击 | RAM 加密 / 可信执行环境 |
| 供应链攻击 | 证书链验证 + 信任锚 |
| 侧信道攻击 | 恒定时间算法 |

## 9. 测试场景

### 9.1 正常场景

- 有效签名的固件 → 启动成功
- PK/KEK/db 正确配置 → 安全启动启用
- FIT Image 所有组件签名正确 → 验证通过

### 9.2 异常场景

- 无签名的固件 → 拒绝启动
- 吊销的签名 (dbx) → 拒绝启动
- 回滚到低版本 → 反回滚拒绝
- 证书过期 → 验证失败
- 篡改的 FIT → 哈希不匹配

## 10. 参考规范

- UEFI Specification, Chapter 32: Secure Boot
- TCG PC Client Platform Firmware Profile
- NIST SP 800-147: BIOS Protection Guidelines
- NIST SP 800-155: BIOS Integrity Measurement
- Android Verified Boot 2.0
- Chrome OS Verified Boot
- Linux UEFI Secure Boot / shim
