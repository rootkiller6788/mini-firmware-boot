#include "tpm_quote.h"
#include "aik_identity.h"
#include "attest_protocol.h"
#include "verifier_service.h"
#include "rats.h"
#include "eventlog.h"
#include "merkle_pcr.h"
#include "dice.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) do { \
    printf("  TEST %s... ", name); \
} while(0)

#define PASS() do { \
    printf("PASS\n"); tests_passed++; \
} while(0)

#define FAIL(msg) do { \
    printf("FAIL: %s\n", msg); tests_failed++; \
} while(0)

#define CHECK(cond, msg) do { \
    if (!(cond)) { FAIL(msg); return; } \
} while(0)

/* ---- TPM Quote Tests ---- */
static void test_tpm_quote_basic(void) {
    TEST("TPM Quote create/sign/verify");
    TPMPcrComposite comp;
    tpm_pcr_composite_init(&comp);
    int i;
    for (i = 0; i < 4; i++) {
        TPMHash h;
        tpm_hash_init(&h);
        uint8_t data[4] = {(uint8_t)i, (uint8_t)(i+1), (uint8_t)(i+2), (uint8_t)(i+3)};
        tpm_hash_update(&h, data, 4);
        tpm_hash_final(&h);
        tpm_pcr_composite_add(&comp, (uint8_t)i, &h);
    }
    CHECK(comp.pcr_count == 4, "PCR composite count mismatch");

    TPMQuote quote;
    uint8_t nonce[32];
    memset(nonce, 0xAB, 32);
    int32_t r = tpm_quote_create(&quote, &comp, nonce, 32, NULL, 0, 0x00010001);
    CHECK(r == 0, "Quote create failed");
    CHECK(quote.attest.magic == TPM_GENERATED_VALUE, "Magic mismatch");

    TPMKey aik;
    memset(&aik, 0, sizeof(aik));
    for (i = 0; i < 256; i++) aik.modulus[i] = (uint8_t)(i + 0x40);
    aik.modulus_size = 256;
    aik.exponent[0] = 0x01; aik.exponent[1] = 0x00; aik.exponent[2] = 0x01;
    r = tpm_quote_sign(&quote, &aik);
    CHECK(r == 0, "Quote sign failed");

    bool result = false;
    r = tpm_quote_verify(&quote, &aik, &comp, 0x00010001, &result);
    CHECK(r == 0, "Quote verify returned error");
    CHECK(result == true, "Quote verify should pass");
    PASS();
}

static void test_tpm_quote_tampered(void) {
    TEST("TPM Quote tampered PCR detection");
    TPMPcrComposite comp;
    tpm_pcr_composite_init(&comp);
    TPMHash h;
    tpm_hash_init(&h);
    uint8_t data[] = "test-data";
    tpm_hash_update(&h, data, 9);
    tpm_hash_final(&h);
    tpm_pcr_composite_add(&comp, 0, &h);

    TPMQuote quote;
    uint8_t nonce[32];
    memset(nonce, 0xCD, 32);
    tpm_quote_create(&quote, &comp, nonce, 32, NULL, 0, 0x00020001);

    TPMKey aik;
    memset(&aik, 0, sizeof(aik));
    int i;
    for (i = 0; i < 256; i++) aik.modulus[i] = (uint8_t)i;
    aik.modulus_size = 256;
    aik.exponent[0] = 0x01; aik.exponent[1] = 0x00; aik.exponent[2] = 0x01;
    tpm_quote_sign(&quote, &aik);

    TPMPcrComposite tampered;
    tpm_pcr_composite_init(&tampered);
    TPMHash bad_h;
    tpm_hash_init(&bad_h);
    uint8_t bad[] = "bad-data!";
    tpm_hash_update(&bad_h, bad, 9);
    tpm_hash_final(&bad_h);
    tpm_pcr_composite_add(&tampered, 0, &bad_h);

    bool result = true;
    tpm_quote_verify(&quote, &aik, &tampered, 0x00020001, &result);
    CHECK(result == false, "Tampered PCR should be detected");
    PASS();
}

