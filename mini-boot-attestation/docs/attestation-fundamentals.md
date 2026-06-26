# Attestation Fundamentals

> Core concepts of TPM-based attestation — local, remote, quotes, PCRs, event logs, nonces, and identity keys.

## Table of Contents

1. [What is Attestation?](#what-is-attestation)
2. [Local vs Remote Attestation](#local-vs-remote-attestation)
3. [Platform Configuration Registers (PCRs)](#platform-configuration-registers-pcrs)
4. [TPM Quote](#tpm-quote)
5. [PCR Composite Digest](#pcr-composite-digest)
6. [Event Log Replay](#event-log-replay)
7. [Nonce: Providing Freshness](#nonce-providing-freshness)
8. [AIK vs Attestation Key (AK)](#aik-vs-attestation-key-ak)
9. [Endorsement Key & Hierarchy](#endorsement-key--hierarchy)
10. [Signature Verification](#signature-verification)
11. [Attacker Model](#attacker-model)
12. [Common Attestation Topologies](#common-attestation-topologies)
13. [Summary](#summary)

---

## What is Attestation?

Attestation is the process of **proving a platform's state to a remote party**
by making a trustworthy statement about the software/hardware configuration.

In TPM-based attestation, the TPM acts as a **trusted third party** inside the
device. The TPM measures software (firmware, bootloader, OS) as it boots and
stores the measurements in Platform Configuration Registers (PCRs). When asked,
the TPM can produce a **signed statement** (a Quote) that contains:

1. The current PCR values
2. The request nonce (proving freshness)
3. Firmware/clock information

The signature is made with a key (the AIK) that can be traced back to the
TPM manufacturer, providing cryptographic assurance that the statement came
from a genuine TPM.

---

## Local vs Remote Attestation

### Local Attestation

```
Device A  <--->  Device B  (both have TPMs, on same platform)
```

- Both TPMs are on the same physical platform
- Uses TPM-internal attestation (TPM2_Certify)
- No network protocol needed
- Used for: Trusted Execution Environment (TEE) verification, secure enclaves

### Remote Attestation

```
Device (TPM)  <---network--->  Verifier (remote server)
```

- TPM-attested device communicates with a remote verifier over a network
- Uses challenge-response protocol for freshness
- Nonce prevents replay attacks
- AIK preserves privacy (avoids linking to EK identity)
- Used for: Fleet health, cloud workload integrity, Zero Trust networking

### Key Differences

| Aspect | Local | Remote |
|--------|-------|--------|
| **Distance** | Same platform | Network |
| **Freshness** | TPM clock/ticks | Verifier nonce |
| **Privacy** | Not needed | AIK / DAA |
| **Protocol** | TPM2_Certify | TPM2_Quote + challenge |
| **Latency** | Microseconds | Milliseconds+ |

---

## Platform Configuration Registers (PCRs)

### What are PCRs?

PCRs are **tamper-resistant registers** inside the TPM that store hash
measurements of software. The TPM typically has 24 PCRs (TPM 2.0).

### PCR Extend Operation

PCRs cannot be written directly. They are **extended**:

```
PCR_new = Hash(PCR_old || new_measurement)
```

This is a **one-way, append-only** operation. You cannot "undo" a PCR extend.

### Why Extend Instead of Set?

The chain-of-hash (`PCR_new = Hash(PCR_old || new)`) ensures:

1. **Order matters**: Extending A then B != extending B then A
2. **Completeness**: All measurements are captured
3. **History is preserved**: The final PCR value depends on every measurement
4. **Tamper-evident**: Missing or reordered measurements produce a different PCR

### PCR Index Allocation (TCG PC Client)

| PCR | Purpose | Typical Content |
|-----|---------|-----------------|
| 0 | System firmware (BIOS/UEFI) | Hash of firmware code |
| 1 | Host platform configuration | CPU microcode, chipset config |
| 2 | External / option ROM code | PXE ROM, RAID controller ROM |
| 3 | Option ROM configuration | ROM data/config |
| 4 | IPL / MBR code | Master Boot Record |
| 5 | IPL / MBR configuration | Partition table |
| 6 | Platform manufacturer specific | OEM data |
| 7 | Secure Boot policy | PK, KEK, db, dbx |
| 8-15 | OS-level (IMA) | File hashes, kernel modules |
| 16 | Debug | Debug/S3 resume |
| 17 | DRTM | Dynamic root of trust measurements |
| 18-22 | Trusted OS | TEE, SGX measurements |
| 23 | Application | User-defined |

### PCR Banks

TPM 2.0 supports multiple **hash algorithm banks** (e.g., SHA-1 bank and
SHA-256 bank). Each PCR index has a separate value per algorithm. A Quote
always references a specific bank.

---

## TPM Quote

### What is a Quote?

A TPM2_Quote is a TPM-signed structure that contains:

1. **TPMS_ATTEST** header:
   - `magic` = TPM_GENERATED (0xFF544347)
   - `type` = TPM_ST_ATTEST_QUOTE (0x8018)
   - `qualified_signer` = hash of the signing key's name
   - `extra_data` = nonce from verifier
   - `clock_info` = TPM clock snapshot
   - `firmware_version` = TPM firmware version
   - `pcr_select` = which PCRs are included
   - `pcr_digest` = composite hash of selected PCRs

2. **PCR Composite**: The actual PCR values
3. **Signature**: RSA or ECC signature over the TPMS_ATTEST structure

### Quote Flow

```
Verifier sends:  nonce, PCR selection
                      |
                      v
TPM creates:     TPMS_ATTEST{nonce, pcr_digest, clock}
                      |
                      v
TPM signs:       Signature = Sign_AIK(Hash(TPMS_ATTEST))
                      |
                      v
Attester returns: Quote = {attest, pcr_values, signature}
```

### What a Quote Proves

| Property | How it's proven |
|----------|----------------|
| **Identity** | AIK is bound to EK, EK is bound to TPM manufacturer |
| **Integrity** | PCR digest is signed; tampering invalidates signature |
| **Freshness** | Nonce is in the signed structure; old quotes won't match |
| **State** | PCR values represent boot chain measurements |
| **Time** | Clock info from TPM internal timer |

---

## PCR Composite Digest

### What is the Composite Digest?

Instead of quoting every PCR individually (which would be large), the TPM
computes a single **composite hash**:

```
Composite = Hash(
    Hash(PCR[sel_0] || PCR[sel_1] || ... || PCR[sel_n])
)
```

Only this composite digest goes into the signed `TPMS_ATTEST.pcr_digest` field.

### Why Composite?

1. **Efficiency**: Only 32 bytes are signed, regardless of PCR count
2. **Atomicity**: All PCRs or none — cannot selectively tamper
3. **Compatibility**: Works with any number of selected PCRs

### Verification

The verifier:
1. Receives the Quote containing both `pcr_digest` (composite) and individual PCR values
2. Recomputes the composite hash from the individual PCR values
3. Checks that the recomputed hash matches `pcr_digest`
4. Checks that `pcr_digest` was signed by the AIK

---

## Event Log Replay

### Purpose

PCR values are just hashes — they don't tell you **what** was measured, only
the **result**. The event log fills this gap.

### Event Log Structure

Each event records:
```
Event {
    PCRIndex  : uint32    — which PCR was extended
    EventType : uint32    — type of event
    Digest    : [32]byte  — hash of the measured data
    EventSize : uint32    — length of event data
    Event     : variable  — the measured data itself
}
```

### Replay Algorithm

```
for each Event in EventLog:
    expected_pcr[Event.PCRIndex] =
        Hash(expected_pcr[Event.PCRIndex] || Event.Digest)

    if expected_pcr == quote_pcr_values:
        event_was_included = true
    else:
        event_log_is_inconsistent = false
```

### What Event Log Replay Proves

1. **Completeness**: All events are present (missing events would change PCR)
2. **Correctness**: Events match the quoted PCR values
3. **Interpretability**: The verifier can inspect what software was measured

### Common Event Types (TCG)

| Event Type | Value | Description |
|-----------|-------|-------------|
| EV_PREBOOT_CERT | 0x00000000 | Pre-boot certificates |
| EV_POST_CODE | 0x00000001 | Diagnostic post codes |
| EV_EFI_ACTION | 0x00000005 | UEFI actions/strings |
| EV_SEPARATOR | 0x00000004 | Firmware/OS boundary |
| EV_EFI_BOOT_SERVICES_APPLICATION | 0x80000003 | Boot manager app |
| EV_IPL | 0x0000000D | Initial program load |
| EV_IPL_PARTITION_DATA | 0x0000000E | Partition data |

---

## Nonce: Providing Freshness

### Why Nonce?

Without a nonce, an attacker could:

1. Record a valid Quote when the device is in a trusted state
2. Compromise the device (install malware, modify firmware)
3. Replay the old Quote to the verifier — verifier sees "TRUSTED"

The **nonce** breaks this replay attack.

### How Nonce Works

```
Verifier generates: nonce = random_32_bytes()
Verifier sends:     Challenge { nonce, pcr_selection }
Attester:           quote = TPM2_Quote(pcr_selection, extra_data=nonce)
Verifier checks:    quote.extra_data == nonce ?
```

The nonce is placed in `extra_data` of the `TPMS_ATTEST` structure, which is
signed by the TPM. An attacker cannot create a valid Quote with a new nonce
without access to the AIK private key (inside the TPM).

### Nonce Properties

- **Uniqueness**: Each challenge gets a fresh nonce
- **Unpredictability**: Attacker cannot guess future nonces
- **Inclusion in signature**: Nonce binding via TPM signature
- **Size**: Typically 32 bytes (256 bits) — enough for birthday-bound security

---

## AIK vs Attestation Key (AK)

### Terminology

In TPM 2.0, the terms are used somewhat interchangeably:

| Term | Full Name | Context |
|------|-----------|---------|
| **AIK** | Attestation Identity Key | TPM 1.2 terminology, Privacy CA model |
| **AK** | Attestation Key | Generic term in TPM 2.0 |
| **Restricted Signing Key** | — | A key that can only sign TPM-generated data |

### AIK (TPM 1.2 Style)

- Created under the SRK hierarchy
- Certified by a Privacy CA
- Cannot be exported from the TPM
- Used exclusively for signing TPM2_Quote outputs

### AK (TPM 2.0 Style)

- Any signing key with `restricted` and `sign` attributes
- Can be used for Quote, Certify, GetTime, etc.
- Can be anonymous (DAA) or identifiable (Privacy CA)

### Key Types for Attestation

| Key Type | Privacy | Identity | Use |
|----------|---------|----------|-----|
| **EK** | None (unique per device) | Full device identity | Decrypt AIK credentials |
| **AIK (Privacy CA)** | Partial (CA knows identity) | Per-device (via CA) | Sign Quotes |
| **DAA Key** | Full (anonymous) | Per-session/group | Anonymous attestation |
| **AK (Activated Credential)** | Configurable | Configurable | Flexible attestation |

---

## Endorsement Key & Hierarchy

### Key Hierarchy

```
Manufacturer CA (off-TPM)
        |
        v
Endorsement Key (EK) — unique RSA key, burned at manufacture
        |
        v
Storage Root Key (SRK) — platform owner's root key
        |
        v
Attestation Identity Key (AIK) — child of SRK, used for attestation
```

### Endorsement Key (EK)

- **Unique**: One per TPM, never changes
- **Manufacturer provisioned**: Burnt into TPM at fabrication
- **Certificate**: Signed by manufacturer CA (EK Certificate)
- **Usage**: Only for decrypting AIK credentials (not for signing)
- **Privacy risk**: Using EK directly links all quotes to one device

### Storage Root Key (SRK)

- Created during TPM ownership (`TPM2_TakeOwnership`)
- Root of the **Storage Hierarchy** — all user keys under it
- Regeneratable (with the same seed)
- Persists across reboots

### Why the Hierarchy Matters

Without the hierarchy, any key could sign anything. The hierarchy ensures:

1. **AIK is bound to SRK**: Proves the AIK belongs to a TPM that was properly owned
2. **SRK seed is TPM-internal**: Cannot be extracted
3. **Chain of provenance**: AIK -> SRK -> TPM platform -> Manufacturer

---

## Signature Verification

### What Gets Signed?

The TPM signs the hash of `TPMS_ATTEST`:

```
attest_hash = Hash(TPMS_ATTEST)
signature = RSA_Sign(AIK_private, attest_hash)
```

### Verification Steps

```
1. Recompute attest_hash from TPMS_ATTEST in the Quote
2. Decrypt signature with AIK public key: decrypted = RSA_Verify(AIK_pub, signature)
3. Check: decrypted == PKCS#1_v1.5_padding(attest_hash)
4. Check: TPMS_ATTEST.magic == TPM_GENERATED (0xFF544347)
5. Check: TPMS_ATTEST.type == TPM_ST_ATTEST_QUOTE (0x8018)
6. Check: TPMS_ATTEST.extra_data == challenge.nonce
7. Check: recomputed_pcr_composite == TPMS_ATTEST.pcr_digest
```

### Signature Algorithm Considerations

| Algorithm | Key Size | Signature Size | Security |
|-----------|----------|----------------|----------|
| RSA 2048 + SHA-256 | 256 bytes | 256 bytes | ~112 bits |
| ECC P-256 + SHA-256 | 32 bytes (pub) | 64 bytes | ~128 bits |
| RSA 3072 + SHA-384 | 384 bytes | 384 bytes | ~128 bits |

---

## Attacker Model

### Attacker Capabilities (Assumed)

| Capability | Mitigation |
|-----------|-----------|
| Full control of device OS | TPM is tamper-resistant; PCR values cannot be faked |
| Modify firmware/OS | PCR values change, detected by verifier |
| Record and replay Quotes | Nonce prevents replay |
| Extract TPM keys via side channel | TPM is designed to resist extraction |
| Impersonate a device | EK certificate prevents impersonation |
| Man-in-the-middle | Nonce binding + TLS |

### Attacker Limitations

- Cannot extract EK/AIK private keys from TPM (tamper-resistant)
- Cannot predict verifier nonces (cryptographic randomness)
- Cannot forge TPM signatures without AIK private key
- Cannot reverse PCR extends to find original measurements

### What Attestation Does NOT Protect Against

- **Hardware keyloggers**: Physical attacks on the bus
- **TOCTOU after boot**: Runtime compromise after attestation but before action
- **Verifier compromise**: Attacker controls the verifier
- **Boot-time exploits before first measurement**: Very early boot code
- **DMA attacks**: Compromised peripherals reading memory

---

## Common Attestation Topologies

### 1. Direct Attestation

```
Device  <---challenge/response--->  Verifier
```

- Simplest model
- Verifier manages all policies
- Good for: small fleets, lab environments

### 2. Verifier + Relying Party

```
Device  --->  Verifier  --->  Relying Party
                |
                v
          Policy Store
```

- Separation of verification and consumption
- Relying party trusts verifier's attestation result
- Good for: production, multiple relying parties

### 3. Privacy CA Topology

```
Device  --->  Privacy CA  (AIK certification)
  |                |
  v                v
Device  <---challenge/response--->  Verifier
```

- Privacy CA certifies AIK keys off-path
- Verifier trusts Privacy CA for AIK validity
- Good for: privacy-sensitive deployments

### 4. DAA Topology

```
Device  --->  DAA Issuer  (anonymous credential)
  |                |
  v                v
Device  <---challenge/response--->  Verifier + Revocation List
```

- No entity can link quotes to devices
- Requires DAA-capable TPM
- Good for: anonymous attestation, IoT privacy

---

## Summary

| Concept | One-line Summary |
|---------|-----------------|
| **Attestation** | Proving platform state to a remote verifier |
| **PCR** | Hash-extended register capturing boot measurements |
| **Quote** | TPM-signed PCR composite + nonce + metadata |
| **Composite Digest** | Single hash representing all quoted PCRs |
| **Event Log** | Ordered list of what was measured and when |
| **Nonce** | Random value preventing quote replay |
| **AIK** | Attestation Identity Key — signing key for Quotes |
| **EK** | Endorsement Key — unique TPM identity, certified by manufacturer |
| **Privacy CA** | Certifies AIK as belonging to a genuine TPM |
| **DAA** | Anonymous attestation — unlinkable to device identity |
| **Freshness** | Nonce or timestamp guarantees current state, not past |
| **Chain of Trust** | Manufacturer CA -> EK -> AIK -> Quote -> PCR -> Event Log |
