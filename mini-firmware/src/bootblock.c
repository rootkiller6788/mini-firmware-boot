#include "bootblock.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * bootblock.c ? Verified Boot Chain Implementation
 *
 * Reference specifications:
 *   - Intel Boot Guard Technology, Rev 1.4
 *   - ARM Trusted Base System Architecture (TBSA)
 *   - TCG PC Client Platform Firmware Profile Spec, v1.05
 *   - UEFI Secure Boot Specification
 *   - PKCS#1 v2.2 (RSA Cryptography Standard), RFC 8017
 */

#define VBOOT_MAGIC 0x56424F54  /* "VBOT" */

static const char *boot_stage_names[] = {
    [BOOT_STAGE_RESET]   = "Reset Vector",
    [BOOT_STAGE_BOOTROM] = "Boot ROM",
    [BOOT_STAGE_BOOTBLK] = "Bootblock/IBB",
    [BOOT_STAGE_PEI]     = "PEI (Pre-EFI Init)",
    [BOOT_STAGE_DXE]     = "DXE (Driver Execution)",
    [BOOT_STAGE_BDS]     = "BDS (Boot Device Select)",
    [BOOT_STAGE_OSLOAD]  = "OS Loader",
    [BOOT_STAGE_OS]      = "Operating System"
};

static const char *verify_result_names[] = {
    [VERIFY_OK]              = "OK",
    [VERIFY_ERR_SIGNATURE]   = "Signature Verification Failed",
    [VERIFY_ERR_HASH]        = "Hash Mismatch",
    [VERIFY_ERR_ROLLBACK]    = "Anti-rollback Check Failed",
    [VERIFY_ERR_POLICY]      = "Policy Violation",
    [VERIFY_ERR_KEY_REVOKED] = "Signing Key Revoked",
    [VERIFY_ERR_NOT_FOUND]   = "Stage Not Found",
    [VERIFY_ERR_INTERNAL]    = "Internal Error"
};

/* ??? L2: SHA-256 Implementation per FIPS 180-4 ?????????????????? */

static const uint32_t sha256_k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

static inline uint32_t rotr32(uint32_t x, uint32_t n)
{
    return (x >> n) | (x << (32 - n));
}

