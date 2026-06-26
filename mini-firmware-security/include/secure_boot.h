#ifndef SECURE_BOOT_H
#define SECURE_BOOT_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/*
 * SECURE_BOOT: UEFI Secure Boot Implementation
 * Ref: UEFI Spec v2.10 32.4, PKCS#1 v2.2 (RFC 8017)
 *
 * Knowledge:
 *   L1: EFI_SIGNATURE_LIST, EFI_SIGNATURE_DATA, PK/KEK/db/dbx models
 *   L2: Chain of trust PK->KEK->db->authorized image
 *   L3: Signature database enrollment pipeline
 *   L4: UEFI Spec 7.8, PKCS#1 v2.2, RSA-PSS (Bellare-Rogaway 1996)
 *   L5: RSA-2048/3072, SHA-256/384 (FIPS 180-4), X.509 minimal parser
 *   L7: Authenticode PE/COFF verification, Enterprise PKI
 */

#define SB_SIGNATURE_TYPE_SHA256      0x0001
#define SB_SIGNATURE_TYPE_RSA2048     0x0002
#define SB_SIGNATURE_TYPE_RSA3072     0x0003
#define SB_SIGNATURE_TYPE_X509        0x0004
#define SB_SIGNATURE_TYPE_SHA384      0x0005
#define SB_SIGNATURE_TYPE_SHA512      0x0006

#define SB_HASH_SIZE_SHA256           32
#define SB_HASH_SIZE_SHA384           48
#define SB_HASH_SIZE_SHA512           64
#define SB_MAX_CERTIFICATES           8
#define SB_MAX_SIGNATURES             16
#define SB_KEY_SIZE_RSA2048           256
#define SB_KEY_SIZE_RSA3072           384
#define SB_SIGNATURE_SIZE_RSA2048     256
#define SB_SIGNATURE_SIZE_RSA3072     384

#define SB_MODE_SETUP                 0
#define SB_MODE_USER                  1
#define SB_MODE_AUDIT                 2
#define SB_MODE_DEPLOYED              3

#define SB_DB_TYPE_PK                 0
#define SB_DB_TYPE_KEK                1
#define SB_DB_TYPE_DB                 2
#define SB_DB_TYPE_DBX                3
#define SB_DB_TYPE_DBT                4
#define SB_DB_TYPE_DBR                5

#define WIN_CERT_TYPE_PKCS            0x0001
#define WIN_CERT_TYPE_EFI_GUID        0x00F1
#define WIN_CERT_TYPE_EFI_PKCS115     0x00F0
#define PE_CERT_TYPE_X509             0x0002
#define PE_CERT_TYPE_PKCS7            0x0003

typedef struct {
    uint32_t data1;
    uint16_t data2;
    uint16_t data3;
    uint8_t  data4[8];
} EFI_GUID;

typedef struct {
    EFI_GUID signature_type;
    uint32_t signature_list_size;
    uint32_t signature_header_size;
    uint32_t signature_size;
} EFI_SIGNATURE_LIST;

typedef struct {
    EFI_GUID signature_owner;
    uint8_t  signature_data[SB_SIGNATURE_SIZE_RSA2048];
    uint32_t signature_data_size;
} EFI_SIGNATURE_DATA;

typedef struct {
    uint32_t hash_algorithm;
    uint8_t  signature[SB_SIGNATURE_SIZE_RSA3072];
    uint32_t signature_size;
    uint8_t  public_key_exponent[4];
    uint8_t  public_key_modulus[SB_KEY_SIZE_RSA3072];
    uint32_t modulus_size;
} WIN_CERTIFICATE_EFI_PKCS115;

typedef struct {
    uint8_t  modulus[SB_KEY_SIZE_RSA3072];
    uint32_t modulus_size;
    uint32_t public_exponent;
} RSAPublicKey;

typedef struct {
    uint32_t state[8];
    uint64_t bit_count;
    uint8_t  buffer[64];
    size_t   buffer_index;
} SHA256Context;

typedef struct {
    uint64_t state[8];
    uint64_t bit_count_high;
    uint64_t bit_count_low;
    uint8_t  buffer[128];
    size_t   buffer_index;
} SHA384Context;

