# mini-smm-attacks — SMM Attack and Defense

> Reference: Intel 64 and IA-32 Architectures Software Developer's Manual (SDM), Volume 3, Chapter 34: System Management Mode. AMD64 Architecture Programmer's Manual, Volume 2: System Programming, Section 7.

---

## 1. System Management Mode (SMM) Overview

SMM is the most privileged x86 execution mode (Ring -2). It operates transparently to the OS/hypervisor.

### 1.1 Key Properties

| Property          | Value                     |
|-------------------|---------------------------|
| Privilege level   | Ring -2                  |
| Address space     | SMRAM (TSEG + ASEG)      |
| Entry             | SMI# pin assertion        |
| Exit              | RSM instruction            |
| Default SMBASE    | 0x30000 (legacy)          |
| SMRAM size        | Configurable (TSEG size)  |

### 1.2 SMRAM Layout (Legacy)

```
0x30000 +-------------------+
        | SMM Entry Point    |
0x38000 +-------------------+
        | SMM Save State      | (0x400 bytes per logical processor)
0x3FC00 +-------------------+
        | Reserved            |
0x3FFFF +-------------------+
```

### 1.3 TSEG Layout (Modern)

Modern platforms use TSEG (Top of Memory Segment) for SMRAM:

```
TSEGBASE  +-------------------+
          | SMRAM Stolen Memory|
          | SMI Handler Code   |
          | SMM Data           |
          | SMM Save States    |
TSEG LIMIT+-------------------+
```

---

## 2. SMRR Protection (SMM Range Registers)

### 2.1 SMRR Overview

SMRR (IA32_SMRR_PHYSBASE, IA32_SMRR_PHYSMASK) restricts access to SMRAM:

- **SMRR Base** (MSR 0x1F2): Defines the base of SMRAM.
- **SMRR Mask** (MSR 0x1F3): Defines the range and enables protection.

```
SMRR_PHYSBASE: [11:0]=Type, [31:12]=BaseAddr
SMRR_PHYSMASK: [10]=Valid, [11]=Lock, [31:12]=Mask
```

