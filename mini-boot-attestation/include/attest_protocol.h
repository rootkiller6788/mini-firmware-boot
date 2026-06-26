#ifndef ATTEST_PROTOCOL_H
#define ATTEST_PROTOCOL_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "tpm_quote.h"
#include "aik_identity.h"

#define ATTEST_NONCE_SIZE        32
#define ATTEST_MAX_EXTRA_DATA    64
#define ATTEST_EVENT_LOG_MAX     256
#define ATTEST_MAX_PCR_VALUES    24
#define ATTEST_AUTH_SIZE         16
#define ATTEST_POLICY_NAME_SIZE  32

typedef enum {
    ATTEST_POLICY_ALLOW  = 0,
    ATTEST_POLICY_DENY   = 1,
    ATTEST_POLICY_UNKNOWN = 2
} AttestPolicyResult;

typedef struct {
    uint8_t  nonce[ATTEST_NONCE_SIZE];
    uint16_t pcr_selection_mask;
    uint8_t  extra_data[ATTEST_MAX_EXTRA_DATA];
    uint16_t extra_data_size;
    uint32_t challenge_id;
    uint64_t timestamp;
} AttestChallenge;

typedef struct {
    TPMQuote                quote;
    uint8_t                 event_log[ATTEST_EVENT_LOG_MAX];
    uint16_t                event_log_size;
    AIKCredential           aik_credential;
    bool                    aik_credential_provided;
    uint32_t                challenge_id;
} AttestResponse;

typedef struct {
    TPMHash     known_good_pcr_values[ATTEST_MAX_PCR_VALUES];
    uint16_t    known_good_pcr_count;
    TPMKeyPublic accepted_aiks[8];
    uint16_t    accepted_aik_count;
    uint8_t     privacy_ca_public[AIK_KEY_SIZE];
    uint8_t     privacy_ca_id[PRIVACY_CA_ID_SIZE];
    bool        check_firmware_version;
    uint64_t    min_firmware_version;
    bool        check_clock;
    uint64_t    max_clock_drift;
} AttestVerifier;

typedef struct {
    AttestPolicyResult result;
    uint8_t  detail[256];
    uint16_t detail_len;
    uint32_t nonce_matched  : 1;
    uint32_t pcr_matched    : 1;
    uint32_t aik_verified   : 1;
    uint32_t quote_verified : 1;
    uint32_t fw_ok          : 1;
    uint32_t clock_ok       : 1;
    uint32_t event_log_ok   : 1;
    uint32_t policy_pass    : 1;
} AttestVerdict;

int32_t  attest_challenge_create(AttestChallenge *challenge,
                                 uint16_t pcr_selection_mask,
                                 const uint8_t *extra_data, uint16_t ed_size);

int32_t  attest_challenge_verify_freshness(const AttestChallenge *challenge,
                                           uint64_t max_age_ms, bool *fresh);

int32_t  attest_response_create(AttestResponse *response,
                                const TPMKeyAIK *aik,
                                const AIKCredential *aik_cred,
                                const TPMPcrComposite *pcr_composite,
                                const AttestChallenge *challenge,
                                uint64_t firmware_version,
                                const uint8_t *event_log, uint16_t el_size);

int32_t  attest_verify(const AttestResponse *response,
                       const AttestChallenge *challenge,
                       const AttestVerifier *verifier,
                       AttestVerdict *verdict);

int32_t  attest_verify_policy(const TPMPcrComposite *pcr_composite,
                              uint64_t firmware_version,
                              const AttestVerifier *verifier,
                              AttestPolicyResult *result);

int32_t  attest_replay_event_log(const uint8_t *event_log, uint16_t el_size,
                                 TPMPcrComposite *recomputed_pcr,
                                 bool *match);

int32_t  attest_pcr_compare(const TPMPcrComposite *a,
                            const TPMPcrComposite *b,
                            uint16_t pcr_mask, bool *match);

void     attest_verdict_print(const AttestVerdict *verdict);

void     attest_challenge_dump(const AttestChallenge *challenge);
void     attest_response_dump(const AttestResponse *response);

#endif