/* ---- AIK Identity Tests ---- */
static void test_aik_identity_flow(void) {
    TEST("AIK Identity create/certify/activate");
    TPMKeyPublic ek_pub;
    int32_t r = tpm_create_ek(&ek_pub);
    CHECK(r == 0, "EK create failed");
    CHECK(ek_pub.hierarchy == TPM_KEY_HIERARCHY_EK, "EK hierarchy wrong");

    TPMKeyPublic srk;
    memset(&srk, 0, sizeof(srk));
    srk.hierarchy = TPM_KEY_HIERARCHY_SRK;
    int i;
    for (i = 0; i < 256; i++) srk.modulus[i] = (uint8_t)(i + 0x30);
    srk.modulus_size = 256;
    srk.exponent[0] = 0x01; srk.exponent[1] = 0x00; srk.exponent[2] = 0x01;

    TPMKeyAIK aik;
    r = tpm_create_aik(&aik, &srk, &ek_pub);
    CHECK(r == 0, "AIK create failed");
    CHECK(aik.parent == TPM_KEY_HIERARCHY_SRK, "AIK parent wrong");

    uint8_t ek_hash[EK_PUB_HASH_SIZE];
    r = tpm_get_ek_pub_hash(&ek_pub, ek_hash);
    CHECK(r == 0, "EK hash failed");

    uint8_t ca_id[PRIVACY_CA_ID_SIZE];
    memset(ca_id, 0xCA, PRIVACY_CA_ID_SIZE);
    EKCertificate ek_cert;
    memset(&ek_cert, 0, sizeof(ek_cert));
    memcpy(ek_cert.ek_pub_hash, ek_hash, EK_PUB_HASH_SIZE);

    TPMKeyPublic aik_pub;
    memset(&aik_pub, 0, sizeof(aik_pub));
    memcpy(aik_pub.modulus, aik.aik_pub_modulus, aik.aik_pub_modulus_size);
    aik_pub.modulus_size = aik.aik_pub_modulus_size;
    memcpy(aik_pub.exponent, aik.aik_pub_exponent, 3);

    AIKCredential cred;
    r = tpm_aik_certify(&cred, &aik_pub, &ek_pub, &ek_cert, ca_id);
    CHECK(r == 0, "AIK certify failed");
    CHECK(cred.active == true, "Credential not active");

    bool verified = false;
    r = tpm_verify_aik_credential(&cred, &aik_pub, ca_id, &verified);
    CHECK(r == 0, "AIK credential verify failed");
    CHECK(verified == true, "Credential should verify");
    PASS();
}

/* ---- Attest Protocol Tests ---- */
static void test_attest_challenge_response(void) {
    TEST("Attest challenge-response protocol");
    AttestChallenge challenge;
    uint8_t extra[] = "test-challenge";
    attest_challenge_create(&challenge, 0x00FF, extra, 13);
    CHECK(challenge.pcr_selection_mask == 0x00FF, "PCR mask mismatch");

    bool fresh = false;
    attest_challenge_verify_freshness(&challenge, 3600000, &fresh);
    CHECK(fresh == true, "Challenge should be fresh");

    AttestVerifier verifier;
    memset(&verifier, 0, sizeof(verifier));
    int i;
    for (i = 0; i < 4; i++) {
        TPMHash h;
        tpm_hash_init(&h);
        uint8_t d[4] = {(uint8_t)i, (uint8_t)(i+1), (uint8_t)(i+2), (uint8_t)(i+3)};
        tpm_hash_update(&h, d, 4);
        tpm_hash_final(&h);
        memcpy(verifier.known_good_pcr_values[i].digest, h.digest, TPM_SHA256_DIGEST_SIZE);
    }
    verifier.known_good_pcr_count = 4;

    TPMPcrComposite pcr;
    tpm_pcr_composite_init(&pcr);
    for (i = 0; i < 4; i++) {
        TPMHash h;
        tpm_hash_init(&h);
        uint8_t d[4] = {(uint8_t)i, (uint8_t)(i+1), (uint8_t)(i+2), (uint8_t)(i+3)};
        tpm_hash_update(&h, d, 4);
        tpm_hash_final(&h);
        tpm_pcr_composite_add(&pcr, (uint8_t)i, &h);
    }

    TPMKeyAIK aik;
    memset(&aik, 0, sizeof(aik));
    for (i = 0; i < 256; i++) aik.aik_pub_modulus[i] = (uint8_t)(i + 0x50);
    aik.aik_pub_modulus_size = 256;
    aik.aik_pub_exponent[0] = 0x01;

    AttestResponse response;
    attest_response_create(&response, &aik, NULL, &pcr, &challenge,
                           0x00010001, NULL, 0);

    AttestVerdict verdict;
    attest_verify(&response, &challenge, &verifier, &verdict);
    CHECK(verdict.nonce_matched == 1, "Nonce should match");
    PASS();
}

