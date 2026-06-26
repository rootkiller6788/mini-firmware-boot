#ifndef TPM2_STRUCTS_H
#define TPM2_STRUCTS_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define SHA1_DIGEST_SIZE    20
#define SHA256_DIGEST_SIZE  32
#define SHA384_DIGEST_SIZE  48
#define SHA512_DIGEST_SIZE  64
#define TPM2B_MAX_BUFFER    1024
#define PCR_COUNT           24
#define PCR_SELECT_MAX      8

typedef enum {
    TPM_ALG_RSA      = 0x0001,
    TPM_ALG_SHA1     = 0x0004,
    TPM_ALG_HMAC     = 0x0005,
    TPM_ALG_AES      = 0x0006,
    TPM_ALG_MGF1     = 0x0007,
    TPM_ALG_KEYEDHASH = 0x0008,
    TPM_ALG_XOR      = 0x000A,
    TPM_ALG_SHA256   = 0x000B,
    TPM_ALG_SHA384   = 0x000C,
    TPM_ALG_SHA512   = 0x000D,
    TPM_ALG_NULL     = 0x0010,
    TPM_ALG_RSASSA   = 0x0014,
    TPM_ALG_RSAPSS   = 0x0016,
    TPM_ALG_OAEP     = 0x0017,
    TPM_ALG_ECDSA    = 0x0018,
    TPM_ALG_ECDH     = 0x0019,
    TPM_ALG_ECDAA    = 0x001A,
    TPM_ALG_ECC      = 0x0023,
} TPM2_ALG_ID;

typedef uint16_t TPMI_ALG_HASH;

typedef enum {
    TPM_ST_ATTEST_QUOTE    = 0x8018,
    TPM_ST_ATTEST_CREATION = 0x8012,
    TPM_ST_ATTEST_NV       = 0x8014,
    TPM_ST_ATTEST_TIME     = 0x8016,
} TPM_ST;

typedef enum {
    TPM_HANDLE_OWNER       = 0x40000001,
    TPM_HANDLE_ENDORSEMENT = 0x4000000B,
    TPM_HANDLE_LOCKOUT     = 0x4000000A,
    TPM_HANDLE_PLATFORM    = 0x4000000C,
    TPM_HANDLE_NV_INDEX    = 0x01000000,
} TPM2_HANDLE;

typedef enum {
    TPM_RC_SUCCESS          = 0x000,
    TPM_RC_BAD_TAG          = 0x01E,
    TPM_RC_INITIALIZE       = 0x100,
    TPM_RC_FAILURE          = 0x101,
    TPM_RC_SEQUENCE         = 0x103,
    TPM_RC_VALUE            = 0x084,
    TPM_RC_PCR_CHANGED      = 0x128,
    TPM_RC_BAD_AUTH         = 0x12F,
    TPM_RC_LOCKOUT          = 0x138,
    TPM_RC_PCR              = 0x152,
} TPM_RC;

typedef struct {
    uint16_t size;
    uint8_t  buffer[TPM2B_MAX_BUFFER];
} TPM2B;

typedef struct {
    uint16_t     hash_alg;
    uint8_t      sizeofSelect;
    uint8_t      pcrSelect[PCR_SELECT_MAX];
} TPMS_PCR_SELECTION;

typedef struct {
    uint32_t            count;
    TPMS_PCR_SELECTION  selections[4];
} TPML_PCR_SELECTION;

typedef struct {
    TPM_ST          type;
    TPM2B           qualified_signer;
    TPM2B           extra_data;
    TPML_PCR_SELECTION pcr_select;
    uint8_t         pcr_digest[32];
} TPMS_ATTEST;

typedef struct {
    uint16_t        hash_alg;
    uint8_t         digest[64];
} TPMT_HA;

typedef struct {
    uint32_t        handle;
    TPMT_HA         name;
    TPM2B           qualified_name;
} TPM_HANDLE_CTX;

typedef struct {
    uint16_t tag;
    uint32_t size;
    uint32_t command_code;
} TPM_CMD_HEADER;

typedef struct {
    uint16_t tag;
    uint32_t size;
    uint32_t response_code;
} TPM_RSP_HEADER;

const char* tpm2_alg_id_to_string(TPM2_ALG_ID alg);
const char* tpm2_st_to_string(TPM_ST st);
const char* tpm2_rc_to_string(TPM_RC rc);
uint16_t    tpm2_hash_size(TPMI_ALG_HASH alg);

#endif
