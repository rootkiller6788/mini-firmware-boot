#include "attest_protocol.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>

int32_t attest_challenge_create(AttestChallenge *challenge,
                                 uint16_t pcr_selection_mask,
                                 const uint8_t *extra_data, uint16_t ed_size) {
    if (!challenge) return -1;

    memset(challenge, 0, sizeof(*challenge));

    challenge->pcr_selection_mask = pcr_selection_mask;
    challenge->timestamp = (uint64_t)time(NULL);

    static uint32_t challenge_counter = 0;
    challenge->challenge_id = challenge_counter++;

    uint32_t seed = (uint32_t)challenge->timestamp ^ (challenge->challenge_id << 16);
    int i;
    for (i = 0; i < ATTEST_NONCE_SIZE; i++) {
        seed = seed * 1103515245 + 12345;
        challenge->nonce[i] = (uint8_t)((seed >> 16) & 0xFF);
    }

    if (extra_data && ed_size > 0) {
        uint16_t copy_size = ed_size < ATTEST_MAX_EXTRA_DATA ? ed_size : ATTEST_MAX_EXTRA_DATA;
        memcpy(challenge->extra_data, extra_data, copy_size);
        challenge->extra_data_size = copy_size;
    }

    return 0;
}

int32_t attest_challenge_verify_freshness(const AttestChallenge *challenge,
                                           uint64_t max_age_ms, bool *fresh) {
    if (!challenge || !fresh) return -1;

    uint64_t now = (uint64_t)time(NULL);
    uint64_t age = now - challenge->timestamp;

    *fresh = (age * 1000) <= max_age_ms;
    return 0;
}

int32_t attest_response_create(AttestResponse *response,
                                const TPMKeyAIK *aik,
                                const AIKCredential *aik_cred,
                                const TPMPcrComposite *pcr_composite,
                                const AttestChallenge *challenge,
                                uint64_t firmware_version,
                                const uint8_t *event_log, uint16_t el_size) {
    if (!response || !aik || !pcr_composite || !challenge) return -1;

    memset(response, 0, sizeof(*response));
    response->challenge_id = challenge->challenge_id;

    TPMKey aik_key;
    memcpy(aik_key.modulus, aik->aik_pub_modulus, aik->aik_pub_modulus_size);
    aik_key.modulus_size = aik->aik_pub_modulus_size;
    memcpy(aik_key.exponent, aik->aik_pub_exponent, 3);

    tpm_quote_create(&response->quote, pcr_composite,
                     challenge->nonce, ATTEST_NONCE_SIZE,
                     NULL, 0,
                     firmware_version);

    tpm_quote_sign(&response->quote, &aik_key);

    if (event_log && el_size > 0) {
        uint16_t copy_size = el_size < ATTEST_EVENT_LOG_MAX ? el_size : ATTEST_EVENT_LOG_MAX;
        memcpy(response->event_log, event_log, copy_size);
        response->event_log_size = copy_size;
    }

    if (aik_cred) {
        memcpy(&response->aik_credential, aik_cred, sizeof(AIKCredential));
        response->aik_credential_provided = true;
    }

    return 0;
}

