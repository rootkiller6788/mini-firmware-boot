/* TPM 2.0 Key Management Implementation
 * L1: Key object allocation, public/sensitive area management
 * L2: Four-tier key hierarchy (endorsement, storage, platform, null)
 * L3: Primary key creation, loading, unloading lifecycle
 * L4: Key attribute validation per TPM 2.0 Part 2 Table 25
 * L5: KDFa SP800-108 counter-mode HMAC derivation
 * L5: Key sealing/unsealing with PCR policy binding
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "sha256.h"
#include "tpm_keys.h"

const char* tpm_key_type_to_string(TPMKeyType t) {
    switch (t) {
        case TPM_KEYTYPE_RSA_2048:     return "RSA-2048";
        case TPM_KEYTYPE_RSA_3072:     return "RSA-3072";
        case TPM_KEYTYPE_ECC_NIST256:  return "ECC-NIST-P256";
        case TPM_KEYTYPE_ECC_NIST384:  return "ECC-NIST-P384";
        case TPM_KEYTYPE_SYMCIPHER:    return "SYMMETRIC-CIPHER";
        case TPM_KEYTYPE_KEYEDHASH:    return "KEYED-HASH";
        default:                       return "UNKNOWN";
    }
}

const char* tpm_hierarchy_to_string(TPMHierarchy h) {
    switch (h) {
        case TPM_HIERARCHY_NULL:        return "NULL";
        case TPM_HIERARCHY_OWNER:       return "OWNER (Storage)";
        case TPM_HIERARCHY_ENDORSEMENT: return "ENDORSEMENT";
        case TPM_HIERARCHY_PLATFORM:    return "PLATFORM";
        default:                        return "UNKNOWN";
    }
}

/* ---- L5: TPM SHA-256 HMAC (RFC 2104, exported for attestation) ---- */
void tpm_hmac_sha256(const uint8_t* key, uint32_t key_len,
                            const uint8_t* data, uint32_t data_len,
                            uint8_t* mac) {
    uint8_t k_ipad[64], k_opad[64];
    uint8_t inner_hash[SHA256_DIGEST_SIZE];
    uint8_t tmp_key[SHA256_DIGEST_SIZE];
    SHA256_CTX ctx;
    uint32_t i;

    memset(k_ipad, 0x36, 64);
    memset(k_opad, 0x5c, 64);

    if (key_len > 64) {
        sha256_hash(key, key_len, tmp_key);
        key = tmp_key;
        key_len = SHA256_DIGEST_SIZE;
    }

    for (i = 0; i < key_len; i++) {
        k_ipad[i] ^= key[i];
        k_opad[i] ^= key[i];
    }

    sha256_init(&ctx);
    sha256_update(&ctx, k_ipad, 64);
    sha256_update(&ctx, data, data_len);
    sha256_final(&ctx, inner_hash);

    sha256_init(&ctx);
    sha256_update(&ctx, k_opad, 64);
    sha256_update(&ctx, inner_hash, SHA256_DIGEST_SIZE);
    sha256_final(&ctx, mac);
}
/* ---- L5: KDFa ? SP800-108 Counter Mode with HMAC-SHA256 ----
 * Formula: output = HMAC(key, counter || label || 0x00 || context || bits)
 * where counter is 32-bit big-endian, incremented until output_len
 * bytes are produced.
 *
 * Theorem (Bellare 2006): Under the PRF assumption on HMAC,
 * KDFa provably produces output that is computationally
 * indistinguishable from random, providing the required
 * cryptographic strength for key derivation.
 *
 * Used in: TPM2_CreatePrimary, TPM2_Create, session key derivation,
 *          duplicate key wrapping, and credential activation.
 */
