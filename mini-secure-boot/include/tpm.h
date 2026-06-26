#ifndef TPM_H
#define TPM_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/*
 * TPM 2.0 Core Module
 *
 * Implements: TCG TPM 2.0 Library Specification (Family "2.0", Level 00, Revision 01.59)
 * Reference:  TCG PC Client Platform TPM Profile (PTP) Specification
 *
 * Knowledge coverage:
 *   L2: Platform Configuration Registers (PCRs) ? tamper-resistant measurement storage
 *   L3: TPM hierarchy architecture (Platform, Storage, Endorsement)
 *   L4: TCG PC Client Platform TPM Profile, TPM 2.0 Library Specification
 *   L5: PCR extend algorithm (SHA-256), quote signing, attestation verification
 *   L8: Remote attestation ? proving system integrity to remote verifier
 */

#define TPM_SHA256_DIGEST_SIZE   32
#define TPM_MAX_PCR_BANKS        5
#define TPM_MAX_PCRS             24
#define TPM_MAX_NV_INDICES       32
#define TPM_MAX_NV_DATA_SIZE     1024
#define TPM_MAX_SESSIONS         3
#define TPM_MAX_NAME_LEN         64
#define TPM_NONCE_SIZE           32
#define TPM_MAX_ATTEST_DATA      512

/* ??? TPM 2.0 Constants from TCG Specification ??? */

/* Algorithm IDs (TPM_ALG_ID) */
typedef enum {
    TPM_ALG_SHA1     = 0x0004,
    TPM_ALG_SHA256   = 0x000B,
    TPM_ALG_SHA384   = 0x000C,
    TPM_ALG_SHA512   = 0x000D,
    TPM_ALG_RSASSA   = 0x0014,
    TPM_ALG_RSAPSS   = 0x0016,
    TPM_ALG_ECDSA    = 0x0018,
    TPM_ALG_ECDH     = 0x0019,
    TPM_ALG_ECDAA    = 0x001A,
    TPM_ALG_KEYEDHASH = 0x0008,
    TPM_ALG_NULL     = 0x0010
} TPM_ALG_ID;

/* Hierarchy handles (TPM_HANDLE) */
#define TPM_RH_PLATFORM     0x4000000C
#define TPM_RH_OWNER        0x40000001
#define TPM_RH_ENDORSEMENT  0x4000000B
#define TPM_RH_NULL         0x40000007

/* NV Index attributes */
#define TPMA_NV_PPWRITE        0x00000001
#define TPMA_NV_OWNERWRITE     0x00000002
#define TPMA_NV_AUTHWRITE      0x00000004
#define TPMA_NV_POLICYWRITE    0x00000008
#define TPMA_NV_PPREAD         0x00010000
#define TPMA_NV_OWNERREAD      0x00020000
#define TPMA_NV_AUTHREAD       0x00040000
#define TPMA_NV_POLICYREAD     0x00080000
#define TPMA_NV_READLOCKED     0x00400000
#define TPMA_NV_WRITELOCKED    0x00800000
#define TPMA_NV_WRITEDEFINE    0x01000000
#define TPMA_NV_WRITTEN        0x02000000
#define TPMA_NV_PLATFORMCREATE 0x04000000

typedef struct {
    uint8_t     digest[TPM_SHA256_DIGEST_SIZE];
    TPM_ALG_ID  hash_alg;
    uint32_t    pcr_index;
    uint32_t    extend_count;
    bool        allocated;
} TPMPCR;

typedef struct {
    TPMPCR     pcrs[TPM_MAX_PCRS];
    uint32_t   active_pcrs;
    TPM_ALG_ID active_banks[TPM_MAX_PCR_BANKS];
    uint32_t   bank_count;
} TPMBank;

typedef enum {
    TPM_NV_TYPE_ORDINARY = 0,
    TPM_NV_TYPE_COUNTER,
    TPM_NV_TYPE_BITFIELD,
    TPM_NV_TYPE_EXTEND
} TPMNVType;

typedef struct {
    uint32_t    nv_index;
    TPMNVType   nv_type;
    uint32_t    nv_attributes;
    uint8_t     nv_data[TPM_MAX_NV_DATA_SIZE];
    uint32_t    nv_data_size;
    uint32_t    nv_auth_handle;
    bool        written;
} TPMNVIndex;

typedef struct {
    TPMNVIndex indices[TPM_MAX_NV_INDICES];
    uint32_t   index_count;
} TPMNVStore;

typedef enum {
    TPM_SE_HMAC = 0,
    TPM_SE_POLICY,
    TPM_SE_TRIAL
} TPMSessionType;

typedef struct {
    uint32_t          session_handle;
    TPMSessionType    session_type;
    uint8_t           session_key[TPM_SHA256_DIGEST_SIZE];
    uint8_t           nonce_tpm[TPM_NONCE_SIZE];
    uint8_t           nonce_caller[TPM_NONCE_SIZE];
    bool              is_bound;
    uint32_t          bound_handle;
} TPMSession;

/* ??? Quote / Attestation ??? */

typedef struct {
    uint32_t    magic;                          /* TPM_GENERATED_VALUE (0xFF544347) */
    uint32_t    type;                           /* TPM_ST_ATTEST_QUOTE = 0x8018 */
    uint8_t     qualified_signer[TPM_SHA256_DIGEST_SIZE];
    uint8_t     extra_data[TPM_NONCE_SIZE];     /* nonce from verifier */
    uint64_t    clock_info;
    uint64_t    firmware_version;
    uint8_t     pcr_select[TPM_MAX_PCRS];
    uint8_t     pcr_digest[TPM_SHA256_DIGEST_SIZE];
} TPMQuoteInfo;

