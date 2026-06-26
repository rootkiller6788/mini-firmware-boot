# mini-remote-attestation — Remote Attestation Deep Dive

> 参考 TPM 2.0 Spec Part 1 (Section 16), TCG Trusted Attestation Protocol (TAP),  
> IETF RATS Architecture (RFC 9334), NIST SP 800-155

## Table of Contents

1. [Overview](#overview)
2. [Background: Trust & Attestation](#background-trust--attestation)
3. [Challenge-Response Protocol](#challenge-response-protocol)
4. [TPM Quote Deep Dive](#tpm-quote-deep-dive)
5. [Event Log Replay](#event-log-replay)
6. [Privacy CA Architecture](#privacy-ca-architecture)
7. [Direct Anonymous Attestation (DAA)](#direct-anonymous-attestation-daa)
8. [Security Properties](#security-properties)
9. [Implementation Walkthrough](#implementation-walkthrough)
10. [References](#references)

---

## Overview

Remote attestation is the process by which a **Verifier** (remote party) cryptographically
checks the software state of an **Attester** (device with TPM). The goal is to answer:

> *"Is this device running the expected firmware and software?"*

This module (`mini-boot-attestation`) implements the core building blocks:
TPM2_Quote creation, AIK-based signing, challenge-response protocol, PCR event log
replay, and the Privacy CA trust model.

### Key Terms

| Term | Definition |
|------|-----------|
| **Attester** | Device producing attestation evidence (contains TPM) |
| **Verifier** | Remote party that checks evidence |
| **Relying Party** | Consumer of the attestation result |
| **Endorsement Key (EK)** | Unique RSA key burned into TPM by manufacturer |
| **Attestation Identity Key (AIK)** | Alias key for signing Quotes (privacy-preserving) |
| **Platform Configuration Register (PCR)** | TPM register holding hash-extended measurements |
| **TPM2_Quote** | Signed structure containing PCR composite + nonce |
| **Event Log** | Replayable log of all PCR extend operations |
| **Privacy CA** | Trusted third-party that certifies AIKs |
| **Nonce** | Random challenge value for freshness/liveness |

---

## Background: Trust & Attestation

### Root of Trust

The attestation trust chain starts at the **TPM manufacturer**:

```
Manufacturer CA
    |
    v  (signs)
Endorsement Key Certificate (EK Cert)
    |
    v  (Privacy CA verifies EK cert)
Attestation Identity Key Credential (AIK Cred)
    |
    v  (AIK signs)
TPM2_Quote (attestation evidence)
```

Each layer must be verified to establish trust in the final attestation result.

### Local vs Remote Attestation

| | Local Attestation | Remote Attestation |
|---|---|---|
| **Scope** | Within same platform | Across network |
| **Protocol** | TPM-internal | Challenge-response |
| **Freshness** | TPM clock/tick | Verifier nonce |
| **Privacy** | N/A | AIK / DAA |
| **Use Case** | Boot integrity | Fleet health, Zero Trust |

### PCR Banks

The TPM maintains multiple PCR banks (SHA-1, SHA-256). PCR values are extended
(not set) using:

```
PCR_new = Hash(PCR_old || measured_value)
```

This chain-of-hash property ensures that the PCR value captures the *entire history*
of measurements in order.

---

## Challenge-Response Protocol

The fundamental attestation protocol has three phases:

### Phase 1: Challenge

```
Verifier -> Attester:
    {
        nonce: 32-byte random,
        pcr_selection: bitmask of PCR indices to include,
        extra_data: optional context
    }
```

- **Nonce** provides freshness/replay protection. The verifier checks that the
  response quotes this exact nonce.
- **PCR Selection** tells the attester which PCRs to include in the Quote.

### Phase 2: Quote Generation (Attester)

The attester:
1. Calls TPM2_Quote with the nonce and PCR selection
2. TPM creates a `TPMS_ATTEST` structure (type=TPM_ST_ATTEST_QUOTE)
3. TPM signs the structure with the AIK
4. Returns Quote + PCR composite + signature

### Phase 3: Verification

The verifier:
1. Checks nonce in Quote matches challenge nonce
2. Verifies Quote signature using AIK public key
3. Validates AIK certificate chain (AIK -> Privacy CA -> Manufacturer CA)
4. Compares PCR composite with expected ("golden") PCR values
5. Replays event log and compares with PCR composite
6. Checks firmware version, clock info, attributes

### Simplified Sequence Diagram

```
Attester                          Verifier
   |                                  |
   |  <--- [1] Challenge ------------  |
   |       (nonce, PCR selection)     |
   |                                  |
   |  [2] TPM2_Quote(nonce, PCRs)     |
   |  [3] Sign quote with AIK         |
   |  [4] Gather event log            |
   |                                  |
   |  --- [5] AttestResponse ------->  |
   |       (Quote, EventLog, AIK cert)|
   |                                  |
   |                            [6] Verify signature
   |                            [7] Verify AIK chain
   |                            [8] Compare PCRs
   |                            [9] Replay event log
   |                            [10] Policy decision
   |                                  |
   |  <--- [11] Result ------------   |
   |       (TRUSTED / UNTRUSTED)     |
   |                                  |
```

---

## TPM Quote Deep Dive

### TPMS_ATTEST Structure

The TPM creates a structured attestation blob:

```
offset  size    field
------  ----    -----
0       4       magic (0xFF544347 = "TPM GENERATED")
4       2       type (0x8018 = TPM_ST_ATTEST_QUOTE)
6       32      qualified_signer (hash of AIK name)
38      32      extra_data (nonce placed here)
70      16      clock_info (TPMS_CLOCK_INFO)
86      8       firmware_version (uint64)
94      4+      pcr_select (TPMS_PCR_SELECTION)
98+     32      pcr_digest (composite hash of selected PCRs)
```

The entire `TPMS_ATTEST` is hashed and signed by the TPM using the AIK.

### PCR Composite Hash

Given PCRs 0-7, the composite digest is:

```
composite = Hash(
    Hash(PCR[0] || PCR[1] || ... || PCR[7])
)
```

This single hash represents the entire selected PCR state.

### TPM2_Quote Command Flow

```
Host sends:
    TPM2_Quote(
        signHandle: AIK handle,
        qualifyingData: nonce,
        signingScheme: TPM_ALG_RSASSA,
        PCRselect: TPML_PCR_SELECTION
    )

TPM responds:
    {
        quoted: TPMS_ATTEST (signed attestation structure),
        signature: TPMT_SIGNATURE (RSA signature over quoted)
    }
```

---

## Event Log Replay

### Purpose

PCR values alone don't tell you *what was measured*, only the final hash. The
event log provides the sequence of measurements that extend each PCR.

### Event Log Structure (TCG Format)

```
Event {
    PCRIndex: uint32,
    EventType: uint32,
    Digest: TPMU_HA (hash of measured data),
    EventSize: uint32,
    Event: variable (the measured data)
}
```

### Replay Algorithm

```
for each Event in event_log:
    expected_pcr[Event.PCRIndex] =
        Hash(expected_pcr[Event.PCRIndex] || Event.Digest)
    assert expected_pcr[Event.PCRIndex] == quote.PCR_composite[Event.PCRIndex]
```

The replay proves that:
1. The quoted PCR values match the event log.
2. The event log is complete (no missing measurements).
3. The measured software is the expected software (by inspecting event data).

### Verified Boot Integration

In a typical UEFI Secure Boot / Verified Boot setup:

| PCR | Purpose |
|-----|---------|
| 0 | Platform firmware / BIOS |
| 1 | Platform firmware configuration |
| 2 | External / option ROM code |
| 3 | Option ROM configuration |
| 4 | Boot manager code (MBR) |
| 5 | Boot manager configuration |
| 6 | Platform manufacturer specific |
| 7 | Secure Boot policy |

---

## Privacy CA Architecture

### Problem

Using the **Endorsement Key (EK)** directly for attestation would allow linking
all attestation quotes to a single device identity — a privacy violation.

### Solution: Attestation Identity Key (AIK)

The AIK is a secondary RSA key that substitutes for the EK in signing quotes.
But how does the verifier trust the AIK?

### Privacy CA Protocol

```
1. Device generates AIK (inside TPM, under SRK)
2. Device sends AIK public + EK certificate to Privacy CA
3. Privacy CA:
   a. Validates EK certificate against manufacturer CA
   b. Issues AIK credential:
      - Certifies AIK public key
      - Encrypts credential to EK (so only genuine TPM can decrypt)
   c. Returns encrypted AIK credential to device
4. Device activates credential: TPM decrypts using EK
5. Device now has a Privacy-CA-signed AIK credential
6. During attestation, device provides AIK credential to verifier
```

### MakeCredential / ActivateCredential

```
Privacy CA:
    credential = { AIK_pub, expiry, CA_signature }
    encrypted_credential = RSA_OAEP_Encrypt(EK_pub, credential)

Device (TPM):
    TPM2_ActivateCredential(
        activateHandle: AIK,
        keyHandle: EK,
        credentialBlob: encrypted_credential,
        secret: seed
    ) -> credentialBlob (decrypted AIK credential)
```

### Trust Model

```
Verifier trusts:
    1. TPM Manufacturer CA (pre-installed certificate)
    2. Privacy CA (agreed-upon third party)
    3. EK Certificate chain: Manufacturer CA -> EK
    4. AIK Credential chain: Privacy CA -> AIK
```

---

## Direct Anonymous Attestation (DAA)

### Motivation

Privacy CA model has a limitation: the Privacy CA can link AIK to EK. If the
Privacy CA is compromised or colludes, device privacy is lost.

### DAA Solution

DAA (introduced in TPM 2.0, based on EPID / ECC) provides:

1. **Anonymity**: Verifier cannot identify the specific device
2. **Unlinkability**: Different attestations from the same device cannot be linked
3. **Revocation**: Rogue devices can be identified via revocation lists

### DAA Join/Attest Protocol

```
Join:
    Device -> Issuer: Join request with EK-based key
    Issuer -> Device: DAA credential (anonymous)

Attest:
    Device -> Verifier: TPM2_Quote(basename=verifier_id)
    Verifier: Checks DAA signature, revocation list
```

### Comparison

| Property | Privacy CA | DAA |
|----------|-----------|-----|
| **Device Privacy** | Depends on CA trustworthiness | Strong (cryptographic) |
| **Revocation** | AIK credential expiry | Private-key revocation |
| **Performance** | RSA signing (fast) | ECC-based (faster) |
| **Complexity** | Lower | Higher |
| **TPM 2.0 Support** | Universal | Requires DAA-capable TPM |

---

## Security Properties

### Freshness

The Verifier's **nonce** is placed in `extra_data` of the attestation structure.
This prevents replay attacks. Without a nonce, an attacker could replay a past
Quote after the device has been compromised.

### Liveness

The nonce also proves *liveness*: the device was alive at the time the nonce was
issued (assuming nonce generation and Quote production happen within a small time
window).

### Binding

- **Nonce binding**: Quote signature covers the nonce, binding the Quote to the
  specific challenge
- **PCR binding**: Quote signature covers PCR composite, binding state to identity
- **AIK binding**: AIK credential binds the AIK to a genuine TPM via the EK

### Integrity

- PCR extend chain-of-hash ensures measurements are ordered and complete
- Quote signature prevents tampering with PCR values
- Event log replay detects missing or forged event entries

### Attacks Mitigated

| Attack | Defense |
|--------|---------|
| **Replay** | Verifier nonce, timestamps |
| **Masquerading** | EK certificate chain |
| **PCR Tampering** | Quote signature, event log replay |
| **Key Substitution** | AIK credential binding to EK |
| **Time-of-check/time-of-use** | Freshness check, clock monitoring |
| **Man-in-the-Middle** | TLS + attestation binding |

---

## Implementation Walkthrough

### File Map

```
include/
    tpm_quote.h         — TPM2_Quote data structures and functions
    aik_identity.h      — EK, AIK, Privacy CA credential types
    attest_protocol.h   — Challenge-response protocol
    verifier_service.h  — Fleet verifier service
    rats.h              — IETF RATS concepts

src/
    tpm_quote.c         — Quote creation/signing/verification
    aik_identity.c      — Key creation, credential make/activate
    attest_protocol.c   — Challenge/response creation, verification, policy
    verifier_service.c  — Device database, policy management
    rats.c              — Evidence generation, appraisal, relying party

examples/
    tpm_quote_demo.c        — Quote creation, signing, event log replay
    attest_demo.c           — Full challenge-response attestation
    privacy_ca_demo.c       — Privacy CA protocol simulation
```

### Key Design Decisions

1. **All structures are plain C** — no external dependencies beyond libc
2. **Simulated cryptography** — deterministic pseudo-SHA256 and pseudo-RSA
   for educational/demo purposes
3. **Modular headers** — each subsystem has a clear interface
4. **C99 + snake_case + PascalCase** — consistent coding conventions

---

## References

- **TPM 2.0 Specification Part 1**: Architecture, Section 16 — Attestation
- **TPM 2.0 Specification Part 3**: Commands, TPM2_Quote
- **TCG Trusted Attestation Protocol (TAP)**: TCG Published Spec
- **IETF RATS Architecture (RFC 9334)**: Remote ATtestation procedureS
- **NIST SP 800-155**: BIOS Integrity Measurement Guidelines
- **TCG PC Client Platform Firmware Profile**: PCR usage specification
- **Keylime**: Linux IMA-based remote attestation
- **OpenAttestation**: Intel's attestation SDK (discontinued, concepts live on)
- **Brickell, Camenisch, Chen**: "Direct Anonymous Attestation" (ACM CCS 2004)
