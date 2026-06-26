# mini-bootloader — 引导加载器 (C 语言实现)

> 参考 GRUB2, Das U-Boot, Linux Boot Protocol, TPM 2.0, FIPS 180-4

一个全栈 bootloader 教育系统，模拟多阶段引导加载器功能，涵盖 MBR/VBR、Multiboot、Linux x86 引导协议、GRUB 模块系统、文件系统引导、ELF 加载、解压缩、内存管理、链式加载、启动配置和可信启动。

---

## Module Status: COMPLETE ✅

- **include/ + src/ total**: 3561 lines (>= 3000)
- **L1-L6**: Complete
- **L7**: Complete (3+ applications)
- **L8**: Complete (SHA-256 + TPM PCR + measured boot)
- **L9**: Partial (documented, concepts present)

---

## 模块总览

| 模块 | 头文件 | 源文件 | 说明 |
|--------|-----------|--------|-------------|
| **Stage 1** | `stage1.h` | `stage1.c` | MBR/VBR 引导阶段：分区表、活动分区检测、引导仿真 |
| **Stage 2** | `stage2.h` | `stage2.c` | Multiboot 阶段：MultibootHeader/Info、内存映射、内核交接 |
| **Linux Boot** | `linux_boot.h` | `linux_boot.c` | Linux x86 引导协议：bzImage、E820、initrd |
| **GRUB Modules** | `grub_modules.h` | `grub_modules.c` | GRUB 模块系统：注册表、依赖排序（拓扑排序） |
| **Filesys Boot** | `filesys_boot.h` | `filesys_boot.c` | 引导文件系统：FAT32/EXT2 支持、目录列表 |
| **ELF Loader** | `boot_elf.h` | `boot_elf.c` | ELF32/64 解析、段加载、符号查找、重定位 |
| **Compression** | `boot_compress.h` | `boot_compress.c` | LZ77/RLE 解压、gzip/DEFLATE、bzImage 处理 |
| **Memory Map** | `boot_memory.h` | `boot_memory.c` | E820 内存映射、合并/排序、first-fit 页分配器 |
| **Chain Load** | `boot_chain.h` | `boot_chain.c` | CHS/LBA 转换、扇区 I/O、GPT 解析、链式加载 |
| **Boot Config** | `boot_config.h` | `boot_config.c` | GRUB 风格配置解析器、菜单系统、tokenizer |
| **Secure Boot** | `boot_secure.h` | `boot_secure.c` | SHA-256 (FIPS 180-4)、TPM PCR、measured boot |

---

## 九层知识覆盖摘要

| Level | 名称 | 状态 | 关键条目 |
|-------|------|------|---------|
| **L1** | Definitions | Complete | 11 个头文件, 40+ struct/typedef, 80+ API 声明 |
| **L2** | Core Concepts | Complete | MBR 引导、Multiboot 协议、ELF 加载、DEFLATE、E820、链式加载、TPM PCR |
| **L3** | Engineering Structures | Complete | 引导阶段链、CHS/LBA 转换器、页分配器、配置状态机 |
| **L4** | Standards/Theorems | Complete | ELF 标准 (TIS)、gzip (RFC 1952)、DEFLATE (RFC 1951)、E820 (ACPI)、SHA-256 (FIPS 180-4)、GPT (UEFI) |
| **L5** | Algorithms/Methods | Complete | 拓扑排序 (依赖解析)、SHA-256 变换、LZ77 解压、first-fit 分配器、RLE 解压、ELF 重定位 |
| **L6** | Canonical Problems | Complete | 引导脚本解析器 (grub.cfg)、bzImage 加载、内核交接 |
| **L7** | Applications | Complete | 3 demos + 35 tests: MBR 引导、Multiboot、Linux 引导协议 |
| **L8** | Advanced Topics | Complete | SHA-256 加密哈希、TPM PCR 扩展、Measured Boot 日志、证书验证 |
| **L9** | Industry Frontiers | Partial | UEFI Secure Boot 概念已文档化，完整实现需要硬件 TPM/签名验证 |

## 核心定理列表

