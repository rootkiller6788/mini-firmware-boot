# mini-firmware-signing — 固件签名详解

> 本文详解固件签名的完整流程：密钥生成、PK/KEK/db 证书注册、Authenticode 签名格式、PKCS#7/CMS 签名结构以及 sbsign 等签名工具的使用。

## 1. 概述

固件签名是安全启动的核心环节。签名确保固件：
- **完整性 (Integrity)**: 未被篡改
- **真实性 (Authenticity)**: 来自可信来源
- **不可否认性 (Non-repudiation)**: 签名者无法否认

## 2. 密钥体系

### 2.1 密钥层次

```
┌─────────────────────────────────────────────────────────────────┐
│                      KEY HIERARCHY                              │
│                                                                 │
│   Level 0:  Platform Key (PK)                                   │
│   ├── 平台所有者 (OEM) 控制                                     │
│   ├── 自签名根证书 (Root CA)                                     │
│   └── 用于签名 KEK                                              │
│                                                                 │
│   Level 1:  Key Exchange Key (KEK)                              │
│   ├── OS 厂商密钥 (Microsoft, Red Hat, Canonical...)             │
│   ├── 由 PK 签名                                                │
│   └── 用于签名 db/dbx                                           │
│                                                                 │
│   Level 2:  Authorized Database (db)                            │
│   ├── 引导程序签名证书                                          │
│   ├── 驱动程序签名证书                                          │
│   ├── SHA-256 哈希 (白名单)                                     │
│   └── 由 KEK 签名                                               │
│                                                                 │
│   Level 3:  Forbidden Database (dbx)                            │
│   ├── 吊销的证书/签名/哈希                                      │
│   ├── 黑名单                                                    │
│   └── 由 KEK 签名                                               │
└─────────────────────────────────────────────────────────────────┘
```

### 2.2 密钥生成命令

```bash
# 生成 PK (Platform Key) — 自签名根证书
openssl req -new -x509 -newkey rsa:2048 -keyout PK.key \
    -out PK.crt -days 3650 -nodes -sha256 \
    -subj "/CN=Platform Key/O=OEM/C=US"

# 生成 KEK (Key Exchange Key)
openssl req -new -x509 -newkey rsa:2048 -keyout KEK.key \
    -out KEK.crt -days 3650 -nodes -sha256 \
    -subj "/CN=Key Exchange Key/O=OEM/C=US"

# 生成 db 签名密钥 (用于签名引导程序)
openssl req -new -x509 -newkey rsa:2048 -keyout db.key \
    -out db.crt -days 3650 -nodes -sha256 \
    -subj "/CN=Boot Signing Key/O=OEM/C=US"

# 导出为 DER 格式
openssl x509 -in PK.crt -outform DER -out PK.der
openssl x509 -in KEK.crt -outform DER -out KEK.der
openssl x509 -in db.crt -outform DER -out db.der

# 提取公钥
openssl rsa -in db.key -pubout -out db.pub.pem
```

### 2.3 证书格式

| 格式 | 扩展名 | 编码 | 用途 |
|------|--------|------|------|
| PEM | .pem, .crt | Base64 + Header/Footer | 文本格式，便于查看 |
| DER | .der, .cer | 二进制 (ASN.1 DER) | UEFI 固件存储格式 |
| P12 | .p12, .pfx | PKCS#12 容器 | 密钥对 + 证书打包 |

## 3. UEFI Secure Boot 变量注册

### 3.1 注册 PK

```bash
# 使用 KeyTool (UEFI Shell)
KeyTool.efi

# 使用 efi-updatevar (Linux)
efi-updatevar -f PK.der PK

# 使用 sbsigntools
sbkeysync --pk PK.der PK.key

# 编程方式 (本库 API)
EFISignature pk;
pk.type = SB_SIG_TYPE_X509_CERT;
pk.signature_size = cert_size;
memcpy(pk.signature_data, der_cert, cert_size);
sb_enroll_pk(&sb_vars, &pk);
```

### 3.2 注册 KEK

```bash
# KeyTool
KeyTool.efi → Edit KEK → Add KEK

# efi-updatevar
efi-updatevar -a -f KEK.der KEK

# sbkeysync
sbkeysync --kek KEK.der KEK.key

# 编程方式
sb_enroll_kek(&sb_vars, &kek_entry);
```

### 3.3 注册 db 签名

```bash
# 添加证书
efi-updatevar -a -f db.crt db

# 添加 SHA256 哈希
sha256sum bootloader.efi > bootloader.hash
# 将哈希转换为 EFI 签名格式并注册

# 编程方式
sb_enroll_db(&sb_vars, &db_entry);
```

### 3.4 注册 dbx 吊销

```bash
# 吊销已知的恶意签名 (例如 BlackLotus)
efi-updatevar -a -f revocations.bin dbx
```

## 4. Authenticode / PE 签名格式

### 4.1 PE 文件布局