/* ---- Verifier Service Tests ---- */
static void test_verifier_service(void) {
    TEST("Verifier service device registration and verification");
    AttestDB db;
    uint8_t vid[ATTEST_DEVICE_ID_SIZE];
    memset(vid, 0x56, ATTEST_DEVICE_ID_SIZE);
    attest_service_init(&db, vid);
    CHECK(db.initialized == true, "DB not initialized");

    uint8_t dev_id[ATTEST_DEVICE_ID_SIZE];
    memset(dev_id, 0x01, ATTEST_DEVICE_ID_SIZE);
    uint8_t ek_hash[EK_PUB_HASH_SIZE];
    memset(ek_hash, 0xAA, EK_PUB_HASH_SIZE);

    int32_t r = attest_service_register_device(&db, dev_id, ek_hash, NULL, 0);
    CHECK(r == 0, "Device registration failed");
    CHECK(db.device_count == 1, "Device count should be 1");

    AttestDeviceEntry entry;
    r = attest_service_find_device(&db, dev_id, &entry);
    CHECK(r == 0, "Device find failed");
    CHECK(entry.registered == true, "Device not registered");

    AttestResult result;
    r = attest_service_get_attest_result(&db, dev_id, &result);
    CHECK(r == 0, "Get attest result failed");
    PASS();
}

/* ---- RATS Tests ---- */
static void test_rats_evidence_appraisal(void) {
    TEST("RATS evidence generation and appraisal");
    TPMPcrComposite pcr;
    tpm_pcr_composite_init(&pcr);
    TPMHash h;
    tpm_hash_init(&h);
    uint8_t d[] = "rats-test";
    tpm_hash_update(&h, d, 9);
    tpm_hash_final(&h);
    tpm_pcr_composite_add(&pcr, 0, &h);

    TPMQuote quote;
    uint8_t nonce[32];
    memset(nonce, 0x5A, 32);
    tpm_quote_create(&quote, &pcr, nonce, 32, NULL, 0, 0x00030001);

    uint8_t verifier_id[RATS_VERIFIER_ID_SIZE];
    memset(verifier_id, 0x42, RATS_VERIFIER_ID_SIZE);

    RATSEvidence evidence;
    int32_t r = rats_generate_evidence(&evidence, &quote, verifier_id, NULL, 0);
    CHECK(r == 0, "Evidence generation failed");
    CHECK(evidence.evidence_type == RATS_EVIDENCE_TYPE_TPM_QUOTE, "Type mismatch");
    CHECK(evidence.claim_count >= 3, "Should have at least 3 claims");

    RATSVerifier verifier;
    rats_verifier_init(&verifier, verifier_id);
    r = rats_verifier_set_reference(&verifier, pcr.pcr_digests, pcr.pcr_count);
    CHECK(r == 0, "Set reference failed");

    RATSAppraisalResult appraisal;
    r = rats_appraise_evidence(&evidence, &verifier, &appraisal);
    CHECK(r == 0, "Appraisal failed");
    CHECK(appraisal == RATS_APPRAISAL_PASS, "Should pass appraisal");
    PASS();
}

