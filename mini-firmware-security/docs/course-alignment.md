# Course Alignment — Firmware Security Module

> Mapping of `mini-firmware-security` content to industry standards, academic curricula, and vendor documentation.

---

## 1. Standards Alignment

### 1.1 NIST SP 800-193 — Platform Firmware Resiliency

| NIST SP 800-193 Section            | Module Coverage                              | Implementation File     |
|-------------------------------------|----------------------------------------------|-------------------------|
| 3.1 Protection — Secure Update     | `resilient_fw_secure_update`                 | firmware_resiliency.c   |
| 3.1 Protection — Firmware Integrity | `resilient_fw_verify_active`                | firmware_resiliency.c   |
| 3.2 Detection — Corruption Detection| `resilient_fw_detect_corruption`            | firmware_resiliency.c   |
| 3.2 Detection — Secure Auditing    | `resilient_fw_audit_log`                     | firmware_resiliency.c   |
| 3.3 Recovery — Automatic Recovery  | `resilient_fw_recover_to_golden`             | firmware_resiliency.c   |
| 3.3 Recovery — Known Good State    | `resilient_fw_validate_golden`               | firmware_resiliency.c   |
| 4.0 Rollback Protection            | `resilient_fw_rollback_protection`           | firmware_resiliency.c   |

### 1.2 NIST SP 800-147 — BIOS Protection

| NIST SP 800-147 Requirement          | Module Coverage                          | Implementation File     |
|---------------------------------------|------------------------------------------|-------------------------|
| 3.1 Authenticated BIOS Update         | `spi_set_protected_range`, PRx config    | spi_protection.c        |
| 3.2 Secure Local Update               | `spi_lock_config` (BLE, BIOSWE)          | spi_protection.c        |
| 3.3 Optional Presence (detect)        | `spi_check_access`                       | spi_protection.c        |
| 4.0 Integrity Protection              | SPILock.bios_we, SPI Descriptor          | spi_protection.c        |
| 4.1 Non-Bypassability                 | SPI_LOCK_FLOCKDN                         | spi_protection.c        |

### 1.3 NIST SP 800-147B — BIOS Protection for Servers

| Server-Specific Requirement           | Module Coverage                          |
|----------------------------------------|------------------------------------------|
| BMC firmware update protection         | `bmc_ipmi_command`, BMC virtual media    |
| Dual SPI flash support                 | Active/Recovery/Golden slots             |
| Verified boot chain                    | ME/PSP secure boot check                 |

---

## 2. Vendor Documentation Alignment

### 2.1 Intel Platform Documentation

| Document                                      | Section Used                          | Module           |
|-----------------------------------------------|---------------------------------------|------------------|
| Intel BIOS Writer's Guide (BWG) Rev 2.x       | SPI Flash Descriptor, PRx registers   | spi_protection   |
| Intel 200 Series PCH Datasheet Vol 1          | SPI0 controller, BIOS_CNTL, HSFS      | spi_protection   |
| Intel CSME 12.x Security White Paper          | ME manufacturing mode, HAP, JTAG     | bmc_me           |
| Intel SDM Vol 3, Ch 34 (SMM)                  | SMRR, D_LCK, SMI handler             | smm_attacks      |
| Intel VT-d Specification Rev 3.3              | DMA remapping, device table, ATS     | dma_attacks      |
| Intel STM Specification Rev 1.0               | SMM Transfer Monitor                 | smm_attacks      |
| Intel CSME Manufacturing Mode Guide           | `me_check_manufacturing_mode`        | bmc_me           |
| Intel Flash Programming Tool (FPT) Manual     | Flash descriptor layout              | spi_protection   |

### 2.2 AMD Platform Documentation

| Document                                      | Section Used                          | Module           |
|-----------------------------------------------|---------------------------------------|------------------|
| AMD Platform Security Processor (PSP) Guide   | JTAG, manufacturing mode, fusing     | bmc_me           |
| AMD-Vi (IOMMU) Specification Rev 2.0          | Device table, page tables, DTE       | dma_attacks      |
| AMD BIOS and Kernel Developer's Guide (BKDG)  | SPI controller, flash security       | spi_protection   |
| AMD64 APM Vol 2 (System Programming)          | SMM on AMD, SMM TSeg                 | smm_attacks      |

---

## 3. Industry Conference References

