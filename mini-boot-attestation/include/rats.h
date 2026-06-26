#ifndef RATS_H
#define RATS_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "tpm_quote.h"
#include "verifier_service.h"

#define RATS_MAX_CLAIMS            16
#define RATS_CLAIM_KEY_SIZE        32
#define RATS_CLAIM_VALUE_SIZE      64
#define RATS_MAX_REFERENCE_VALUES  24
#define RATS_VERIFIER_ID_SIZE      32
#define RATS_RP_ID_SIZE            32
#define RATS_POLICY_DESCRIPTION_SIZE 128
#define RATS_EVIDENCE_SIGNATURE_SIZE 256

typedef enum {
    RATS_EVIDENCE_TYPE_TPM_QUOTE       = 0,
    RATS_EVIDENCE_TYPE_TPM_ATTESTATION  = 1,
    RATS_EVIDENCE_TYPE_SGX_QUOTE       = 2,
    RATS_EVIDENCE_TYPE_SEV_ATTESTATION = 3
} RATSEvidenceType;

typedef enum {
    RATS_APPRAISAL_PASS  = 0,
    RATS_APPRAISAL_FAIL  = 1,
    RATS_APPRAISAL_ERROR = 2
} RATSAppraisalResult;

typedef enum {
    RATS_TRUST_TIER_ZERO  = 0,
    RATS_TRUST_TIER_ONE   = 1,
    RATS_TRUST_TIER_TWO   = 2,
    RATS_TRUST_TIER_THREE = 3
} RATSTrustTier;

typedef struct {
    uint8_t  key[RATS_CLAIM_KEY_SIZE];
    uint8_t  value[RATS_CLAIM_VALUE_SIZE];
    uint16_t key_len;
    uint16_t value_len;
} RATSClaim;

typedef struct {
    RATSClaim claims[RATS_MAX_CLAIMS];
    uint16_t  claim_count;
    RATSEvidenceType evidence_type;
    uint8_t   verifier_identity[RATS_VERIFIER_ID_SIZE];
    uint64_t  timestamp;
    TPMHash   pcr_values[TPM_MAX_PCRS];
    uint16_t  pcr_count;
    uint8_t   signature[RATS_EVIDENCE_SIGNATURE_SIZE];
    uint16_t  signature_size;
} RATSEvidence;

typedef struct {
    TPMHash  reference_values[RATS_MAX_REFERENCE_VALUES];
    uint16_t reference_count;
    uint8_t  reference_labels[RATS_MAX_REFERENCE_VALUES][RATS_CLAIM_KEY_SIZE];
    uint64_t minimum_timestamp;
    uint64_t maximum_age_ms;
    bool     require_fresh_evidence;
} RATSReferenceValues;

typedef struct {
    uint8_t  description[RATS_POLICY_DESCRIPTION_SIZE];
    uint16_t pcr_mask_required;
    bool     check_timestamp;
    bool     check_signature;
    bool     check_chain_of_trust;
    bool     require_pcr_match;
    RATSTrustTier minimum_trust_tier;
    uint16_t claim_rules_count;
    struct {
        uint8_t  key[RATS_CLAIM_KEY_SIZE];
        uint8_t  expected_value[RATS_CLAIM_VALUE_SIZE];
        uint16_t key_len;
        uint16_t value_len;
    } claim_rules[RATS_MAX_CLAIMS];
} RATSAppraisalPolicy;

typedef struct {
    uint8_t   verifier_id[RATS_VERIFIER_ID_SIZE];
    RATSReferenceValues reference_values;
    RATSAppraisalPolicy appraisal_policy;
    RATSAppraisalResult last_result;
    uint64_t  last_appraisal_time;
    bool      initialized;
} RATSVerifier;

typedef struct {
    uint8_t  rp_identity[RATS_RP_ID_SIZE];
    uint8_t  attester_identity[ATTEST_DEVICE_ID_SIZE];
    uint8_t  attestation_result;
    RATSTrustTier trust_tier;
    uint64_t timestamp;
    bool     verified;
    uint8_t  supplementary_info[128];
    uint16_t supp_info_len;
} RATSRelyingPartyResult;

int32_t  rats_generate_evidence(RATSEvidence *evidence,
                                const TPMQuote *quote,
                                const uint8_t *verifier_id,
                                const RATSClaim *extra_claims,
                                uint16_t extra_claim_count);

int32_t  rats_appraise_evidence(const RATSEvidence *evidence,
                                const RATSVerifier *verifier,
                                RATSAppraisalResult *result);

int32_t  rats_verify_reference_values(const TPMHash *evidence_pcrs,
                                      uint16_t evidence_pcr_count,
                                      const RATSReferenceValues *refs,
                                      bool *match);

int32_t  rats_check_policy_compliance(const RATSEvidence *evidence,
                                      const RATSAppraisalPolicy *policy,
                                      bool *compliant);

int32_t  rats_relying_party_interface(const RATSEvidence *evidence,
                                      const RATSAppraisalResult appraisal,
                                      const RATSVerifier *verifier,
                                      RATSRelyingPartyResult *rp_result);

int32_t  rats_verifier_init(RATSVerifier *verifier,
                            const uint8_t *verifier_id);

int32_t  rats_verifier_set_reference(RATSVerifier *verifier,
                                     const TPMHash *references,
                                     uint16_t ref_count);

void     rats_evidence_dump(const RATSEvidence *evidence);
void     rats_appraisal_result_to_string(RATSAppraisalResult result,
                                         char *buf, size_t buf_size);
void     rats_trust_tier_to_string(RATSTrustTier tier,
                                   char *buf, size_t buf_size);

#endif
