# mini-spi-flash-security — SPI Flash Security

> Reference implementations based on Intel ICH/PCH SPI controller, NIST SP 800-147, and Intel BIOS Writer's Guide.

---

## 1. SPI Flash Architecture Overview

The SPI (Serial Peripheral Interface) flash device on modern Intel platforms stores critical firmware components:

- **BIOS/UEFI Firmware** — Platform initialization and boot services.
- **Intel ME Firmware** — Management Engine code and configuration.
- **GBE Firmware** — Gigabit Ethernet controller firmware.
- **Flash Descriptor** — Region map, access control table, and soft straps.
- **Platform Data Region (PDR)** — OEM-specific configuration data.

### 1.1 Physical Layout

```
+---------------------------------------------------+
| Flash Descriptor (0x000000 - 0x000FFF)             |
|  - Signature: 0x0FF0A55A                           |
|  - Region base/limit for BIOS, ME, GBE, PDR, DevExp|
|  - Master access permissions table                 |
+---------------------------------------------------+
| BIOS Region (0x001000 - 0x5FFFFF)                  |
|  - UEFI firmware volumes                           |
|  - Boot block (top of flash)                       |
|  - NVRAM variables                                 |
+---------------------------------------------------+
| ME Region (0x600000 - 0x9FFFFF)                    |
|  - Intel ME kernel, modules, configuration         |
|  - BUP (Bring-Up Platform) code                    |
|  - File system (Minix or MFS)                      |
+---------------------------------------------------+
| GBE Region (0xA00000 - 0xBFFFFF)                   |
|  - Ethernet firmware and configuration             |
|  - MAC address, PXE ROM                            |
+---------------------------------------------------+
| PDR (0xC00000 - 0xCFFFFF)                          |
|  - OEM-specific platform data                      |
|  - Manufacturing information                       |
+---------------------------------------------------+
| Device Expansion (0xD00000 - 0xFFFFFF)             |
|  - Optional: EC firmware, TPM configuration        |
+---------------------------------------------------+
```

---

## 2. Flash Descriptor

### 2.1 Region Descriptor Table

Each region is described by a base and limit register that defines its address range within the flash. The flash descriptor at offset 0x0000 contains:

| Offset | Size | Field              |
|--------|------|--------------------|
| 0x00   | 4    | FLVALSIG (0x0FF0A55A) |
| 0x04   | 4    | FLMAP0 — component density, FCBA, NC |
| 0x08   | 4    | FLMAP1 — region count, master count |
| 0x0C   | 4    | FLMAP2 — PCH Strap Length |
| 0x54   | 4    | FLMSTR1 — Master CPU/BIOS permissions |
| 0x58   | 4    | FLMSTR2 — Master ME permissions |
| 0x5C   | 4    | FLMSTR3 — Master GbE permissions |

### 2.2 Master Access Permissions

The Flash Master (FLMSTR) registers define what each master can do:

| Master | Region | Read | Write | Description |
|--------|--------|------|-------|-------------|
| CPU/BIOS | BIOS | 1 | 1 | Full BIOS access |
| CPU/BIOS | ME | 1 | 0 | Read ME, cannot modify |
| CPU/BIOS | GbE | 1 | 0 | Read GbE, cannot modify |
| ME | ME | 1 | 1 | Full ME access |
| ME | BIOS | 0 | 0 | No BIOS access (post-boot) |
| GbE | GbE | 1 | 1 | Full GbE access |
| GbE | BIOS | 0 | 0 | No BIOS access |

The implementation models these permissions in the `permissions_per_master[]` array within each `SPIDescriptorRegion`.

---

## 3. Protected Ranges (PRx)

### 3.1 PR0-PR4 Registers

Protected Ranges provide hardware-enforced read/write protection at the flash controller level. Five ranges (PR0-PR4) can be configured:

```
BIOS_CNTL Register (I/O 0xDC):
  Bit 0: BIOSWE (BIOS Write Enable)
  Bit 1: BLE (BIOS Lock Enable)
  Bit 5: SMM_BWP (SMM BIOS Write Protect)

HSFS Register (SPI BAR + 0x04):
  Bit 15: FLOCKDN (Flash Configuration Lock-Down)

PRx Register Format (SPI BAR + 0x74 + (x * 4)):
  Bits 31:16: PRL (Protected Range Limit)
  Bits 14:0:  PRB (Protected Range Base)
  Bit 15:     RPE (Read Protect Enable)
  Bit 31:     WPE (Write Protect Enable)
```

### 3.2 Protection Flow

1. Configure PRx base and limit registers.
2. Set write-protect (WPE) and/or read-protect (RPE) bits.
3. Set BIOS Lock Enable (BLE) to prevent BIOS_CNTL modifications.
4. Set SMM_BWP for additional protection during SMM execution.
5. Set FLOCKDN to permanently lock all SPI configuration registers.