| Conference / Research                              | Topic                               | Module        |
|----------------------------------------------------|--------------------------------------|---------------|
| DefCon 22 — "Attacks on UEFI Security"           | SPI flash attacks                    | spi_protection|
| Black Hat 2015 — "SMM Attack Surface"            | SMM confused deputy, callout        | smm_attacks   |
| CanSecWest 2014 — "SMM Attacks"                  | SMM privilege escalation            | smm_attacks   |
| Black Hat 2017 — "Thunderstrike 2"               | Evil Maid DMA via Thunderbolt       | dma_attacks   |
| 35C3 — "Intel ME Manufacturing Mode"             | ME supply chain detection           | bmc_me        |
| Black Hat EU 2020 — "BMC Security"               | IPMI, SOL, KCS attacks              | bmc_me        |
| USENIX 2021 — "Firmware Resiliency"              | NIST SP 800-193 recovery            | firmware_resiliency |

---

## 4. Academic Curriculum Mapping

### 4.1 Computer Security Courses

| Course Topic                          | Module Coverage                       |
|---------------------------------------|---------------------------------------|
| Hardware Security Fundamentals        | SPI flash, ME/PSP, BMC architecture   |
| Trusted Computing Base                 | SMM as TCB, Ring -2 privilege         |
| Side-Channel Attacks                  | DMA via PCIe (non-atomic)             |
| Secure Boot Chains                    | Resilient FW, golden/recovery slots   |
| Firmware Threat Modeling              | Supply chain, pre-boot, runtime       |

### 4.2 Embedded Systems Security

| Topic                                 | Coverage                              |
|---------------------------------------|---------------------------------------|
| Flash memory protection               | PRx, FLOCKDN, descriptor locking      |
| Memory-mapped I/O security            | IOMMU, DMA remapping, ATS             |
| Platform management interfaces        | IPMI/KCS, BMC virtual media           |
| Secure firmware update                | Signed capsule, anti-rollback         |

### 4.3 Operating Systems Security

| Topic                                 | Coverage                              |
|---------------------------------------|---------------------------------------|
| Memory isolation (SMRR)               | SMM Range Registers                   |
| IOMMU/DMA protection                  | VT-d / AMD-Vi page table translation |
| Privilege rings (x86)                 | Ring 0 → Ring 3, Ring -2 (SMM)       |

---

## 5. Test and Validation Frameworks

| Framework               | Alignment                                       |
|--------------------------|-------------------------------------------------|
| CHIPSEC                  | `spi_lock`, `spi_desc`, `smm` modules           |
| Firmware Test Suite (FWTS)| UEFI SMM test, SPI lock test                   |
| Intel Converged Security Suite (Intel CSS) | ME security state, Boot Guard      |
| AMD Platform Security Checker | PSP security verification                  |
| OpenBMC                  | IPMI interface testing                           |

---

## 6. Learning Path Progression

```
Module 1: SPI Protection (spi_protection.c)
  └── Understand: Flash descriptor, PRx, BIOS_CNTL, HSFS
  └── Demo: spi_lock_demo — lock flash, attempt write → DENIED

Module 2: SMM Attacks (smm_attacks.c)
  └── Understand: SMRAM, SMRR, SMI handler, confused deputy
  └── Demo: smm_attack_demo — craft buffer, check SMRR

Module 3: DMA/IOMMU (dma_attacks.c)
  └── Understand: VT-d, device table, page table walk
  └── Demo: iommu_demo — evil DMA blocked by IOMMU

Module 4: BMC/ME (bmc_me.c)
  └── Understand: IPMI, KCS, manufacturing mode, JTAG
  └── Risk: Detect supply chain tampering

Module 5: Firmware Resiliency (firmware_resiliency.c)
  └── Understand: Protect / Detect / Recover cycle
  └── Demo: Corruption detection → auto recovery
```

---

## 7. Certification Exam Alignment

| Certification          | Relevant Domains Covered                     |
|------------------------|----------------------------------------------|
| CompTIA Security+      | Firmware security, secure boot               |
| CISSP Domain 3         | Hardware/firmware security architecture      |
| OSCP (OffSec)          | Firmware exploitation techniques             |
| GIAC GCFA / GREM       | Rootkit detection, SMM malware               |

---

*Document version 1.0 — aligned with NIST SP 800-193 Rev 1, NIST SP 800-147 Rev 1, Intel BWG Rev 2.4, AMD PSP Guide Rev 3.0*