/* ---- Event Log Tests ---- */
static void test_eventlog_basic(void) {
    TEST("Event log add/replay");
    TCGEventLogSHA256 log;
    tcg_eventlog_sha256_init(&log);

    uint8_t data1[] = "BIOS POST";
    uint8_t data2[] = "Bootloader";
    uint8_t data3[] = "OS Kernel";
    int32_t r;

    r = tcg_eventlog_sha256_add(&log, 0, EV_POST_CODE, data1, 9);
    CHECK(r == 0, "Add event 1 failed");
    r = tcg_eventlog_sha256_add(&log, 0, EV_IPL, data2, 10);
    CHECK(r == 0, "Add event 2 failed");
    r = tcg_eventlog_sha256_add(&log, 1, EV_ACTION, data3, 9);
    CHECK(r == 0, "Add event 3 failed");
    CHECK(log.entry_count == 3, "Entry count mismatch");

    r = tcg_eventlog_sha256_seal(&log);
    CHECK(r == 0, "Seal failed");
    CHECK(log.sealed == true, "Log not sealed");

    PASS();
}

static void test_eventlog_replay(void) {
    TEST("Event log replay verification");
    TCGEventLogSHA256 log;
    tcg_eventlog_sha256_init(&log);

    int i;
    const char *events[] = {"CRTM", "BIOS", "OptionROM", "MBR", "Bootloader", "Kernel"};
    for (i = 0; i < 6; i++) {
        tcg_eventlog_sha256_add(&log, 0, EV_ACTION,
                                (const uint8_t *)events[i],
                                (uint32_t)strlen(events[i]));
    }

    TPMPcrComposite expected;
    tpm_pcr_composite_init(&expected);
    TPMHash pcr0;
    tpm_hash_init(&pcr0);
    for (i = 0; i < 6; i++) {
        tcg_pcr_extend_with_data(&pcr0,
                                  (const uint8_t *)events[i],
                                  (uint32_t)strlen(events[i]));
    }
    tpm_hash_final(&pcr0);
    tpm_pcr_composite_add(&expected, 0, &pcr0);

    TPMPcrComposite recomputed;
    bool match = false;
    int32_t r = tcg_eventlog_replay_sha256(&log, &recomputed, &match,
                                            &expected, 0x0001);
    CHECK(r == 0, "Replay return error");
    CHECK(match == true, "Recomputed PCR should match expected");
    PASS();
}

static void test_eventlog_integrity(void) {
    TEST("Event log integrity hash");
    TCGEventLogSHA256 log;
    tcg_eventlog_sha256_init(&log);
    tcg_eventlog_sha256_add(&log, 0, EV_S_CRTM_CONTENTS,
                            (const uint8_t *)"CRTM", 4);

    TPMHash expected_hash;
    tpm_hash_init(&expected_hash);
    tpm_hash_update(&expected_hash, log.entries[0].digest, TPM_SHA256_DIGEST_SIZE);
    tpm_hash_update(&expected_hash, (const uint8_t *)&log.entries[0].event_type, 4);
    tpm_hash_update(&expected_hash, &log.entries[0].pcr_index, 1);
    tpm_hash_final(&expected_hash);

    bool valid = false;
    tcg_eventlog_integrity_check(&log, &expected_hash, &valid);
    CHECK(valid == true, "Integrity check should pass");
    PASS();
}

static void test_pcr_extend(void) {
    TEST("PCR extend operation properties");
    TPMHash pcr_a, pcr_b;
    tpm_hash_init(&pcr_a);
    tpm_hash_init(&pcr_b);

    uint8_t data1[] = "measurement-A";
    uint8_t data2[] = "measurement-B";

    tcg_pcr_extend_with_data(&pcr_a, data1, 13);
    tcg_pcr_extend_with_data(&pcr_b, data1, 13);

    CHECK(memcmp(pcr_a.digest, pcr_b.digest, TPM_SHA256_DIGEST_SIZE) == 0,
          "Same extend should produce same PCR");

    tcg_pcr_extend_with_data(&pcr_a, data2, 13);

    CHECK(memcmp(pcr_a.digest, pcr_b.digest, TPM_SHA256_DIGEST_SIZE) != 0,
          "Different extend should produce different PCR");
    PASS();
}

