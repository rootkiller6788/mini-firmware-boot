#include "rats.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>

int32_t rats_generate_evidence(RATSEvidence *evidence,
                                const TPMQuote *quote,
                                const uint8_t *verifier_id,
                                const RATSClaim *extra_claims,
                                uint16_t extra_claim_count) {
    if (!evidence || !quote || !verifier_id) return -1;

    memset(evidence, 0, sizeof(*evidence));

    evidence->evidence_type = RATS_EVIDENCE_TYPE_TPM_QUOTE;
    evidence->timestamp = (uint64_t)time(NULL);

    memcpy(evidence->verifier_identity, verifier_id, RATS_VERIFIER_ID_SIZE);

    uint16_t i;
    for (i = 0; i < quote->pcr_composite.pcr_count && i < TPM_MAX_PCRS; i++) {
        memcpy(evidence->pcr_values[i].digest,
               quote->pcr_composite.pcr_digests[i].digest,
               TPM_SHA256_DIGEST_SIZE);
    }
    evidence->pcr_count = quote->pcr_composite.pcr_count;

    uint16_t claim_idx = 0;

    RATSClaim fw_claim;
    memcpy(fw_claim.key, "firmware_version", 16);
    fw_claim.key_len = 16;
    snprintf((char *)fw_claim.value, RATS_CLAIM_VALUE_SIZE,
             "%llu", (unsigned long long)quote->attest.firmware_version);
    fw_claim.value_len = (uint16_t)strlen((char *)fw_claim.value);
    if (claim_idx < RATS_MAX_CLAIMS) {
        evidence->claims[claim_idx++] = fw_claim;
    }

    RATSClaim magic_claim;
    memcpy(magic_claim.key, "tpms_generated_magic", 20);
    magic_claim.key_len = 20;
    snprintf((char *)magic_claim.value, RATS_CLAIM_VALUE_SIZE,
             "0x%08X", quote->attest.magic);
    magic_claim.value_len = (uint16_t)strlen((char *)magic_claim.value);
    if (claim_idx < RATS_MAX_CLAIMS) {
        evidence->claims[claim_idx++] = magic_claim;
    }

    RATSClaim type_claim;
    memcpy(type_claim.key, "attestation_type", 16);
    type_claim.key_len = 16;
    snprintf((char *)type_claim.value, RATS_CLAIM_VALUE_SIZE,
             "TPM2_Quote");
    type_claim.value_len = (uint16_t)strlen((char *)type_claim.value);
    if (claim_idx < RATS_MAX_CLAIMS) {
        evidence->claims[claim_idx++] = type_claim;
    }

    if (extra_claims) {
        for (i = 0; i < extra_claim_count && claim_idx < RATS_MAX_CLAIMS; i++) {
            evidence->claims[claim_idx++] = extra_claims[i];
        }
    }

    evidence->claim_count = claim_idx;

    memcpy(evidence->signature, quote->signature,
           quote->signature_size < RATS_EVIDENCE_SIGNATURE_SIZE ?
           quote->signature_size : RATS_EVIDENCE_SIGNATURE_SIZE);
    evidence->signature_size = quote->signature_size;

    return 0;
}

int32_t rats_appraise_evidence(const RATSEvidence *evidence,
                                const RATSVerifier *verifier,
                                RATSAppraisalResult *result) {
    if (!evidence || !verifier || !result) return -1;

    *result = RATS_APPRAISAL_ERROR;

    if (!verifier->initialized) {
        return -2;
    }

    bool refs_match = false;
    if (verifier->reference_values.reference_count > 0) {
        rats_verify_reference_values(evidence->pcr_values,
                                     evidence->pcr_count,
                                     &verifier->reference_values,
                                     &refs_match);
        if (!refs_match) {
            *result = RATS_APPRAISAL_FAIL;
            return 0;
        }
    }

    bool policy_compliant = false;
    rats_check_policy_compliance(evidence, &verifier->appraisal_policy,
                                 &policy_compliant);
    if (!policy_compliant) {
        *result = RATS_APPRAISAL_FAIL;
        return 0;
    }

    if (verifier->appraisal_policy.check_timestamp) {
        uint64_t now = (uint64_t)time(NULL);
        if (evidence->timestamp < verifier->reference_values.minimum_timestamp) {
            *result = RATS_APPRAISAL_FAIL;
            return 0;
        }
        if (verifier->reference_values.require_fresh_evidence) {
            uint64_t age_ms = (now - evidence->timestamp) * 1000;
            if (age_ms > verifier->reference_values.maximum_age_ms) {
                *result = RATS_APPRAISAL_FAIL;
                return 0;
            }
        }
    }

    *result = RATS_APPRAISAL_PASS;
    return 0;
}

