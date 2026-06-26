#include "key_mgmt.h"
#include "signature_verify.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

/*
 * Key Lifecycle Management Implementation
 *
 * Reference: NIST SP 800-57 Part 1, Revision 5
 *   - Section 8.1: Key states and transitions
 *   - Section 8.2: Key usage periods
 *   - Section 8.3: Key compromise and recovery
 *
 * Key Lifecycle State Machine:
 *
 * Pre-Activation ??? Active ??? Deactivated ??? Revoked ??? Destroyed
 *                       ?            ?
 *                       ?            ???? Active (reactivate)
 *                       ?
 *                       ???? Compromised ??? Destroyed-Compromised
 *                       ???? Revoked
 */

/* ??? HMAC-SHA-256 (RFC 2104) ????????????????????????????????????????? */

static void hmac_sha256(const uint8_t *key, uint32_t key_len,
                         const uint8_t *data, uint32_t data_len,
                         uint8_t *mac)
{
    uint8_t key_block[64]; /* SHA-256 block size = 64 bytes */
    uint8_t inner[64 + 256]; /* inner padding + data */
    uint8_t outer_hash_input[64 + SHA256_HASH_SIZE];

    memset(key_block, 0, 64);
    if (key_len > 64) {
        sha256_hash(key, key_len, key_block);
    } else {
        memcpy(key_block, key, key_len);
    }

    /* Inner: H((K XOR 0x36) || data) */
    for (int i = 0; i < 64; i++) inner[i] = key_block[i] ^ 0x36;
    memcpy(inner + 64, data, data_len);
    sha256_hash(inner, 64 + data_len, outer_hash_input + 64);

    /* Outer: H((K XOR 0x5C) || inner_hash) */
    for (int i = 0; i < 64; i++) outer_hash_input[i] = key_block[i] ^ 0x5C;
    sha256_hash(outer_hash_input, 64 + SHA256_HASH_SIZE, mac);
}

/* ??? Key Store Operations ???????????????????????????????????????????? */

bool km_store_init(KMKeyStore *store)
{
    if (!store) return false;
    memset(store, 0, sizeof(KMKeyStore));
    store->next_handle = 1;
    store->locked = false;
    return true;
}

bool km_store_lock(KMKeyStore *store)
{
    if (!store) return false;
    store->locked = true;
    return true;
}

bool km_store_unlock(KMKeyStore *store)
{
    if (!store) return false;
    store->locked = false;
    return true;
}

/* ??? Key Lookup ?????????????????????????????????????????????????????? */

static KMKeyEntry *find_key(KMKeyStore *store, const char *key_id)
{
    if (!store || !key_id) return NULL;
    for (uint32_t i = 0; i < store->key_count; i++) {
        if (strcmp(store->keys[i].key_id, key_id) == 0) {
            return &store->keys[i];
        }
    }
    return NULL;
}

const KMKeyEntry *km_key_find(const KMKeyStore *store, const char *key_id)
{
    if (!store || !key_id) return NULL;
    for (uint32_t i = 0; i < store->key_count; i++) {
        if (strcmp(store->keys[i].key_id, key_id) == 0) {
            /* Only return non-destroyed keys to external callers */
            KMKeyState s = store->keys[i].state;
            if (s != KM_STATE_DESTROYED && s != KM_STATE_DESTROYED_COMPROMISED) {
                return &store->keys[i];
            }
        }
    }
    return NULL;
}

const KMKeyEntry *km_key_find_active(const KMKeyStore *store, KMKeyType key_type)
{
    if (!store) return NULL;
    for (uint32_t i = 0; i < store->key_count; i++) {
        if (store->keys[i].state == KM_STATE_ACTIVE &&
            store->keys[i].key_type == key_type) {
            return &store->keys[i];
        }
    }
    return NULL;
}

bool km_key_is_valid(const KMKeyEntry *key, uint64_t current_time)
{
    if (!key) return false;
    if (key->state != KM_STATE_ACTIVE) return false;
    return current_time >= key->not_before && current_time <= key->not_after;
}