```
┌─────────────────────────────────────────────────────────────────┐
│  PE / COFF Executable                                           │
│  ┌─────────────────────────────────────────────────────────────┐│
│  │  MS-DOS Header                                              ││
│  │  e_magic = "MZ"                                             ││
│  │  e_lfanew → PE Signature offset                             ││
│  ├─────────────────────────────────────────────────────────────┤│
│  │  PE Signature = "PE\0\0"                                    ││
│  ├─────────────────────────────────────────────────────────────┤│
│  │  COFF File Header                                           ││
│  │  Machine, NumberOfSections, ...                              ││
│  ├─────────────────────────────────────────────────────────────┤│
│  │  Optional Header                                           ││
│  │  PE32 / PE32+                                               ││
│  │  AddressOfEntryPoint                                        ││
│  │  ImageBase                                                  ││
│  │  DataDirectory[4]: CERTIFICATE_TABLE                        ││
│  │    VirtualAddress → RVA of signature                        ││
│  │    Size          → Size of signature data                   ││
│  ├─────────────────────────────────────────────────────────────┤│
│  │  Section Headers (.text, .data, .reloc, ...)                ││
│  ├─────────────────────────────────────────────────────────────┤│
│  │  Section Data                                               ││
│  ├─────────────────────────────────────────────────────────────┤│
│  │  Certificate Table (签名区域)                                ││
│  │  ┌─────────────────────────────────────────────────────────┐││
│  │  │ WIN_CERTIFICATE                                         │││
│  │  │  dwLength         : Total size                          │││
│  │  │  wRevision        : 0x0200 (WIN_CERT_REVISION_2_0)      │││
│  │  │  wCertificateType : 0x0002 (PKCS_SIGNED_DATA)           │││
│  │  │  bCertificate     : PKCS#7 SignedData                   │││
│  │  └─────────────────────────────────────────────────────────┘││
│  └─────────────────────────────────────────────────────────────┘│
└─────────────────────────────────────────────────────────────────┘
```

### 4.2 签署 PE 文件 (sbsign)

```bash
# 使用 sbsigntools
sbsign --key db.key --cert db.crt \
    --output bootloader-signed.efi bootloader.efi

# 验证签名
sbverify --cert db.crt bootloader-signed.efi

# 查看签名信息
sbattach --list bootloader-signed.efi

# 剥离签名
sbattach --remove --output bootloader-unsigned.efi bootloader-signed.efi
```

### 4.3 编程实现

```c
/* 验证 PE 签名 */
const RSAKey trusted_pub = { ... };
uint8_t *pe_image = load_file("bootloader.efi");

if (sig_verify_efi_image(pe_image, file_size, &trusted_pub)) {
    printf("EFI image verified successfully\n");
} else {
    printf("EFI image signature check FAILED\n");
}
```

## 5. PKCS#7 / CMS 签名结构

### 5.1 ASN.1 结构

```
ContentInfo (PKCS#7)
├── contentType: 1.2.840.113549.1.7.2 (signedData)
└── content: SignedData
    ├── version: 1
    ├── digestAlgorithms:
    │   └── AlgorithmIdentifier (sha256)
    ├── encapContentInfo:
    │   ├── contentType: 1.3.6.1.4.1.311.2.1.4 (SPC_INDIRECT_DATA)
    │   └── content (optional)
    ├── certificates (optional):
    │   ├── Signer Certificate (X.509)
    │   └── Intermediate CA(s)
    ├── crls (optional)
    └── signerInfos:
        └── SignerInfo
            ├── version: 1
            ├── issuerAndSerialNumber
            │   ├── issuer: DistinguishedName
            │   └── serialNumber
            ├── digestAlgorithm: sha256
            ├── authenticatedAttributes:
            │   ├── contentType: SPC_INDIRECT_DATA
            │   ├── messageDigest: SHA-256 hash
            │   ├── signingTime
            │   └── SPC_SP_OPUS_INFO
            ├── digestEncryptionAlgorithm: rsaEncryption
            ├── encryptedDigest: RSA signature
            └── unauthenticatedAttributes (optional)
```

### 5.2 SPC_INDIRECT_DATA

```
SPC_INDIRECT_DATA
├── data:
│   └── SpcAttributeTypeAndOptionalValue
│       ├── type: SPC_PE_IMAGE_DATA_OBJID
│       └── value: SpcPeImageData
│           ├── flags: 0
│           └── file:
│               └── SpcLink
│                   └── url: "<<<obsolete>>>"
└── messageDigest:
    └── DigestInfo
        ├── digestAlgorithm: sha256
        └── digest: SHA-256 hash of PE image
```

### 5.3 签名验证流程