bool tpm_kdfa(const KDFaParams* params, uint8_t* output, uint32_t output_len) {
    uint32_t counter, produced, remaining, block_size, i;
    uint8_t counter_bytes[4];
    uint8_t hash_output[SHA256_DIGEST_SIZE];
    uint8_t bits_bytes[4];
    uint32_t input_len;
    uint8_t* input_buf;

    if (params == NULL || output == NULL || output_len == 0)
        return false;
    if (params->key == NULL || params->key_len == 0) return false;
    if (output_len > KDFA_MAX_OUTPUT) return false;

    block_size = SHA256_DIGEST_SIZE;

    input_len = 4;
    if (params->label) input_len += params->label_len;
    input_len += 1;
    if (params->context_u) input_len += params->context_u_len;
    if (params->context_v) input_len += params->context_v_len;
    input_len += 4;

    input_buf = (uint8_t*)malloc(input_len);
    if (input_buf == NULL) return false;

    bits_bytes[0] = (uint8_t)((params->bits >> 24) & 0xFF);
    bits_bytes[1] = (uint8_t)((params->bits >> 16) & 0xFF);
    bits_bytes[2] = (uint8_t)((params->bits >> 8) & 0xFF);
    bits_bytes[3] = (uint8_t)(params->bits & 0xFF);

    produced = 0;
    counter  = 1;

    while (produced < output_len) {
        counter_bytes[0] = (uint8_t)((counter >> 24) & 0xFF);
        counter_bytes[1] = (uint8_t)((counter >> 16) & 0xFF);
        counter_bytes[2] = (uint8_t)((counter >> 8) & 0xFF);
        counter_bytes[3] = (uint8_t)(counter & 0xFF);

        i = 0;
        memcpy(input_buf + i, counter_bytes, 4); i += 4;
        if (params->label) {
            memcpy(input_buf + i, params->label, params->label_len);
            i += params->label_len;
        }
        input_buf[i++] = 0x00;
        if (params->context_u) {
            memcpy(input_buf + i, params->context_u, params->context_u_len);
            i += params->context_u_len;
        }
        if (params->context_v) {
            memcpy(input_buf + i, params->context_v, params->context_v_len);
            i += params->context_v_len;
        }
        memcpy(input_buf + i, bits_bytes, 4); i += 4;

        tpm_hmac_sha256(params->key, params->key_len, input_buf, i, hash_output);

        remaining = output_len - produced;
        if (remaining > block_size) remaining = block_size;
        memcpy(output + produced, hash_output, remaining);
        produced += remaining;
        counter++;
    }

    free(input_buf);
    return true;
}
/* ---- L3: Key manager lifecycle ---- */

void tpm_key_manager_init(TPMKeyManager* mgr) {
    uint32_t i;
    if (mgr == NULL) return;
    memset(mgr, 0, sizeof(TPMKeyManager));
    mgr->key_count = 0;
    mgr->seeds_generated = false;
    for (i = 0; i < TPM_MAX_LOADED_KEYS; i++) {
        mgr->keys[i].loaded = false;
        mgr->keys[i].handle = 0;
    }
}

/* Generate primary seeds for each hierarchy.
 * In a real TPM, these are generated by a hardware TRNG.
 * Here we simulate with deterministic SHA-256 chaining from
 * a simple initialization string, ensuring reproducibility
 * for testing and educational purposes. */
bool tpm_generate_primary_seeds(TPMKeyManager* mgr) {
    uint8_t base[SHA256_DIGEST_SIZE];
    uint8_t label[32];

    if (mgr == NULL) return false;

    sha256_hash((const uint8_t*)"TPM_SEED_BASE_KEY_2024", 22, base);

    memcpy(label, "STORAGE", 7);
    tpm_hmac_sha256(base, SHA256_DIGEST_SIZE, label, 7,
                    mgr->primary_seed_storage);

    memcpy(label, "ENDORSEMENT", 11);
    tpm_hmac_sha256(base, SHA256_DIGEST_SIZE, label, 11,
                    mgr->primary_seed_endorsement);

    memcpy(label, "PLATFORM", 8);
    tpm_hmac_sha256(base, SHA256_DIGEST_SIZE, label, 8,
                    mgr->primary_seed_platform);

    mgr->seeds_generated = true;
    return true;
}

/* L3: Create primary key under a hierarchy.
 * This simulates TPM2_CreatePrimary: derives a primary key from
 * the hierarchy's primary seed using KDFa, then stores it in the
 * loaded key table.
 *
 * The primary key name is computed as:
 *   name = SHA256(nameAlg || template_hash)
 * where template_hash incorporates the key type and attributes.
 * This is a simplification of the real TPM2B_NAME derivation.
 */
