# course-alignment — 课程参考规范对齐

> 本文档说明 mini-measured-boot 各模块与相关行业规范的对应关系

## TPM 2.0 Library Specification (TCG)

### Part 1: Architecture

| 章节                | 内容                        | 对应模块                      |
|---------------------|-----------------------------|-------------------------------|
| 8. TPM Architectures | TPM 内部子系统架构           | `demos/mini-tpm-internals`   |
| 11. PCR             | PCR 操作定义                | `src/pcr_bank.c`             |
| 12. Session         | 授权会话                    | `src/tpm_locality.c`         |
| 16. Attestation     | 证明结构 (Quote, Creation)  | `include/tpm2_structs.h`     |
| 17. Audit           | 审计功能                    | `demos/mini-tpm-internals`   |
| 18. Clock           | 内部时钟                    | `demos/mini-tpm-internals`   |
| 19. Dictionary Attack | 字典攻击防护              | `demos/mini-tpm-internals`   |
| 21. NV Storage      | 非易失性存储                | `demos/mini-tpm-internals`   |
| 23. Hierarchies     | 层级体系                    | `demos/mini-tpm-internals`   |

### Part 2: Structures

| 章节              | 内容                     | 对应模块                    |
|-------------------|--------------------------|-----------------------------|
| 10. TPM2B         | 带长度前缀的缓冲区       | `include/tpm2_structs.h`   |
| 11. TPMA          | 属性结构                 | `include/tpm2_structs.h`   |
| 12. TPMT/TPMS     | 标记/选择结构            | `include/tpm2_structs.h`   |
| 13. TPML          | 列表结构                 | `include/tpm2_structs.h`   |
| 14. TPMU          | 联合结构                 | `include/tpm2_structs.h`   |
| 15. TPMI          | 接口类型                 | `include/tpm2_structs.h`   |
| 16. TPM_ST (Attest) | 证明结构类型            | `include/tpm2_structs.h`   |
| Table 12: Algorithm IDs | 算法标识符          | `include/tpm2_structs.h`   |

### Part 3: Commands

| 章节/命令              | 功能                     | 对应模块                    |
|------------------------|--------------------------|-----------------------------|
| 18. TPM2_PCR_Extend   | PCR 扩展                 | `src/pcr_bank.c`           |
| 19. TPM2_PCR_Read     | PCR 读取                 | `src/pcr_bank.c`           |
| 20. TPM2_PCR_Reset    | PCR 复位 (DRTM)          | `src/pcr_bank.c`           |
| 22. TPM2_StartAuthSession | 启动授权会话         | `src/tpm_locality.c`       |
| 23. TPM2_PolicyPCR    | PCR 策略绑定             | `src/tpm_locality.c`       |
| 24. TPM2_PolicySecret | 密钥策略                 | `src/tpm_locality.c`       |
| 25. TPM2_PolicyOr     | OR 策略                  | `src/tpm_locality.c`       |
| 26. TPM2_PolicyPassword | 密码策略              | `src/tpm_locality.c`       |
| 27. TPM2_FlushContext | 刷新上下文               | `src/tpm_locality.c`       |
| 30. TPM2_Quote        | 远程证明                 | `include/tpm2_structs.h`   |
| 31. TPM2_Create       | 创建对象                 | `demos/mini-measured-boot-flow` |
| 32. TPM2_Unseal       | 解封数据                 | `demos/mini-measured-boot-flow` |

---

## TCG PC Client Platform Specifications

### PC Client Platform TPM Profile (PTP)

| 章节                          | 内容                         | 对应模块                      |
|-------------------------------|------------------------------|-------------------------------|
| 5. PCR Allocation             | PCR 分配方案                 | `include/pcr_bank.h`         |
| 6. PCR Banks                  | PCR Bank 配置               | `src/pcr_bank.c`             |
| 7. Locality                   | TPM Locality 定义            | `include/tpm_locality.h`     |
| 8. NV Indices                 | PC Client NV 索引分配        | `demos/mini-tpm-internals`   |
| Table 2: PCR Allocation       | 各 PCR 的使用               | `include/pcr_bank.h`         |
| Table 5: Locality Assignments | Locality 使用场景            | `include/tpm_locality.h`     |