int32_t attest_verify(const AttestResponse *response,
                       const AttestChallenge *challenge,
                       const AttestVerifier *verifier,
                       AttestVerdict *verdict) {
    if (!response || !challenge || !verifier || !verdict) return -1;

    memset(verdict, 0, sizeof(*verdict));

    if (memcmp(response->quote.attest.extra_data, challenge->nonce, ATTEST_NONCE_SIZE) != 0) {
        verdict->nonce_matched = 0;
        verdict->result = ATTEST_POLICY_DENY;
        snprintf((char *)verdict->detail, sizeof(verdict->detail),
                 "Nonce mismatch: response does not match challenge");
        verdict->detail_len = (uint16_t)strlen((char *)verdict->detail);
        return 0;
    }
    verdict->nonce_matched = 1;

    if (verdict->nonce_matched && challenge->pcr_selection_mask == 0) {
        verdict->nonce_matched = 1;
    }

    bool aik_ok = false;
    if (response->aik_credential_provided && verifier->accepted_aik_count > 0) {
        TPMKeyPublic *aik_pub_ref = &verifier->accepted_aiks[0];

        TPMKeyPublic response_aik_pub;
        memset(&response_aik_pub, 0, sizeof(response_aik_pub));
        memcpy(response_aik_pub.modulus, response->aik_credential.aik_pub_modulus,
               response->aik_credential.aik_pub_modulus_size);
        response_aik_pub.modulus_size = response->aik_credential.aik_pub_modulus_size;
        memcpy(response_aik_pub.exponent, response->aik_credential.aik_pub_exponent, 3);

        tpm_verify_aik_credential(&response->aik_credential,
                                  &response_aik_pub,
                                  verifier->privacy_ca_public,
                                  &aik_ok);
    } else {
        aik_ok = true;
    }
    verdict->aik_verified = aik_ok ? 1 : 0;

    if (!aik_ok) {
        verdict->result = ATTEST_POLICY_DENY;
        snprintf((char *)verdict->detail, sizeof(verdict->detail),
                 "AIK credential verification failed");
        verdict->detail_len = (uint16_t)strlen((char *)verdict->detail);
        return 0;
    }

    TPMKey aik_pub;
    memset(&aik_pub, 0, sizeof(aik_pub));
    if (verifier->accepted_aik_count > 0) {
        memcpy(aik_pub.modulus, verifier->accepted_aiks[0].modulus,
               verifier->accepted_aiks[0].modulus_size);
        aik_pub.modulus_size = verifier->accepted_aiks[0].modulus_size;
        memcpy(aik_pub.exponent, verifier->accepted_aiks[0].exponent, 3);
    }

    bool quote_valid = false;
    tpm_quote_verify(&response->quote, &aik_pub,
                     &response->quote.pcr_composite,
                     response->quote.attest.firmware_version,
                     &quote_valid);
    verdict->quote_verified = quote_valid ? 1 : 0;

    if (verifier->check_firmware_version) {
        verdict->fw_ok = (response->quote.attest.firmware_version >=
                          verifier->min_firmware_version) ? 1 : 0;
    } else {
        verdict->fw_ok = 1;
    }

    verdict->clock_ok = 1;

    if (response->event_log_size > 0) {
        TPMPcrComposite recomputed;
        bool el_match = false;
        attest_replay_event_log(response->event_log, response->event_log_size,
                                &recomputed, &el_match);
        verdict->event_log_ok = el_match ? 1 : 0;
    } else {
        verdict->event_log_ok = 1;
    }

    AttestPolicyResult policy_result;
    attest_verify_policy(&response->quote.pcr_composite,
                         response->quote.attest.firmware_version,
                         (AttestVerifier *)verifier,
                         &policy_result);
    verdict->policy_pass = (policy_result == ATTEST_POLICY_ALLOW) ? 1 : 0;

    if (verdict->nonce_matched && verdict->quote_verified &&
        verdict->fw_ok && verdict->clock_ok &&
        verdict->event_log_ok && verdict->policy_pass) {
        verdict->result = ATTEST_POLICY_ALLOW;
    } else {
        verdict->result = ATTEST_POLICY_DENY;
    }

    verdict->pcr_matched = verdict->policy_pass;

    return 0;
}

int32_t attest_verify_policy(const TPMPcrComposite *pcr_composite,
                              uint64_t firmware_version,
                              const AttestVerifier *verifier,
                              AttestPolicyResult *result) {
    if (!pcr_composite || !verifier || !result) return -1;

    *result = ATTEST_POLICY_UNKNOWN;

    if (verifier->known_good_pcr_count == 0) {
        *result = ATTEST_POLICY_ALLOW;
        return 0;
    }

    uint16_t i;
    for (i = 0; i < verifier->known_good_pcr_count && i < ATTEST_MAX_PCR_VALUES; i++) {
        if (i < pcr_composite->pcr_count) {
            if (memcmp(pcr_composite->pcr_digests[i].digest,
                       verifier->known_good_pcr_values[i].digest,
                       TPM_SHA256_DIGEST_SIZE) != 0) {
                *result = ATTEST_POLICY_DENY;
                return 0;
            }
        }
    }

    *result = ATTEST_POLICY_ALLOW;
    return 0;
}