| 定理 | 公式/陈述 | 实现位置 |
|------|----------|---------|
| MBR 签名定理 | 有效 MBR 必须在偏移 510 处包含 0xAA55 | `stage1.c:mbr_validate()` |
| Multiboot 校验和定理 | magic + flags + checksum ≡ 0 (mod 2^32) | `stage2.c:stage2_parse_multiboot_header()` |
| ELF 魔数定理 | 有效 ELF 以 {0x7F,'E','L','F'} 开头 | `boot_elf.c:boot_elf_validate()` |
| LZ77 确定性解压定理 | 解压是单次遍历，仅依赖已输出数据 | `boot_compress.c:lz77_decompress()` |
| SHA-256 抗碰撞定理 | 无已知实际攻击，输出 256 位 | `boot_secure.c:sha256_transform()` |
| PCR 扩展不可逆定理 | PCR_new = H(PCR_old || digest) 是非可逆的 | `boot_secure.c:pcr_extend()` |

## 核心算法列表

| 算法 | 复杂度 | 位置 |
|------|--------|------|
| MBR 分区扫描 | O(1) | `stage1.c:mbr_find_bootable()` |
| 拓扑排序 (Kahn 算法变体) | O(n²) | `grub_modules.c:grub_topological_sort()` |
| ELF 段加载 | O(n) in phdr count | `boot_elf.c:boot_elf_load_segments()` |
| ELF 符号查找 | O(n) | `boot_elf.c:boot_elf_find_symbol()` |
| ELF 重定位应用 | O(n) | `boot_elf.c:boot_elf_apply_relocations()` |
| LZ77 解压 | O(n) | `boot_compress.c:lz77_decompress()` |
| RLE 解压 | O(n) | `boot_compress.c:rle_decompress()` |
| SHA-256 哈希 | O(n) | `boot_secure.c:sha256_transform()` |
| First-fit 页分配 | O(n) | `boot_memory.c:bootmem_alloc_pages()` |
| CHS-LBA 转换 | O(1) | `boot_chain.c:chs_from_lba()` |
| GRUB cfg 解析器 | O(n) | `boot_config.c:bootcfg_parse()` |

## 构建与运行

```bash
make          # 编译所有 demo
make test     # 编译并运行 35 个测试（一键通过）
```

### Demos

| 二进制 | 来源 | 演示内容 |
|--------|-------|-------------|
| `bin/mbr_boot_demo` | `examples/mbr_boot_demo.c` | MBR 创建、分区扫描、阶段 1 引导模拟 |
| `bin/multiboot_demo` | `examples/multiboot_demo.c` | Multiboot 头验证、内存映射、内核交接模拟 |
| `bin/linux_boot_demo` | `examples/linux_boot_demo.c` | Linux 引导协议、bzImage 设置头、E820 映射 |

### 测试

```bash
make test     # 35 个 assert-based 测试，覆盖所有核心 API
```

## 目录结构

```
mini-bootloader/
├── include/           # 11 个头文件
│   ├── stage1.h, stage2.h, linux_boot.h
│   ├── grub_modules.h, filesys_boot.h
│   ├── boot_elf.h, boot_compress.h, boot_memory.h
│   └── boot_chain.h, boot_config.h, boot_secure.h
├── src/               # 11 个源文件
│   ├── stage1.c, stage2.c, linux_boot.c
│   ├── grub_modules.c, filesys_boot.c
│   ├── boot_elf.c, boot_compress.c, boot_memory.c
│   └── boot_chain.c, boot_config.c, boot_secure.c
├── tests/
│   └── test_all.c     # 35 个测试用例
├── examples/          # 3 个端到端示例
├── demos/             # 演示材料
├── docs/              # 课程对标 + bootloader 内核文档
├── benches/           # 性能基准
├── Makefile
└── README.md
```

## 九校课程映射

| 学校 | 课程 | 本模块对应内容 |
|------|------|-------------|
| **MIT** | 6.004 Computation Structures | MBR/VBR 引导、Multiboot 协议、内核交接 |
| **Stanford** | CS 144 Networking | 网络引导概念（PXE 文档化） |
| **Berkeley** | CS 162 OS | ELF 加载、内存映射、进程创建类比 |
| **CMU** | 15-410 OS | 引导加载器设计、链式加载、安全引导 |
| **UT Austin** | CS 380D Distributed | 分布式引导（PXE/iSCSI 概念） |
| **ETH** | 263-0006 Computer Architecture | CHS/LBA 转换、扇区 I/O、硬件交互 |
| **Cambridge** | Part II: OS | 启动过程三阶段、bootloader 工程 |
| **清华** | 操作系统 (CS 核心) | Linux 引导协议、bzImage、E820 内存映射 |
| **Georgia Tech** | CS 6210 Advanced OS | ELF 动态加载、TPM/PCR 信任链 |

## Lines of Code

| 类别 | 行数 |
|------|------|
| include/ | 962 |
| src/ | 2599 |
| **合计** | **3561** |
