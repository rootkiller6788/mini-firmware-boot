#include "tpm_quote.h"
#include "aik_identity.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int main(void) {
    printf("=== TPM Quote Demo ===\n\n");

    printf("[1] Creating Endorsement Key (EK)...\n");
    TPMKeyPublic ek_pub;
    tpm_create_ek(&ek_pub);
    printf("    EK modulus_size = %u\n", ek_pub.modulus_size);

    printf("\n[2] Creating Storage Root Key (SRK)...\n");
    TPMKeyPublic srk;
    memset(&srk, 0, sizeof(srk));
    srk.hierarchy = TPM_KEY_HIERARCHY_SRK;
    int i;
    for (i = 0; i < 256; i++) srk.modulus[i] = (uint8_t)(i + 0x30);
    srk.modulus_size = 256;
    srk.exponent[0] = 0x01; srk.exponent[1] = 0x00; srk.exponent[2] = 0x01;
    printf("    SRK created (hierarchy=%d)\n", srk.hierarchy);

    printf("\n[3] Creating Attestation Identity Key (AIK)...\n");
    TPMKeyAIK aik;
    tpm_create_aik(&aik, &srk, &ek_pub);
    printf("    AIK modulus_size = %u\n", aik.aik_pub_modulus_size);
    printf("    AIK name = ");
    for (i = 0; i < 8; i++) printf("%02X", aik.aik_name[i]);
    printf("\n");

    printf("\n[4] Extending PCRs with known values...\n");
    TPMPcrComposite pcr_composite;
    tpm_pcr_composite_init(&pcr_composite);

    const char *events[] = {
        "BIOS ACM",
        "BIOS code",
        "Option ROM",
        "MBR",
        "Bootloader",
        "OS Kernel",
        "InitRD",
        "Kernel Modules"
    };

    for (i = 0; i < 8; i++) {
        TPMHash hash;
        tpm_hash_init(&hash);
        tpm_hash_update(&hash, (const uint8_t *)events[i], strlen(events[i]));
        tpm_hash_final(&hash);

        tpm_pcr_composite_add(&pcr_composite, (uint8_t)i, &hash);

        printf("    PCR%u: ", i);
        int j;
        for (j = 0; j < 8; j++) printf("%02X", hash.digest[j]);
        printf(" <- \"%s\"\n", events[i]);
    }
    printf("    Total PCRs extended: %u\n", pcr_composite.pcr_count);

    printf("\n[5] Creating TPM Quote...\n");
    TPMQuote quote;
    uint8_t nonce[32];
    for (i = 0; i < 32; i++) nonce[i] = (uint8_t)(0xAA + i);
    uint64_t fw_version = 0x00020001;

    tpm_quote_create(&quote, &pcr_composite,
                     nonce, 32,
                     NULL, 0,
                     fw_version);
    printf("    Quote created (magic=0x%08X, type=0x%04X)\n",
           quote.attest.magic, quote.attest.type);

    printf("\n[6] Signing Quote with AIK...\n");
    TPMKey aik_key;
    memcpy(aik_key.modulus, aik.aik_pub_modulus, aik.aik_pub_modulus_size);
    aik_key.modulus_size = aik.aik_pub_modulus_size;
    memcpy(aik_key.exponent, aik.aik_pub_exponent, 3);

    tpm_quote_sign(&quote, &aik_key);
    printf("    Signature size: %u bytes\n", quote.signature_size);

    printf("\n[7] Verifying Quote signature...\n");
    bool result = false;
    tpm_quote_verify(&quote, &aik_key, &pcr_composite, fw_version, &result);
    printf("    Signature verification: %s\n", result ? "PASS" : "FAIL");

    printf("\n[8] Tampering test — change PCR0 and verify...\n");
    TPMPcrComposite tampered_pcr;
    memcpy(&tampered_pcr, &pcr_composite, sizeof(TPMPcrComposite));
    tampered_pcr.pcr_digests[0].digest[0] ^= 0xFF;

    bool tamper_result = false;
    tpm_quote_verify(&quote, &aik_key, &tampered_pcr, fw_version, &tamper_result);
    printf("    Tampered PCR verify: %s (expected FAIL)\n",
           tamper_result ? "PASS" : "FAIL");

    printf("\n[9] Replaying event log...\n");
    uint8_t event_log[256];
    uint16_t el_size = 0;
    for (i = 0; i < 8; i++) {
        event_log[el_size++] = (uint8_t)i;
        uint32_t len = (uint32_t)strlen(events[i]);
        event_log[el_size++] = (uint8_t)(len & 0xFF);
        event_log[el_size++] = (uint8_t)((len >> 8) & 0xFF);
        event_log[el_size++] = (uint8_t)((len >> 16) & 0xFF);
        event_log[el_size++] = (uint8_t)((len >> 24) & 0xFF);
        memcpy(event_log + el_size, events[i], len);
        el_size += (uint16_t)len;
    }

    TPMPcrComposite recomputed;
    bool el_match = false;
    attest_replay_event_log(event_log, el_size, &recomputed, &el_match);
    printf("    Event log replay: %s\n", el_match ? "PASS" : "FAIL");

    bool pcr_cmp = false;
    attest_pcr_compare(&pcr_composite, &recomputed, 0xFF, &pcr_cmp);
    printf("    PCR comparison:   %s\n", pcr_cmp ? "MATCH" : "MISMATCH");

    printf("\n[10] Quote dump:\n");
    tpm_quote_dump(&quote);

    printf("\n=== Demo Complete ===\n");
    return 0;
}
