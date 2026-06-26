/* TPM 2.0 Key Management ? Key hierarchy, creation, loading, sealing.
 * Reference: TPM 2.0 Part 1 section 12 (Key Hierarchy)
 *            TPM 2.0 Part 3 section 12 (TPM2_CreatePrimary)
 *
 * Knowledge coverage:
 *   L1: Key object structures (public, private, sensitive areas)
 *   L2: Four-tier key hierarchy (endorsement, storage, platform, null)
 *   L3: Key creation process and loading/unloading lifecycle
 *   L4: Key attributes and usage restrictions
 *   L5: KDFa key derivation (SP800-108 counter mode)
 *   L5: Key sealing/unsealing with PCR policy binding
 *   L6: Primary key creation and certification flow
 */
#ifndef TPM_KEYS_H
#define TPM_KEYS_H

#include <stdbool.h>
#include <stdint.h>
#include "tpm2_structs.h"

#define TPM_MAX_KEY_SIZE      512
#define TPM_MAX_KEY_NAME      64
#define TPM_MAX_LOADED_KEYS   16
#define KDFA_MAX_OUTPUT       64
#define KDFA_LABEL_MAX        32

/* L1: Key types per TPM 2.0 Part 2 section 12 */
typedef enum {
    TPM_KEYTYPE_RSA_2048    = 0x0001,
    TPM_KEYTYPE_RSA_3072    = 0x0002,
    TPM_KEYTYPE_ECC_NIST256 = 0x0010,
    TPM_KEYTYPE_ECC_NIST384 = 0x0011,
    TPM_KEYTYPE_SYMCIPHER   = 0x0020,
    TPM_KEYTYPE_KEYEDHASH   = 0x0021,
} TPMKeyType;

/* L1: Key usage restrictions per TPM 2.0 Part 2 table 25 */
typedef enum {
    TPM_KEYATTR_SIGN         = 0x0001,
    TPM_KEYATTR_DECRYPT      = 0x0002,
    TPM_KEYATTR_RESTRICTED   = 0x0010,
    TPM_KEYATTR_FIXEDTPM     = 0x0020,
    TPM_KEYATTR_FIXEDPARENT  = 0x0040,
    TPM_KEYATTR_SENSITIVEDATAORIGIN = 0x0080,
    TPM_KEYATTR_USERWITHAUTH = 0x0100,
    TPM_KEYATTR_ADMINWITHPOLICY = 0x0200,
    TPM_KEYATTR_NODA         = 0x0400,
    TPM_KEYATTR_ENCRYPTEDDUPLICATION = 0x0800,
} TPMKeyAttr;

/* L2: Key hierarchy identifiers */
typedef enum {
    TPM_HIERARCHY_NULL        = 0x40000007,
    TPM_HIERARCHY_OWNER       = 0x40000001,
    TPM_HIERARCHY_ENDORSEMENT = 0x4000000B,
    TPM_HIERARCHY_PLATFORM    = 0x4000000C,
} TPMHierarchy;

/* L1: Key public area (TPMT_PUBLIC analog) */
typedef struct {
    TPMKeyType      key_type;
    uint32_t        attributes;
    uint8_t         name_hash[SHA256_DIGEST_SIZE];
    uint8_t         public_data[256];
    uint16_t        public_data_size;
    uint8_t         auth_policy[SHA256_DIGEST_SIZE];
    bool            auth_policy_set;
    uint16_t        hash_alg;
    uint8_t         unique[SHA256_DIGEST_SIZE];
} TPMKeyPublic;

/* L1: Key sensitive area (TPMT_SENSITIVE analog) */
typedef struct {
    uint8_t   auth_value[32];
    uint16_t  auth_value_size;
    uint8_t   seed_value[SHA256_DIGEST_SIZE];
    uint8_t   private_data[256];
    uint16_t  private_data_size;
} TPMKeySensitive;

/* L1: Loaded key object */
typedef struct {
    uint32_t         handle;
    uint32_t         parent_handle;
    TPMKeyPublic     public_part;
    TPMKeySensitive  sensitive;
    TPMHierarchy     hierarchy;
    bool             loaded;
    bool             is_primary;
    uint8_t          name[SHA256_DIGEST_SIZE];
} TPMKeyObject;

/* L3: Key manager ? stores and manages loaded keys */
typedef struct {
    TPMKeyObject   keys[TPM_MAX_LOADED_KEYS];
    uint32_t       key_count;
    uint8_t        primary_seed_storage[SHA256_DIGEST_SIZE];
    uint8_t        primary_seed_endorsement[SHA256_DIGEST_SIZE];
    uint8_t        primary_seed_platform[SHA256_DIGEST_SIZE];
    bool           seeds_generated;
} TPMKeyManager;

/* L5: KDFa context for SP800-108 counter-mode key derivation */
typedef struct {
    const uint8_t* key;
    uint32_t       key_len;
    const uint8_t* label;
    uint32_t       label_len;
    const uint8_t* context_u;
    uint32_t       context_u_len;
    const uint8_t* context_v;
    uint32_t       context_v_len;
    uint16_t       hash_alg;
    uint32_t       bits;
} KDFaParams;

void     tpm_key_manager_init(TPMKeyManager* mgr);
bool     tpm_generate_primary_seeds(TPMKeyManager* mgr);

/* L5: Key derivation function KDFa (SP800-108 counter mode, HMAC-SHA256).
 * Used for key derivation from primary seeds and session key generation.
 * Theorem: KDFa is a PRF under the assumption that HMAC-SHA256 is a PRF. */
bool     tpm_kdfa(const KDFaParams* params, uint8_t* output, uint32_t output_len);

/* L3: Primary key creation (EK, SRK, platform key) */
bool     tpm_create_primary(TPMKeyManager* mgr,
                            TPMHierarchy hierarchy,
                            TPMKeyType key_type,
                            uint32_t attributes,
                            const uint8_t* auth_value, uint16_t auth_size,
                            const uint8_t* policy,
                            uint32_t* out_handle);

/* L3: Key loading and unloading */
bool     tpm_load_key(TPMKeyManager* mgr, uint32_t parent_handle,
                      const TPMKeyPublic* public_part,
                      const TPMKeySensitive* sensitive,
                      uint32_t* out_handle);
bool     tpm_flush_key(TPMKeyManager* mgr, uint32_t handle);

/* L5: Key sealing ? bind data to PCR state.
 * Creates a data blob that can only be decrypted when PCR values
 * match the specified policy. Based on Merkle-Damgard chaining
 * property of SHA-256 for PCR composite hashing. */
bool     tpm_seal(TPMKeyManager* mgr, uint32_t parent_handle,
                  const uint8_t* data, uint16_t data_len,
                  const uint8_t* pcr_selection, uint32_t pcr_count,
                  uint8_t* sealed_blob, uint32_t* blob_len);

/* L5: Key unsealing ? recover sealed data when PCR policy matches.
 * Verifies PCR composite hash against policy, then decrypts. */
bool     tpm_unseal(TPMKeyManager* mgr, uint32_t parent_handle,
                    const uint8_t* sealed_blob, uint32_t blob_len,
                    const uint8_t* current_pcrs, uint32_t pcr_count,
                    uint8_t* data_out, uint16_t* data_len);

/* L4: Key attribute validation */
bool     tpm_validate_key_attrs(uint32_t attributes, TPMKeyType key_type);
const char* tpm_key_type_to_string(TPMKeyType t);
const char* tpm_hierarchy_to_string(TPMHierarchy h);
void     tpm_key_manager_print(const TPMKeyManager* mgr);

#endif