int32_t rats_verify_reference_values(const TPMHash *evidence_pcrs,
                                      uint16_t evidence_pcr_count,
                                      const RATSReferenceValues *refs,
                                      bool *match) {
    if (!evidence_pcrs || !refs || !match) return -1;

    *match = true;

    if (refs->reference_count == 0) {
        *match = true;
        return 0;
    }

    uint16_t i;
    for (i = 0; i < refs->reference_count && i < evidence_pcr_count; i++) {
        if (memcmp(evidence_pcrs[i].digest,
                   refs->reference_values[i].digest,
                   TPM_SHA256_DIGEST_SIZE) != 0) {
            *match = false;
            return 0;
        }
    }

    return 0;
}

int32_t rats_check_policy_compliance(const RATSEvidence *evidence,
                                      const RATSAppraisalPolicy *policy,
                                      bool *compliant) {
    if (!evidence || !policy || !compliant) return -1;

    *compliant = true;

    if (policy->require_pcr_match && evidence->pcr_count == 0) {
        *compliant = false;
        return 0;
    }

    uint16_t i;
    for (i = 0; i < policy->claim_rules_count && i < RATS_MAX_CLAIMS; i++) {
        bool found = false;
        uint16_t j;
        for (j = 0; j < evidence->claim_count; j++) {
            if (policy->claim_rules[i].key_len == evidence->claims[j].key_len &&
                memcmp(policy->claim_rules[i].key, evidence->claims[j].key,
                       policy->claim_rules[i].key_len) == 0) {
                if (policy->claim_rules[i].value_len == evidence->claims[j].value_len &&
                    memcmp(policy->claim_rules[i].expected_value,
                           evidence->claims[j].value,
                           policy->claim_rules[i].value_len) == 0) {
                    found = true;
                    break;
                }
            }
        }
        if (!found) {
            *compliant = false;
            return 0;
        }
    }

    return 0;
}

int32_t rats_relying_party_interface(const RATSEvidence *evidence,
                                      const RATSAppraisalResult appraisal,
                                      const RATSVerifier *verifier,
                                      RATSRelyingPartyResult *rp_result) {
    if (!evidence || !verifier || !rp_result) return -1;

    memset(rp_result, 0, sizeof(*rp_result));

    memcpy(rp_result->rp_identity, "relying-party-example", 21);
    memcpy(rp_result->attester_identity, evidence->verifier_identity,
           RATS_VERIFIER_ID_SIZE);
    rp_result->timestamp = evidence->timestamp;

    switch (appraisal) {
        case RATS_APPRAISAL_PASS:
            rp_result->verified = true;
            rp_result->trust_tier = RATS_TRUST_TIER_TWO;
            rp_result->attestation_result = ATTEST_RESULT_TRUSTED;
            break;
        case RATS_APPRAISAL_FAIL:
            rp_result->verified = false;
            rp_result->trust_tier = RATS_TRUST_TIER_ZERO;
            rp_result->attestation_result = ATTEST_RESULT_UNTRUSTED;
            break;
        case RATS_APPRAISAL_ERROR:
        default:
            rp_result->verified = false;
            rp_result->trust_tier = RATS_TRUST_TIER_ZERO;
            rp_result->attestation_result = ATTEST_RESULT_UNKNOWN;
            break;
    }

    snprintf((char *)rp_result->supplementary_info,
             sizeof(rp_result->supplementary_info),
             "Appraisal: %d, PCRs: %u, Claims: %u",
             (int)appraisal, evidence->pcr_count, evidence->claim_count);
    rp_result->supp_info_len = (uint16_t)strlen((char *)rp_result->supplementary_info);

    return 0;
}