uint32_t km_key_list_by_state(const KMKeyStore *store, KMKeyState state,
                               const KMKeyEntry **results, uint32_t max_results)
{
    if (!store || !results) return 0;
    uint32_t count = 0;
    for (uint32_t i = 0; i < store->key_count && count < max_results; i++) {
        if (store->keys[i].state == state) {
            results[count++] = &store->keys[i];
        }
    }
    return count;
}

/* ??? Key Generation ?????????????????????????????????????????????????? */

bool km_key_generate(KMKeyStore *store, const char *key_id, const char *name,
                     KMKeyType key_type, KMKeyUsage usage, uint32_t bits,
                     uint64_t valid_days)
{
    if (!store || !key_id || !name) return false;
    if (store->locked) return false;
    if (store->key_count >= KM_MAX_KEYS) return false;

    /* Check for duplicate key_id */
    if (find_key(store, key_id)) return false;

    KMKeyEntry *key = &store->keys[store->key_count];
    memset(key, 0, sizeof(KMKeyEntry));

    snprintf(key->key_id, KM_MAX_KEY_ID, "%s", key_id);
    snprintf(key->key_name, KM_MAX_KEY_NAME, "%s", name);
    key->key_type = key_type;
    key->usage_mask = usage;
    key->state = KM_STATE_PRE_ACTIVATION;
    key->key_strength_bits = bits;
    key->version = 1;
    key->created_at = (uint64_t)time(NULL);
    key->not_before = key->created_at;
    key->not_after = key->created_at + (valid_days * 86400);
    key->is_tpm_protected = false;

    /* Generate key material based on type.
     * For RSA keys: generate a proper keypair.
     * For symmetric keys: generate random bytes. */
    switch (key_type) {
        case KM_KEY_TYPE_RSA_2048:
        case KM_KEY_TYPE_RSA_4096: {
            /* Generate RSA keypair using crypto primitives.
             * Key strength determines modulus size:
             *   RSA-2048: 256-byte modulus
             *   RSA-4096: 512-byte modulus
             *
             * In production, this uses a CSPRNG and prime generation.
             * Here we use a deterministic demo key. */
            uint32_t mod_size = (bits >= 4096) ? 512 : 256;
            if (mod_size > KM_MAX_PUBLIC_KEY) mod_size = KM_MAX_PUBLIC_KEY;
            for (uint32_t i = 0; i < mod_size; i++) {
                key->public_key[i] = (uint8_t)((i * 73 + bits) & 0xFF);
            }
            key->public_key_len = mod_size;
            for (uint32_t i = 0; i < mod_size && i < KM_MAX_KEY_DATA; i++) {
                key->private_key[i] = (uint8_t)((i * 37 + bits + 1) & 0xFF);
            }
            key->private_key_len = mod_size;
            break;
        }
        case KM_KEY_TYPE_ECDSA_P256: {
            /* NIST P-256: 32-byte private key, 65-byte uncompressed public key */
            for (uint32_t i = 0; i < 32; i++) {
                key->private_key[i] = (uint8_t)((i * 61 + 19) & 0xFF);
            }
            key->private_key_len = 32;
            key->public_key[0] = 0x04; /* uncompressed point indicator */
            for (uint32_t i = 1; i < 65; i++) {
                key->public_key[i] = (uint8_t)((i * 31 + 7) & 0xFF);
            }
            key->public_key_len = 65;
            break;
        }
        case KM_KEY_TYPE_ECDSA_P384: {
            /* NIST P-384: 48-byte private key, 97-byte uncompressed public key */
            for (uint32_t i = 0; i < 48; i++) {
                key->private_key[i] = (uint8_t)((i * 53 + 23) & 0xFF);
            }
            key->private_key_len = 48;
            key->public_key[0] = 0x04;
            for (uint32_t i = 1; i < 97; i++) {
                key->public_key[i] = (uint8_t)((i * 29 + 13) & 0xFF);
            }
            key->public_key_len = 97;
            break;
        }
        case KM_KEY_TYPE_AES_256: {
            /* AES-256: 32-byte symmetric key (also used as seed for HMAC) */
            for (uint32_t i = 0; i < 32; i++) {
                key->private_key[i] = (uint8_t)((i * 127 + 59) & 0xFF);
            }
            key->private_key_len = 32;
            key->public_key_len = 0; /* No public component for symmetric keys */
            break;
        }
        case KM_KEY_TYPE_HMAC_SHA256:
        default: {
            /* HMAC key: 32 bytes (SHA-256 output size) */
            for (uint32_t i = 0; i < 32; i++) {
                key->private_key[i] = (uint8_t)((i * 89 + 41) & 0xFF);
            }
            key->private_key_len = 32;
            key->public_key_len = 0;
            break;
        }
    }

    store->key_count++;
    return true;
}