typedef struct {
    uint8_t  db_type;
    EFI_GUID owner_guid;
    uint8_t  signature_owner_guid[16];
    uint8_t  cert_data[512];
    uint32_t cert_size;
    uint8_t  cert_type;
    bool     revoked;
    uint32_t revocation_time;
} SignatureDBEntry;

typedef struct {
    SignatureDBEntry pk;
    SignatureDBEntry kek[SB_MAX_CERTIFICATES];
    SignatureDBEntry db[SB_MAX_SIGNATURES];
    SignatureDBEntry dbx[SB_MAX_SIGNATURES];
    SignatureDBEntry dbt[SB_MAX_SIGNATURES];
    uint8_t  kek_count;
    uint8_t  db_count;
    uint8_t  dbx_count;
    uint8_t  dbt_count;
    uint8_t  operating_mode;
    bool     secure_boot_enabled;
    bool     setup_mode;
    bool     audit_mode;
    bool     deployed_mode;
} SecureBootPolicy;

typedef struct {
    uint8_t  image_hash[SB_HASH_SIZE_SHA256];
    uint8_t  image_hash_algorithm;
    uint8_t  image_signature[SB_SIGNATURE_SIZE_RSA3072];
    uint32_t image_signature_size;
    bool     verified;
    bool     revoked;
    bool     deferred_exec;
} SBImageContext;

void     sb_init(SecureBootPolicy *sb);
bool     sb_enroll_pk(SecureBootPolicy *sb, const EFI_GUID *owner,
                      const uint8_t *cert, uint32_t cert_size);
bool     sb_enroll_kek(SecureBootPolicy *sb, const EFI_GUID *owner,
                      const uint8_t *cert, uint32_t cert_size);
bool     sb_enroll_db(SecureBootPolicy *sb, const EFI_GUID *owner,
                     const uint8_t *signature, uint32_t sig_size);
bool     sb_enroll_dbx(SecureBootPolicy *sb, const EFI_GUID *owner,
                      const uint8_t *digest, uint32_t digest_size);
bool     sb_delete_pk(SecureBootPolicy *sb);
bool     sb_delete_kek(SecureBootPolicy *sb, uint8_t index);
bool     sb_query_db(SecureBootPolicy *sb, const uint8_t *signature,
                    uint32_t sig_size);
bool     sb_query_dbx(SecureBootPolicy *sb, const uint8_t *digest,
                     uint32_t digest_size);
bool     sb_verify_image(SecureBootPolicy *sb,
                         const uint8_t *image, size_t image_size,
                         const uint8_t *signature, size_t sig_size,
                         SBImageContext *ctx);
bool     sb_verify_with_authenticode(SecureBootPolicy *sb,
                                     const uint8_t *pe_image, size_t image_size);
bool     sb_rsa_verify_pkcs1_v15(const RSAPublicKey *key,
                                  const uint8_t *hash,
                                  uint32_t hash_size,
                                  const uint8_t *signature,
                                  uint32_t sig_size);
bool     sb_rsa_verify_pss(const RSAPublicKey *key,
                            const uint8_t *hash,
                            uint32_t hash_size,
                            const uint8_t *signature,
                            uint32_t sig_size);
void     sha256_init(SHA256Context *ctx);
void     sha256_update(SHA256Context *ctx, const uint8_t *data, size_t len);
void     sha256_final(SHA256Context *ctx, uint8_t *digest);
void     sha256_hash(const uint8_t *data, size_t len, uint8_t *digest);
void     sha384_init(SHA384Context *ctx);
void     sha384_update(SHA384Context *ctx, const uint8_t *data, size_t len);
void     sha384_final(SHA384Context *ctx, uint8_t *digest);
bool     sb_x509_extract_public_key(const uint8_t *cert, size_t cert_size,
                                     RSAPublicKey *key);
bool     sb_x509_validate_chain(const uint8_t **certs, size_t *cert_sizes,
                                 uint8_t count);
void     sb_clear_secure_boot_keys(SecureBootPolicy *sb);
bool     sb_export_public_key(const RSAPublicKey *key,
                              uint8_t *modulus_out, uint32_t *modulus_size,
                              uint32_t *exponent_out);
bool     sb_check_setup_mode(SecureBootPolicy *sb);
bool     sb_check_deployed_mode(SecureBootPolicy *sb);
bool     sb_transition_to_user_mode(SecureBootPolicy *sb);
bool     sb_transition_to_deployed_mode(SecureBootPolicy *sb);

#endif