### PC Client Platform Firmware Profile

| 章节                      | 内容                       | 对应模块                      |
|---------------------------|----------------------------|-------------------------------|
| 2. Measured Boot          | 度量启动规范               | `demos/mini-measured-boot-flow` |
| 2.1 SRTM                  | 静态信任根                | `src/crtm_drtm.c`            |
| 2.3 Event Log             | 事件日志格式              | `include/event_log.h`        |
| 2.4 PCR Extend            | PCR 扩展协议              | `src/pcr_bank.c`             |
| 3. EFI Protocols          | EFI 度量接口              | `include/event_log.h`        |

### TCG EFI Platform Specification

| 章节                        | 内容                       | 对应模块                      |
|-----------------------------|----------------------------|-------------------------------|
| 5. EFI_TCG2_PROTOCOL       | EFI TCG2 协议定义          | `include/event_log.h`        |
| 5.1 HashLogExtendEvent     | 扩展事件到 PCR             | `src/event_log.c`            |
| 6. Event Types              | EFI 事件类型定义           | `include/event_log.h`        |
| 6.1 EV_EFI_*               | EFI 事件类型常量           | `include/event_log.h`        |

---

## Intel TXT (Trusted Execution Technology)

### Intel TXT Measured Launched Environment Developer's Guide

| 章节                    | 内容                       | 对应模块                      |
|-------------------------|----------------------------|-------------------------------|
| 2. TXT Architecture     | TXT 架构概述               | `demos/mini-measured-boot-flow` |
| 3. SENTER Instruction   | GETSEC[SENTER] 指令语义    | `src/crtm_drtm.c`            |
| 4. SINIT ACM            | SINIT 认证码模块           | `src/crtm_drtm.c`            |
| 5. MLE Development      | MLE 开发指南               | `src/crtm_drtm.c`            |
| 6. MLE Header           | MLE 头部结构               | `include/crtm_drtm.h`        |
| 7. TXT Heap             | TXT 堆内存结构             | `include/crtm_drtm.h`        |
| 8. PCR Usage            | TXT DRTM PCR 使用          | `include/pcr_bank.h` / `crtm_drtm.c` |

### Intel TXT Software Development Guide

| 章节                   | 内容                       | 对应模块                      |
|------------------------|----------------------------|-------------------------------|
| 3. SENTER Flow         | SENTER 执行流程            | `src/crtm_drtm.c`            |
| 4. MLE Launch          | MLE 启动流程               | `src/crtm_drtm.c`            |
| 7. TXT Error Codes     | TXT 错误码                 | `include/crtm_drtm.h`        |

---

## AMD SKINIT (Secure Kernel Init)

### AMD64 Architecture Programmer's Manual, Vol. 2

| 章节                    | 内容                       | 对应模块                      |
|-------------------------|----------------------------|-------------------------------|
| 15.27 SKINIT            | SKINIT 指令规范            | `demos/mini-measured-boot-flow` |
| 15.28 Secure Loader     | SL 验证及加载              | `demos/mini-measured-boot-flow` |
| 15.29 SLB               | Secure Loader Block        | `demos/mini-measured-boot-flow` |

---

## 规范版本参考

| 规范                                         | 版本   | 日期        |
|----------------------------------------------|--------|-------------|
| TPM 2.0 Library Specification Part 1-4       | Rev 1.59 | 2019-Nov  |
| TCG PC Client Platform TPM Profile (PTP)     | Rev 1.05 | 2020-May   |
| TCG PC Client Platform Firmware Profile      | Rev 1.05 | 2020-Aug   |
| TCG EFI Platform Specification               | Rev 1.06 | 2020-Jun   |
| Intel TXT MLE Developer's Guide              | Rev 013  | 2020-Apr   |
| Intel TXT Software Development Guide         | Rev 013  | 2020-Mar   |
| AMD64 APM Vol. 2: System Programming (SKINIT)| Rev 3.41  | 2021-Dec   |
