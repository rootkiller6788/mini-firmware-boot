#ifndef AIK_IDENTITY_H
#define AIK_IDENTITY_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define AIK_KEY_SIZE          256
#define AIK_CREDENTIAL_SIZE   512
#define AIK_CERT_SIZE         1024
#define AIK_MAX_CHAIN_SIZE    4
#define EK_PUB_HASH_SIZE      32
#define PRIVACY_CA_ID_SIZE    32
#define AIK_LABEL_SIZE        64

typedef enum {
    TPM_KEY_HIERARCHY_EK  = 0,
    TPM_KEY_HIERARCHY_SRK = 1,
    TPM_KEY_HIERARCHY_AIK = 2
} TPMKeyHierarchy;

typedef struct {
    uint8_t  modulus[AIK_KEY_SIZE];
    uint8_t  exponent[3];
    uint16_t modulus_size;
    uint8_t  ek_pub_hash[EK_PUB_HASH_SIZE];
    uint8_t  manufacturer_ca_id[32];
    uint8_t  serial_number[16];
    uint8_t  signature[AIK_CERT_SIZE / 2];
    uint16_t signature_size;
} EKCertificate;

typedef struct {
    uint8_t  aik_pub_modulus[AIK_KEY_SIZE];
    uint16_t aik_pub_modulus_size;
    uint8_t  aik_pub_exponent[3];
    uint8_t  privacy_ca_id[PRIVACY_CA_ID_SIZE];
    uint8_t  aik_name[AIK_LABEL_SIZE];
    uint32_t issue_date;
    uint32_t expiry_date;
    uint8_t  serial_number[16];
    uint8_t  signature[AIK_CERT_SIZE / 2];
    uint16_t signature_size;
    bool     active;
} AIKCredential;

typedef struct {
    uint8_t  data[AIK_CREDENTIAL_SIZE];
    uint16_t size;
} AIKEncryptedCredential;

typedef struct {
    uint8_t  aik_pub_modulus[AIK_KEY_SIZE];
    uint16_t aik_pub_modulus_size;
    uint8_t  aik_priv_modulus[AIK_KEY_SIZE];
    uint8_t  aik_pub_exponent[3];
    uint8_t  aik_priv_exponent[AIK_KEY_SIZE];
    uint8_t  aik_name[AIK_LABEL_SIZE];
    TPMKeyHierarchy parent;
} TPMKeyAIK;

typedef struct {
    uint8_t  modulus[AIK_KEY_SIZE];
    uint8_t  exponent[3];
    uint16_t modulus_size;
    TPMKeyHierarchy hierarchy;
} TPMKeyPublic;

int32_t  tpm_create_ek(TPMKeyPublic *ek_pub);

int32_t  tpm_create_aik(TPMKeyAIK *aik,
                        const TPMKeyPublic *srk,
                        const TPMKeyPublic *ek_pub);

int32_t  tpm_aik_certify(AIKCredential *credential,
                         const TPMKeyPublic *aik_pub,
                         const TPMKeyPublic *ek_pub,
                         const EKCertificate *ek_cert,
                         const uint8_t *privacy_ca_id);

int32_t  tpm_make_credential(AIKEncryptedCredential *enc_cred,
                             const AIKCredential *credential,
                             const TPMKeyPublic *ek_pub);

int32_t  tpm_activate_credential(AIKCredential *credential_out,
                                 const AIKEncryptedCredential *enc_cred,
                                 const TPMKeyAIK *aik);

int32_t  tpm_verify_ek_certificate(const EKCertificate *cert,
                                   const TPMKeyPublic *ek_pub,
                                   bool *result);

int32_t  tpm_verify_aik_credential(const AIKCredential *credential,
                                   const TPMKeyPublic *aik_pub,
                                   const uint8_t *privacy_ca_pub,
                                   bool *result);

int32_t  tpm_get_ek_pub_hash(const TPMKeyPublic *ek_pub,
                             uint8_t *hash_out);

void     aik_credential_dump(const AIKCredential *credential);

#endif