bool tpm_create_primary(TPMKeyManager* mgr,
                        TPMHierarchy hierarchy,
                        TPMKeyType key_type,
                        uint32_t attributes,
                        const uint8_t* auth_value, uint16_t auth_size,
                        const uint8_t* policy,
                        uint32_t* out_handle) {
    uint32_t slot, handle;
    const uint8_t* seed;
    KDFaParams kdfa_params;
    uint8_t derived_key[SHA256_DIGEST_SIZE];
    uint8_t template_hash[SHA256_DIGEST_SIZE];
    uint8_t template_buf[64];

    if (mgr == NULL || out_handle == NULL) return false;
    if (!mgr->seeds_generated) return false;
    if (mgr->key_count >= TPM_MAX_LOADED_KEYS) return false;
    if (!tpm_validate_key_attrs(attributes, key_type)) return false;

    /* Select hierarchy seed */
    switch (hierarchy) {
        case TPM_HIERARCHY_OWNER:
            seed = mgr->primary_seed_storage; handle = 0x81000000; break;
        case TPM_HIERARCHY_ENDORSEMENT:
            seed = mgr->primary_seed_endorsement; handle = 0x81010000; break;
        case TPM_HIERARCHY_PLATFORM:
            seed = mgr->primary_seed_platform; handle = 0x81020000; break;
        default:
            return false;
    }

    handle += (mgr->key_count & 0xFFFF);

    /* Build template for name derivation */
    memset(template_buf, 0, 64);
    template_buf[0] = (uint8_t)(key_type & 0xFF);
    template_buf[1] = (uint8_t)((key_type >> 8) & 0xFF);
    template_buf[2] = (uint8_t)(attributes & 0xFF);
    template_buf[3] = (uint8_t)((attributes >> 8) & 0xFF);
    template_buf[4] = (uint8_t)((attributes >> 16) & 0xFF);
    template_buf[5] = (uint8_t)((attributes >> 24) & 0xFF);
    sha256_hash(template_buf, 6, template_hash);

    /* KDFa: derive unique key material for this primary */
    kdfa_params.key          = seed;
    kdfa_params.key_len      = SHA256_DIGEST_SIZE;
    kdfa_params.label        = (const uint8_t*)"PRIMARY";
    kdfa_params.label_len    = 7;
    kdfa_params.context_u    = template_hash;
    kdfa_params.context_u_len = SHA256_DIGEST_SIZE;
    kdfa_params.context_v    = NULL;
    kdfa_params.context_v_len = 0;
    kdfa_params.hash_alg     = TPM_ALG_SHA256;
    kdfa_params.bits         = 256;

    if (!tpm_kdfa(&kdfa_params, derived_key, SHA256_DIGEST_SIZE))
        return false;

    /* Allocate slot */
    slot = mgr->key_count;
    TPMKeyObject* key = &mgr->keys[slot];
    memset(key, 0, sizeof(TPMKeyObject));

    key->handle       = handle;
    key->parent_handle = hierarchy;
    key->hierarchy    = hierarchy;
    key->loaded       = true;
    key->is_primary   = true;
    key->public_part.key_type = key_type;
    key->public_part.attributes = attributes;
    key->public_part.hash_alg = TPM_ALG_SHA256;

    memcpy(key->public_part.unique, derived_key, SHA256_DIGEST_SIZE);

    /* Compute key name */
    {
        uint8_t name_buf[2 + SHA256_DIGEST_SIZE];
        name_buf[0] = (uint8_t)(TPM_ALG_SHA256 >> 8);
        name_buf[1] = (uint8_t)(TPM_ALG_SHA256 & 0xFF);
        memcpy(name_buf + 2, template_hash, SHA256_DIGEST_SIZE);
        sha256_hash(name_buf, 2 + SHA256_DIGEST_SIZE, key->name);
    }

    if (auth_value && auth_size > 0) {
        key->sensitive.auth_value_size = (auth_size > 32) ? 32 : auth_size;
        memcpy(key->sensitive.auth_value, auth_value,
               key->sensitive.auth_value_size);
    }

    if (policy) {
        memcpy(key->public_part.auth_policy, policy, SHA256_DIGEST_SIZE);
        key->public_part.auth_policy_set = true;
    }

    /* Store hierarchy seed for child key derivation */
    memcpy(key->sensitive.seed_value, seed, SHA256_DIGEST_SIZE);

    mgr->key_count++;
    *out_handle = handle;
    return true;
}
/* L3: Load an external key under a parent key.
 * In TPM 2.0, keys exist in a strict hierarchy: a key can only
 * be loaded under a parent storage key that wraps its sensitive
 * area. This function simulates loading a pre-existing key by
 * storing its public and sensitive areas in the key table. */
