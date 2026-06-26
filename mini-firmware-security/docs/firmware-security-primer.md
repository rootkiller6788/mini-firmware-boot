# Firmware Security Primer

> A comprehensive introduction to platform firmware security: threats, protections, and the Protect-Detect-Recover framework.

---

## 1. The Firmware Threat Model

### 1.1 Supply Chain Threats

Firmware attacks can be introduced at any point in the supply chain:

```
OEM → Contract Manufacturer → Logistics → Distribution → End User
  |           |                   |            |            |
  v           v                   v            v            v
BIOS source  Unlocked ME     Evil Maid    Interdiction   Physical
tampering    mfg mode        attack       (implants)     access
```

**Supply chain risk indicators:**
- Intel ME in manufacturing mode (HFS[MFG_MODE]=1)
- AMD PSP JTAG left enabled
- Flash descriptor unlocked (no FLOCKDN)
- Debug interfaces active (DCI, XDP, JTAG)
- Unsigned firmware on shipping hardware

### 1.2 Pre-Boot Threats

Before the OS loads, firmware components initialize the platform:

```
Power-On → uCode → Boot ROM → UEFI PEI → UEFI DXE → BDS → OS Loader
```

Threats at each stage:
- **Boot ROM**: Immutable, but S3 resume may bypass
- **PEI (Pre-EFI Init)**: Memory not initialized, limited protection
- **DXE (Driver Execution)**: Full drivers, large attack surface
- **BDS (Boot Device Selection)**: Option ROM execution risks
- **OS Loader**: Shim/GRUB vulnerabilities

### 1.3 Runtime Threats

After OS boot, firmware remains resident:

| Threat                                 | Vector                             |
|----------------------------------------|------------------------------------|
| SMM rootkit                            | SMI handler subversion             |
| SPI flash overwrite from kernel        | `/dev/mem`, MMIO to SPI BAR        |
| DMA attack via malicious PCIe device   | Bus mastering, ATS bypass          |
| ME/PSP backdoor access                 | HECI interface from host           |
| UEFI runtime variable corruption       | SetVariable() with crafted data    |
| Firmware update tampering              | Capsule update poisoning           |

---

## 2. The Protect-Detect-Recover Framework

Based on NIST SP 800-193:

```
     ┌──────────┐
     │ PROTECT  │ ← Secure update, signed firmware, locked config
     └────┬─────┘
          │
     ┌────▼─────┐
     │  DETECT  │ ← Integrity measurement, audit logging
     └────┬─────┘
          │ (corruption detected)
     ┌────▼─────┐
     │ RECOVER  │ ← Boot golden copy, rollback to known good
     └──────────┘
```

### 2.1 Protection Mechanisms

| Mechanism              | Scope                  | Implementation         |
|------------------------|------------------------|------------------------|
| Flash Descriptor Lock  | SPI flash regions      | HSFS[FLOCKDN]          |
| Protected Ranges (PRx) | Boot block, ME region  | PR0-PR4 registers      |
| SMRR                   | SMM code/data          | MSR 0x1F2, 0x1F3       |
| IOMMU (VT-d/AMD-Vi)    | DMA from devices       | Device table + PT      |
| BIOS Lock Enable (BLE) | BIOS_CNTL write enable | I/O 0xDC[1]            |
| Signed Firmware        | Update authenticity    | RSA/ECDSA verification |
| Anti-rollback          | Version enforcement    | SVN / version check    |

### 2.2 Detection Mechanisms

| Mechanism              | What it detects                        |
|------------------------|----------------------------------------|
| Hash comparison        | Unauthorized firmware modification     |
| Boot measurement       | Changes in code at each boot stage     |
| Audit logging          | Anomalous configuration changes        |
| ME/PSP health check    | Manufacturing mode, debug interfaces   |
| SMM callout detection  | SMI handler accessing non-SMRAM code   |

### 2.3 Recovery Mechanisms

| Mechanism              | How it works                           |
|------------------------|----------------------------------------|
| Golden image           | Factory-programmed known-good copy     |
| Dual SPI flash         | Alternate flash chip with backup       |
| Recovery capsule       | Signed recovery image from OEM         |
| Top swap               | Boot from backup boot block            |
| Auto-recovery policy   | Automatically switch on corruption     |

---

## 3. Secure Flash Update Flow

### 3.1 Update Process