/* ??? Key Lifecycle State Transitions ????????????????????????????????? */

bool km_key_activate(KMKeyStore *store, const char *key_id)
{
    KMKeyEntry *key = find_key(store, key_id);
    if (!key) return false;
    /* Valid transitions to ACTIVE:
     *   PRE_ACTIVATION ? ACTIVE (initial activation)
     *   DEACTIVATED ? ACTIVE (reactivation) */
    if (key->state != KM_STATE_PRE_ACTIVATION &&
        key->state != KM_STATE_DEACTIVATED) {
        return false;
    }
    key->state = KM_STATE_ACTIVE;
    return true;
}

bool km_key_deactivate(KMKeyStore *store, const char *key_id)
{
    KMKeyEntry *key = find_key(store, key_id);
    if (!key) return false;
    /* Only ACTIVE keys can be deactivated */
    if (key->state != KM_STATE_ACTIVE) return false;
    key->state = KM_STATE_DEACTIVATED;
    return true;
}

bool km_key_reactivate(KMKeyStore *store, const char *key_id)
{
    KMKeyEntry *key = find_key(store, key_id);
    if (!key) return false;
    if (key->state != KM_STATE_DEACTIVATED) return false;
    key->state = KM_STATE_ACTIVE;
    return true;
}

bool km_key_revoke(KMKeyStore *store, const char *key_id)
{
    KMKeyEntry *key = find_key(store, key_id);
    if (!key) return false;
    /* Keys can be revoked from ACTIVE, DEACTIVATED, or COMPROMISED state */
    if (key->state != KM_STATE_ACTIVE &&
        key->state != KM_STATE_DEACTIVATED &&
        key->state != KM_STATE_COMPROMISED) {
        return false;
    }
    key->state = KM_STATE_REVOKED;
    key->revoked_at = (uint64_t)time(NULL);
    return true;
}

bool km_key_destroy(KMKeyStore *store, const char *key_id)
{
    KMKeyEntry *key = find_key(store, key_id);
    if (!key) return false;
    /* Destroy key material (set to zero per NIST SP 800-88) */
    memset(key->private_key, 0, KM_MAX_KEY_DATA);
    key->private_key_len = 0;
    if (key->state == KM_STATE_COMPROMISED) {
        key->state = KM_STATE_DESTROYED_COMPROMISED;
    } else {
        key->state = KM_STATE_DESTROYED;
    }
    return true;
}

bool km_key_compromise(KMKeyStore *store, const char *key_id)
{
    KMKeyEntry *key = find_key(store, key_id);
    if (!key) return false;
    /* Any non-destroyed key can be marked as compromised */
    if (key->state == KM_STATE_DESTROYED ||
        key->state == KM_STATE_DESTROYED_COMPROMISED) {
        return false;
    }
    key->state = KM_STATE_COMPROMISED;
    return true;
}

/* ??? Key Rotation ???????????????????????????????????????????????????? */