static void sha256_transform(uint32_t state[8], const uint8_t block[64])
{
    uint32_t w[64];
    uint32_t a, b, c, d, e, f, g, h;
    uint32_t t1, t2;

    for (int i = 0; i < 16; i++) {
        w[i] = ((uint32_t)block[i * 4]     << 24) |
               ((uint32_t)block[i * 4 + 1] << 16) |
               ((uint32_t)block[i * 4 + 2] << 8)  |
               ((uint32_t)block[i * 4 + 3]);
    }
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = rotr32(w[i - 15], 7) ^ rotr32(w[i - 15], 18) ^
                      (w[i - 15] >> 3);
        uint32_t s1 = rotr32(w[i - 2], 17) ^ rotr32(w[i - 2], 19) ^
                      (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    a = state[0]; b = state[1]; c = state[2]; d = state[3];
    e = state[4]; f = state[5]; g = state[6]; h = state[7];

    for (int i = 0; i < 64; i++) {
        uint32_t S1  = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
        uint32_t ch  = (e & f) ^ ((~e) & g);
        t1 = h + S1 + ch + sha256_k[i] + w[i];
        uint32_t S0  = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        t2 = S0 + maj;

        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }

    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

/*
 * SHA-256 hash computation per FIPS PUB 180-4.
 *
 * Theorem (Preimage Resistance): Given H(m), finding m requires ~2^256
 * operations (brute force over 256-bit output space).
 *
 * Theorem (Collision Resistance): Finding m1 != m2 with H(m1) == H(m2)
 * requires ~2^128 operations (birthday bound for 256-bit hash).
 *
 * Complexity: O(n) time, O(1) space
 */
static void sha256_hash(const uint8_t *data, uint32_t len, SHA256Digest *out)
{
    uint32_t state[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
    };

    uint8_t  block[64];
    uint32_t block_idx = 0;
    uint64_t bit_len   = (uint64_t)len * 8;

    for (uint32_t i = 0; i < len; i++) {
        block[block_idx++] = data[i];
        if (block_idx == 64) {
            sha256_transform(state, block);
            block_idx = 0;
        }
    }

    block[block_idx++] = 0x80;
    if (block_idx > 56) {
        while (block_idx < 64) block[block_idx++] = 0;
        sha256_transform(state, block);
        block_idx = 0;
    }
    while (block_idx < 56) block[block_idx++] = 0;

    for (int i = 7; i >= 0; i--) {
        block[56 + i] = (uint8_t)(bit_len & 0xFF);
        bit_len >>= 8;
    }

    sha256_transform(state, block);

    for (int i = 0; i < 8; i++) {
        out->data[i * 4]     = (uint8_t)(state[i] >> 24);
        out->data[i * 4 + 1] = (uint8_t)(state[i] >> 16);
        out->data[i * 4 + 2] = (uint8_t)(state[i] >> 8);
        out->data[i * 4 + 3] = (uint8_t)(state[i]);
    }
}

/* ??? L4/L5: RSA-2048 Signature Verification (simplified) ???????? */

/*
 * RSA signature verification with PKCS#1 v1.5 padding.
 *
 * verify(m, s) = (s^e mod n) == EMSA-PKCS1-v1_5-ENCODE(m)
 * where e = 65537 (standard public exponent).
 *
 * Theorem (RSA Correctness): For any m < n, (m^e)^d ? m (mod n)
 * where ed ? 1 (mod lcm(p-1, q-1)).
 *
 * Security: Factoring RSA-2048 requires ~2^112 operations (GNFS).
 * Reference: RFC 8017 ?5.2.2.
 */
VerifyResult vb_verify_signature(const SHA256Digest *digest,
                                  const RSASignature *sig,
                                  const PublicKeyFingerprint *key)
{
    if (!digest || !sig || !key) return VERIFY_ERR_INTERNAL;
    if (key->revoked) return VERIFY_ERR_KEY_REVOKED;

    /*
     * Simulated signature check: the signature is treated as valid if
     * sig[0] == digest[0] XOR key->fingerprint[0] XOR 0x55.
     * This deterministic simulation represents the crypto binding
     * between digest, key, and signature.
     * Real firmware uses hardware crypto accelerators.
     */
    uint8_t expected = (uint8_t)(digest->data[0] ^ key->fingerprint[0] ^ 0x55);
    if (sig->data[0] != expected) return VERIFY_ERR_SIGNATURE;

    return VERIFY_OK;
}

/* ??? L2/L3: Verification Chain Initialization ?????????????????? */

bool vb_init_chain(VerificationChain *vc, const SHA256Digest *rot_hash)
{
    if (!vc || !rot_hash) return false;
    memset(vc, 0, sizeof(VerificationChain));
    memcpy(&vc->root_of_trust_hash, rot_hash, sizeof(SHA256Digest));
    vb_init_policy(&vc->policy, true, true);
    vb_init_pcr_log(&vc->pcr_log);
    vc->chain_valid = true;
    vc->last_good_version = 0;
    return true;
}

bool vb_init_policy(BootPolicy *policy, bool verified_enabled,
                    bool measured_enabled)
{
    if (!policy) return false;
    memset(policy, 0, sizeof(BootPolicy));
    policy->verified_boot_enabled = verified_enabled;
    policy->measured_boot_enabled = measured_enabled;
    policy->min_rollback_version  = 0;

    /* Default: require signature for all stages from Bootblock onward */
    for (int stage = BOOT_STAGE_BOOTBLK; stage <= BOOT_STAGE_OS; stage++) {
        BootPolicyRule rule;
        memset(&rule, 0, sizeof(rule));
        rule.type       = BOOT_RULE_REQUIRE_SIGNATURE;
        rule.applies_to = (BootStage)stage;
        vb_add_policy_rule(policy, &rule);
    }
    return true;
}

bool vb_add_policy_rule(BootPolicy *policy, const BootPolicyRule *rule)
{
    if (!policy || !rule) return false;
    if (policy->rule_count >= BOOT_POLICY_MAX_RULES) return false;
    memcpy(&policy->rules[policy->rule_count], rule, sizeof(BootPolicyRule));
    policy->rule_count++;
    return true;
}

bool vb_register_key(VerificationChain *vc,
                     const uint8_t *fingerprint, uint32_t len)
{
    if (!vc || !fingerprint || len == 0) return false;
    if (len > MAX_KEY_FINGERPRINT) len = MAX_KEY_FINGERPRINT;
    if (vc->num_trusted_keys >= 8) return false;

    PublicKeyFingerprint *k = &vc->trusted_keys[vc->num_trusted_keys];
    memcpy(k->fingerprint, fingerprint, len);
    k->fingerprint_len = len;
    k->revoked = false;
    k->revocation_count = 0;
    vc->num_trusted_keys++;
    return true;
}

bool vb_revoke_key(VerificationChain *vc,
                   const uint8_t *fingerprint, uint32_t len)
{
    if (!vc || !fingerprint) return false;

    for (uint32_t i = 0; i < vc->num_trusted_keys; i++) {
        PublicKeyFingerprint *k = &vc->trusted_keys[i];
        if (k->fingerprint_len == len &&
            memcmp(k->fingerprint, fingerprint, len) == 0) {
            k->revoked = true;
            k->revocation_count++;
            return true;
        }
    }
    return false;
}

/* ??? L5: Image Hash Computation ???????????????????????????????? */

bool vb_compute_image_hash(const uint8_t *image_data, uint32_t image_size,
                           SHA256Digest *out_digest)
{
    if (!image_data || !out_digest || image_size == 0) return false;
    sha256_hash(image_data, image_size, out_digest);
    return true;
}

/* ??? L4/L5: VBootHeader Validation ????????????????????????????? */

VerifyResult vb_validate_header(const VBootHeader *hdr, uint32_t min_version)
{
    if (!hdr) return VERIFY_ERR_INTERNAL;

    /* Check magic number */
    if (hdr->magic != VBOOT_MAGIC) return VERIFY_ERR_NOT_FOUND;

    /* Size sanity */
    if (hdr->header_size < sizeof(VBootHeader) || hdr->image_size == 0)
        return VERIFY_ERR_INTERNAL;

    /* Anti-rollback version check */
    if (hdr->version < min_version) return VERIFY_ERR_ROLLBACK;

    /* Algorithm support check */
    if (hdr->hash_alg != HASH_ALG_SHA256 && hdr->hash_alg != HASH_ALG_SHA384)
        return VERIFY_ERR_INTERNAL;

    return VERIFY_OK;
}

/* ??? L3: Hash Chain Extension ?????????????????????????????????? */

/*
 * Extend verification chain with new stage.
 *
 * composite_i = H(stage_hash_i || composite_{i-1})
 * composite_0 = root_of_trust_hash
 *
 * Security: Any modification to any stage invalidates all subsequent
 * composites (avalanche property of SHA-256).
 *
 * This is the same construction used by:
 *   - TPM 2.0 PCR Extend (TCG PC Client Spec)
 *   - Android Verified Boot hash tree
 *   - Chromium OS kernel verification
 */
bool vb_extend_chain(VerificationChain *vc, BootStage stage,
                     const SHA256Digest *stage_hash, uint32_t version)
{
    if (!vc || !stage_hash) return false;
    if (vc->chain_length >= BOOTBLOCK_MAX_STAGES) return false;

    HashChainEntry *entry = &vc->chain[vc->chain_length];

    uint8_t concat[SHA256_DIGEST_SIZE * 2];
    memcpy(concat, stage_hash->data, SHA256_DIGEST_SIZE);

    if (vc->chain_length == 0) {
        memcpy(concat + SHA256_DIGEST_SIZE,
               vc->root_of_trust_hash.data, SHA256_DIGEST_SIZE);
    } else {
        memcpy(concat + SHA256_DIGEST_SIZE,
               vc->chain[vc->chain_length - 1].composite.data,
               SHA256_DIGEST_SIZE);
    }

    SHA256Digest composite;
    sha256_hash(concat, sizeof(concat), &composite);

    entry->stage    = stage;
    memcpy(&entry->hash, stage_hash, sizeof(SHA256Digest));
    memcpy(&entry->composite, &composite, sizeof(SHA256Digest));
    entry->version  = version;
    entry->verify_status = VERIFY_OK;
    entry->pcr_index = (uint32_t)stage;

    vc->chain_length++;
    return true;
}

/* ??? L5: Full Chain Verification Walk ?????????????????????????? */

/*
 * Verified Boot Walk algorithm:
 *   prev := root_of_trust_hash
 *   FOR i = 0 TO n-1:
 *     expected := H(chain[i].hash || prev)
 *     IF expected != chain[i].composite RETURN false
 *     IF anti_rollback_check(chain[i].version) FAILS RETURN false
 *     IF policy_check(chain[i].stage) FAILS RETURN false
 *     prev := chain[i].composite
 *   RETURN true
 *
 * Time: O(n * m) where n = chain_length, m = policy rule count
 * Space: O(1)
 */
bool vb_verify_chain(VerificationChain *vc)
{
    if (!vc) return false;

    SHA256Digest prev_composite;
    memcpy(&prev_composite, &vc->root_of_trust_hash, sizeof(SHA256Digest));

    for (uint32_t i = 0; i < vc->chain_length; i++) {
        HashChainEntry *entry = &vc->chain[i];

        /* Recompute composite */
        uint8_t concat[SHA256_DIGEST_SIZE * 2];
        memcpy(concat, entry->hash.data, SHA256_DIGEST_SIZE);
        memcpy(concat + SHA256_DIGEST_SIZE,
               prev_composite.data, SHA256_DIGEST_SIZE);

        SHA256Digest expected;
        sha256_hash(concat, sizeof(concat), &expected);

        if (memcmp(&expected, &entry->composite, sizeof(SHA256Digest)) != 0) {
            entry->verify_status = VERIFY_ERR_HASH;
            vc->chain_valid = false;
            return false;
        }

        if (entry->version < vc->policy.min_rollback_version) {
            entry->verify_status = VERIFY_ERR_ROLLBACK;
            vc->chain_valid = false;
            return false;
        }

        /* Check policy rules */
        for (uint32_t r = 0; r < vc->policy.rule_count; r++) {
            BootPolicyRule *rule = &vc->policy.rules[r];
            if (rule->applies_to != entry->stage) continue;

            switch (rule->type) {
            case BOOT_RULE_ANTI_ROLLBACK:
                if (entry->version < rule->config.min_version) {
                    entry->verify_status = VERIFY_ERR_ROLLBACK;
                    vc->chain_valid = false;
                    return false;
                }
                break;
            case BOOT_RULE_REQUIRE_SIGNATURE:
                if (entry->verify_status != VERIFY_OK) {
                    entry->verify_status = VERIFY_ERR_SIGNATURE;
                    vc->chain_valid = false;
                    return false;
                }
                break;
            default:
                break;
            }
        }

        entry->verify_status = VERIFY_OK;
        memcpy(&prev_composite, &entry->composite, sizeof(SHA256Digest));
    }

    vc->chain_valid = true;
    vc->last_good_version = vc->chain_length > 0 ?
        vc->chain[vc->chain_length - 1].version : 0;
    return true;
}

/* ??? L7: TPM PCR Extend ???????????????????????????????????????? */

void vb_init_pcr_log(PCRLog *log)
{
    if (!log) return;
    memset(log, 0, sizeof(PCRLog));
}

/*
 * TPM 2.0 PCR_Extend: newPCR = H(oldPCR || measurement)
 * Per TCG PC Client Platform Firmware Profile ?6.1.
 */
bool vb_extend_pcr(PCRLog *log, uint32_t pcr_index,
                   const SHA256Digest *measurement,
                   uint32_t event_type, const char *description)
{
    if (!log || !measurement) return false;
    if (pcr_index >= BOOTBLOCK_MAX_PCR) return false;
    if (log->event_count >= BOOTBLOCK_MAX_PCR) return false;

    uint8_t concat[SHA256_DIGEST_SIZE * 2];
    memcpy(concat, log->pcr_values[pcr_index].data, SHA256_DIGEST_SIZE);
    memcpy(concat + SHA256_DIGEST_SIZE, measurement->data, SHA256_DIGEST_SIZE);

    SHA256Digest new_pcr;
    sha256_hash(concat, sizeof(concat), &new_pcr);
    memcpy(&log->pcr_values[pcr_index], &new_pcr, sizeof(SHA256Digest));

    PCREvent *evt = &log->events[log->event_count];
    evt->pcr_index  = pcr_index;
    memcpy(&evt->digest, measurement, sizeof(SHA256Digest));
    evt->event_type = event_type;
    if (description) {
        size_t desc_len = strlen(description);
        if (desc_len >= sizeof(evt->event_description))
            desc_len = sizeof(evt->event_description) - 1;
        memcpy(evt->event_description, description, desc_len);
        evt->event_description[desc_len] = '\0';
    }

    log->event_count++;
    return true;
}

/* ??? L8: Anti-rollback Version Check ??????????????????????????? */

/*
 * Anti-rollback stored in monotonic counter (eFuse/RPMB/TPM NV).
 * This prevents downgrade attacks: an attacker with physical access
 * cannot flash older vulnerable firmware.
 */
bool vb_check_anti_rollback(uint32_t image_version,
                            uint32_t stored_min_version)
{
    return image_version >= stored_min_version;
}

/* ??? L7: Diagnostics ???????????????????????????????????????????? */

void vb_print_pcr_log(const PCRLog *log)
{
    if (!log) return;

    printf("=== TPM PCR Measurement Log ===\n");
    printf("Total events: %u\n\n", log->event_count);

    for (uint32_t i = 0; i < log->event_count; i++) {
        const PCREvent *evt = &log->events[i];
        printf("[PCR%02u] Event %u: %s\n", evt->pcr_index, i,
               evt->event_description);
        printf("         Digest: ");
        for (int j = 0; j < 8; j++) printf("%02x", evt->digest.data[j]);
        printf("...\n");
    }

    printf("\n=== Current PCR Values ===\n");
    for (uint32_t i = 0; i < BOOTBLOCK_MAX_PCR; i++) {
        bool has_value = false;
        for (int j = 0; j < SHA256_DIGEST_SIZE; j++) {
            if (log->pcr_values[i].data[j] != 0) { has_value = true; break; }
        }
        if (has_value) {
            printf("PCR%02u: ", i);
            for (int j = 0; j < 16; j++)
                printf("%02x", log->pcr_values[i].data[j]);
            printf("...\n");
        }
    }
}

void vb_print_chain(const VerificationChain *vc)
{
    if (!vc) return;

    printf("=== Verified Boot Chain ===\n");
    printf("Root of Trust Hash: ");
    for (int i = 0; i < 16; i++) printf("%02x", vc->root_of_trust_hash.data[i]);
    printf("...\n");
    printf("Chain Length: %u stages, Valid: %s\n",
           vc->chain_length, vc->chain_valid ? "YES" : "NO");
    printf("Policy: Verified=%s, Measured=%s\n",
           vc->policy.verified_boot_enabled ? "ON" : "OFF",
           vc->policy.measured_boot_enabled ? "ON" : "OFF");
    printf("Trusted Keys: %u\n", vc->num_trusted_keys);

    for (uint32_t i = 0; i < vc->chain_length; i++) {
        const HashChainEntry *e = &vc->chain[i];
        const char *sname = (e->stage < BOOTBLOCK_MAX_STAGES) ?
            boot_stage_names[e->stage] : "Unknown";
        printf("\n  [%u] %s (v%u)\n", i, sname, e->version);
        printf("      Hash:      ");
        for (int j = 0; j < 8; j++) printf("%02x", e->hash.data[j]);
        printf("...\n");
        printf("      Composite: ");
        for (int j = 0; j < 8; j++) printf("%02x", e->composite.data[j]);
        printf("...\n");
        printf("      Status:    %s\n", verify_result_names[e->verify_status]);
    }

    printf("\nLast Good Version: %u\n", vc->last_good_version);
}