Once FLOCKDN is set, PRx registers cannot be modified until the next platform reset (full power cycle or global reset).

---

## 4. BIOS_CNTL Register

The BIOS_CNTL register (I/O 0xDC on Intel chipsets) controls top-level BIOS write access:

| Bit | Name     | Description                                    |
|-----|----------|------------------------------------------------|
| 0   | BIOSWE   | BIOS Write Enable — must be set before writes  |
| 1   | BLE      | BIOS Lock Enable — locks BIOSWE bit            |
| 2   | SRC      | SPI Read Configuration                         |
| 3   | TSS      | Top Swap Status                                |
| 4   | SMM_BWP  | SMM BIOS Write Protect — only SMM can write    |
| 5   | BBS      | Boot BIOS Straps                               |
| 6   | BILD     | BIOS Interface Lock-Down                       |

**Write sequence:**
1. Set BIOSWE = 1.
2. Perform SPI write operation.
3. Clear BIOSWE = 0.
4. Optionally, set BLE = 1 to lock further modifications.

---

## 5. Secure Flash Update Flow

```
1. Receive signed firmware update capsule
       |
2. Verify RSA/ECDSA signature against OEM key
       |
3. Check firmware version (anti-rollback)
       |
4. Set BIOSWE = 1 (unlock BIOS region for write)
       |
5. Erase target sectors (SPI block erase command)
       |
6. Write new firmware image
       |
7. Verify written data (read-back compare)
       |
8. Clear BIOSWE = 0
       |
9. Set flash lockdown if permanent (optional)
       |
10. Update NVRAM boot variables
       |
11. Trigger platform reset for new firmware
```

---

## 6. Common Attack Vectors

### 6.1 SPI Flash Descriptor Overwrite

If the flash descriptor region is not locked, an attacker can rewrite the descriptor to grant themselves full access to all regions. Mitigation: FLOCKDN prevents descriptor modification.

### 6.2 PRx Bypass via Race Condition

Attacker attempts to modify PRx registers between the time BIOSWE is set and the write completes. Mitigation: SMM-based flash update ensures atomic operations.

### 6.3 Evil Maid Attack

Physical attacker attaches SPI programmer (Dediprog, CH341A) directly to flash chip. Mitigation: Intel Boot Guard with verified boot using OEM key fused into PCH. AMD equivalent: Platform Secure Boot (PSB).

### 6.4 ME Region Read via Descriptor

Attacker with kernel code execution modifies descriptor to grant CPU read access to ME region, dumping ME firmware for vulnerability analysis. Mitigation: Descriptor locked at manufacturing, HSFS[FLOCKDN] = 1.

---

## 7. Implementation Details

### 7.1 Data Structures

```c
typedef struct {
    uint32_t base;
    uint32_t limit;
    uint8_t  permissions_per_master[4];
} SPIDescriptorRegion;

typedef struct {
    uint32_t base;
    uint32_t limit;
    uint8_t  permissions;
    bool     write_protect;
    bool     read_protect;
} SPIProtectedRange;

typedef struct {
    bool bios_we;
    bool smm_bwp;
    bool ble;
    bool flockdn;
} SPILock;
```

### 7.2 Key Functions

| Function               | Description                                      |
|------------------------|--------------------------------------------------|
| `spi_protect_init`     | Initialize controller with default descriptor    |
| `spi_set_protected_range` | Configure PRx base/limit/permissions           |
| `spi_lock_config`      | Set BIOS_CNTL lock bits (BLE, SMM_BWP, FLOCKDN)  |
| `spi_check_access`     | Verify if master_id can read/write at address    |
| `spi_attack_attempt`   | Simulate malicious write attempt via Host master |

### 7.3 Running the Demo

```bash
make
./bin/spi_lock_demo
```

---

## 8. References

- **NIST SP 800-147**: BIOS Protection Guidelines
- **NIST SP 800-147B**: BIOS Protection Guidelines for Servers
- **Intel BIOS Writer's Guide (BWG)** — Section on SPI programming
- **Intel 7 Series / C216 Chipset Family PCH Datasheet** — SPI chapter
- **Intel PCH SPI Programming Guide** — Register definitions
- **CHIPSEC Framework** — `spi_lock`, `spi_desc` modules

---

## 9. Glossary

| Term     | Definition                                          |
|----------|-----------------------------------------------------|
| BLE      | BIOS Lock Enable — locks BIOS Write Enable bit      |
| BWE      | BIOS Write Enable — gates write access to BIOS region |
| FLOCKDN  | Flash Configuration Lock-Down — permanent lock      |
| HSFS     | Host Software Flash Status register                 |
| PCH      | Platform Controller Hub (Southbridge)               |
| PRx      | Protected Range Register (PR0 through PR4)          |
| SPI BAR  | SPI Base Address Register (PCI config space)        |
| VSCC     | Vendor Specific Component Capabilities              |
