# mini-firmware-security — 固件安全 (C 语言实现)

> 参考 NIST SP 800-193 (Platform Firmware Resiliency), NIST SP 800-147 (BIOS Protection), Intel CSME Security

---

## 模块总览

| 模块 | 头文件 | 源文件 | 演示 | 说明 |
|------|--------|--------|------|------|
| **SPI Flash 保护** | `include/spi_protection.h` | `src/spi_protection.c` | `examples/spi_lock_demo.c` | Flash 描述符区域、PRx 保护范围、BIOS_CNTL 锁定、FLOCKDN |
| **SMM 攻击与防御** | `include/smm_attacks.h` | `src/smm_attacks.c` | `examples/smm_attack_demo.c` | Confused deputy、SMM callout、SMRR 保护、Ring -2 权限 |
| **DMA 攻击与 IOMMU** | `include/dma_attacks.h` | `src/dma_attacks.c` | `examples/iommu_demo.c` | VT-d/AMD-Vi、设备表、页表翻译、恶意的 PCIe DMA |
| **BMC 与 Intel ME/AMD PSP** | `include/bmc_me.h` | `src/bmc_me.c` | — | IPMI/KCS 接口、ME 制造模式、JTAG 锁定、供应链检测 |
| **固件弹性 (Resiliency)** | `include/firmware_resiliency.h` | `src/firmware_resiliency.c` | — | NIST SP 800-193: 保护/检测/恢复、黄金镜像、审计日志 |

---

## 快速开始

```bash
# 编译所有演示程序
make

# 运行 SPI 闪存保护演示
make run-spi

# 运行 SMM 攻击演示
make run-smm

# 运行 IOMMU/DMA 攻击演示
make run-iommu

# 清理
make clean
```

---

## 目录结构

```
mini-firmware-security/
├── include/
│   ├── spi_protection.h          # SPI 闪存保护 API
│   ├── smm_attacks.h             # SMM 攻击与防御 API
│   ├── dma_attacks.h             # DMA 攻击与 IOMMU API
│   ├── bmc_me.h                  # BMC 和 Intel ME/AMD PSP API
│   └── firmware_resiliency.h     # 固件弹性 API (NIST SP 800-193)
├── src/
│   ├── spi_protection.c          # 实现 (180+ 行)
│   ├── smm_attacks.c             # 实现 (180+ 行)
│   ├── dma_attacks.c             # 实现 (150+ 行)
│   ├── bmc_me.c                  # 实现 (180+ 行)
│   └── firmware_resiliency.c     # 实现 (180+ 行)
├── examples/
│   ├── spi_lock_demo.c           # SPI 锁定和攻击演示
│   ├── smm_attack_demo.c         # SMM 攻击模拟演示
│   └── iommu_demo.c              # IOMMU 保护演示
├── demos/
│   ├── mini-spi-flash-security/
│   │   └── README.md             # SPI 闪存安全详解 (280+ 行)
│   └── mini-smm-attacks/
│       └── README.md             # SMM 攻击与防御详解 (280+ 行)
├── docs/
│   ├── course-alignment.md       # 课程对齐映射
│   └── firmware-security-primer.md # 固件安全导论
├── Makefile
└── README.md
```

---

## 构建依赖

- **编译器**: GCC (C99 标准)
- **库**: libc + libm
- **平台**: Linux / Windows (MinGW) / macOS

---

## 参考标准

| 标准 | 版本 | URI |
|------|------|-----|
| NIST SP 800-193 | Rev 1 (2018) | Platform Firmware Resiliency |
| NIST SP 800-147 | Rev 1 (2011) | BIOS Protection Guidelines |
| NIST SP 800-147B | Rev 1 (2014) | BIOS Protection for Servers |
| Intel CSME Security | 12.x - 16.x | Intel Security White Papers |
| Intel VT-d Spec | Rev 3.3 (2020) | DMA Remapping |
| AMD-Vi Spec | Rev 2.0 | AMD I/O Virtualization |
| Intel SDM Vol 3 | Ch 34 | System Management Mode |

---

## 许可证

本实现仅用于教育目的。所有参考标准归其各自所有者所有。
