# Course Alignment — 课程规范对齐

> 本文档将 mini-secure-boot 的实现映射到工业标准规范：UEFI Spec Ch 32, TCG PC Client, Android Verified Boot, Chrome OS Verified Boot。

## 1. UEFI Specification Chapter 32: Secure Boot

### 1.1 映射表

| UEFI Spec 章节 | UEFI Spec 内容 | mini-secure-boot 实现 |
|:---|:---|:---|
| 32.1 Overview | 安全启动架构 | `uefi_sb.h` — `SecureBootVars` 结构体 |
| 32.2 Variables | PK, KEK, db, dbx, dbt, dbr 变量定义 | `uefi_sb.c` — 变量初始化和管理 |
| 32.3 Signature Types | X.509, SHA-256, RSA-2048 | `signature_verify.h` — `SignatureType` 枚举 |
| 32.4 Setup Mode | 平台初始化状态 | `sb_init()` — `setup_mode = true` |
| 32.5 Image Verification | 镜像验证算法 | `sb_verify_image()` — db/dbx 查询逻辑 |
| 32.6 Authenticated Variables | EFI_VARIABLE_AUTHENTICATION_2 | `uefi_sb.h` — `variable_attributes` |
| 32.7 Timestamp Checks | 时间戳防回滚 | `sb.pktimestamp`, `kek_timestamp` 等 |
| 32.8 Key Management | 密钥生命周期管理 | `sb_enroll_pk/kek/db/dbx()` |

### 1.2 变量 GUID 对照

```
EFI_GLOBAL_VARIABLE              {8BE4DF61-93CA-11D2-AA0D-00E098032B8C}
  ├── SecureBoot                 → sb.secure_boot
  ├── SetupMode                  → sb.setup_mode
  ├── PK                         → sb.pk
  ├── KEK                        → sb.kek
  └── ...

EFI_IMAGE_SECURITY_DATABASE_GUID {D719B2CB-3D3A-4596-A3BC-DAD00E67656F}
  ├── db                         → sb.db
  ├── dbx                        → sb.dbx
  ├── dbt                        → sb.dbt
  └── dbr                        → sb.dbr
```

## 2. TCG PC Client Platform Firmware Profile

### 2.1 测量启动 (Measured Boot) 架构

| TCG 概念 | 规范要求 | 本库实现 |
|:---|:---|:---|
| CRTM (Core Root of Trust for Measurement) | 第一段不可变启动代码 | `root_of_trust.c` — ROM stage |
| PCR 0-7 | 平台配置寄存器 | `trust_chain.c` — `verified_bitmap` |
| S-CRTM (Static CRTM) | 静态信任根 | `rot_verify_first_stage()` |
| D-CRTM (Dynamic CRTM) | 动态信任根 (DRTM) | — (future) |
| Event Log | 启动事件日志 | `trust_chain_print_status()` |
| TPM2_Extend | 扩展 PCR 值 | — (future: TPM integration) |
| Authority Measurements | 代码签名者身份 | `X509Cert` → `subject` |

### 2.2 启动流程对照

```
TCG Profile:
  S-CRTM → Verify SPL → Measure SPL → Transfer → SPL
  SPL    → Verify TPL → Measure TPL → Transfer → TPL
  ...

mini-secure-boot:
  rot_verify_first_stage() → trust_chain_verify_component() → ...
```

## 3. NIST SP 800-147: BIOS Protection

| NIST 要求 | 说明 | 本库实现 |
|:---|:---|:---|
| Authenticated BIOS Update | 签名验证更新 | `firmware_update.c` — `fw_capsule_validate()` |
| Secure Local Update | 本地安全更新 | `fw_capsule_set_image()` + 签名验证 |
| Optional Remote Update | 可选远程更新 | — (future) |
| Integrity Protection | BIOS 完整性保护 | RSA-2048 + SHA-256 |
| Non-Bypassability | 不可绕过 | `rot_lock_device()` → `ROT_POLICY_LOCKED` |

### NIST SP 800-155: BIOS Integrity Measurement

- **测量**: 计算固件 SHA-256 哈希
- **存储**: PCR / Event Log
- **报告**: 远程证明 (Attestation)
- **验证**: 与已知良好值比较

本库中：`sha256_hash()` 用于测量，`trust_chain_verify_all()` 用于验证。