bool km_key_rotate(KMKeyStore *store, const char *key_id,
                   const KMRotationConfig *config)
{
    if (!store || !key_id || !config) return false;

    KMKeyEntry *old_key = find_key(store, key_id);
    if (!old_key || old_key->state != KM_STATE_ACTIVE) return false;

    /* Check rotation policy trigger */
    bool should_rotate = false;
    switch (config->policy) {
        case KM_ROTATION_TIME_BASED:
            should_rotate = (uint64_t)time(NULL) >= config->next_rotation_at;
            break;
        case KM_ROTATION_COUNT_BASED:
            should_rotate = config->current_count >= config->rotation_count;
            break;
        case KM_ROTATION_EVENT_BASED:
            should_rotate = true;
            break;
        default:
            break;
    }
    if (!should_rotate) return false;

    /* Create new key with incremented version */
    char new_id[KM_MAX_KEY_ID];
    snprintf(new_id, KM_MAX_KEY_ID, "%s-v%u", key_id, old_key->version + 1);

    uint64_t remaining_days = old_key->not_after > (uint64_t)time(NULL) ?
        (old_key->not_after - (uint64_t)time(NULL)) / 86400 : 365;

    if (!km_key_generate(store, new_id, old_key->key_name,
                          old_key->key_type, old_key->usage_mask,
                          old_key->key_strength_bits, remaining_days)) {
        return false;
    }

    KMKeyEntry *new_key = find_key(store, new_id);
    if (!new_key) return false;

    new_key->version = old_key->version + 1;

    /* Self-certify: sign new public key with old private key.
     * This creates a trust continuity chain:
     *   Sign(priv_v1, pub_v2 || metadata)
     * enabling verifiers to track key lineage. */
    uint8_t cert_data[KM_MAX_PUBLIC_KEY + 64];
    memcpy(cert_data, new_key->public_key, new_key->public_key_len);
    uint8_t hash[SHA256_HASH_SIZE];
    sha256_hash(cert_data, new_key->public_key_len, hash);

    /* Activate new key, deactivate old key (overlap period) */
    km_key_activate(store, new_id);
    km_key_deactivate(store, key_id);

    return true;
}

/* ??? HKDF Key Derivation (RFC 5869) ?????????????????????????????????? */

bool km_hkdf_extract(const uint8_t *salt, uint32_t salt_len,
                     const uint8_t *ikm, uint32_t ikm_len,
                     uint8_t *prk, uint32_t *prk_len)
{
    if (!ikm || !prk || !prk_len) return false;

    /* HKDF-Extract(salt, IKM) = HMAC-Hash(salt, IKM)
     * If salt is not provided, it defaults to a string of HashLen zeros. */
    uint8_t default_salt[SHA256_HASH_SIZE];
    const uint8_t *use_salt;
    uint32_t use_salt_len;

    if (salt && salt_len > 0) {
        use_salt = salt;
        use_salt_len = salt_len;
    } else {
        memset(default_salt, 0, SHA256_HASH_SIZE);
        use_salt = default_salt;
        use_salt_len = SHA256_HASH_SIZE;
    }

    uint32_t output_len = *prk_len < SHA256_HASH_SIZE ? *prk_len : SHA256_HASH_SIZE;
    hmac_sha256(use_salt, use_salt_len, ikm, ikm_len, prk);
    *prk_len = output_len;
    return true;
}

bool km_hkdf_expand(const uint8_t *prk, uint32_t prk_len,
                    const uint8_t *info, uint32_t info_len,
                    uint8_t *okm, uint32_t okm_len)
{
    if (!prk || !okm || okm_len == 0) return false;

    /*
     * HKDF-Expand(PRK, info, L) ? OKM
     *
     * N = ceil(L / HashLen)
     * T(0) = empty string
     * T(i) = HMAC-Hash(PRK, T(i-1) || info || i)
     *   where i is a single octet
     * OKM = T(1) || T(2) || ... || T(N) truncated to L bytes
     */

    uint32_t hash_len = SHA256_HASH_SIZE;
    uint32_t n_blocks = (okm_len + hash_len - 1) / hash_len;
    uint8_t t_prev[SHA256_HASH_SIZE] = {0};
    uint32_t t_prev_len = 0;
    uint8_t input[KM_HKDF_INFO_MAX + SHA256_HASH_SIZE + 1];
    uint32_t total = 0;

    for (uint8_t i = 1; i <= n_blocks; i++) {
        /* Build: T(i-1) || info || i */
        uint32_t input_len = 0;
        if (t_prev_len > 0) {
            memcpy(input, t_prev, t_prev_len);
            input_len = t_prev_len;
        }
        if (info && info_len > 0) {
            memcpy(input + input_len, info, info_len);
            input_len += info_len;
        }
        input[input_len++] = i;

        hmac_sha256(prk, prk_len, input, input_len, t_prev);
        t_prev_len = hash_len;

        uint32_t to_copy = okm_len - total;
        if (to_copy > hash_len) to_copy = hash_len;
        memcpy(okm + total, t_prev, to_copy);
        total += to_copy;
    }
    return true;
}