```
+------------------+
| 1. Receive Capsule|  UEFI UpdateCapsule() or OS driver
+--------+----------+
         |
+--------v----------+
| 2. Verify Signature|  Check RSA-3072 / ECDSA P-384 against OEM key
+--------+----------+
         |
+--------v----------+
| 3. Check Version   |  Prevent downgrade (CompareSecurityVersion)
+--------+----------+
         |
+--------v----------+
| 4. Unlock Flash    |  Set BIOSWE=1 (if BIOS region)
+--------+----------+
         |
+--------v----------+
| 5. Erase Sectors   |  Block erase (4KB/64KB sectors)
+--------+----------+
         |
+--------v----------+
| 6. Write Data       |  Page program (256 bytes/page)
+--------+----------+
         |
+--------v----------+
| 7. Verify Write     |  Read-back compare
+--------+----------+
         |
+--------v----------+
| 8. Lock Flash       |  Clear BIOSWE, set BLE
+--------+----------+
         |
+--------v----------+
| 9. Reset Platform   |  Cold reboot to activate
+--------------------+
```

### 3.2 Anti-Rollback

```
Boot Media Descriptor → Security Version Number (SVN)
  ├── Active FW version = 5
  ├── Recovery FW version = 3 (minimum allowed)
  └── Attempt to flash version 2 → BLOCKED (downgrade attack)
```

---

## 4. Key Registers and Hardware Interfaces

### 4.1 x86 Memory Architecture

```
+--------------------------------------------------+
| Ring 3: User applications                         |
| Ring 0: OS kernel, drivers                        |
| Ring -1: Hypervisor (VMX root)                    |
| Ring -2: SMM (System Management Mode)             |
| Ring -3: Intel ME / AMD PSP                       |
+--------------------------------------------------+
```

### 4.2 Critical MSRs

| MSR                 | Address    | Purpose                          |
|---------------------|------------|----------------------------------|
| IA32_SMRR_PHYSBASE  | 0x000001F2 | SMRR base address                |
| IA32_SMRR_PHYSMASK  | 0x000001F3 | SMRR mask and enable             |
| IA32_FEATURE_CONTROL| 0x0000003A | Lock bit for VMX, SMX, SMRR      |
| MSR_SMM_MCA_CAP     | 0x0000017D | SMM monitor features             |
| IA32_BIOS_UPDT_TRIG | 0x00000079 | BIOS update trigger (uCode)      |

### 4.3 PCI Configuration Space

| Register            | Bus:Dev.Func | Offset  | Purpose                      |
|---------------------|--------------|---------|------------------------------|
| SPI BAR             | 0:1F.5       | 0x10    | SPI base address register    |
| BIOS_CNTL           | 0:1F.0 (LPC) | 0xDC    | BIOS write / lock control    |
| HSFS                | SPI BAR+0x04 | —       | Hardware sequencing flash status |
| DMIBAR              | 0:0.0        | 0x68    | DMI / VT-d base address      |

---

## 5. Common CWE/CVE Patterns in Firmware

| CWE              | Pattern in Firmware                       | Example                     |
|------------------|-------------------------------------------|-----------------------------|
| CWE-119          | Buffer overflow in SMI handler            | CVE-2015-0949               |
| CWE-20           | Insufficient input validation (comm buf)  | CVE-2017-5705               |
| CWE-862          | Missing authorization (flash write)       | BIOS_CNTL not locked        |
| CWE-306          | Missing authentication (update)           | Unsigned capsule accepted   |
| CWE-276          | Incorrect default permissions              | ME mfg mode shipped         |
| CWE-367          | TOCTOU in comm buffer validation          | SmmIsBufferOutsideSmmValid  |
| CWE-787          | Out-of-bounds write in SMM                | SMI stack overflow          |
| CWE-693          | Protection mechanism failure              | SMRR not enabled            |

---

## 6. Tools of the Trade

| Tool               | Purpose                                    |
|--------------------|--------------------------------------------|
| **CHIPSEC**        | Platform security assessment framework     |
| **UEFITool**       | UEFI firmware image parsing and analysis   |
| **Flashrom**        | SPI flash read/write/verify                |
| **RWEverything**    | Arbitrary HW register read/write (Windows) |
| **Intel CSME Tools**| ME firmware analysis and version detection |
| **chipsec_util**   | CHIPSEC CLI for register dumping           |
| **dediprog / SF100**| Hardware SPI programmer                   |
| **Bus Pirate**     | Low-level SPI/I2C/JTAG debug interface     |
| **OpenBMC**        | BMC firmware development and testing       |

---

## 7. Further Reading

- **NIST SP 800-193**: Platform Firmware Resiliency Guidelines
- **NIST SP 800-147**: BIOS Protection Guidelines
- **NIST SP 800-147B**: BIOS Protection Guidelines for Servers
- **Intel BIOS Writer's Guide (BWG)**: SPI programming reference
- **Intel CSME 12.x/14.x/15.x Security White Papers**
- **AMD Platform Security Processor (PSP) Documentation**
- **UEFI Platform Initialization (PI) Specification**
- **TCG PC Client Platform Firmware Integrity Measurement**
- **Open Compute Project — Cerberus (firmware attestation)**

---

*Primer version 1.0 — coverage aligned with NIST SP 800-193, SP 800-147, and Intel/AMD platform security documentation.*
