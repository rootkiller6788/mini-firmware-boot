#ifndef TPM_QUOTE_H
#define TPM_QUOTE_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define TPM_GENERATED_VALUE      0xFF544347
#define TPM_ST_ATTEST_QUOTE      0x8018
#define TPM_MAX_PCRS             24
#define TPM_SHA256_DIGEST_SIZE   32
#define TPM_MAX_SIG_SIZE         256
#define TPM_NONCE_SIZE           32
#define TPM_CLOCK_INFO_SIZE      16
#define TPM_FIRMWARE_VERSION_SIZE 8
#define TPM_PCR_SELECT_SIZE      4
#define TPM_EXTRA_DATA_SIZE      32

#define TPM_ALG_SHA256           0x000B
#define TPM_ALG_RSASSA           0x0014
#define TPM_ALG_NULL             0x0010

typedef struct {
    uint8_t  digest[TPM_SHA256_DIGEST_SIZE];
} TPMHash;

typedef struct {
    uint16_t  algorithm_id;
    uint8_t   size_of_select;
    uint8_t   pcr_select[TPM_PCR_SELECT_SIZE];
} TPMPcrSelection;

typedef struct {
    uint32_t  count;
    uint8_t   pcr_count;
    TPMPcrSelection pcr_selections[4];
    TPMHash   pcr_digests[TPM_MAX_PCRS];
} TPMPcrComposite;

typedef struct {
    uint32_t    magic;
    uint16_t    type;
    uint8_t     qualified_signer[TPM_SHA256_DIGEST_SIZE];
    uint8_t     extra_data[TPM_EXTRA_DATA_SIZE];
    uint8_t     clock_info[TPM_CLOCK_INFO_SIZE];
    uint64_t    firmware_version;
    TPMPcrSelection pcr_select;
    TPMHash     pcr_digest;
} TPMAttest;

typedef struct {
    TPMAttest         attest;
    TPMPcrComposite   pcr_composite;
    uint8_t           signature[TPM_MAX_SIG_SIZE];
    uint16_t          signature_size;
} TPMQuote;

typedef struct {
    uint8_t  modulus[256];
    uint8_t  exponent[3];
    uint16_t modulus_size;
} TPMKey;

int32_t  tpm_quote_create(TPMQuote *quote,
                          const TPMPcrComposite *pcr_composite,
                          const uint8_t *nonce, uint32_t nonce_size,
                          const uint8_t *qualifying_data, uint32_t qdata_size,
                          uint64_t firmware_version);

int32_t  tpm_quote_sign(TPMQuote *quote, const TPMKey *aik);

int32_t  tpm_quote_verify(const TPMQuote *quote,
                          const TPMKey *aik_pub,
                          const TPMPcrComposite *expected_pcr,
                          uint64_t expected_fw_version,
                          bool *result);

int32_t  tpm_certify(const TPMKey *certifying_key,
                     const TPMAttest *attest,
                     uint8_t *signature_out, uint16_t *sig_size);

void     tpm_hash_init(TPMHash *hash);
void     tpm_hash_update(TPMHash *hash, const uint8_t *data, size_t len);
void     tpm_hash_final(TPMHash *hash);

void     tpm_pcr_composite_init(TPMPcrComposite *comp);
int32_t  tpm_pcr_composite_add(TPMPcrComposite *comp,
                               uint8_t pcr_index, const TPMHash *value);
int32_t  tpm_pcr_composite_get(const TPMPcrComposite *comp,
                               uint8_t pcr_index, TPMHash *value);
void     tpm_pcr_composite_hash(const TPMPcrComposite *comp, TPMHash *out);

void     tpm_quote_dump(const TPMQuote *quote);

#endif
