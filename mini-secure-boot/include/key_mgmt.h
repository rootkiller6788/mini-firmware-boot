#ifndef KEY_MGMT_H
#define KEY_MGMT_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/*
 * Key Lifecycle Management Module
 *
 * Implements: NIST SP 800-57 Part 1 (Key Management Guidelines)
 * Reference:  TCG TPM 2.0 Part 2 (Key Hierarchy), RFC 5869 (HKDF)
 *
 * Knowledge coverage:
 *   L2: Key lifecycle states (Pre-activation?Active?Suspended?Revoked?Destroyed)
 *   L3: TPM-protected key hierarchy (Platform?Storage?Endorsement keys)
 *   L5: HKDF key derivation (RFC 5869), key rotation, secure key generation
 *   L7: Production key management for Secure Boot signing keys
 */

#define KM_MAX_KEYS              64
#define KM_MAX_KEY_NAME          64
#define KM_MAX_KEY_DATA          512
#define KM_MAX_KEY_ID            64
#define KM_MAX_PUBLIC_KEY        256
#define KM_HKDF_MAX_OUTPUT       128
#define KM_HKDF_SALT_SIZE        32
#define KM_HKDF_INFO_MAX         64

/* ??? Key Lifecycle States (NIST SP 800-57, Section 8) ??? */

typedef enum {
    KM_STATE_PRE_ACTIVATION = 0,  /* Key generated but not yet authorized for use */
    KM_STATE_ACTIVE,               /* Key available for cryptographic operations */
    KM_STATE_DEACTIVATED,          /* Key suspended; may be reactivated */
    KM_STATE_COMPROMISED,          /* Key suspected of compromise */
    KM_STATE_DESTROYED,            /* Key material destroyed */
    KM_STATE_DESTROYED_COMPROMISED,/* Key destroyed after confirmed compromise */
    KM_STATE_REVOKED,              /* Key revoked by issuer (certificate revoked) */
    KM_STATE_COUNT
} KMKeyState;

/* ??? Key Types ??? */

typedef enum {
    KM_KEY_TYPE_RSA_2048 = 0,      /* RSA 2048-bit signing key */
    KM_KEY_TYPE_RSA_4096,          /* RSA 4096-bit signing key */
    KM_KEY_TYPE_ECDSA_P256,        /* ECDSA P-256 */
    KM_KEY_TYPE_ECDSA_P384,        /* ECDSA P-384 */
    KM_KEY_TYPE_AES_256,           /* AES-256 symmetric key */
    KM_KEY_TYPE_HMAC_SHA256,       /* HMAC-SHA-256 key */
    KM_KEY_TYPE_COUNT
} KMKeyType;

/* ??? Key Usage ??? */

typedef enum {
    KM_USAGE_SIGN        = 0x0001,  /* Digital signature */
    KM_USAGE_VERIFY      = 0x0002,  /* Signature verification */
    KM_USAGE_ENCRYPT     = 0x0004,  /* Encryption */
    KM_USAGE_DECRYPT     = 0x0008,  /* Decryption */
    KM_USAGE_KEY_AGREE   = 0x0010,  /* Key agreement (ECDH) */
    KM_USAGE_KEY_WRAP    = 0x0020,  /* Key wrapping */
    KM_USAGE_DERIVE      = 0x0040,  /* Key derivation */
    KM_USAGE_ANY         = 0xFFFF
} KMKeyUsage;

/* ??? Key Entry ??? */

typedef struct {
    char          key_id[KM_MAX_KEY_ID];     /* Unique identifier */
    char          key_name[KM_MAX_KEY_NAME]; /* Human-readable name */
    KMKeyType     key_type;
    KMKeyUsage    usage_mask;
    KMKeyState    state;
    uint8_t       public_key[KM_MAX_PUBLIC_KEY];
    uint32_t      public_key_len;
    uint8_t       private_key[KM_MAX_KEY_DATA];
    uint32_t      private_key_len;
    uint64_t      not_before;               /* Unix timestamp */
    uint64_t      not_after;                /* Unix timestamp (expiry) */
    uint64_t      created_at;
    uint64_t      revoked_at;
    uint32_t      key_strength_bits;
    uint32_t      version;                  /* Key version for rotation */
    bool          is_tpm_protected;
    uint32_t      tpm_handle;               /* TPM key handle if TPM-protected */
} KMKeyEntry;

/* ??? Key Store ??? */

typedef struct {
    KMKeyEntry    keys[KM_MAX_KEYS];
    uint32_t      key_count;
    uint32_t      next_handle;              /* Monotonic handle allocator */
    bool          locked;                   /* Key store locked for modification */
} KMKeyStore;

/* ??? Key Rotation Policy ??? */