When SMRR is enabled, accesses to SMRAM region from non-SMM code result in no data being returned (writes are dropped, reads return all 1's).

### 2.2 SMRR Configuration Sequence

1. Enter SMM (SMI# triggered).
2. Write `IA32_SMRR_PHYSBASE` with correct base address.
3. Write `IA32_SMRR_PHYSMASK` with Valid=1, mask value.
4. Exit SMM (RSM). No further SMRR writes from non-SMM.

---

## 3. SMM Attack Taxonomy

### 3.1 Confused Deputy Attack

**Mechanism:** The SMI handler trusts a communication buffer passed from the OS. An attacker crafts a malicious buffer that causes the handler (the "deputy") to perform unauthorized operations.

```
OS (Ring 0):
  1. Allocate comm buffer in normal RAM
  2. Fill with crafted data pointing to SMM-managed data
  3. Trigger SMI# via port 0xB2 write

SMI Handler (Ring -2):
  4. Read comm buffer (trusts OS-supplied pointer!)
  5. Dereferences pointer inside SMRAM → security boundary violation
  6. Returns modified data back to OS
```

**Mitigation:** 
- Validate that all pointers in the comm buffer point within the comm buffer itself.
- Copy comm buffer contents into SMRAM before processing.
- Use SMM communication buffer at fixed location within SMRAM.

### 3.2 SMM Callout Attack

**Mechanism:** SMI handler calls UEFI services (DXE drivers) or OS code outside SMRAM, breaking the isolation boundary.

```
SMI Handler:
  1. Receives SMI with request to perform work
  2. Calls gBS->LocateProtocol() to find a protocol
  3. DXE driver code runs OUTSIDE SMRAM
  4. Attacker has patched DXE code = code execution inside "SMM"

Detection: SMM code should NOT use UEFI Boot Services in SMM.
```

**Mitigation:**
- SMM must only use SMM-specific services (SmmServicesTable).
- Static analysis to detect CALL/RET/JMP to addresses outside SMRR range.
- Intel STM (SMM Transfer Monitor) enforces SMM code execution boundaries.

### 3.3 TOCTOU in Communication Buffer

**Time-of-Check Time-of-Use:**

```
SMI Handler:
  1. Validates comm buffer contents [Time of Check]
  2. SmmIsBufferOutsideSmmValid() returns TRUE ✓
  3. ... timing gap ...
  4. Attacker remaps page to SMRAM via MMIO/PCI hole [Time of Use]
  5. Handler reads "validated" buffer → reads SMRAM → leak!
```

### 3.4 Ring -2 to Ring 0 Privilege Escalation

**Speaker Fallback Attack (CVE-2020-xxxx):** When SMI handler processes ACPI/ASL code, it may call "speaker beep" fallback that executes from unprotected flash. Attacker can modify this code.

### 3.5 Smashing the Stack in SMM

SMI handler uses stack in SMRAM. If a nested SMI occurs while handling, the default SMBASE may be used for the new SMI's save state, potentially overwriting the handler's stack.

---

## 4. SMM Mitigations

### 4.1 SMRR Configuration

```c
// Must be executed inside SMM:
uint64_t smrr_base = (SMRAM_BASE_PHYS & 0xFFFFF000) | MTRR_TYPE_WB;
uint64_t smrr_mask = (~(SMRAM_SIZE - 1)) & 0xFFFFF000 | SMRR_MASK_VALID;

wrmsr(IA32_SMRR_PHYSBASE, smrr_base);
wrmsr(IA32_SMRR_PHYSMASK, smrr_mask);
```

### 4.2 D_LCK Bit

After SMRR is configured, set MSR `IA32_SMM_MONITOR_CTL[0]` to lock further SMRR modifications.

### 4.3 SMM Communication Buffer Validation

```c
// Validate that comm buffer is outside SMRAM
if (!SmmIsBufferOutsideSmmValid(CommBuffer, CommBufferSize)) {
    return EFI_ACCESS_DENIED;
}

// Copy into SMRAM-local buffer before processing
CopyMem(SmmLocalBuffer, CommBuffer, MIN(CommBufferSize, BUFFER_SIZE));

// Validate all embedded pointers reference SmmLocalBuffer only
for (int i = 0; i < pointer_count; i++) {
    uintptr_t ptr = SmmLocalBuffer->pointers[i];
    if (ptr < (uintptr_t)SmmLocalBuffer ||
        ptr >= (uintptr_t)SmmLocalBuffer + BUFFER_SIZE) {
        return EFI_INVALID_PARAMETER;
    }
}
```

### 4.4 Intel STM (SMM Transfer Monitor)

STM is a hypervisor that runs below SMM and intercepts VM exits during SMM execution:
- Blocks SMI handler from accessing non-SMRAM code pages.
- Enforces that all memory mapped for SMM access is properly configured.
- Monitors MSR accesses during SMM.
- Provides measured SMM launch.

---

## 5. Implementation Details

### 5.1 Data Structures

```c
typedef struct {
    uint32_t                   smbase;
    uint32_t                   entry_point;
    void                      *handlers[SMM_MAX_HANDLERS];
    uint8_t                    comm_buffer[SMM_COMM_BUFFER_SIZE];
    bool                       smrr_enabled;
    uint32_t                   smrr_base;
    uint32_t                   smrr_mask;
    bool                       d_lock;
    bool                       d_open;
} SMMHandler;

typedef struct {
    uint8_t  sw_smi_code;
    uint8_t *comm_buffer_ptr;
    size_t   buffer_size;
    uint32_t handler_index;
} SMMCall;
```

### 5.2 Key Functions

| Function                    | Description                                         |
|-----------------------------|-----------------------------------------------------|
| `smm_init`                  | Initialize SMM context with legacy SMBASE           |
| `smm_handler_register`      | Register SMI handler function                       |
| `smm_handler_invoke`        | Invoke handler with comm buffer                     |
| `smm_attack_confused_deputy`| Simulate confused deputy attack                     |
| `smm_smm_callout_check`     | Detect SMM callouts to non-SMRAM                    |
| `smm_ring3_to_ring2_attack` | Attempt Ring 3 → SMM memory access                  |
| `smm_set_smrr`              | Configure SMRR base/mask                            |
| `smm_validate_smrr_access`  | Check if address falls within SMRR-protected range  |

---

## 6. Running the Demo

```bash
make
./bin/smm_attack_demo
```

Expected output shows:
1. SMRR enabling flow.
2. Confused deputy attack blocked by pointer validation.
3. SMM callout detection for non-SMRAM addresses.
4. SMRR access validation results.

---

## 7. Reference

| Document                                                    | Relevance                                       |
|-------------------------------------------------------------|-------------------------------------------------|
| Intel SDM Vol 3, Chapter 34                                 | SMM Architecture Reference                      |
| AMD APM Vol 2, Section 7                                    | SMM on AMD platforms                            |
| Intel STM Specification                                     | SMM Transfer Monitor                            |
| CanSecWest 2014 — SMM Attacks                               | Rafal Wojtczuk, Corey Kallenberg                |
| Black Hat 2015 — SMM Attack Surface                         | Xeno Kovah, Corey Kallenberg                    |
| CVE-2015-0949 — SafeBoot SMM Vulnerability                  | D_LCK bypass                                    |
| CVE-2018-12126 — Microarchitectural Store Buffer Data Sampling | MDS affecting SMM                          |

---

## 8. Glossary

| Term     | Definition                                          |
|----------|-----------------------------------------------------|
| ASEG     | A/B Segment — legacy SMRAM location at 0xA0000     |
| D_LCK    | SMRAM D_LCK bit — locks SMRAM open/close config     |
| D_OPEN   | SMRAM D_OPEN bit — SMRAM visible to non-SMM code     |
| D_CLS    | SMRAM D_CLS bit — SMRAM closed to non-SMM code       |
| RSM      | Resume from System Management — exit SMM            |
| SMI#     | System Management Interrupt — hardware pin           |
| SMRAM    | System Management RAM — memory visible only in SMM   |
| SMRR     | SMM Range Register — protects SMRAM from DMA/CPU     |
| STM      | SMM Transfer Monitor — hypervisor-based SMM guard    |
| TSEG     | Top of Memory Segment — modern SMRAM location        |
