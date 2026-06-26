# Secure Boot Fundamentals — 安全启动基本原理

> 本文从零开始讲解安全启动的核心原理：PK→KEK→db→bootloader→OS 信任链、Linux shim/MOK 机制以及 Secure Boot 密钥管理。

## 1. 为什么需要安全启动？

### 1.1 威胁模型

```
没有安全启动:                            有安全启动:
  Boot ROM                                 Boot ROM
    │  (信任 ROM，但...)                      │  (信任 ROM)
    ▼                                         ▼
  Bootloader ◀── 恶意软件替换               Bootloader ◀── 验证签名 ✓
    │                                         │
    ▼                                         ▼
  OS Kernel ◀── Rootkit 注入               OS Kernel ◀── 验证签名 ✓
    │                                         │
    ▼                                         ▼
  已入侵系统                                 安全系统
```

### 1.2 安全启动提供的保护

| 保护 | 说明 |
|------|------|
| 完整性 | 固件和操作系统代码未被篡改 |
| 真实性 | 代码来自可信源（OEM / OS 厂商） |
| 启动时保护 | 在恶意软件加载之前拦截 |
| 供应链安全 | 确保交付的组件未被替换 |

## 2. 信任链模型

### 2.1 PK → KEK → db 信任链

```
┌──────────────────────────────────────────────────────────────────┐
│                      TRUST CHAIN                                 │
│                                                                  │
│   PK (Platform Key)                                              │
│   │                                                              │
│   │  由平台所有者 (OEM) 控制                                      │
│   │  自签名根证书                                                 │
│   │                                                              │
│   ├──▶ 签名 ──▶ KEK (Key Exchange Key)                          │
│   │              │                                               │
│   │              │  由 OS 厂商控制                                │
│   │              │  可存在多个 (Microsoft, Red Hat, ...)          │
│   │              │                                               │
│   │              ├──▶ 签名 ──▶ db (Authorized Database)          │
│   │              │              │                                │
│   │              │              │  允许的引导程序/驱动签名        │
│   │              │              │  白名单 (证书/哈希)             │
│   │              │              │                                │
│   │              │              └──▶ 验证 ──▶ Bootloader         │
│   │              │                     (如 shim.efi)              │
│   │              │                              │                 │
│   │              │                              └──▶ OS Kernel   │
│   │              │                                              │
│   │              └──▶ 签名 ──▶ dbx (Forbidden Database)         │
│   │                             │                                │
│   │                             │  禁止的引导程序/驱动签名        │
│   │                             │  黑名单                         │
│   │                             │                                │
│   │                             └──▶ 阻止 ──▶ 恶意 Bootloader    │
│   │                                                        │     │
│   │                                                        ▼     │
│   │                                                    拒绝启动  │
│   │                                                                │
│   └──▶ 删除 PK ──▶ 进入 Setup Mode ──▶ 禁用所有检查              │
└──────────────────────────────────────────────────────────────────┘
```

### 2.2 验证流程伪代码

```c
bool verify_image(image) {
    // 1. Setup Mode → 跳过验证
    if (SetupMode) return true;

    // 2. 不启用 Secure Boot → 跳过验证
    if (!SecureBoot) return true;

    // 3. 检查黑名单 (dbx)
    if (hash_in_dbx(image.hash) || cert_in_dbx(image.cert)) {
        return false;  // 已被吊销
    }
    if (hash_in_dbr(image.hash)) {
        return false;  // 已被吊销
    }

    // 4. 检查时间戳授权 (dbt)
    if (hash_in_dbt(image.hash) && timestamp_valid(image.time)) {
        return true;
    }

    // 5. 检查白名单 (db)
    if (hash_in_db(image.hash)) return true;
    if (cert_in_db(image.cert)) return true;

    // 6. 不匹配任何许可 → 拒绝
    return false;
}
```

## 3. Setup Mode 与 User Mode

### 3.1 状态转换

```
Power-On / Reset
    │
    ├── PK 存在? ──是──▶ User Mode (安全启动可能启用)
    │                    │
    └── PK 不存在 ──▶ Setup Mode (所有检查禁用)
                         │
                    用户可以:
                    ├── 安装 PK (进入 User Mode)
                    ├── 管理 KEK/db/dbx
                    ├── 修改所有安全变量
                    └── 无验证启动任意代码
```

### 3.2 Mode 对比

| 特性 | Setup Mode | User Mode |
|------|-----------|-----------|
| PK | 无 | 存在 |
| 镜像验证 | 跳过 | 检查 db/dbx |
| 变量更新 | 任意 | 需要签名 |
| 安全级别 | 无保护 | 受保护 |
| 用途 | 初始配置/恢复 | 正常操作 |

## 4. Linux Shim 与 MOK

### 4.1 为什么需要 Shim？

```
问题:
  Microsoft 签名了 shim → 在大多数 PC 上可以启动 ✓
  但用户/发行版签名的 GRUB → 不在标准 db 中 ✗

解决方案:
  shim.efi (由 Microsoft 签名)
    ↓
  MOK (Machine Owner Key) — 用户自己的密钥
    ↓
  GRUB.efi (由 MOK 签名)
    ↓
  Linux Kernel (由 MOK 签名或自签名)
```

### 4.2 MOK 管理流程