typedef enum {
    KM_ROTATION_NONE = 0,      /* No rotation */
    KM_ROTATION_TIME_BASED,    /* Rotate after a fixed period */
    KM_ROTATION_COUNT_BASED,   /* Rotate after N uses */
    KM_ROTATION_EVENT_BASED    /* Rotate on security event */
} KMRotationPolicy;

typedef struct {
    KMRotationPolicy policy;
    uint64_t         rotation_period_sec;   /* For time-based: seconds */
    uint32_t         rotation_count;         /* For count-based: max uses */
    uint32_t         current_count;          /* Current usage count */
    uint64_t         next_rotation_at;       /* Next rotation timestamp */
} KMRotationConfig;

/* ??? Key Store Operations ??? */

bool km_store_init(KMKeyStore *store);
bool km_store_lock(KMKeyStore *store);
bool km_store_unlock(KMKeyStore *store);

/* ??? Key Lifecycle Operations ??? */

bool km_key_generate(KMKeyStore *store, const char *key_id, const char *name,
                     KMKeyType key_type, KMKeyUsage usage, uint32_t bits,
                     uint64_t valid_days);
bool km_key_activate(KMKeyStore *store, const char *key_id);
bool km_key_deactivate(KMKeyStore *store, const char *key_id);
bool km_key_reactivate(KMKeyStore *store, const char *key_id);
bool km_key_revoke(KMKeyStore *store, const char *key_id);
bool km_key_destroy(KMKeyStore *store, const char *key_id);
bool km_key_compromise(KMKeyStore *store, const char *key_id);

/* ??? Key Query ??? */

const KMKeyEntry *km_key_find(const KMKeyStore *store, const char *key_id);
const KMKeyEntry *km_key_find_active(const KMKeyStore *store, KMKeyType key_type);
bool km_key_is_valid(const KMKeyEntry *key, uint64_t current_time);
uint32_t km_key_list_by_state(const KMKeyStore *store, KMKeyState state,
                               const KMKeyEntry **results, uint32_t max_results);

/* ??? Key Rotation ??? */

bool km_key_rotate(KMKeyStore *store, const char *key_id,
                   const KMRotationConfig *config);

/*
 * Key Rotation Algorithm:
 *   1. Generate a new key pair (version N+1)
 *   2. Sign the new public key with the old private key (self-certification)
 *   3. Activate the new key (parallel run period)
 *   4. Deactivate the old key
 *   5. After a grace period, destroy the old key
 *
 * Corollary (NIST SP 800-57, Section 8.3.5):
 *   TBK_SB(n+1) = Sign(priv_n, pub_{n+1} || metadata)
 */

/* ??? HKDF Key Derivation (RFC 5869) ??? */

bool km_hkdf_extract(const uint8_t *salt, uint32_t salt_len,
                     const uint8_t *ikm, uint32_t ikm_len,
                     uint8_t *prk, uint32_t *prk_len);
bool km_hkdf_expand(const uint8_t *prk, uint32_t prk_len,
                    const uint8_t *info, uint32_t info_len,
                    uint8_t *okm, uint32_t okm_len);
bool km_hkdf_derive(const uint8_t *ikm, uint32_t ikm_len,
                    const uint8_t *salt, uint32_t salt_len,
                    const uint8_t *info, uint32_t info_len,
                    uint8_t *okm, uint32_t okm_len);

/*
 * HKDF (HMAC-based Key Derivation Function):
 *   RFC 5869 specifies a two-phase key derivation:
 *     Extract:  PRK = HMAC-Hash(salt, IKM)
 *       - Concentrates entropy from IKM into a fixed-size PRK
 *       - Salt is optional but recommended for security
 *     Expand:   OKM = HMAC-Hash(PRK, info || 0x01) ||
 *                     HMAC-Hash(PRK, previous || info || 0x02) || ...
 *       - Expands the PRK into arbitrary-length output key material
 *       - "info" binds the derived key to a specific context
 */

/* ??? TPM-Backed Key Operations ??? */

bool km_key_import_from_tpm(KMKeyStore *store, const char *key_id,
                             uint32_t tpm_handle, KMKeyType key_type,
                             const uint8_t *public_key, uint32_t pub_key_len);
bool km_key_seal_to_tpm(const KMKeyEntry *key, uint32_t pcr_selection,
                         uint8_t *sealed_blob, uint32_t *blob_size);

/*
 * TPM Key Sealing:
 *   Binds a key to a specific PCR state. The key can only be
 *   unsealed (released) when the PCRs match the specified values.
 *   This ensures the key is only available in a trusted boot state.
 *
 *   sealed_data = TPM2_Seal(handle, parent_key, data, pcr_selection)
 *   data = TPM2_Unseal(handle, parent_key, sealed_data, pcr_selection)
 */

/* ??? Utility ??? */

const char *km_state_str(KMKeyState state);
const char *km_key_type_str(KMKeyType key_type);
void km_print_store(const KMKeyStore *store);

#endif /* KEY_MGMT_H */