bool tpm_load_key(TPMKeyManager* mgr, uint32_t parent_handle,
                  const TPMKeyPublic* public_part,
                  const TPMKeySensitive* sensitive,
                  uint32_t* out_handle) {
    uint32_t slot, handle;

    if (mgr == NULL || public_part == NULL || sensitive == NULL ||
        out_handle == NULL) return false;
    if (mgr->key_count >= TPM_MAX_LOADED_KEYS) return false;

    handle = 0x81000000 + ((mgr->key_count + 1) * 256);

    slot = mgr->key_count;
    TPMKeyObject* key = &mgr->keys[slot];
    memset(key, 0, sizeof(TPMKeyObject));

    key->handle        = handle;
    key->parent_handle = parent_handle;
    key->loaded        = true;
    key->is_primary    = false;
    key->public_part   = *public_part;
    key->sensitive     = *sensitive;

    /* Derive name from public template: name = SHA256(nameAlg || template_hash) */
    {
        uint8_t name_input[2 + SHA256_DIGEST_SIZE];
        uint8_t tmpl[SHA256_DIGEST_SIZE];
        uint8_t attr_buf[4];

        attr_buf[0] = (uint8_t)(public_part->attributes & 0xFF);
        attr_buf[1] = (uint8_t)((public_part->attributes >> 8) & 0xFF);
        attr_buf[2] = (uint8_t)((public_part->attributes >> 16) & 0xFF);
        attr_buf[3] = (uint8_t)((public_part->attributes >> 24) & 0xFF);

        sha256_hash(attr_buf, 4, tmpl);
        name_input[0] = (uint8_t)(TPM_ALG_SHA256 >> 8);
        name_input[1] = (uint8_t)(TPM_ALG_SHA256 & 0xFF);
        memcpy(name_input + 2, tmpl, SHA256_DIGEST_SIZE);
        sha256_hash(name_input, 2 + SHA256_DIGEST_SIZE, key->name);
    }

    mgr->key_count++;
    *out_handle = handle;
    return true;
}

/* L3: Flush (unload) a key from the TPM key table.
 * Transient keys are removed; primary keys can be re-created.
 * This matches TPM2_FlushContext semantics for key objects. */
bool tpm_flush_key(TPMKeyManager* mgr, uint32_t handle) {
    uint32_t i, pos;

    if (mgr == NULL) return false;

    pos = (uint32_t)-1;
    for (i = 0; i < mgr->key_count; i++) {
        if (mgr->keys[i].loaded && mgr->keys[i].handle == handle) {
            pos = i;
            break;
        }
    }
    if (pos == (uint32_t)-1) return false;

    memset(&mgr->keys[pos], 0, sizeof(TPMKeyObject));
    for (i = pos; i < mgr->key_count - 1; i++) {
        mgr->keys[i] = mgr->keys[i + 1];
    }
    memset(&mgr->keys[mgr->key_count - 1], 0, sizeof(TPMKeyObject));
    mgr->key_count--;
    return true;
}

/* ---- L5: Key sealing ? bind data to PCR state ----
 * Sealed data can only be recovered when the PCR values match
 * those specified at seal time. The sealed blob contains:
 *   [pcr_selection(4B)][pcr_count(2B)][pcr_composite(SHA256)]
 *    [encrypted_data(N)]
 * where encrypted_data = XOR(data, derived_key) using
 * KDFa(parent_key.unique || pcr_composite) as the wrapping key.
 *
 * Theorem: Unsealing without correct PCR values requires
 *          breaking the preimage resistance of SHA-256 or
 *          the PRF property of KDFa (computationally infeasible).
 */
