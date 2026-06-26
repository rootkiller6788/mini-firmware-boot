#ifndef BOOTBLOCK_H
#define BOOTBLOCK_H

/*
 * bootblock.h — Boot Block / ROM Stage Verification
 *
 * Implements the hardware root of trust boot flow as defined in:
 *   - Intel Boot Guard (IBB verification)
 *   - ARM TF-A BL1 (ROM-based boot)
 *   - UEFI PI SEC Phase
 *
 * Knowledge coverage:
 *   L1: BootBlock, VBootHeader, HashChain, BootPolicy struct/enum
 *   L2: Verified Boot / Hardware Root of Trust concept
 *   L3: Multi-stage verification pipeline with hash chain
 *   L4: Security theorem — collision resistance requirement for hash chain
 *   L5: Boot verification walk algorithm, secure hash chain extension
 *   L7: Measured boot, TPM PCR log generation
 *   L8: Anti-rollback counter verification
 */

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define BOOTBLOCK_MAX_STAGES     8
#define BOOTBLOCK_MAX_PCR        24
#define SHA256_DIGEST_SIZE       32
#define RSA2048_SIG_SIZE         256
#define BOOT_POLICY_MAX_RULES    16
#define MAX_KEY_FINGERPRINT      32
#define ANTI_ROLLBACK_VERSION    32

typedef enum {
    BOOT_STAGE_RESET   = 0,
    BOOT_STAGE_BOOTROM = 1,
    BOOT_STAGE_BOOTBLK = 2,
    BOOT_STAGE_PEI     = 3,
    BOOT_STAGE_DXE     = 4,
    BOOT_STAGE_BDS     = 5,
    BOOT_STAGE_OSLOAD  = 6,
    BOOT_STAGE_OS      = 7
} BootStage;

typedef enum {
    HASH_ALG_SHA1    = 1,
    HASH_ALG_SHA256  = 2,
    HASH_ALG_SHA384  = 3,
    HASH_ALG_SHA512  = 4,
    HASH_ALG_SM3     = 5
} HashAlgorithm;

typedef enum {
    SIG_ALG_RSA2048_SHA256 = 1,
    SIG_ALG_RSA3072_SHA384 = 2,
    SIG_ALG_ECDSA_P256     = 3,
    SIG_ALG_ECDSA_P384     = 4
} SignatureAlgorithm;

typedef enum {
    BOOT_RULE_REQUIRE_SIGNATURE  = 1,
    BOOT_RULE_REQUIRE_HASH       = 2,
    BOOT_RULE_ANTI_ROLLBACK      = 3,
    BOOT_RULE_REQUIRE_PCR_EXTEND = 4,
    BOOT_RULE_ALLOW_UNSIGNED     = 5
} BootPolicyRuleType;

typedef enum {
    VERIFY_OK              = 0,
    VERIFY_ERR_SIGNATURE   = 1,
    VERIFY_ERR_HASH        = 2,
    VERIFY_ERR_ROLLBACK    = 3,
    VERIFY_ERR_POLICY      = 4,
    VERIFY_ERR_KEY_REVOKED = 5,
    VERIFY_ERR_NOT_FOUND   = 6,
    VERIFY_ERR_INTERNAL    = 7
} VerifyResult;

typedef struct {
    uint8_t data[SHA256_DIGEST_SIZE];
} SHA256Digest;

typedef struct {
    uint8_t data[RSA2048_SIG_SIZE];
} RSASignature;

typedef struct {
    uint8_t  fingerprint[MAX_KEY_FINGERPRINT];
    uint32_t fingerprint_len;
    bool     revoked;
    uint32_t revocation_count;
} PublicKeyFingerprint;

typedef struct {
    uint32_t          magic;
    uint16_t          header_version;
    uint16_t          header_size;
    uint32_t          image_size;
    uint32_t          load_address;
    uint32_t          entry_point;
    uint32_t          version;
    HashAlgorithm     hash_alg;
    SignatureAlgorithm sig_alg;
    SHA256Digest      image_hash;
    RSASignature      signature;
    uint8_t           key_fingerprint[MAX_KEY_FINGERPRINT];
    uint8_t           reserved[64];
    uint32_t          header_checksum;
} VBootHeader;

typedef struct {
    BootStage    stage;
    SHA256Digest hash;
    SHA256Digest composite;
    uint32_t     version;
    VerifyResult verify_status;
    uint32_t     pcr_index;
} HashChainEntry;

typedef struct {
    uint32_t     pcr_index;
    SHA256Digest digest;
    uint32_t     event_type;
    char         event_description[64];
} PCREvent;

typedef struct {
    PCREvent    events[BOOTBLOCK_MAX_PCR];
    uint32_t    event_count;
    SHA256Digest pcr_values[BOOTBLOCK_MAX_PCR];
} PCRLog;

typedef struct {
    BootPolicyRuleType type;
    BootStage          applies_to;
    union {
        uint32_t     min_version;
        SHA256Digest expected_hash;
        uint32_t     pcr_slot;
    } config;
} BootPolicyRule;

typedef struct {
    BootPolicyRule rules[BOOT_POLICY_MAX_RULES];
    uint32_t       rule_count;
    bool           verified_boot_enabled;
    bool           measured_boot_enabled;
    uint32_t       min_rollback_version;
} BootPolicy;

typedef struct {
    HashChainEntry        chain[BOOTBLOCK_MAX_STAGES];
    uint32_t              chain_length;
    BootPolicy            policy;
    PCRLog                pcr_log;
    SHA256Digest          root_of_trust_hash;
    PublicKeyFingerprint  trusted_keys[8];
    uint32_t              num_trusted_keys;
    bool                  chain_valid;
    uint32_t              last_good_version;
} VerificationChain;

bool vb_init_chain(VerificationChain *vc, const SHA256Digest *rot_hash);
bool vb_init_policy(BootPolicy *policy, bool verified_enabled,
                    bool measured_enabled);
bool vb_add_policy_rule(BootPolicy *policy, const BootPolicyRule *rule);
bool vb_register_key(VerificationChain *vc,
                     const uint8_t *fingerprint, uint32_t len);
bool vb_revoke_key(VerificationChain *vc,
                   const uint8_t *fingerprint, uint32_t len);
bool vb_compute_image_hash(const uint8_t *image_data, uint32_t image_size,
                           SHA256Digest *out_digest);
VerifyResult vb_verify_signature(const SHA256Digest *digest,
                                 const RSASignature *sig,
                                 const PublicKeyFingerprint *key);
VerifyResult vb_validate_header(const VBootHeader *hdr, uint32_t min_version);
bool vb_extend_chain(VerificationChain *vc, BootStage stage,
                     const SHA256Digest *stage_hash, uint32_t version);
bool vb_verify_chain(VerificationChain *vc);
void vb_init_pcr_log(PCRLog *log);
bool vb_extend_pcr(PCRLog *log, uint32_t pcr_index,
                   const SHA256Digest *measurement,
                   uint32_t event_type, const char *description);
void vb_print_pcr_log(const PCRLog *log);
void vb_print_chain(const VerificationChain *vc);
bool vb_check_anti_rollback(uint32_t image_version,
                            uint32_t stored_min_version);

#endif /* BOOTBLOCK_H */