/* ---- Merkle Tree Tests ---- */
static void test_merkle_tree_basic(void) {
    TEST("Merkle tree build and root");
    TPMHash leaves[4];
    int i;
    for (i = 0; i < 4; i++) {
        tpm_hash_init(&leaves[i]);
        uint8_t d[2] = {(uint8_t)i, (uint8_t)(i + 10)};
        tpm_hash_update(&leaves[i], d, 2);
        tpm_hash_final(&leaves[i]);
    }

    MerkleTree tree;
    merkle_tree_init(&tree);
    int32_t r = merkle_tree_build(&tree, leaves, 4);
    CHECK(r == 0, "Tree build failed");
    CHECK(tree.built == true, "Tree not built");
    CHECK(tree.leaf_count == 4, "Leaf count mismatch");

    TPMHash root;
    r = merkle_tree_get_root(&tree, &root);
    CHECK(r == 0, "Get root failed");
    PASS();
}

static void test_merkle_proof(void) {
    TEST("Merkle inclusion proof");
    TPMHash leaves[8];
    int i;
    for (i = 0; i < 8; i++) {
        tpm_hash_init(&leaves[i]);
        uint8_t d[4] = {(uint8_t)(i*4), (uint8_t)(i*4+1), (uint8_t)(i*4+2), (uint8_t)(i*4+3)};
        tpm_hash_update(&leaves[i], d, 4);
        tpm_hash_final(&leaves[i]);
    }

    MerkleTree tree;
    merkle_tree_init(&tree);
    merkle_tree_build(&tree, leaves, 8);

    MerkleProof proof;
    int32_t r = merkle_generate_proof(&tree, 3, &proof);
    CHECK(r == 0, "Proof generation failed");
    CHECK(proof.leaf_index == 3, "Leaf index mismatch");

    bool valid = false;
    r = merkle_verify_proof(&proof, &valid);
    CHECK(r == 0, "Proof verification error");
    CHECK(valid == true, "Proof should be valid");

    proof.leaf_value.digest[0] ^= 0xFF;
    r = merkle_verify_proof(&proof, &valid);
    CHECK(r == 0, "Proof verification error");
    CHECK(valid == false, "Tampered proof should be invalid");
    PASS();
}

static void test_merkle_pcr_bank(void) {
    TEST("Merkle root from PCR bank");
    TPMPcrComposite bank;
    tpm_pcr_composite_init(&bank);
    int i;
    for (i = 0; i < 5; i++) {
        TPMHash h;
        tpm_hash_init(&h);
        uint8_t d[4] = {(uint8_t)i, (uint8_t)(i+1), (uint8_t)(i+2), (uint8_t)(i+3)};
        tpm_hash_update(&h, d, 4);
        tpm_hash_final(&h);
        tpm_pcr_composite_add(&bank, (uint8_t)i, &h);
    }

    TPMHash root_a, root_b;
    merkle_root_from_pcr_bank(&bank, &root_a);
    merkle_root_from_pcr_bank(&bank, &root_b);

    bool match = false;
    merkle_compare_roots(&root_a, &root_b, &match);
    CHECK(match == true, "Same bank should produce same root");
    PASS();
}