## 4. Android Verified Boot (AVB 2.0)

### 4.1 AVB 链 vs 本库链

```
AVB:
  Boot ROM → Bootloader → boot.img → system.img → vendor.img

mini-secure-boot:
  RoT → SPL → U-Boot → Linux Kernel → Initrd → FDT
```

### 4.2 VBMeta 结构 vs FIT Image

| AVB 概念 | AVB 实现 | mini-secure-boot |
|:---|:---|:---|
| vbmeta.img | 验证元数据镜像 | `FITImage` → `FITConfiguration` |
| Hash Tree (dm-verity) | 块级哈希树 | SHA-256 per-component |
| Chained Partitions | 链式分区 | `BootChain` → `verified_bitmap` |
| Rollback Protection | 反回滚索引 | `anti_rollback_counter` |
| libavb | 验证库 | `trust_chain.c` |

### 4.3 关键差异

| 特性 | AVB | mini-secure-boot |
|:---|:---|:---|
| 签名格式 | AVB Footer / VBMeta | X.509 / Authenticode |
| 分区管理 | vbmeta + 链式分区 | FIT Image |
| 哈希树 | dm-verity Merkle Tree | Per-image SHA-256 |
| 语言 | C++ (libavb) | C99 |
| 平台 | Android | 通用嵌入式 |

## 5. Chrome OS Verified Boot

### 5.1 关键概念

| Chrome OS 概念 | 说明 | 本库对应 |
|:---|:---|:---|
| Root Key | 根密钥 (硬件烧录) | `RootOfTrust.public_key_hash` |
| Firmware Signing Key | 固件签名密钥 | `X509Chain` — intermediate CA |
| Kernel Signing Keys | 内核签名密钥 | `FITConfiguration.signature_data` |
| Google Binary Block (GBB) | 固件标志存储 | `FWUpdateContext` flags |
| Recovery Mode | 恢复模式 | `ROT_POLICY_UNLOCKED` |
| Developer Mode | 开发者模式 | `sb.setup_mode` → `true` |
| Verified Boot | 验证启动 | `trust_chain_verify_all()` |

### 5.2 分区布局对比

```
Chrome OS:
  ROOT-A  (root key hash)
  ├── FIRMWARE
  ├── KERN-A  (signed kernel)
  ├── KERN-B  (backup kernel)
  └── ROOTFS-A (dm-verity)

mini-secure-boot:
  ROM (root key hash)
  ├── SPL (signed)
  ├── U-Boot (signed, FIT)
  └── Linux Kernel + FDT + Initrd (FIT)
```

## 6. Linux Shim / MOK

| 概念 | Linux 实现 | 本库对应 |
|:---|:---|:---|
| shim.efi | 签名的 EFI 引导管理器 | — |
| MOK (Machine Owner Key) | 机器所有者密钥 | `sb_enroll_db()` |
| MOK Manager | MOK 管理界面 | — |
| mokutil | MOK 命令行工具 | — |
| shim_lock | Shim 验证协议 | — (future: EFI protocol) |

## 7. 工具链对照

| 标准工具 | 功能 | 本库 API |
|:---|:---|:---|
| `openssl genrsa` | 生成 RSA 密钥 | `rsa_generate_simple_keypair()` |
| `openssl dgst -sha256` | SHA-256 哈希 | `sha256_hash()` |
| `sbsign` | 签名 EFI 镜像 | `sig_verify_efi_image()` |
| `sbverify` | 验证签名 | `rsa_sha256_verify()` |
| `efitools` | UEFI 密钥管理 | `sb_enroll_pk/kek/db()` |
| `KeyTool.efi` | 图形化密钥管理 | `sb_print_state()` |
| `efi-updatevar` | UEFI 变量更新 | `sb_enroll_*()` |

## 8. 未来扩展

- [ ] TPM 2.0 集成 (`tpm2_extend`, `tpm2_quote`)
- [ ] dm-verity 集成 (Merkle Tree 验证)
- [ ] UEFI Runtime Services 完整实现
- [ ] Secure Enclave (ARM TrustZone, Intel SGX)
- [ ] Device Firmware Update (DFU) via USB
- [ ] Capsule Update via UEFI Runtime Services
- [ ] TPM-based Attestation / Remote Attestation
- [ ] PKCS#11 Hardware Security Module (HSM) support