int32_t rats_verifier_init(RATSVerifier *verifier,
                            const uint8_t *verifier_id) {
    if (!verifier || !verifier_id) return -1;

    memset(verifier, 0, sizeof(*verifier));
    memcpy(verifier->verifier_id, verifier_id, RATS_VERIFIER_ID_SIZE);
    verifier->appraisal_policy.check_timestamp = true;
    verifier->appraisal_policy.check_signature = true;
    verifier->appraisal_policy.check_chain_of_trust = true;
    verifier->appraisal_policy.require_pcr_match = true;
    verifier->appraisal_policy.minimum_trust_tier = RATS_TRUST_TIER_ONE;
    verifier->initialized = true;

    return 0;
}

int32_t rats_verifier_set_reference(RATSVerifier *verifier,
                                     const TPMHash *references,
                                     uint16_t ref_count) {
    if (!verifier || !references) return -1;
    if (!verifier->initialized) return -2;

    uint16_t count = ref_count < RATS_MAX_REFERENCE_VALUES ?
                     ref_count : RATS_MAX_REFERENCE_VALUES;

    uint16_t i;
    for (i = 0; i < count; i++) {
        memcpy(verifier->reference_values.reference_values[i].digest,
               references[i].digest, TPM_SHA256_DIGEST_SIZE);
        snprintf((char *)verifier->reference_values.reference_labels[i],
                 RATS_CLAIM_KEY_SIZE, "pcr_%u", i);
    }
    verifier->reference_values.reference_count = count;
    verifier->reference_values.minimum_timestamp = (uint64_t)time(NULL);
    verifier->reference_values.maximum_age_ms = 300000;
    verifier->reference_values.require_fresh_evidence = true;

    return 0;
}

void rats_evidence_dump(const RATSEvidence *evidence) {
    if (!evidence) {
        printf("RATSEvidence: (null)\n");
        return;
    }
    printf("=== RATS Evidence ===\n");
    printf("  Type:          %d\n", (int)evidence->evidence_type);
    printf("  Verifier ID:   %.16s\n", evidence->verifier_identity);
    printf("  Timestamp:     %llu\n", (unsigned long long)evidence->timestamp);
    printf("  PCR Count:     %u\n", evidence->pcr_count);
    printf("  Claim Count:   %u\n", evidence->claim_count);
    uint16_t i;
    for (i = 0; i < evidence->claim_count; i++) {
        printf("  Claim[%u]:      %.*s = %.*s\n",
               i,
               evidence->claims[i].key_len, evidence->claims[i].key,
               evidence->claims[i].value_len, evidence->claims[i].value);
    }
    printf("======================\n");
}

void rats_appraisal_result_to_string(RATSAppraisalResult result,
                                      char *buf, size_t buf_size) {
    if (!buf || buf_size == 0) return;
    switch (result) {
        case RATS_APPRAISAL_PASS:
            snprintf(buf, buf_size, "PASS"); break;
        case RATS_APPRAISAL_FAIL:
            snprintf(buf, buf_size, "FAIL"); break;
        case RATS_APPRAISAL_ERROR:
            snprintf(buf, buf_size, "ERROR"); break;
        default:
            snprintf(buf, buf_size, "UNKNOWN"); break;
    }
}

void rats_trust_tier_to_string(RATSTrustTier tier,
                                char *buf, size_t buf_size) {
    if (!buf || buf_size == 0) return;
    switch (tier) {
        case RATS_TRUST_TIER_ZERO:  snprintf(buf, buf_size, "TIER_0"); break;
        case RATS_TRUST_TIER_ONE:   snprintf(buf, buf_size, "TIER_1"); break;
        case RATS_TRUST_TIER_TWO:   snprintf(buf, buf_size, "TIER_2"); break;
        case RATS_TRUST_TIER_THREE: snprintf(buf, buf_size, "TIER_3"); break;
        default:                    snprintf(buf, buf_size, "UNKNOWN"); break;
    }
}
