# Course Alignment — mini-boot-attestation

> Mapping module concepts to standard specifications, academic courses, and industry projects.

## Table of Contents

1. [TPM 2.0 Specification Mapping](#tpm-20-specification-mapping)
2. [TCG Trusted Attestation Protocol (TAP)](#tcg-trusted-attestation-protocol-tap)
3. [IETF RATS (RFC 9334) Mapping](#ietf-rats-rfc-9334-mapping)
4. [Keylime Project Comparison](#keylime-project-comparison)
5. [Academic Course Mapping](#academic-course-mapping)
6. [Industry Certifications](#industry-certifications)
7. [Standards-to-Code Cross-Reference](#standards-to-code-cross-reference)

---

## TPM 2.0 Specification Mapping

### Part 1: Architecture

| Spec Section | Concept | Module Implementation |
|-------------|---------|----------------------|
| 10.0 — Key Hierarchies | EK, SRK, AIK hierarchy | `tpm_quote.h` — `TPMKeyHierarchy` enum |
| 16.1 — Attestation Overview | TPM Attestation of platform state | `tpm_quote_create` — builds attest structure |
| 16.2 — TPM2_Quote | Quote PCR values | `tpm_quote.h` — `TPMQuote`, `TPMAttest` |
| 16.3 — TPM2_Certify | Certify a key/object | `tpm_certify` function |
| 16.4 — TPM2_GetTime | Attest to TPM time | (clock_info in TPMAttest) |
| 28.1 — Remote Attestation | Protocol overview | `attest_protocol.c` — challenge/response |
| 28.2 — Privacy Considerations | AIK, Privacy CA | `aik_identity.h` — AIK credential flow |

### Part 3: Commands

| Spec Section | Command | Module Implementation |
|-------------|---------|----------------------|
| 12.3 | TPM2_CreatePrimary (EK) | `tpm_create_ek` |
| 12.4 | TPM2_Create (AIK) | `tpm_create_aik` |
| 18.4 | TPM2_Quote | `tpm_quote_create` + `tpm_quote_sign` |
| 18.3 | TPM2_Certify | `tpm_certify` |
| 18.9 | TPM2_MakeCredential | `tpm_make_credential` |
| 18.10 | TPM2_ActivateCredential | `tpm_activate_credential` |

### Part 2: Structures

| Spec Section | Structure | Module Definition |
|-------------|-----------|-------------------|
| 10.12.4 | TPMS_ATTEST | `TPMAttest` struct in `tpm_quote.h` |
| 10.12.5 | TPMS_QUOTE_INFO | Embedded in `TPMAttest` |
| 10.12.7 | TPMS_CERTIFY_INFO | For certify outputs |
| 10.11 | TPMS_PCR_SELECTION | `TPMPcrSelection` |
| 10.9 | TPML_PCR_SELECTION | `TPMPcrComposite.pcr_selections[]` |
| 10.10 | TPML_DIGEST | `TPMPcrComposite.pcr_digests[]` |

### TPM Constants Used

| Spec Constant | Value | Module Constant |
|--------------|-------|-----------------|
| TPM_GENERATED_VALUE | 0xFF544347 | `TPM_GENERATED_VALUE` |
| TPM_ST_ATTEST_QUOTE | 0x8018 | `TPM_ST_ATTEST_QUOTE` |
| TPM_ALG_SHA256 | 0x000B | `TPM_ALG_SHA256` |
| TPM_ALG_RSASSA | 0x0014 | `TPM_ALG_RSASSA` |
| TPM_ALG_NULL | 0x0010 | `TPM_ALG_NULL` |

---

## TCG Trusted Attestation Protocol (TAP)

TCG TAP is a higher-level protocol that defines how attestation messages are
exchanged between components.

### TAP Roles vs Module Roles

| TAP Role | Module Equivalent |
|----------|-------------------|
| **Attester** | Device with TPM, runs `attest_response_create` |
| **Verifier** | `AttestVerifier` + `verifier_service.c` |
| **Relying Party** | `RATSRelyingPartyResult` interface |
| **Endorser** | `EKCertificate` (manufacturer CA) |
| **Reference Value Provider** | `AttestDB.expected_pcr_values` |

### TAP Message Types vs Module Messages

| TAP Message | Module Implementation |
|-------------|----------------------|
| **Attestation Challenge** | `AttestChallenge` (nonce + PCR selection) |
| **Attestation Evidence** | `AttestResponse` (Quote + event log + AIK cert) |
| **Appraisal Policy** | `AttestPolicyRule` + `AttestVerifier.known_good_pcr_values` |
| **Attestation Result** | `AttestVerdict` (ALLOW/DENY/UNKNOWN) |

### TAP Freshness Models Supported

| TAP Freshness | Module Support |
|--------------|----------------|
| **Nonce-based** | `AttestChallenge.nonce` — primary freshness |
| **Timestamp-based** | `AttestChallenge.timestamp` — secondary |
| **Clock-based** | `TPMAttest.clock_info` — TPM internal clock |

---

## IETF RATS (RFC 9334) Mapping

### RFC 9334 Architecture vs Module

RFC 9334 defines the Remote ATtestation procedureS architecture. The module
maps concepts but is a **conceptual simulation**, not a protocol implementation.

| RFC 9334 Concept | Module Concept | File |
|-----------------|---------------|------|
| **Evidence** | `RATSEvidence` | `rats.h` |
| **Reference Values** | `RATSReferenceValues` | `rats.h` |
| **Endorsements** | `EKCertificate` | `aik_identity.h` |
| **Attestation Results** | `RATSAppraisalResult` + `RATSRelyingPartyResult` | `rats.h` |
| **Appraisal Policy for Evidence** | `RATSAppraisalPolicy` | `rats.h` |
| **Verifier Role** | `RATSVerifier` | `rats.h` |
| **Relying Party Role** | `RATSRelyingPartyResult` | `rats.h` |

### RATS Topologies

RFC 9334 defines three topologies:

| Topology | Module Implementation |
|----------|----------------------|
| **Passport Model** | Not implemented — evidence bundled by attester |
| **Background-Check Model** | Partially — verifier queries reference values |
| **Direct Model** | `attest_protocol.c` — challenge-response directly |

### RATS Claim-Based Evidence

The `RATSEvidence.claims[]` array represents claim-based evidence as
defined in RFC 9334 Section 7. Pre-populated claims:

| Claim Key | Description |
|-----------|-------------|
| `firmware_version` | TPM Quote firmware version |
| `tpms_generated_magic` | TPMS_ATTEST magic number |
| `attestation_type` | String "TPM2_Quote" |

### IETF draft-ietf-rats-msg-wrap

The conceptual message wrapping model for RATS interactions:

```
Conceptual Message (draft-ietf-rats-msg-wrap):
    Attester -> Verifier: Evidence
    Verifier -> RP: AttestationResult

Module implementation:
    rats_generate_evidence()       -> RATSEvidence
    rats_appraise_evidence()       -> RATSAppraisalResult
    rats_relying_party_interface() -> RATSRelyingPartyResult
```

---

## Keylime Project Comparison

Keylime is the most widely deployed open-source Linux remote attestation
system. The module implements a simplified educational model.

### Feature Comparison

| Feature | Keylime | mini-boot-attestation |
|---------|---------|-----------------------|
| **Language** | Python 3 | C (C99) |
| **TPM Interface** | tpm2-tools / tpm2-pytss | Simulated |
| **IMA (Integrity Measurement Architecture)** | Full support | Simulated event log |
| **REST API** | Full REST with TLS | C library API |
| **Database Backend** | SQLite / MySQL / PostgreSQL | In-memory array |
| **Agent Registration** | mTLS + UUID + EK | Direct function call |
| **Revocation Framework** | Notary-based | Direct verifier |
| **Multi-Tenant** | Yes | Single-tenant |
| **Cloud Orchestrator Integration** | K8s, OpenStack | N/A |
| **DAA/ECDAA Support** | Planned | Conceptual only |

### Architectural Equivalence

```
Keylime:
    Tenant -> Registrar -> Agent -> Verifier -> Notary(CA)

mini-boot-attestation:
    Admin -> attest_service_register_device()
    Attester -> attest_response_create()
    Verifier -> attest_verify() + attest_service_verify_quote()
    CA -> tpm_aik_certify()
```

---

## Academic Course Mapping

### "Computer Security" / "System Security" Course

| Topic | Module Coverage |
|-------|----------------|
| **Secure Boot** | PCR values 0-7 represent boot chain |
| **Root of Trust** | EK -> AIK trust chain |
| **Trusted Computing** | TPM 2.0 concepts, attestation |
| **Chain of Trust** | PCR extend semantics, event log |
| **Remote Attestation** | Challenge-response protocol |
| **Privacy-preserving Crypto** | Privacy CA, DAA |
| **Integrity Measurement** | PCR measurement, event log replay |

### "Cryptographic Protocols" Course

| Topic | Module Coverage |
|-------|----------------|
| **Challenge-Response** | Nonce-based freshness |
| **Digital Signatures** | RSA signing of Quote |
| **Certificate Chains** | EK -> Privacy CA -> AIK |
| **Hash Chains** | PCR extend: H(old || new) |
| **Zero-Knowledge** | DAA concepts |
| **Key Hierarchy** | EK / SRK / AIK |

### "Embedded Systems Security" Course

| Topic | Module Coverage |
|-------|----------------|
| **Firmware Integrity** | Firmware version in Quote |
| **Measured Boot** | PCR 0-7 example |
| **TPM on Embedded** | C99, no external deps |
| **IoT Attestation** | Fleet management concepts |
| **Anti-Rollback** | Version checking in policy |

---

## Industry Certifications

### CompTIA Security+ (SY0-601/701)

| Domain | Related Module Concepts |
|--------|------------------------|
| 2.0 Architecture & Design | Trusted computing, TPM |
| 3.0 Implementation | Secure boot, measured boot |
| 5.0 Governance, Risk, Compliance | Attestation as compliance tool |

### CISSP (Certified Information Systems Security Professional)

| Domain | Related Module Concepts |
|--------|------------------------|
| Security Architecture | TPM, root of trust, chain of trust |
| Security Assessment | Attestation, continuous monitoring |
| Software Security | Firmware integrity, measured boot |

### (ISC)² CCSP

| Domain | Related Module Concepts |
|--------|------------------------|
| Cloud Platform Security | Remote attestation for cloud workloads |
| Compliance | Attestation-based audit trails |

---

## Standards-to-Code Cross-Reference

### TPM 2.0 Part 1 -> Code

| Part 1 Reference | Code Location |
|-----------------|---------------|
| Section 16.1 "Attestation of Platform State" | `tpm_quote.c:120` `tpm_quote_create()` |
| Section 16.2 "TPM2_Quote" | `tpm_quote.c:95` `tpm_quote_create()` |
| Section 28.1 "Remote Attestation" | `attest_protocol.c:1` entire file |
| Section 28.2 "Privacy" | `aik_identity.c:80` `tpm_aik_certify()` |

### TCG TAP -> Code

| TAP Concept | Code Location |
|-------------|---------------|
| Challenge creation | `attest_protocol.c:15` `attest_challenge_create()` |
| Response assembly | `attest_protocol.c:55` `attest_response_create()` |
| Evidence verification | `attest_protocol.c:85` `attest_verify()` |
| Policy evaluation | `attest_protocol.c:185` `attest_verify_policy()` |

### IETF RATS RFC 9334 -> Code

| RFC 9334 Section | Code Location |
|-----------------|---------------|
| Section 7 "Evidence" | `rats.c:15` `rats_generate_evidence()` |
| Section 8 "Appraisal" | `rats.c:75` `rats_appraise_evidence()` |
| Section 9 "Attestation Results" | `rats.c:155` `rats_relying_party_interface()` |

### Keylime -> Code

| Keylime Component | Code Location |
|-------------------|---------------|
| Verifier | `verifier_service.c` — `AttestDB` + verification |
| Agent | `attest_protocol.c` — response creation |
| Registrar | `verifier_service.c:38` `attest_service_register_device()` |
| IMA Allowlist | `verifier_service.c:285` `attest_service_update_pcr_expected()` |

---

## Relationship to Other mini-everything Modules

```
mini-hardware-physical
    (TPM as physical chip — PCR regs, NV storage, RNG)
            |
            v
mini-boot-attestation <-- (YOU ARE HERE)
    (Attestation: Quote, AIK, challenge-response)
            |
            v
mini-boot-sbrm (future)
    (Secure Boot + Runtime Measurement)
```

| Upstream Module | Contribution |
|-----------------|-------------|
| `mini-hardware-physical` | TPM chip model with PCR registers |

| Downstream Concepts | This Module's Foundation |
|---------------------|-------------------------|
| Secure Boot | PCR 0-7 extend chain |
| Measured Boot | Event log replay |
| IMA (Integrity Measurement Architecture) | PCR 8-15 for runtime files |
| DRTM (Dynamic Root of Trust) | PCR 17-22 trust model |