```
1. 解析 PE 文件 → 获取 CERTIFICATE_TABLE RVA
2. 读取 WIN_CERTIFICATE → 提取 PKCS#7 SignedData
3. 解析 SignerInfo → 获取加密摘要
4. 使用签名者公钥解密摘要 → 得到期望哈希
5. 读取 authenticatedAttributes → 获取 messageDigest
6. 计算 PE 文件 SHA-256 哈希 (不含证书表区域)
7. 比较期望哈希与实际哈希
8. 验证证书链 → 检查签名者证书是否可追溯到信任锚
9. 检查证书吊销列表 (CRL) / OCSP
```

## 6. sbsigntools 工具链

### 6.1 工具列表

| 工具 | 功能 |
|------|------|
| `sbsign` | 对 EFI 镜像进行 Authenticode 签名 |
| `sbverify` | 验证 EFI 镜像签名 |
| `sbattach` | 附加/剥离/查看 EFI 签名 |
| `sbvarsign` | 签名 UEFI 变量更新 |
| `sbkeysync` | 同步密钥到 UEFI 变量存储 |

### 6.2 完整签名工作流

```bash
#!/bin/bash
# 完整固件签名工作流

# 1. 创建输出目录
mkdir -p signed

# 2. 对 EFI 应用签名
for efi in *.efi; do
    sbsign --key db.key --cert db.crt \
        --output "signed/${efi}" "$efi"
    echo "Signed: ${efi}"
done

# 3. 验证所有签名
for efi in signed/*.efi; do
    sbverify --cert db.crt "$efi"
    if [ $? -ne 0 ]; then
        echo "VERIFICATION FAILED: ${efi}"
        exit 1
    fi
done

# 4. 签名 UEFI 变量更新
sbvarsign --key KEK.key --cert KEK.crt \
    --output db.auth db db.bin

# 5. 同步到固件 (需要 KeyTool 或 efivarfs)
# cp db.auth /sys/firmware/efi/efivars/db-d719b2cb-...

echo "All signatures valid."
```

### 6.3 使用本库 API

```c
/* 本库提供的签名验证 API */
#include "signature_verify.h"

/* 1. 解析 X.509 证书 */
X509Cert cert;
uint8_t *der_data = load_file("signer.der");
x509_parse_cert(&cert, der_data, der_len);

/* 2. 验证证书链 */
X509Chain chain;
/* ... 填充链 ... */
x509_verify_chain(&chain, &root_key);

/* 3. 验证 EFI 镜像 */
bool ok = sig_verify_efi_image(efi_data, efi_size, &trusted_pub);
```

## 7. 哈希算法选择

| 算法 | 输出长度 | UEFI 支持 | 安全性 |
|------|----------|-----------|--------|
| SHA-1 | 160 bits | 是 (已弃用) | 弱 — 不应使用 |
| SHA-256 | 256 bits | 是 | 当前标准 |
| SHA-384 | 384 bits | 是 | 更高安全性 |
| SHA-512 | 512 bits | 是 | 最高安全性 |

**注意**: 现代安全启动实现应使用 SHA-256 或更强，避免 SHA-1。

## 8. 吊销机制

### 8.1 吊销类型

| 类型 | 目标 | 粒度 |
|------|------|------|
| 证书吊销 | 吊销整个证书 | 粗粒度 |
| 哈希吊销 | 吊销特定二进制 | 细粒度 |
| 时间戳吊销 | 吊销某个时间前的签名 | 中等粒度 |

### 8.2 dbx 条目格式

```
EFI_SIGNATURE_LIST
├── EFI_SIGNATURE_DATA (SHA-256 Hash)
│   ├── SignatureOwner = 77fa9abd-0359-4d32-bd60-28f4e78f784b
│   └── SignatureData   = [32 bytes SHA-256 hash]
│
├── EFI_SIGNATURE_DATA (X.509 Certificate)
│   ├── SignatureOwner = 77fa9abd-0359-4d32-bd60-28f4e78f784b
│   └── SignatureData   = [DER encoded X.509 cert]
```

## 9. TPM 与测量启动

### 9.1 PCR 扩展

```
PCR[0]: 固件代码 (CRTM)
PCR[1]: 固件配置
PCR[2]: 外部 ROM 代码
PCR[3]: 外部 ROM 配置
PCR[4]: IPL 代码 (OS Loader)
PCR[5]: IPL 配置
PCR[6]: 状态转换和唤醒事件
PCR[7]: 平台特定固件
```

### 9.2 TPM PCR 与安全启动结合

```
安全启动 → 只允许签名固件执行
   +
测量启动 → 记录执行的固件哈希到 PCR
   =
完整信任链 — 既能防止未授权代码执行，又能审计执行记录
```

## 10. 参考

- UEFI Specification v2.10, Chapter 32
- Microsoft Windows Secure Boot Key Creation and Management Guidance
- PE/COFF Specification, Chapter 5: Certificate Table
- PKCS#7 / Cryptographic Message Syntax (CMS), RFC 5652
- Authenticode PE Signature Format
- TCG PC Client Platform Firmware Profile v1.05
- sbsigntools: https://git.kernel.org/pub/scm/linux/kernel/git/jejb/sbsigntools.git/