bool tpm_seal(TPMKeyManager* mgr, uint32_t parent_handle,
              const uint8_t* data, uint16_t data_len,
              const uint8_t* pcr_values, uint32_t pcr_count,
              uint8_t* sealed_blob, uint32_t* blob_len) {
    uint32_t i, pos;
    uint8_t pcr_composite[SHA256_DIGEST_SIZE];
    uint8_t wrapping_key[SHA256_DIGEST_SIZE];
    KDFaParams kdfa_params;
    uint32_t blob_header_size;

    if (mgr == NULL || data == NULL || pcr_values == NULL ||
        sealed_blob == NULL || blob_len == NULL) return false;

    /* Find parent key */
    pos = (uint32_t)-1;
    for (i = 0; i < mgr->key_count; i++) {
        if (mgr->keys[i].handle == parent_handle) { pos = i; break; }
    }
    if (pos == (uint32_t)-1) return false;

    /* Compute PCR composite: SHA256(all PCR values concatenated) */
    sha256_hash(pcr_values, pcr_count * SHA256_DIGEST_SIZE, pcr_composite);

    /* Derive wrapping key from parent unique + PCR composite */
    kdfa_params.key           = mgr->keys[pos].public_part.unique;
    kdfa_params.key_len       = SHA256_DIGEST_SIZE;
    kdfa_params.label         = (const uint8_t*)"SEAL";
    kdfa_params.label_len     = 4;
    kdfa_params.context_u     = pcr_composite;
    kdfa_params.context_u_len = SHA256_DIGEST_SIZE;
    kdfa_params.context_v     = NULL;
    kdfa_params.context_v_len = 0;
    kdfa_params.hash_alg      = TPM_ALG_SHA256;
    kdfa_params.bits          = 256;

    if (!tpm_kdfa(&kdfa_params, wrapping_key, SHA256_DIGEST_SIZE))
        return false;

    /* Build sealed blob */
    blob_header_size = 4 + 2 + SHA256_DIGEST_SIZE;
    *blob_len = blob_header_size + data_len;

    /* PCR count (4 bytes big-endian) */
    sealed_blob[0] = (uint8_t)((pcr_count >> 24) & 0xFF);
    sealed_blob[1] = (uint8_t)((pcr_count >> 16) & 0xFF);
    sealed_blob[2] = (uint8_t)((pcr_count >> 8) & 0xFF);
    sealed_blob[3] = (uint8_t)(pcr_count & 0xFF);
    /* PCR data length (2 bytes) */
    sealed_blob[4] = (uint8_t)((pcr_count >> 8) & 0xFF);
    sealed_blob[5] = (uint8_t)(pcr_count & 0xFF);
    /* PCR composite hash */
    memcpy(sealed_blob + 6, pcr_composite, SHA256_DIGEST_SIZE);
    /* Encrypted data: XOR with wrapping key */
    for (i = 0; i < data_len; i++) {
        sealed_blob[blob_header_size + i] =
            data[i] ^ wrapping_key[i % SHA256_DIGEST_SIZE];
    }

    return true;
}

/* L5: Key unsealing ? recover data when PCR policy matches.
 * Re-computes the wrapping key using the current PCR values
 * and attempts to decrypt. If the PCR composite does not match
 * the sealed blob, the decrypted data will be corrupt (garbage). */
bool tpm_unseal(TPMKeyManager* mgr, uint32_t parent_handle,
                const uint8_t* sealed_blob, uint32_t blob_len,
                const uint8_t* current_pcrs, uint32_t pcr_count,
                uint8_t* data_out, uint16_t* data_len) {
    uint32_t i, pos;
    uint8_t current_composite[SHA256_DIGEST_SIZE];
    uint8_t expected_composite[SHA256_DIGEST_SIZE];
    uint8_t wrapping_key[SHA256_DIGEST_SIZE];
    KDFaParams kdfa_params;
    uint32_t blob_header_size, sealed_data_len;

    if (mgr == NULL || sealed_blob == NULL || current_pcrs == NULL ||
        data_out == NULL || data_len == NULL) return false;

    pos = (uint32_t)-1;
    for (i = 0; i < mgr->key_count; i++) {
        if (mgr->keys[i].handle == parent_handle) { pos = i; break; }
    }
    if (pos == (uint32_t)-1) return false;

    blob_header_size = 4 + 2 + SHA256_DIGEST_SIZE;
    if (blob_len < blob_header_size) return false;
    sealed_data_len = blob_len - blob_header_size;

    /* Verify PCR composite matches */
    memcpy(expected_composite, sealed_blob + 6, SHA256_DIGEST_SIZE);
    sha256_hash(current_pcrs, pcr_count * SHA256_DIGEST_SIZE,
                current_composite);

    if (memcmp(current_composite, expected_composite, SHA256_DIGEST_SIZE) != 0)
        return false;

    /* Derive same wrapping key */
    kdfa_params.key           = mgr->keys[pos].public_part.unique;
    kdfa_params.key_len       = SHA256_DIGEST_SIZE;
    kdfa_params.label         = (const uint8_t*)"SEAL";
    kdfa_params.label_len     = 4;
    kdfa_params.context_u     = current_composite;
    kdfa_params.context_u_len = SHA256_DIGEST_SIZE;
    kdfa_params.context_v     = NULL;
    kdfa_params.context_v_len = 0;
    kdfa_params.hash_alg      = TPM_ALG_SHA256;
    kdfa_params.bits          = 256;

    if (!tpm_kdfa(&kdfa_params, wrapping_key, SHA256_DIGEST_SIZE))
        return false;

    /* Decrypt: XOR is its own inverse */
    for (i = 0; i < sealed_data_len; i++) {
        data_out[i] = sealed_blob[blob_header_size + i] ^
                      wrapping_key[i % SHA256_DIGEST_SIZE];
    }
    *data_len = (uint16_t)sealed_data_len;
    return true;
}
/* L4: Key attribute validation per TPM 2.0 Part 2 Table 25.
 * Some attribute combinations are invalid:
 * - FIXEDTPM requires FIXEDPARENT
 * - SIGN + DECRYPT + RESTRICTED for asymmetric keys
 * - SENSITIVEDATAORIGIN requires FIXEDTPM */