/* ---- DICE Tests ---- */
static void test_dice_uds_cdi(void) {
    TEST("DICE UDS derivation and CDI computation");
    uint8_t pud[32];
    int i;
    for (i = 0; i < 32; i++) pud[i] = (uint8_t)(i * 7 + 13);

    DICEUniqueDeviceSecret uds;
    int32_t r = dice_uds_derive(pud, 32, &uds);
    CHECK(r == 0, "UDS derive failed");

    DICELayerInputs l0;
    memset(&l0, 0, sizeof(l0));
    l0.level = DICE_LAYER_L0_HARDWARE;
    l0.version = 1;
    for (i = 0; i < DICE_CODE_HASH_SIZE; i++) l0.code_hash[i] = (uint8_t)(i + 0xA0);

    DICECompoundDeviceID cdi0;
    r = dice_derive_cdi_from_uds(&uds, &l0, &cdi0);
    CHECK(r == 0, "CDI derive failed");
    CHECK(cdi0.valid == true, "CDI not valid");

    DICELayerInputs l1;
    memset(&l1, 0, sizeof(l1));
    l1.level = DICE_LAYER_L1_BOOT_ROM;
    l1.version = 2;
    for (i = 0; i < DICE_CODE_HASH_SIZE; i++) l1.code_hash[i] = (uint8_t)(i + 0xB0);

    DICECompoundDeviceID cdi1;
    r = dice_cdi_compute(&cdi0, &l1, &cdi1);
    CHECK(r == 0, "CDI chain failed");
    CHECK(cdi1.valid == true, "CDI1 not valid");
    PASS();
}

static void test_dice_layer_keys(void) {
    TEST("DICE layer key derivation");
    DICECompoundDeviceID cdi;
    memset(&cdi, 0, sizeof(cdi));
    int i;
    for (i = 0; i < DICE_CDI_SIZE; i++) cdi.data[i] = (uint8_t)(i * 3 + 7);
    cdi.valid = true;

    DICELayerState state;
    int32_t r = dice_layer_derive_keys(&state, &cdi);
    CHECK(r == 0, "Key derive failed");
    CHECK(state.key_pair_generated == true, "Keys not generated");

    TPMKeyPublic dev_pub;
    r = dice_export_device_id_public(&state, &dev_pub);
    CHECK(r == 0, "Export DeviceID failed");

    TPMKeyPublic alias_pub;
    r = dice_export_alias_key_public(&state, &alias_pub);
    CHECK(r == 0, "Export AliasKey failed");

    bool diff = (memcmp(dev_pub.modulus, alias_pub.modulus, AIK_KEY_SIZE) != 0);
    CHECK(diff == true, "DeviceID and AliasKey should differ");
    PASS();
}

static void test_dice_cert_chain(void) {
    TEST("DICE certificate chain build and verify");
    uint8_t pud[32];
    int i;
    for (i = 0; i < 32; i++) pud[i] = (uint8_t)(i * 11 + 31);

    DICEUniqueDeviceSecret uds;
    dice_uds_derive(pud, 32, &uds);

    DICELayerInputs layers[4];
    DICELayerLevel levels[] = {
        DICE_LAYER_L0_HARDWARE, DICE_LAYER_L1_BOOT_ROM,
        DICE_LAYER_L2_BOOTLOADER, DICE_LAYER_L3_OS_KERNEL
    };

    for (i = 0; i < 4; i++) {
        memset(&layers[i], 0, sizeof(DICELayerInputs));
        layers[i].level = levels[i];
        layers[i].version = (uint64_t)(i + 1);
        uint8_t j;
        for (j = 0; j < DICE_CODE_HASH_SIZE; j++)
            layers[i].code_hash[j] = (uint8_t)(i * 64 + j);
        for (j = 0; j < DICE_CONFIG_HASH_SIZE; j++)
            layers[i].config_hash[j] = (uint8_t)(i * 32 + j + 128);
        for (j = 0; j < DICE_AUTHORITY_HASH_SIZE; j++)
            layers[i].authority_hash[j] = (uint8_t)(i * 16 + j + 64);
    }

    DICECertChain chain;
    int32_t r = dice_chain_build_layered_attestation(&uds, layers, 4, &chain);
    CHECK(r == 0, "Chain build failed");
    CHECK(chain.cert_count == 4, "Cert count mismatch");

    bool verified = false;
    r = dice_chain_verify(&chain, &verified);
    CHECK(r == 0, "Chain verify error");
    CHECK(verified == true, "Chain should verify");
    PASS();
}

