# mini-attestation-service — Attestation Service & Fleet Management

> 参考 TCG Infrastructure WG — Attestation Trust Model,  
> IETF RATS Conceptual Messages (draft-ietf-rats-msg-wrap),  
> Keylime Architecture, OpenAttestation SDK

## Table of Contents

1. [Overview](#overview)
2. [Fleet Attestation Architecture](#fleet-attestation-architecture)
3. [Device Database & Registration](#device-database--registration)
4. [PCR Whitelist Management](#pcr-whitelist-management)
5. [Firmware Version Policy](#firmware-version-policy)
6. [Provisioning Workflow](#provisioning-workflow)
7. [Attestation Service Operations](#attestation-service-operations)
8. [Keylime Integration Concepts](#keylime-integration-concepts)
9. [OpenAttestation Comparison](#openattestation-comparison)
10. [Policy Engine Design](#policy-engine-design)
11. [Scalability Considerations](#scalability-considerations)
12. [Security & Operational Guidance](#security--operational-guidance)
13. [API Design](#api-design)
14. [References](#references)

---

## Overview

An **Attestation Service** is the centralized component that manages device
identities, tracks expected configurations, performs continuous attestation
verification, and provides trust decisions to relying parties.

Key responsibilities:
- **Device registration** — enrolling devices with their EK public key hash
- **Policy management** — maintaining PCR whitelists and firmware baselines
- **Quote verification** — validating incoming TPM Quotes
- **Fleet health tracking** — monitoring attestation success/failure rates
- **Result distribution** — providing attestation results to relying parties

---

## Fleet Attestation Architecture

### High-Level Architecture

```
┌─────────────┐     ┌──────────────────┐     ┌──────────────┐
│             │     │                  │     │              │
│  Devices    │────▶│  Attestation     │────▶│  Relying     │
│  (TPM)      │     │  Service         │     │  Parties     │
│             │◀────│  (Verifier)      │◀────│  (Apps)      │
└─────────────┘     └──────────────────┘     └──────────────┘
  Attesters              Verifier + DB         Consumers
```

### Component Roles

| Component | Role | Examples |
|-----------|------|----------|
| **Attester** | Generates TPM Quotes | Servers, laptops, IoT devices |
| **Verifier Service** | Validates Quotes, manages policies | Keylime verifier, OpenAttestation |
| **Policy Store** | Expected PCR values, FW whitelist | Config, database |
| **Relying Party** | Consumes attestation result | Orchestrator (K8s), load balancer, NAC |
| **Privacy CA** | Issues AIK certificates | Optional, for privacy-preserving attestation |
| **Manufacturer CA** | Issues EK certificates | TPM vendor PKI |

### Attestation Flow

```
1. [Scheduled] Verifier sends AttestChallenge to device
2. Device builds TPM Quote + Event Log
3. Device sends AttestResponse
4. Verifier validates:
   a. Nonce matches
   b. AIK certificate chain
   c. Quote signature
   d. PCR values vs expected
   e. Firmware version vs policy
   f. Event log replays correctly
5. Result: TRUSTED / UNTRUSTED / UNKNOWN
6. Verifier updates device state, notifies relying parties
```

---

## Device Database & Registration

### Device Entry Schema

Each device tracked by the attestation service has:

```
AttestDeviceEntry {
    device_id          : unique identifier (hostname, UUID)
    ek_pub_hash        : SHA-256 of EK public modulus
    expected_pcr_values : array of expected PCR digests
    firmware_whitelist : list of approved firmware hashes
    policy_rules       : custom policy rules for this device
    last_attest_time   : timestamp of last successful attestation
    attest_fail_count  : consecutive failures
    last_result        : TRUSTED / UNTRUSTED / UNKNOWN
    registered         : provisioning complete
    ownership_taken    : TPM ownership established
    locked             : device locked (security hold)
}
```

### Registration Workflow

```
1. Administrator obtains device EK public key hash
   (from manufacturer certificate or initial boot)

2. Administrator registers device:
   attest_service_register_device(db, device_id, ek_hash, pcrs, count)

3. Administrator takes TPM ownership:
   attest_service_take_ownership(db, device_id)
   (simulates TPM2_DictionaryAttackLockReset, TPM2_Clear)

4. Administrator sets baseline PCR values:
   attest_service_update_pcr_expected(db, device_id, golden_pcrs, count)

5. Administrator adds firmware whitelist:
   attest_service_add_firmware_whitelist(db, device_id, fw_records, count)

6. Administrator sets policy rules:
   attest_service_update_policy(db, device_id, rules, count)

7. Device is now in TRUSTED state baseline
```

---

## PCR Whitelist Management

### Golden PCR Values

"Golden" PCR values represent the expected state of a correctly booted and
configured device. These are typically captured during provisioning:

```
1. Boot device in known-good environment
2. Measure all PCR values
3. Store as "golden" reference
4. Future attestations compare against golden values
```

### PCR Evolution Over Time

PCR values change when:
- Firmware is updated
- OS kernel is updated
- Bootloader configuration changes

The attestation service must support **PCR update workflows**:

```
1. Operator initiates maintenance window
2. Device locked from relying party decisions
3. Firmware/software update performed
4. Device rebooted, new PCR values measured
5. New golden PCR values recorded
6. Device unlocked
7. Attestation resumes with new baselines
```

### PCR Selection Policy

Not all PCRs need to be verified. Typical selection:

| PCR | Verify? | Reason |
|-----|---------|--------|
| 0-7 | Yes | Boot chain integrity |
| 8-15 | Optional | OS-level measurements (IMA) |
| 16 | Sometimes | Debug PCR |
| 17-22 | Context-dependent | DRTM, locality |
| 23 | Optional | Application-specific |

---

## Firmware Version Policy

### Version Checking

The TPM Quote includes `firmware_version` — a uint64 set during Quote creation.
The verifier checks it against a minimum required version:

```
if quote.firmware_version < min_required:
    result = UNTRUSTED
    detail = "Firmware version too old"
```

### Firmware Whitelist

Beyond version numbers, individual firmware binaries can be whitelisted:

```
AttestFirmwareRecord {
    binary_hash : SHA-256 of firmware binary
    version     : human-readable version string
    release_date: when this firmware was approved
}
```

### Rollback Protection

Firmware rollback attacks are mitigated by:
1. TPM monotonic counters for firmware version
2. Quote includes firmware version signed by TPM
3. Verifier rejects versions older than policy minimum
4. Anti-rollback firmware (TPM-backed) on device side

---

## Provisioning Workflow

### Day-0: Manufacturing

```
1. TPM manufacturer burns EK into TPM
2. Manufacturer signs EK certificate
3. Device ships with EK cert in firmware/NVRAM
```

### Day-1: Initial Enrollment

```
1. Device boots in provisioning network
2. Administrator reads EK public key (Tpm2_ReadPublic)
3. Administrator registers device in Attestation Service
4. TPM ownership taken (Tpm2_TakeOwnership)
5. SRK created, AIK created
6. AIK sent to Privacy CA for certification
7. AIK credential stored on device
8. Initial golden PCR values captured
9. Baseline attestation run — expect TRUSTED
```

### Day-N: Ongoing Operation

```
1. Attestation service periodically challenges device
2. Each attestation result recorded
3. Consecutive failures trigger alert
4. Policy updates pushed as needed
5. Firmware updates go through maintenance workflow
```

---

## Attestation Service Operations

### Periodic Attestation

```
while true:
    for each registered device:
        challenge = create_challenge(nonce, pcr_selection)
        send challenge to device
        response = receive response with timeout
        verdict = attest_verify(response, challenge, verifier)
        update device status
        if verdict == DENY:
            increment fail_count
            if fail_count > threshold:
                alert "device X failed attestation N times"
            lock device from relying parties
        else:
            reset fail_count
        sleep(attestation_interval)
```

### Take Ownership (Simulated)

The `attest_service_take_ownership` function simulates:
- `TPM2_DictionaryAttackLockReset` — resets the TPM's brute-force defense
- `TPM2_Clear` — clears all TPM state, returns to factory state
- Ownership establishment — creates new SRK, sets authorization values

### Device Lockdown

When a device repeatedly fails attestation, the service can:
1. **Lock** the device (`attest_service_lock_device`)
2. Prevent relying parties from trusting it
3. Require manual remediation
4. Generate security incident

---

## Keylime Integration Concepts

### What is Keylime?

[Keylime](https://keylime.dev) is an open-source remote attestation framework
for Linux systems using TPM 2.0 and Linux IMA (Integrity Measurement Architecture).

### Keylime Architecture vs mini-attestation-service

| Feature | Keylime | mini-attestation-service |
|---------|---------|--------------------------|
| **Language** | Python | C (C99) |
| **IMA support** | Yes | Simulated |
| **PCR Banks** | SHA-1 + SHA-256 | SHA-256 only |
| **Database** | SQLite / MySQL | In-memory array |
| **REST API** | Yes | Not implemented |
| **Agent on device** | Python agent | Library calls |
| **Revocation** | Notary model | Direct verifier model |
| **Multi-tenant** | Yes | Single-tenant |

### Keylime Concepts Adapted

- **Verifier**: Equivalent to `AttestVerifier` + `AttestDB`
- **Agent**: Equivalent to attester-side `tpm_quote_create` + `attest_response_create`
- **Registrar**: Equivalent to `attest_service_register_device`
- **Notary (CA)**: Equivalent to `tpm_aik_certify`
- **Allowlist**: Equivalent to `known_good_pcr_values` + `AttestPolicyRule`
- **Excludelist**: Negative equivalent of policy — denied PCR values

---

## OpenAttestation Comparison

### What was OpenAttestation?

Intel's OpenAttestation (OA) was a Java-based SDK for TPM-based remote
attestation. While discontinued, its architectural patterns are widely
referenced.

### Architecture Mapping

| OpenAttestation Concept | mini-attestation-service Equivalent |
|-------------------------|-------------------------------------|
| **Host** (attested entity) | Attester device |
| **Appraisal Service** | `attest_verify` + `attest_verify_policy` |
| **Privacy CA** | `tpm_aik_certify` + `tpm_make_credential` |
| **Trust Policy Store** | `AttestPolicyRule` in `AttestDB` |
| **Host Registration** | `attest_service_register_device` |
| **Integrity Report** | `AttestResponse` |
| **Trust Decision** | `AttestVerdict` + `AttestResult` |
| **SAML Assertion** | Not implemented (future: JWT/RATS) |

---

## Policy Engine Design

### Policy Rules

Each device can have custom rules:

```
AttestPolicyRule {
    pcr_index       : which PCR this rule applies to
    expected_value  : the required digest value
    pcr_mask        : bitmask for multi-PCR rules
    default_action  : ALLOW or DENY
    rule_name       : human-readable label
}
```

### Policy Evaluation

```
evaluate_policy(pcr_composite, rules):
    for each rule in rules:
        expected = rules[i].expected_value
        actual   = pcr_composite.digests[rules[i].pcr_index]
        if expected != actual:
            return rules[i].default_action  # typically DENY
    return ALLOW
```

### Composite Policies

More sophisticated policies can be implemented:

1. **Majority voting**: Require N of M PCRs to match
2. **Weighted PCRs**: Some PCRs more important than others
3. **Conditional rules**: If PCR X matches, skip PCR Y
4. **Time-based**: Different policies during maintenance windows
5. **Location-based**: Different policies per datacenter/region

---

## Scalability Considerations

### Fleet Size

The in-memory `AttestDB` supports `ATTEST_DB_MAX_DEVICES` (256) entries. For
larger fleets, the design would extend to:

1. **Database backend**: SQLite / PostgreSQL for device state
2. **Sharding**: Devices partitioned by hash range
3. **Caching**: Golden PCR values cached in memory
4. **Async verification**: Quote verification offloaded to worker pool

### Attestation Intervals

| Fleet Size | Interval | Quotes/second |
|------------|----------|---------------|
| 100 | 60s | ~1.7 | 
| 1000 | 60s | ~16.7 |
| 10000 | 300s | ~33.3 |
| 100000 | 600s | ~166.7 |

Each quote verification is O(n) in PCR count and typically < 1ms.

---

## Security & Operational Guidance

### Separation of Duties

| Role | Responsibility |
|------|----------------|
| **Security Admin** | Sets attestation policies, approves firmware |
| **Operations** | Manages device lifecycle, maintenance windows |
| **Auditor** | Reviews attestation logs, detects anomalies |
| **TPM Owner** | Holds TPM owner authorization |

### Monitoring

Metric to track:
- **Attestation success rate** per device / fleet
- **Latency** of quote verification
- **Consecutive failures** (early warning of compromise)
- **PCR value changes** between attestations
- **Firmware version distribution** across fleet

### Incident Response

When attestation fails:
1. **Immediate**: Lock device, notify security team
2. **Short-term**: Investigate event log, compare with known-good state
3. **Medium-term**: Remediate device (re-image, replace if hardware compromise)
4. **Long-term**: Update golden measurements if change was authorized

---

## API Design

### Current (C Library API)

```
// Registration
attest_service_init(db, verifier_id)
attest_service_register_device(db, device_id, ek_hash, pcrs, count)
attest_service_take_ownership(db, device_id)

// Verification
attest_service_verify_quote(db, device_id, response, challenge, &result, &verdict)

// Policy Management
attest_service_update_policy(db, device_id, rules, count)
attest_service_update_pcr_expected(db, device_id, pcrs, count)
attest_service_add_firmware_whitelist(db, device_id, fw_records, count)

// Query
attest_service_get_attest_result(db, device_id, &result)
attest_service_find_device(db, device_id, &entry)
```

### Planned Extensions

Future REST API endpoints (mapping to Keylime/OA patterns):

```
POST   /v1/devices/register        — Register new device
POST   /v1/attestation/challenge   — Create challenge for device
POST   /v1/attestation/verify      — Verify attestation response
GET    /v1/devices/{id}/status     — Get device trust status
PUT    /v1/devices/{id}/policy     — Update device policy
GET    /v1/health                   — Service health check
```

---

## References

- **TCG Infrastructure WG**: Attestation Trust Model for TPM 2.0
- **IETF RATS**: Remote ATtestation procedureS (RFC 9334, draft-ietf-rats-msg-wrap)
- **Keylime**: https://keylime.dev — Linux remote attestation
- **OpenAttestation**: https://github.com/OpenAttestation (archived, historical reference)
- **TCG PC Client Platform TPM Profile**: PCR allocation, event log formats
- **NIST SP 800-193**: Platform Firmware Resiliency Guidelines
- **IETF TEEP**: Trusted Execution Environment Provisioning (related architecture)
- **Linux IMA**: Integrity Measurement Architecture, kernel documentation
- **TPM 2.0 Part 1**: Section 16 — Attestation; Section 28 — Remote Attestation
