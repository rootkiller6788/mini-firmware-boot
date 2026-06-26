#ifndef TPM_ATTESTATION_H
#define TPM_ATTESTATION_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/*
 * TPM_ATTESTATION: TPM 2.0 Measured Boot and Remote Attestation
 *
 * Reference: TCG PC Client Platform Firmware Profile Spec v1.05
 *            TPM 2.0 Library Spec Part 1-4
 *            TCG Algorithm Registry
 *
 * Knowledge:
 *   L1: TPML_PCR_SELECTION, TPMS_ATTEST, TPM2B_DIGEST, PCR banks
 *   L2: Measured boot chain (CRTM -> POST -> OS loader -> OS)
 *   L3: PCR extend pipeline (hash+append -> SHA-256)
 *   L4: TCG Algorithm Registry (TPM_ALG_SHA256/384)
 *        TPM 2.0 Part 2 Section 10 (PCR operations)
 *   L5: PCR Extend = H(PCR_old || digest)
 *        Quote = Sign(PCR values || nonce)
 *   L7: Remote attestation protocol
 *   L8: Key hierarchy: EK (Endorsement) -> SRK (Storage Root) -> AK (Attestation)
 */

/* ?? TPM 2.0 Constants (TCG Algorithm Registry) ?????????????????? */

#define TPM_ALG_SHA1                0x0004
#define TPM_ALG_SHA256              0x000B
#define TPM_ALG_SHA384              0x000C
#define TPM_ALG_SHA512              0x000D
#define TPM_ALG_RSA                 0x0001
#define TPM_ALG_RSASSA              0x0014
#define TPM_ALG_RSAPSS              0x0016
#define TPM_ALG_ECDSA               0x0018

#define TPM_PCR_COUNT               24
#define TPM_PCR_SELECT_MAX          3

#define TPM_MAX_PCR_BANKS           4
#define TPM_SHA256_DIGEST_SIZE      32
#define TPM_SHA384_DIGEST_SIZE      48
#define TPM_SHA1_DIGEST_SIZE        20

#define TPM_EVENT_LOG_MAX_ENTRIES   64
#define TPM_EVENT_LOG_ENTRY_MAX     256

#define TPM_NONCE_SIZE              32
#define TPM_QUOTE_SIGNATURE_SIZE    256

/* Key hierarchy constants */
#define TPM_RH_EK                   0x40000001
#define TPM_RH_SRK                  0x40000002
#define TPM_RH_AK                   0x40000003
#define TPM_RH_NULL                 0x40000007

/* ?? L1: Core Data Structures ???????????????????????????????????? */

/* PCR selection: which PCRs in which bank (TCG PC Client 3.3.4) */
typedef struct {
    uint16_t hash_algorithm;
    uint8_t  size_of_select;
    uint8_t  pcr_select[4];
} TPMS_PCR_SELECTION;

/* List of PCR selections for multiple banks */
typedef struct {
    uint32_t             count;
    TPMS_PCR_SELECTION   selections[TPM_PCR_SELECT_MAX];
} TPML_PCR_SELECTION;

/* PCR digest value for a single bank */
typedef struct {
    uint8_t  bank_id;
    uint16_t hash_algorithm;
    uint8_t  digest[TPM_SHA384_DIGEST_SIZE];
    uint16_t digest_size;
    bool     initialized;
} TPMPcrValue;

/* Full PCR bank: all 24 PCRs for one hash algorithm */
typedef struct {
    uint16_t     hash_algorithm;
    TPMPcrValue  pcrs[TPM_PCR_COUNT];
    bool         active;
} TPMPcrBank;

/* TPM event log entry (measured boot event) */
typedef struct {
    uint32_t pcr_index;
    uint32_t event_type;
    uint8_t  digest[TPM_SHA256_DIGEST_SIZE];
    uint8_t  event_data[TPM_EVENT_LOG_ENTRY_MAX];
    uint32_t event_data_size;
} TPMEventLogEntry;

/* TPM event log: ordered list of measured boot events */
typedef struct {
    TPMEventLogEntry entries[TPM_EVENT_LOG_MAX_ENTRIES];
    size_t           entry_count;
} TPMEventLog;

/* TPM Quote structure (TPM2B_ATTEST) */
typedef struct {
    uint32_t magic;
    uint32_t type;
    uint8_t  qualified_signer[TPM_SHA256_DIGEST_SIZE];
    uint8_t  extra_data[TPM_NONCE_SIZE];
    uint32_t clock_info;
    uint64_t firmware_version;
    uint8_t  pcr_digest[TPM_SHA256_DIGEST_SIZE];
    uint8_t  pcr_select[TPM_PCR_COUNT / 8];
} TPMS_ATTEST;

/* TPM key hierarchy: EK -> SRK -> AK */
typedef struct {
    uint32_t handle;
    uint8_t  public_key[TPM_SHA256_DIGEST_SIZE * 4];
    uint16_t key_size;
    uint16_t key_type;
    bool     generated;
} TPMKey;

/* Full TPM state machine (L2: measured boot + L8: key hierarchy) */
typedef struct {
    TPMPcrBank     pcr_banks[TPM_MAX_PCR_BANKS];
    uint8_t        active_bank_count;
    TPMEventLog    event_log;
    TPMKey         ek;
    TPMKey         srk;
    TPMKey         ak;
    uint8_t        tpm_nonce[TPM_NONCE_SIZE];
    bool           tpm_initialized;
    bool           tpm_self_test_passed;
    bool           locality_0_4_active[5];
} TPMState;

/* ?? L1: API Declarations ???????????????????????????????????????? */

/* TPM Core Lifecycle */
void     tpm_init(TPMState *tpm);
bool     tpm_self_test(TPMState *tpm);
bool     tpm_set_locality(TPMState *tpm, uint8_t locality);

/* L2/L5: PCR Operations (Measured Boot) */
bool     tpm_pcr_extend(TPMState *tpm, uint16_t hash_alg,
                        uint32_t pcr_index, const uint8_t *digest,
                        uint16_t digest_size);
bool     tpm_pcr_read(TPMState *tpm, uint16_t hash_alg,
                      uint32_t pcr_index, uint8_t *digest_out,
                      uint16_t *digest_size);
bool     tpm_pcr_reset(TPMState *tpm, uint32_t pcr_index);

/* L2: Measured Boot Event Log */
bool     tpm_event_log_add(TPMState *tpm, uint32_t pcr_index,
                           uint32_t event_type, const uint8_t *event_data,
                           uint32_t event_data_size);
bool     tpm_event_log_verify(TPMState *tpm);

/* L7: Remote Attestation */
bool     tpm_quote_create(TPMState *tpm, const uint8_t *nonce,
                          uint32_t nonce_size, TPMS_ATTEST *quote);
bool     tpm_quote_verify(TPMState *tpm, const TPMS_ATTEST *quote,
                          const uint8_t *nonce, uint32_t nonce_size,
                          const uint8_t *expected_pcr_values,
                          uint32_t expected_pcr_size);

/* L8: Key Hierarchy Management */
bool     tpm_create_ek(TPMState *tpm);
bool     tpm_create_srk(TPMState *tpm);
bool     tpm_create_ak(TPMState *tpm);
bool     tpm_activate_credential(TPMState *tpm, const TPMKey *ak,
                                 const uint8_t *credential_blob,
                                 uint32_t blob_size);

/* Utility */
bool     tpm_check_pcr_policy(TPMState *tpm, uint16_t hash_alg,
                              const uint8_t *pcr_mask,
                              const uint8_t *expected_composite);

#endif
