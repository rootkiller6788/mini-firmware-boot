#include "tpm_quote.h"
#include "aik_identity.h"
#include "attest_protocol.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int main(void) {
    printf("=== Attestation Challenge-Response Demo ===\n\n");

    printf("[1] Verifier setup...\n");
    AttestVerifier verifier;
    memset(&verifier, 0, sizeof(verifier));
    verifier.check_firmware_version = true;
    verifier.min_firmware_version = 0x00020001;
    verifier.check_clock = true;
    verifier.max_clock_drift = 5000;

    int i;
    for (i = 0; i < 8; i++) {
        TPMHash hash;
        tpm_hash_init(&hash);
        char event_name[32];
        snprintf(event_name, sizeof(event_name), "known-event-%d", i);
        tpm_hash_update(&hash, (const uint8_t *)event_name, strlen(event_name));
        tpm_hash_final(&hash);
        memcpy(verifier.known_good_pcr_values[i].digest, hash.digest, TPM_SHA256_DIGEST_SIZE);
    }
    verifier.known_good_pcr_count = 8;

    printf("    Known good PCRs: %u\n", verifier.known_good_pcr_count);
    printf("    Min FW version:  0x%08llX\n", (unsigned long long)verifier.min_firmware_version);

    printf("\n[2] Verifier creates challenge...\n");
    AttestChallenge challenge;
    uint8_t extra[] = "attest-request-1";
    attest_challenge_create(&challenge, 0x00FF, extra, (uint16_t)strlen((char *)extra));
    attest_challenge_dump(&challenge);

    printf("\n[3] Attester: create EK, SRK, AIK...\n");
    TPMKeyPublic ek_pub;
    tpm_create_ek(&ek_pub);

    TPMKeyPublic srk;
    memset(&srk, 0, sizeof(srk));
    srk.hierarchy = TPM_KEY_HIERARCHY_SRK;
    for (i = 0; i < 256; i++) srk.modulus[i] = (uint8_t)(i + 0x30);
    srk.modulus_size = 256;
    srk.exponent[0] = 0x01; srk.exponent[1] = 0x00; srk.exponent[2] = 0x01;

    TPMKeyAIK aik;
    tpm_create_aik(&aik, &srk, &ek_pub);
    printf("    AIK created\n");

    printf("\n[4] Attester: build PCR composite from known values...\n");
    TPMPcrComposite pcr_actual;
    tpm_pcr_composite_init(&pcr_actual);

    const char *events[] = {
        "known-event-0", "known-event-1", "known-event-2", "known-event-3",
        "known-event-4", "known-event-5", "known-event-6", "known-event-7"
    };

    uint8_t event_log[256];
    uint16_t el_size = 0;

    for (i = 0; i < 8; i++) {
        TPMHash hash;
        tpm_hash_init(&hash);
        tpm_hash_update(&hash, (const uint8_t *)events[i], strlen(events[i]));
        tpm_hash_final(&hash);
        tpm_pcr_composite_add(&pcr_actual, (uint8_t)i, &hash);

        event_log[el_size++] = (uint8_t)i;
        uint32_t len = (uint32_t)strlen(events[i]);
        event_log[el_size++] = (uint8_t)(len & 0xFF);
        event_log[el_size++] = (uint8_t)((len >> 8) & 0xFF);
        event_log[el_size++] = (uint8_t)((len >> 16) & 0xFF);
        event_log[el_size++] = (uint8_t)((len >> 24) & 0xFF);
        memcpy(event_log + el_size, events[i], len);
        el_size += (uint16_t)len;
    }

    printf("    PCRs built, event log size: %u\n", el_size);

    printf("\n[5] Attester: build response (Quote + EventLog)...\n");
    AttestResponse response;
    attest_response_create(&response, &aik, NULL,
                           &pcr_actual, &challenge,
                           verifier.min_firmware_version,
                           event_log, el_size);
    printf("    Response ready\n");

    printf("\n[6] Verifier: verify attestation response...\n");
    AttestVerdict verdict;
    attest_verify(&response, &challenge, &verifier, &verdict);
    attest_verdict_print(&verdict);

    printf("\n[7] Trusted scenario result: ");
    if (verdict.result == ATTEST_POLICY_ALLOW) {
        printf("DEVICE IS TRUSTED\n");
    } else {
        printf("DEVICE IS UNTRUSTED\n");
    }

    printf("\n[8] Untrusted scenario — tamper with PCR0...\n");
    TPMPcrComposite tampered_pcr;
    memcpy(&tampered_pcr, &pcr_actual, sizeof(TPMPcrComposite));
    tampered_pcr.pcr_digests[0].digest[0] ^= 0xFF;

    AttestResponse bad_response;
    attest_response_create(&bad_response, &aik, NULL,
                           &tampered_pcr, &challenge,
                           verifier.min_firmware_version,
                           event_log, el_size);

    AttestVerdict bad_verdict;
    attest_verify(&bad_response, &challenge, &verifier, &bad_verdict);
    attest_verdict_print(&bad_verdict);

    printf("    Tampered PCR result: %s\n",
           bad_verdict.result == ATTEST_POLICY_ALLOW ? "PASS (unexpected!)" : "FAIL (correct)");

    printf("\n=== Demo Complete ===\n");
    return 0;
}