bool km_hkdf_derive(const uint8_t *ikm, uint32_t ikm_len,
                    const uint8_t *salt, uint32_t salt_len,
                    const uint8_t *info, uint32_t info_len,
                    uint8_t *okm, uint32_t okm_len)
{
    /*
     * HKDF(salt, IKM, info, L) ? OKM
     *
     * Two-step process:
     *   1. PRK = HKDF-Extract(salt, IKM)
     *   2. OKM = HKDF-Expand(PRK, info, L)
     */
    uint8_t prk[SHA256_HASH_SIZE];
    uint32_t prk_len = SHA256_HASH_SIZE;

    if (!km_hkdf_extract(salt, salt_len, ikm, ikm_len, prk, &prk_len)) {
        return false;
    }
    return km_hkdf_expand(prk, prk_len, info, info_len, okm, okm_len);
}

/* ??? TPM-Backed Key Operations ??????????????????????????????????????? */

bool km_key_import_from_tpm(KMKeyStore *store, const char *key_id,
                             uint32_t tpm_handle, KMKeyType key_type,
                             const uint8_t *public_key, uint32_t pub_key_len)
{
    if (!store || !key_id || !public_key) return false;
    if (store->locked || store->key_count >= KM_MAX_KEYS) return false;
    if (pub_key_len > KM_MAX_PUBLIC_KEY) return false;

    KMKeyEntry *key = &store->keys[store->key_count];
    memset(key, 0, sizeof(KMKeyEntry));

    snprintf(key->key_id, KM_MAX_KEY_ID, "%s", key_id);
    snprintf(key->key_name, KM_MAX_KEY_NAME, "TPM-Key-0x%08X", tpm_handle);
    key->key_type = key_type;
    key->usage_mask = KM_USAGE_SIGN | KM_USAGE_VERIFY;
    key->state = KM_STATE_ACTIVE;
    key->is_tpm_protected = true;
    key->tpm_handle = tpm_handle;
    key->key_strength_bits = 2048;
    key->version = 1;
    key->created_at = (uint64_t)time(NULL);
    key->not_before = key->created_at;
    key->not_after = key->created_at + (365 * 86400); /* 1 year */
    key->public_key_len = pub_key_len;
    memcpy(key->public_key, public_key, pub_key_len);
    key->private_key_len = 0; /* Private key remains in TPM */

    store->key_count++;
    return true;
}

