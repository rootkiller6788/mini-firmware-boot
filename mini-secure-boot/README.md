# mini-secure-boot — 安全启动 (C 语言实现)

> 参考 UEFI Spec Chapter 32, TCG PC Client, NIST SP 800-147, Android Verified Boot, Chrome OS Verified Boot

## 概述

mini-secure-boot 是一个用 C99 实现的轻量级安全启动库，仅依赖 libc 和 libm。提供了 UEFI Secure Boot 变量管理、RSA 签名验证、X.509 证书链、FIT 镜像验证、固件胶囊更新和硬件信任根等核心模块。

## 架构

```
mini-secure-boot/
├── include/                     # 公共头文件
│   ├── uefi_sb.h                # UEFI Secure Boot 变量
│   ├── signature_verify.h       # 签名验证 (RSA/SHA-256/X.509)
│   ├── trust_chain.h            # 验证启动链 + FIT Image
│   ├── firmware_update.h        # 固件胶囊更新
│   └── root_of_trust.h          # 硬件信任根
├── src/                         # 实现文件
│   ├── uefi_sb.c
│   ├── signature_verify.c
│   ├── trust_chain.c
│   ├── firmware_update.c
│   └── root_of_trust.c
├── examples/                    # 演示程序
│   ├── secure_boot_demo.c       # UEFI Secure Boot 完整流程
│   ├── signature_demo.c         # RSA 签名 + 证书链验证
│   └── fit_verify_demo.c        # FIT Image 创建/签名/验证
├── demos/                       # 深度讲解文档
│   ├── mini-secure-boot-chain/  # 安全启动链完整分析
│   └── mini-firmware-signing/   # 固件签名详解
├── docs/                        # 课程文档
│   ├── course-alignment.md      # 工业标准对齐
│   └── secure-boot-fundamentals.md  # 基础知识
├── Makefile
└── README.md
```

## 模块

| 模块 | 头文件 | 功能 |
|:---|:---|:---|
| UEFI SB | `uefi_sb.h` | PK/KEK/db/dbx 变量管理、镜像黑白名单验证 |
| Signature | `signature_verify.h` | SHA-256、RSA-2048、X.509 证书解析与链验证 |
| Trust Chain | `trust_chain.h` | 验证启动链 (SPL→U-Boot→Linux)、FIT Image 解析 |
| Firmware Update | `firmware_update.h` | UEFI Capsule Update、反回滚保护 |
| Root of Trust | `root_of_trust.h` | ROM/eFUSE/PUF 信任根、设备秘密派生 |

| 演示 | 源文件 | 说明 |
|:---|:---|:---|
| Secure Boot Demo | `examples/secure_boot_demo.c` | 完整 PK→KEK→db 注册 + 黑白名单验证 |
| Signature Demo | `examples/signature_demo.c` | RSA 密钥生成 + 签名 + X.509 证书链 |
| FIT Verify Demo | `examples/fit_verify_demo.c` | FIT 镜像构建 + 签名 + 验证 + 启动链 |

## 编译与运行

```bash
# 编译所有演示
make all

# 单独运行
make run-secure-boot      # UEFI Secure Boot 演示
make run-signature        # 签名验证演示
make run-fit-verify       # FIT 镜像验证演示

# 清理
make clean
```

## 快速开始

```c
#include "uefi_sb.h"
#include "signature_verify.h"

int main(void) {
    SecureBootVars sb;
    sb_init(&sb);  // 进入 Setup Mode

    // 注册 Platform Key
    EFISignature pk = { .type = SB_SIG_TYPE_X509_CERT, ... };
    sb_enroll_pk(&sb, &pk);  // 退出 Setup Mode

    // 验证启动镜像
    uint8_t hash[32];
    sha256_hash(image, image_size, hash);
    bool ok = sb_verify_image(&sb, hash, 32, signature, sig_size);

    return ok ? 0 : 1;
}
```

## 信任链层级

```
ROM Boot (RoT) → SPL → U-Boot → Linux Kernel + FDT + Initrd
    │              │       │            │
    └─Verify──────▶┘       │            │
                   └─Verify┘            │
                           └───Verify───┘
```

## 证书链

```
Root CA (PK) → Intermediate CA (KEK) → Signing Certificate (db) → Bootloader
```

## 许可证

MIT License — 仅供学习和教育用途。