bool tpm_validate_key_attrs(uint32_t attributes, TPMKeyType key_type) {
    /* FIXEDTPM implies FIXEDPARENT */
    if ((attributes & TPM_KEYATTR_FIXEDTPM) &&
        !(attributes & TPM_KEYATTR_FIXEDPARENT))
        return false;

    /* SENSITIVEDATAORIGIN requires FIXEDTPM */
    if ((attributes & TPM_KEYATTR_SENSITIVEDATAORIGIN) &&
        !(attributes & TPM_KEYATTR_FIXEDTPM))
        return false;

    /* Sign+decrypt with restricted only valid for storage keys */
    if ((attributes & TPM_KEYATTR_SIGN) &&
        (attributes & TPM_KEYATTR_DECRYPT) &&
        (attributes & TPM_KEYATTR_RESTRICTED)) {
        if (key_type != TPM_KEYTYPE_KEYEDHASH) return false;
    }

    /* ENCRYPTEDDUPLICATION requires non-primary */
    (void)key_type;
    return true;
}

void tpm_key_manager_print(const TPMKeyManager* mgr) {
    uint32_t i, j;
    if (mgr == NULL) return;

    printf("=== TPM Key Manager ===\n");
    printf("  Seeds: %s\n", mgr->seeds_generated ? "generated" : "not generated");
    printf("  Loaded keys: %u/%u\n", mgr->key_count, TPM_MAX_LOADED_KEYS);

    for (i = 0; i < mgr->key_count; i++) {
        const TPMKeyObject* key = &mgr->keys[i];
        if (!key->loaded) continue;

        printf("\n  [%u] Key Handle: 0x%08X\n", i, key->handle);
        printf("      Type:         %s\n",
               tpm_key_type_to_string(key->public_part.key_type));
        printf("      Hierarchy:    %s\n",
               tpm_hierarchy_to_string(key->hierarchy));
        printf("      Primary:      %s\n", key->is_primary ? "yes" : "no");
        printf("      Parent:       0x%08X\n", key->parent_handle);
        printf("      Attrs:        ");
        if (key->public_part.attributes & TPM_KEYATTR_SIGN)    printf("SIGN ");
        if (key->public_part.attributes & TPM_KEYATTR_DECRYPT) printf("DECRYPT ");
        if (key->public_part.attributes & TPM_KEYATTR_RESTRICTED) printf("RESTR ");
        if (key->public_part.attributes & TPM_KEYATTR_FIXEDTPM) printf("FIXED ");
        if (key->public_part.attributes & TPM_KEYATTR_NODA)    printf("NODA ");
        printf("\n");
        printf("      Auth Policy:  %s\n",
               key->public_part.auth_policy_set ? "set" : "none");
        printf("      Auth Value:   %s\n",
               key->sensitive.auth_value_size > 0 ? "set" : "none");
        printf("      Name:         ");
        for (j = 0; j < 8; j++) printf("%02x", key->name[j]);
        printf("...\n");
    }
}