```
1. 生成 MOK
   openssl req -new -x509 -newkey rsa:2048 -keyout MOK.key \
       -out MOK.crt -days 3650 -nodes -sha256

2. 使用 MOK 签名 GRUB
   sbsign --key MOK.key --cert MOK.crt grubx64.efi

3. 注册 MOK (一次性)
   mokutil --import MOK.crt
   → 重启 → MOK Manager (蓝色界面) → 注册 MOK

4. 之后每次启动:
   shim → 验证 GRUB (MOK 签名) ✓ → 启动 GRUB
```

### 4.3 Shim 验证协议 (EFI_SHIM_LOCK_PROTOCOL)

```
GUID: 605DAB50-E046-4300-ABB6-3DD810DD8B23

接口:
  shim_verify(buffer, size)
    → 检查: shim 内建 db, MOK list, SUSE/... vendor keys
    → 返回: EFI_SUCCESS / EFI_ACCESS_DENIED

GRUB 使用:
  grub_efi_shim_lock_verify(buffer, size)
    → 调用 shim_verify()
    → 验证 Linux Kernel
```

## 5. 密钥管理

### 5.1 密钥生命周期

```
生成 (Generate)
    │
    ▼
存储 (Store) ── HSM / TPM / 加密文件
    │
    ▼
部署 (Deploy) ── 注册到 PK/KEK/db
    │
    ▼
使用 (Use) ── 签名启动组件
    │
    ▼
轮换 (Rotate) ── 生成新密钥，注册到 db
    │
    ▼
吊销 (Revoke) ── 添加到 dbx
    │
    ▼
销毁 (Destroy) ── 安全删除私钥
```

### 5.2 密钥存储安全

| 存储位置 | 安全性 | 使用场景 |
|----------|--------|----------|
| HSM (Hardware Security Module) | 最高 | 企业/OEM 生产签名 |
| TPM | 高 | 密封密钥到 PCR 状态 |
| 加密文件 | 中 | 开发/测试 |
| 明文文件 | 低 | 仅用于 Demo |
| eFUSE / OTP | 最高 (公钥哈希) | 根信任锚 |

### 5.3 最佳实践

1. **私钥永远不离开 HSM**
2. **离线保存根 CA 私钥** (PK)
3. **定期轮换签名密钥**
4. **维护吊销列表 (dbx)**
5. **审计所有签名操作**
6. **使用强算法**: RSA-2048+, SHA-256+

## 6. 时间戳防护

### 6.1 攻击场景

```
攻击者:
  1. 获取旧的、已吊销证书签名的恶意 GRUB
  2. 回滚 EFI 变量到旧时间戳
  3. 尝试加载恶意 GRUB

时间戳防护:
  EFI 变量写入时附加 EFI_TIME 时间戳
  → 验证时检查时间戳是否 ≥ 变量当前时间戳
  → 阻止回滚
```

### 6.2 EFI_TIME 结构

```c
typedef struct {
    uint16_t Year;    // 2024
    uint8_t  Month;   // 1-12
    uint8_t  Day;     // 1-31
    uint8_t  Hour;    // 0-23
    uint8_t  Minute;  // 0-59
    uint8_t  Second;  // 0-59
    uint8_t  Pad1;
    uint32_t Nanosecond;
    int16_t  TimeZone;
    uint8_t  Daylight;
    uint8_t  Pad2;
} EFI_TIME;
```

## 7. 常见问题

### 7.1 禁用 Secure Boot 的影响

| 情景 | 后果 |
|------|------|
| Windows 11 | 需要 Secure Boot 开启 (安装要求) |
| 双启动 Linux | 可能需要 MOK 注册 |
| 加载未签名驱动 | Secure Boot 开启时被阻止 |
| VirtualBox/VMware | 通常默认关闭 |

### 7.2 常见故障排除

| 症状 | 可能原因 | 解决方案 |
|------|----------|----------|
| "Security Boot Violation" | 引导器签名不在 db 中 | 注册签名到 MOK 或 db |
| "Verification failed" | 签名无效/过期 | 重新签名或更新证书 |
| "Access Denied" | dbx 吊销匹配 | 检查是否使用被吊销的引导器 |
| Reset to Setup Mode | PK 被删除 | 重新注册 PK |
| "Invalid signature" 在 GRUB | shim 未正确配置 | 验证 MOK 注册和 shim 版本 |

### 7.3 Secure Boot 状态检查

```bash
# Linux
mokutil --sb-state
# SecureBoot enabled
# SetupMode: user

# 查看 EFI 变量
ls /sys/firmware/efi/efivars/

# 查看 db 内容
efi-readvar -v db

# 查看 PK
efi-readvar -v PK

# 查看 MOK
mokutil --list-enrolled
```

## 8. 参考

- UEFI Specification 2.10: https://uefi.org/specifications
- TCG PC Client Platform Firmware Profile: https://trustedcomputinggroup.org/
- NIST SP 800-147/155: https://csrc.nist.gov/
- shim: https://github.com/rhboot/shim
- sbsigntools: https://git.kernel.org/pub/scm/linux/kernel/git/jejb/sbsigntools.git/
- mokutil: https://github.com/lcp/mokutil
- Linux UEFI Validation Distribution: https://github.com/rhboot/efitools