/* ---- PCR Bank Compare Tests ---- */
static void test_pcr_bank_compare(void) {
    TEST("PCR bank comparison");
    TPMPcrComposite bank_a, bank_b;
    tpm_pcr_composite_init(&bank_a);
    tpm_pcr_composite_init(&bank_b);

    TPMHash h;
    tpm_hash_init(&h);
    uint8_t d[] = "same-data";
    tpm_hash_update(&h, d, 9);
    tpm_hash_final(&h);

    tpm_pcr_composite_add(&bank_a, 0, &h);
    tpm_pcr_composite_add(&bank_b, 0, &h);

    bool match = false;
    tcg_pcr_bank_compare(&bank_a, &bank_b, 0, &match);
    CHECK(match == true, "Same PCR should match");

    tcg_pcr_bank_compare(&bank_a, &bank_b, 1, &match);
    CHECK(match == true, "Unset PCRs should both be zero (match)");
    PASS();
}

/* ---- Hash Sequence Tests ---- */
static void test_hash_sequence(void) {
    TEST("Hash sequence start/update/end");
    TCGHashSequence seq;
    tcg_hash_sequence_start(&seq);
    CHECK(seq.started == true, "Sequence not started");

    uint8_t part1[] = "Hello ";
    uint8_t part2[] = "World!";
    tcg_hash_sequence_update(&seq, part1, 6);
    tcg_hash_sequence_update(&seq, part2, 6);
    CHECK(seq.data_hashed == 12, "Data hashed count off");

    TPMHash result;
    tcg_hash_sequence_end(&seq, &result);
    CHECK(seq.started == false, "Sequence not ended");
    PASS();
}

/* ---- Edge Case Tests ---- */
static void test_null_pointer_safety(void) {
    TEST("Null pointer safety");
    CHECK(tpm_quote_create(NULL, NULL, NULL, 0, NULL, 0, 0) != 0, "Null quote");
    CHECK(tpm_create_ek(NULL) != 0, "Null EK");
    CHECK(tpm_create_aik(NULL, NULL, NULL) != 0, "Null AIK");
    CHECK(attest_challenge_create(NULL, 0, NULL, 0) != 0, "Null challenge");
    CHECK(attest_verify(NULL, NULL, NULL, NULL) != 0, "Null verify");
    CHECK(rats_generate_evidence(NULL, NULL, NULL, NULL, 0) != 0, "Null evidence");
    CHECK(rats_appraise_evidence(NULL, NULL, NULL) != 0, "Null appraise");
    CHECK(attest_service_init(NULL, NULL) != 0, "Null service init");
    CHECK(attest_service_register_device(NULL, NULL, NULL, NULL, 0) != 0, "Null register");
    CHECK(tcg_eventlog_sha256_add(NULL, 0, 0, NULL, 0) != 0, "Null eventlog");
    CHECK(merkle_tree_build(NULL, NULL, 0) != 0, "Null merkle");
    CHECK(merkle_generate_proof(NULL, 0, NULL) != 0, "Null proof gen");
    CHECK(merkle_verify_proof(NULL, NULL) != 0, "Null proof verify");
    CHECK(dice_uds_derive(NULL, 0, NULL) != 0, "Null UDS");
    CHECK(dice_cdi_compute(NULL, NULL, NULL) != 0, "Null CDI");
    CHECK(dice_layer_derive_keys(NULL, NULL) != 0, "Null derive keys");
    CHECK(dice_chain_build_layered_attestation(NULL, NULL, 0, NULL) != 0, "Null chain build");
    PASS();
}

int main(void) {
    printf("=== mini-boot-attestation Test Suite ===\n\n");

    test_tpm_quote_basic();
    test_tpm_quote_tampered();
    test_aik_identity_flow();
    test_attest_challenge_response();
    test_verifier_service();
    test_rats_evidence_appraisal();
    test_eventlog_basic();
    test_eventlog_replay();
    test_eventlog_integrity();
    test_pcr_extend();
    test_merkle_tree_basic();
    test_merkle_proof();
    test_merkle_pcr_bank();
    test_dice_uds_cdi();
    test_dice_layer_keys();
    test_dice_cert_chain();
    test_pcr_bank_compare();
    test_hash_sequence();
    test_null_pointer_safety();

    printf("\n=== Results: %d passed, %d failed ===\n",
           tests_passed, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}