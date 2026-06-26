# Mini Firmware Boot（迷你固件与引导）

**从零开始、零依赖的 C 语言实现**，涵盖固件、引导加载器和系统启动核心概念。每个模块以教学级精度仿真或建模真实固件行为 — 从 BIOS/UEFI 初始化、多级引导加载器到安全启动链、TPM 度量启动和远程证明。模块映射到 MIT、CMU 及工业标准，将固件理论桥接到可运行的 C 代码。

## 模块总览

| 模块 | 主题 | 参考标准 |
|--------|--------|----------------|
| [mini-firmware](mini-firmware/) | 固件架构、Flash 布局、复位向量、ROM/RAM 初始化、MMIO | Intel Firmware, ARM Trusted Firmware |
| [mini-boot-process](mini-boot-process/) | 上电复位、启动阶段（SEC/PEI/DXE/BDS）、CPU 初始化、内存初始化、设备枚举 | UEFI PI Spec, AMD AGESA |
| [mini-bios-uefi](mini-bios-uefi/) | 传统 BIOS（int 0x19、int 0x13）、UEFI（PE/COFF、GPT、Protocol）、CSM 兼容 | Phoenix BIOS, TianoCore EDK II |
| [mini-bootloader](mini-bootloader/) | Stage1/Stage2 引导、GRUB、U-Boot、Linux 启动协议、initramfs、multiboot | GRUB2, Das U-Boot |
| [mini-hardware-desc](mini-hardware-desc/) | 设备树（DTS/DTB）、ACPI 表（DSDT/SSDT）、SMBIOS、HOB 移交块 | Linux DTSpec, ACPI Spec 6.5 |
| [mini-secure-boot](mini-secure-boot/) | UEFI 安全启动、PK/KEK/db/dbx、签名 EFI 镜像、信任根、验证启动链 | UEFI Spec Ch 32 |
| [mini-measured-boot](mini-measured-boot/) | TPM 2.0 PCR 库、度量日志、CRTM、SRTM vs DRTM、Intel TXT | TPM 2.0 Spec, TCG PC Client |
| [mini-boot-attestation](mini-boot-attestation/) | TPM Quote、远程证明协议、证明密钥层级、EK/AIK、验证服务 | TPM 2.0 Spec Part 1, TCG TAP |
| [mini-firmware-security](mini-firmware-security/) | SPI Flash 保护、BMC/ME 安全、SMM 攻击、DMA 攻击、固件更新胶囊 | NIST SP 800-193, DHS CISA |

## 设计理念

- **零外部依赖** — 纯 C（C99/C11），仅使用 `libc` 和 `libm`
- **模块自包含** — 每个目录自带 `Makefile`、`include/`、`src/`、`examples/`、`demos/`、`tests/`
- **用户态固件仿真** — 对固件行为、启动流程和安全协议的教学级建模
- **理论到代码的映射** — 每个模块包含 `docs/` 目录，内有规范对齐说明
- **实用演示程序** — 启动仿真器、TPM 模拟器、安全启动验证器、设备树解析器等

## 构建方式

每个模块相互独立。进入模块目录后运行：

```bash
cd mini-boot-process
make all    # 构建全部
make test   # 运行测试
```

需要 **GCC** 和 **GNU Make**。

## 项目结构

```
mini-firmware-boot/
├── mini-firmware/              # 固件架构与基础
├── mini-boot-process/          # 启动流程与阶段
├── mini-bios-uefi/             # BIOS 与 UEFI 固件
├── mini-bootloader/            # 引导加载器（GRUB、U-Boot）
├── mini-hardware-desc/         # 硬件描述（DT、ACPI）
├── mini-secure-boot/           # UEFI 安全启动
├── mini-measured-boot/         # TPM 度量启动
├── mini-boot-attestation/      # 远程证明
└── mini-firmware-security/     # 固件安全
```

## 许可证

MIT