typedef struct {
    uint8_t  pcr_mask[TPM_MAX_PCRS];
    uint32_t pcr_count;
} TPMPCRSelection;

typedef struct {
    TPMQuoteInfo    quote_info;
    TPM_ALG_ID      sig_alg;
    uint8_t         signature[256];
    uint32_t        sig_size;
    TPMPCRSelection pcr_select;
} TPMQuote;

typedef enum {
    TPM_ATTEST_OK = 0,
    TPM_ATTEST_QUOTE_INVALID,
    TPM_ATTEST_SIGNATURE_INVALID,
    TPM_ATTEST_PCR_MISMATCH,
    TPM_ATTEST_NONCE_MISMATCH,
    TPM_ATTEST_CLOCK_REPLAY
} TPMAttestResult;

typedef struct {
    TPMBank      pcr_bank;
    TPMNVStore   nv_store;
    TPMSession   sessions[TPM_MAX_SESSIONS];
    uint32_t     session_count;
    uint64_t     tpm_clock;
    uint64_t     tpm_reset_count;
    uint32_t     owner_auth;
    uint32_t     lockout_auth;
    bool         is_initialized;
    bool         in_lockout;
    uint32_t     lockout_counter;
    uint32_t     max_lockout_attempts;
} TPMContext;

/* ??? TPM Lifecycle ??? */

bool tpm_init(TPMContext *tpm);
bool tpm_startup(TPMContext *tpm, bool is_resume);
bool tpm_selftest(TPMContext *tpm, bool full_test);
bool tpm_clear(TPMContext *tpm);

/* ??? PCR Operations ??? */

bool tpm_pcr_allocate(TPMContext *tpm, uint32_t pcr_index, TPM_ALG_ID hash_alg);
bool tpm_pcr_extend(TPMContext *tpm, uint32_t pcr_index,
                    const uint8_t *digest, uint32_t digest_size,
                    TPM_ALG_ID hash_alg);
bool tpm_pcr_read(const TPMContext *tpm, uint32_t pcr_index,
                  uint8_t *digest, uint32_t *digest_size);
bool tpm_pcr_reset(TPMContext *tpm, uint32_t pcr_index);
uint32_t tpm_pcr_get_extend_count(const TPMContext *tpm, uint32_t pcr_index);

/*
 * PCR extend formula (per TPM 2.0 spec, Part 3, Section 19.5.2):
 *   PCR_new = H_alg(PCR_old || digest)
 * where H_alg is the hash algorithm associated with the PCR bank.
 */

/* ??? Quote and Attestation ??? */

bool tpm_quote(TPMContext *tpm, const TPMPCRSelection *pcr_select,
               const uint8_t *nonce, uint32_t nonce_size,
               TPMQuote *quote);
TPMAttestResult tpm_verify_quote(const TPMQuote *quote,
                                  const uint8_t expected_pcrs[][TPM_SHA256_DIGEST_SIZE],
                                  uint32_t pcr_count,
                                  const uint8_t *expected_nonce,
                                  uint32_t nonce_size);

/*
 * Remote Attestation Protocol (simplified DICE / TCG RA):
 *
 *   Verifier                          Attester (TPM)
 *      |                                    |
 *      |---(1) nonce, pcr_selection ------->|
 *      |                                    |
 *      |<--(2) Quote, signature ------------|
 *      |                                    |
 *      |---(3) verify Quote, check nonce--->|
 *
 * The Verifier checks:
 *   1. Quote signature is valid (TPM key known to Verifier)
 *   2. Nonce matches what was sent (anti-replay)
 *   3. PCR composite matches expected golden values
 */

/* ??? NV Storage Operations ??? */

bool tpm_nv_define_space(TPMContext *tpm, uint32_t nv_index,
                          TPMNVType nv_type, uint32_t attributes,
                          uint32_t data_size, uint32_t auth_handle);
bool tpm_nv_write(TPMContext *tpm, uint32_t nv_index,
                  const uint8_t *data, uint32_t data_size, uint32_t offset);
bool tpm_nv_read(const TPMContext *tpm, uint32_t nv_index,
                 uint8_t *data, uint32_t *data_size, uint32_t offset);
bool tpm_nv_increment(TPMContext *tpm, uint32_t nv_index);
bool tpm_nv_read_lock(TPMContext *tpm, uint32_t nv_index);
bool tpm_nv_write_lock(TPMContext *tpm, uint32_t nv_index);
bool tpm_nv_undefine_space(TPMContext *tpm, uint32_t nv_index);

/* ??? Session Management ??? */

bool tpm_start_auth_session(TPMContext *tpm, TPMSessionType session_type,
                             uint32_t *session_handle);
bool tpm_flush_context(TPMContext *tpm, uint32_t session_handle);

/* ??? Lockout / Dictionary Attack Protection ??? */

bool tpm_dictionary_attack_lock_reset(TPMContext *tpm);
bool tpm_dictionary_attack_increment(TPMContext *tpm);
bool tpm_is_in_lockout(const TPMContext *tpm);

/* ??? Utility ??? */

const char *tpm_alg_id_str(TPM_ALG_ID alg_id);
const char *tpm_attest_result_str(TPMAttestResult result);
void tpm_print_pcr_banks(const TPMContext *tpm);
void tpm_print_nv_indices(const TPMContext *tpm);

#endif /* TPM_H */