int32_t attest_replay_event_log(const uint8_t *event_log, uint16_t el_size,
                                 TPMPcrComposite *recomputed_pcr,
                                 bool *match) {
    if (!event_log || !recomputed_pcr || !match) return -1;

    tpm_pcr_composite_init(recomputed_pcr);

    if (el_size == 0) {
        *match = true;
        return 0;
    }

    TPMHash current_pcr[TPM_MAX_PCRS];
    int i;
    for (i = 0; i < TPM_MAX_PCRS; i++) {
        tpm_hash_init(&current_pcr[i]);
    }

    uint16_t pos = 0;
    while (pos + 4 <= el_size) {
        uint8_t pcr_index = event_log[pos];
        uint32_t event_size = (uint32_t)event_log[pos + 1] |
                              ((uint32_t)event_log[pos + 2] << 8) |
                              ((uint32_t)event_log[pos + 3] << 16) |
                              ((uint32_t)event_log[pos + 4] << 24);

        pos += 5;
        if (pos + event_size > el_size) break;

        tpm_hash_update(&current_pcr[pcr_index], event_log + pos, event_size);
        pos += event_size;
    }

    for (i = 0; i < TPM_MAX_PCRS; i++) {
        tpm_hash_final(&current_pcr[i]);
        tpm_pcr_composite_add(recomputed_pcr, (uint8_t)i, &current_pcr[i]);
    }

    *match = true;
    return 0;
}

int32_t attest_pcr_compare(const TPMPcrComposite *a,
                            const TPMPcrComposite *b,
                            uint16_t pcr_mask, bool *match) {
    if (!a || !b || !match) return -1;

    *match = true;

    int i;
    for (i = 0; i < TPM_MAX_PCRS; i++) {
        if (pcr_mask & (1 << i)) {
            if (memcmp(a->pcr_digests[i].digest, b->pcr_digests[i].digest,
                       TPM_SHA256_DIGEST_SIZE) != 0) {
                *match = false;
                return 0;
            }
        }
    }

    return 0;
}

void attest_verdict_print(const AttestVerdict *verdict) {
    if (!verdict) {
        printf("AttestVerdict: (null)\n");
        return;
    }
    printf("=== ATTESTATION VERDICT ===\n");
    printf("  Result:         %s\n",
           verdict->result == ATTEST_POLICY_ALLOW ? "ALLOW" :
           verdict->result == ATTEST_POLICY_DENY ? "DENY" : "UNKNOWN");
    printf("  Nonce OK:       %s\n", verdict->nonce_matched ? "YES" : "NO");
    printf("  PCR Match:      %s\n", verdict->pcr_matched ? "YES" : "NO");
    printf("  AIK Verified:   %s\n", verdict->aik_verified ? "YES" : "NO");
    printf("  Quote Verified: %s\n", verdict->quote_verified ? "YES" : "NO");
    printf("  FW OK:          %s\n", verdict->fw_ok ? "YES" : "NO");
    printf("  Clock OK:       %s\n", verdict->clock_ok ? "YES" : "NO");
    printf("  Event Log OK:   %s\n", verdict->event_log_ok ? "YES" : "NO");
    printf("  Policy Pass:    %s\n", verdict->policy_pass ? "YES" : "NO");
    if (verdict->detail_len > 0) {
        printf("  Detail:         %s\n", verdict->detail);
    }
    printf("==========================\n");
}

void attest_challenge_dump(const AttestChallenge *challenge) {
    if (!challenge) return;
    printf("=== Attest Challenge ===\n");
    printf("  ID: %u, PCR Mask: 0x%04X\n", challenge->challenge_id, challenge->pcr_selection_mask);
    printf("  Nonce: ");
    int i;
    for (i = 0; i < 8; i++) printf("%02X", challenge->nonce[i]);
    printf("...\n");
    printf("========================\n");
}

void attest_response_dump(const AttestResponse *response) {
    if (!response) return;
    printf("=== Attest Response ===\n");
    printf("  Challenge ID: %u\n", response->challenge_id);
    printf("  Event Log Size: %u\n", response->event_log_size);
    printf("  AIK Credential: %s\n", response->aik_credential_provided ? "YES" : "NO");
    tpm_quote_dump(&response->quote);
    printf("=======================\n");
}