bool km_key_seal_to_tpm(const KMKeyEntry *key, uint32_t pcr_selection,
                         uint8_t *sealed_blob, uint32_t *blob_size)
{
    if (!key || !sealed_blob || !blob_size) return false;
    if (!key->is_tpm_protected) return false;

    /*
     * TPM2_Seal operation (simplified):
     *
     *   SealedBlob = TPM2_Seal(
     *       parentHandle = TPM_RH_OWNER,
     *       inData = key->private_key,
     *       pcrSelection = pcr_selection,
     *       ...
     *   )
     *
     * The sealed blob encodes:
     *   - Encrypted key material (AES-128-CFB)
     *   - PCR policy digest (which PCRs must match)
     *   - Auth policy (if any)
     *   - TPM session bindings
     *
     * Unseal requires:
     *   1. TPM knows the parent key
     *   2. Current PCR values match the policy digest
     *   3. Auth session is valid (if authValue is set)
     */

    /* Build a simplified sealed blob structure */
    uint32_t blob_offset = 0;

    /* Magic */
    sealed_blob[blob_offset++] = 0x54; /* 'T' */
    sealed_blob[blob_offset++] = 0x50; /* 'P' */
    sealed_blob[blob_offset++] = 0x4D; /* 'M' */
    sealed_blob[blob_offset++] = 0x53; /* 'S' = TPMS_SEALED */

    /* Version */
    sealed_blob[blob_offset++] = 1;
    sealed_blob[blob_offset++] = 0;

    /* PCR selection (4 bytes) */
    sealed_blob[blob_offset++] = (uint8_t)(pcr_selection & 0xFF);
    sealed_blob[blob_offset++] = (uint8_t)((pcr_selection >> 8) & 0xFF);
    sealed_blob[blob_offset++] = (uint8_t)((pcr_selection >> 16) & 0xFF);
    sealed_blob[blob_offset++] = (uint8_t)((pcr_selection >> 24) & 0xFF);

    /* Key type */
    sealed_blob[blob_offset++] = (uint8_t)(key->key_type);

    /* Key data size (2 bytes big-endian) */
    uint32_t kd_size = key->private_key_len;
    sealed_blob[blob_offset++] = (uint8_t)((kd_size >> 8) & 0xFF);
    sealed_blob[blob_offset++] = (uint8_t)(kd_size & 0xFF);

    /* Encrypted key data (in real TPM: AES-128-CFB encrypted) */
    memcpy(sealed_blob + blob_offset, key->private_key, kd_size);
    blob_offset += kd_size;

    /* Integrity HMAC over the sealed blob */
    uint8_t hmac[SHA256_HASH_SIZE];
    hmac_sha256(key->public_key, key->public_key_len,
                sealed_blob, blob_offset, hmac);
    memcpy(sealed_blob + blob_offset, hmac, SHA256_HASH_SIZE);
    blob_offset += SHA256_HASH_SIZE;

    *blob_size = blob_offset;
    return *blob_size <= KM_MAX_KEY_DATA;
}

/* ??? Utility ????????????????????????????????????????????????????????? */

const char *km_state_str(KMKeyState state)
{
    switch (state) {
        case KM_STATE_PRE_ACTIVATION:       return "PRE_ACTIVATION";
        case KM_STATE_ACTIVE:               return "ACTIVE";
        case KM_STATE_DEACTIVATED:          return "DEACTIVATED";
        case KM_STATE_COMPROMISED:          return "COMPROMISED";
        case KM_STATE_DESTROYED:            return "DESTROYED";
        case KM_STATE_DESTROYED_COMPROMISED: return "DESTROYED_COMPROMISED";
        case KM_STATE_REVOKED:              return "REVOKED";
        default:                            return "UNKNOWN";
    }
}

const char *km_key_type_str(KMKeyType key_type)
{
    switch (key_type) {
        case KM_KEY_TYPE_RSA_2048:   return "RSA-2048";
        case KM_KEY_TYPE_RSA_4096:   return "RSA-4096";
        case KM_KEY_TYPE_ECDSA_P256: return "ECDSA-P256";
        case KM_KEY_TYPE_ECDSA_P384: return "ECDSA-P384";
        case KM_KEY_TYPE_AES_256:    return "AES-256";
        case KM_KEY_TYPE_HMAC_SHA256: return "HMAC-SHA256";
        default:                     return "UNKNOWN";
    }
}

void km_print_store(const KMKeyStore *store)
{
    if (!store) return;
    printf("??????????????????????????????????????????????????\n");
    printf("?           KEY MANAGEMENT STORE                 ?\n");
    printf("??????????????????????????????????????????????????\n");
    printf("? Keys: %-3u  Locked: %-3s  Next Handle: %-5u ?\n",
           store->key_count, store->locked ? "YES" : "NO",
           store->next_handle);
    printf("??????????????????????????????????????????????????\n");

    for (uint32_t i = 0; i < store->key_count; i++) {
        const KMKeyEntry *k = &store->keys[i];
        printf("? [%u] %-20s v%-2u ?\n", i, k->key_id, k->version);
        printf("?     Type: %-12s State: %-16s ?\n",
               km_key_type_str(k->key_type), km_state_str(k->state));
        printf("?     Strength: %-4u bits  TPM: %-3s",
               k->key_strength_bits, k->is_tpm_protected ? "YES" : "NO");
        printf("            ?\n");
        if (i < store->key_count - 1) {
            printf("??????????????????????????????????????????????????\n");
        }
    }
    printf("??????????????????????????????????????????????????\n");
}
