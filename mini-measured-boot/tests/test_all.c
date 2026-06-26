/* Comprehensive test suite for mini-measured-boot
 * Covers all core modules: PCR, Event Log, SRTM/DRTM,
 * NV Storage, TPM Keys, Attestation, Locality, HMAC.
 *
 * Each test case verifies a specific knowledge point (L1-L6).
 * Uses assert() for automated pass/fail detection.
 */
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "sha256.h"
#include "pcr_bank.h"
#include "event_log.h"
#include "crtm_drtm.h"
#include "tpm_locality.h"
#include "nv_storage.h"
#include "tpm_keys.h"
#include "attestation.h"
#include "hmac_tpm.h"
#include "tpm2_structs.h"

static int tests_passed = 0;
static int tests_failed = 0;

#define RUN_TEST(name, expr) do { \
    printf("  TEST %-40s ", name); \
    if (expr) { printf("PASS\n"); tests_passed++; } \
    else { printf("FAIL\n"); tests_failed++; } \
} while(0)

/* ---- L1: SHA-256 hash correctness ---- */
static void test_sha256(void) {
    uint8_t hash[SHA256_DIGEST_SIZE];
    uint8_t expected[32] = {
        0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,
        0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,
        0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c,
        0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad
    };
    sha256_hash((const uint8_t*)"abc", 3, hash);
    RUN_TEST("SHA256(abc) known vector",
             memcmp(hash, expected, 32) == 0);
}

/* ---- L2: PCR extend and read ---- */
static void test_pcr_extend(void) {
    PCRBank bank;
    uint8_t data[SHA256_DIGEST_SIZE];
    uint8_t readback[SHA256_DIGEST_SIZE];
    uint8_t zero[SHA256_DIGEST_SIZE];

    memset(zero, 0, SHA256_DIGEST_SIZE);
    pcr_bank_init(&bank, TPM_ALG_SHA256);

    /* PCR should start as zero */
    pcr_read(&bank, 0, readback);
    RUN_TEST("PCR0 initial value is zero",
             memcmp(readback, zero, SHA256_DIGEST_SIZE) == 0);

    /* Extend PCR0 */
    sha256_hash((const uint8_t*)"test_data", 9, data);
    {
        bool ok = pcr_extend(&bank, 0, data, SHA256_DIGEST_SIZE);
        RUN_TEST("PCR0 extend succeeds", ok);
    }

    /* PCR0 should now be non-zero */
    pcr_read(&bank, 0, readback);
    RUN_TEST("PCR0 after extend is non-zero",
             memcmp(readback, zero, SHA256_DIGEST_SIZE) != 0);

    /* Invalid PCR index */
    RUN_TEST("PCR extend invalid index fails",
             !pcr_extend(&bank, 99, data, SHA256_DIGEST_SIZE));

    /* NULL data */
    RUN_TEST("PCR extend NULL data fails",
             !pcr_extend(&bank, 0, NULL, 0));

    /* DRTM PCR reset */
    {
        uint8_t tmp[SHA256_DIGEST_SIZE];
        sha256_hash((const uint8_t*)"drtm_test", 9, tmp);
        pcr_extend(&bank, 17, tmp, SHA256_DIGEST_SIZE);
        bool ok = pcr_reset(&bank, 17);
        RUN_TEST("PCR17 DRTM reset succeeds", ok);
        pcr_read(&bank, 17, readback);
        RUN_TEST("PCR17 after reset is zero",
                 memcmp(readback, zero, SHA256_DIGEST_SIZE) == 0);
    }

    /* Static PCR reset should fail */
    RUN_TEST("PCR0 (static) reset denied",
             !pcr_reset(&bank, 0));

    /* pcr_get_all copies correctly */
    {
        uint8_t all_pcrs[PCR_COUNT][SHA256_DIGEST_SIZE];
        pcr_get_all(&bank, all_pcrs);
        uint8_t pcr0[SHA256_DIGEST_SIZE];
        pcr_read(&bank, 0, pcr0);
        RUN_TEST("pcr_get_all matches pcr_read",
                 memcmp(all_pcrs[0], pcr0, SHA256_DIGEST_SIZE) == 0);
    }
}

/* ---- L3: Event log operations ---- */
static void test_event_log(void) {
    TCGEventLog log;
    PCRBank bank;
    uint8_t hash[SHA256_DIGEST_SIZE];

    event_log_init(&log, 0x0200);
    pcr_bank_init(&bank, TPM_ALG_SHA256);

    RUN_TEST("Event log init count is 0", log.event_count == 0);
    RUN_TEST("Event log SHA256 enabled", log.sha256_used);

    sha256_hash((const uint8_t*)"evt1", 4, hash);
    {
        bool ok = event_log_add(&log, 0, EV_S_CRTM_CONTENTS, hash,
                                (const uint8_t*)"CRTM", 4);
        RUN_TEST("Event log add succeeds", ok);
        RUN_TEST("Event log count incremented", log.event_count == 1);
    }

    /* Replay event log */
    {
        bool ok = event_log_replay(&log, &bank);
        RUN_TEST("Event log replay succeeds", ok);
    }

    /* Verify PCR via event log */
    {
        uint8_t expected_pcr[SHA256_DIGEST_SIZE];
        pcr_read(&bank, 0, expected_pcr);
        bool ok = event_log_verify_pcr(&log, &bank, 0, expected_pcr,
                                        TPM_ALG_SHA256);
        RUN_TEST("Event log PCR verification succeeds", ok);
    }

    RUN_TEST("Event log NULL add fails",
             !event_log_add(NULL, 0, 0, hash, (const uint8_t*)"x", 1));
}
/* ---- L4: NV Storage operations ---- */
static void test_nv_storage(void) {
    NVStorage nv;
    uint8_t auth[32] = "test_auth_key_32_bytes_long!";
    uint8_t data[64] = "Hello TPM NV Storage! This is test data.";
    uint8_t read_back[128];
    uint16_t read_len;
    uint32_t slot;
    uint32_t counter_val;
    bool ok;

    nv_storage_init(&nv);
    RUN_TEST("NV storage init count=0", nv_get_index_count(&nv) == 0);

    /* Define an ordinary NV index */
    ok = nv_define_space(&nv, 0x01000001, NV_TYPE_ORDINARY,
                          NV_ATTR_OWNERREAD | NV_ATTR_OWNERWRITE |
                          NV_ATTR_AUTHREAD | NV_ATTR_AUTHWRITE,
                          128, auth, 30, NULL, &slot);
    RUN_TEST("NV define ordinary index", ok);
    RUN_TEST("NV index count=1", nv_get_index_count(&nv) == 1);

    /* Write data */
    ok = nv_write(&nv, 0x01000001, data, 35,
                  NV_AUTH_INDEX, auth, 30);
    RUN_TEST("NV write with index auth", ok);

    /* Read data back */
    ok = nv_read(&nv, 0x01000001, read_back, &read_len,
                 NV_AUTH_INDEX, auth, 30);
    RUN_TEST("NV read with index auth", ok);
    RUN_TEST("NV read data matches", read_len == 35 &&
             memcmp(read_back, data, 35) == 0);

    /* Wrong auth fails */
    ok = nv_read(&nv, 0x01000001, read_back, &read_len,
                 NV_AUTH_INDEX, (const uint8_t*)"wrong!", 6);
    RUN_TEST("NV read wrong auth fails", !ok);

    /* Define a counter */
    ok = nv_define_space(&nv, 0x01000002, NV_TYPE_COUNTER,
                          NV_ATTR_OWNERWRITE | NV_ATTR_OWNERREAD,
                          8, NULL, 0, NULL, NULL);
    RUN_TEST("NV define counter index", ok);

    /* Increment counter */
    ok = nv_counter_increment(&nv, 0x01000002, &counter_val);
    RUN_TEST("NV counter increment to 1", ok && counter_val == 1);

    ok = nv_counter_increment(&nv, 0x01000002, &counter_val);
    RUN_TEST("NV counter increment to 2", ok && counter_val == 2);

    /* Counter cannot be written directly */
    ok = nv_write(&nv, 0x01000002, data, 4, NV_AUTH_OWNER, NULL, 0);
    RUN_TEST("NV counter direct write denied", !ok);

    /* Define an EXTEND index */
    ok = nv_define_space(&nv, 0x01000003, NV_TYPE_EXTEND,
                          NV_ATTR_OWNERWRITE | NV_ATTR_OWNERREAD,
                          SHA256_DIGEST_SIZE, NULL, 0, NULL, NULL);
    RUN_TEST("NV define extend index", ok);

    /* Extend with data (PCR-like) */
    {
        uint8_t ext_data[32] = "measurement_01_measurement_01_";
        uint8_t after[SHA256_DIGEST_SIZE];
        ok = nv_extend(&nv, 0x01000003, ext_data, 20,
                       NV_AUTH_OWNER, NULL, 0);
        RUN_TEST("NV extend succeeds", ok);

        nv_read(&nv, 0x01000003, after, &read_len, NV_AUTH_OWNER, NULL, 0);
        RUN_TEST("NV extend produces hash", read_len == SHA256_DIGEST_SIZE);
    }

    /* Define a BITFIELD index */
    ok = nv_define_space(&nv, 0x01000004, NV_TYPE_BITFIELD,
                          NV_ATTR_OWNERWRITE | NV_ATTR_OWNERREAD,
                          8, NULL, 0, NULL, NULL);
    RUN_TEST("NV define bitfield index", ok);

    ok = nv_setbits(&nv, 0x01000004, 0x0F, NV_AUTH_OWNER, NULL, 0);
    RUN_TEST("NV setbits first", ok);

    ok = nv_setbits(&nv, 0x01000004, 0xF0, NV_AUTH_OWNER, NULL, 0);
    RUN_TEST("NV setbits second (OR accumulates)", ok);

    /* Global lock */
    nv_global_lock(&nv);
    RUN_TEST("NV global lock activated", nv.global_lock);

    /* Change auth */
    {
        uint8_t new_auth[10] = "newsecret";
        ok = nv_change_auth(&nv, 0x01000001, auth, 30, new_auth, 9);
        RUN_TEST("NV change auth succeeds", ok);
    }
}

/* ---- L5: TPM Key management ---- */
static void test_tpm_keys(void) {
    TPMKeyManager mgr;
    uint32_t srk_handle, ek_handle;
    uint8_t auth[10] = "ownerpass";
    bool ok;

    tpm_key_manager_init(&mgr);
    RUN_TEST("Key manager init empty", mgr.key_count == 0);

    ok = tpm_generate_primary_seeds(&mgr);
    RUN_TEST("Primary seeds generated", ok && mgr.seeds_generated);

    /* Create Storage Root Key */
    ok = tpm_create_primary(&mgr, TPM_HIERARCHY_OWNER,
                            TPM_KEYTYPE_RSA_2048,
                            TPM_KEYATTR_RESTRICTED | TPM_KEYATTR_DECRYPT |
                            TPM_KEYATTR_FIXEDTPM | TPM_KEYATTR_FIXEDPARENT,
                            auth, 9, NULL, &srk_handle);
    RUN_TEST("Create SRK (storage primary)", ok);

    /* Create Endorsement Key */
    ok = tpm_create_primary(&mgr, TPM_HIERARCHY_ENDORSEMENT,
                            TPM_KEYTYPE_RSA_2048,
                            TPM_KEYATTR_RESTRICTED | TPM_KEYATTR_DECRYPT |
                            TPM_KEYATTR_FIXEDTPM | TPM_KEYATTR_FIXEDPARENT,
                            NULL, 0, NULL, &ek_handle);
    RUN_TEST("Create EK (endorsement primary)", ok);

    RUN_TEST("Key manager count=2", mgr.key_count == 2);

    /* Key attribute validation */
    ok = tpm_validate_key_attrs(
             TPM_KEYATTR_FIXEDTPM | TPM_KEYATTR_FIXEDPARENT,
             TPM_KEYTYPE_RSA_2048);
    RUN_TEST("Valid key attrs pass", ok);

    ok = tpm_validate_key_attrs(
             TPM_KEYATTR_FIXEDTPM,  /* missing FIXEDPARENT */
             TPM_KEYTYPE_RSA_2048);
    RUN_TEST("Invalid key attrs (FIXEDTPM w/o FIXEDPARENT) fail", !ok);

    /* Flush a key */
    ok = tpm_flush_key(&mgr, srk_handle);
    RUN_TEST("Flush key succeeds", ok);
    RUN_TEST("Key count decremented", mgr.key_count == 1);
}
/* ---- L6: Remote attestation ---- */
static void test_attestation(void) {
    AttestVerifier verifier;
    TPMKeyManager key_mgr;
    PCRBank bank;
    AttestResponse response;
    uint32_t aik_handle;
    uint32_t pcr_indices[3] = {0, 4, 8};
    uint32_t prop_result;
    bool ok;

    memset(&verifier, 0, sizeof(verifier));
    tpm_key_manager_init(&key_mgr);
    tpm_generate_primary_seeds(&key_mgr);
    pcr_bank_init(&bank, TPM_ALG_SHA256);

    /* Create AIK for attestation */
    ok = tpm_create_primary(&key_mgr, TPM_HIERARCHY_ENDORSEMENT,
                            TPM_KEYTYPE_RSA_2048,
                            TPM_KEYATTR_SIGN | TPM_KEYATTR_RESTRICTED |
                            TPM_KEYATTR_FIXEDTPM | TPM_KEYATTR_FIXEDPARENT,
                            NULL, 0, NULL, &aik_handle);
    RUN_TEST("AIK created for attestation", ok);

    /* Generate challenge */
    attest_generate_challenge(&verifier, pcr_indices, 3);
    RUN_TEST("Challenge generated (pcr_count=3)",
             verifier.last_challenge.pcr_count == 3);

    /* Set golden PCR values */
    {
        uint8_t zero[SHA256_DIGEST_SIZE];
        memset(zero, 0, SHA256_DIGEST_SIZE);
        attest_set_golden_pcr(&verifier, 0, zero);
        attest_set_golden_pcr(&verifier, 4, zero);
        attest_set_golden_pcr(&verifier, 8, zero);
        RUN_TEST("Golden PCRs set", verifier.golden_defined[0] &&
                 verifier.golden_defined[4] && verifier.golden_defined[8]);
    }

    /* Create quote */
    ok = attest_create_quote(&bank, &verifier.last_challenge,
                              &key_mgr, aik_handle, &response.quote);
    RUN_TEST("Quote creation succeeds", ok);

    /* Copy AIK public for verification */
    {
        uint32_t i;
        for (i = 0; i < key_mgr.key_count; i++) {
            if (key_mgr.keys[i].handle == aik_handle) {
                response.aik_public = key_mgr.keys[i].public_part;
                memcpy(response.aik_name, key_mgr.keys[i].name,
                       SHA256_DIGEST_SIZE);
                break;
            }
        }
    }

    /* Verify quote */
    ok = attest_verify_quote(&verifier, &response.quote,
                              &response.aik_public);
    RUN_TEST("Quote verification succeeds", ok);

    /* End-to-end integrity verification */
    ok = attest_verify_integrity(&verifier, &response);
    RUN_TEST("End-to-end attestation integrity verified", ok);

    /* Formal property check */
    prop_result = attest_check_properties(&verifier, &response);
    RUN_TEST("Freshness property holds",
             (prop_result & ATT_PROP_FRESHNESS) != 0);
    RUN_TEST("Binding property holds",
             (prop_result & ATT_PROP_BINDING) != 0);
    RUN_TEST("Trust property holds",
             (prop_result & ATT_PROP_TRUST) != 0);
}

/* ---- L5: TPM Locality and authorization ---- */
static void test_tpm_locality(void) {
    TPMAuthManager mgr;
    uint32_t session_handle;
    uint8_t pcr_digest[SHA256_DIGEST_SIZE];
    uint8_t auth_value[11] = "tpm-secret";
    bool ok;

    tpm_auth_manager_init(&mgr);
    RUN_TEST("Auth manager init", mgr.current_locality == TPM_LOCALITY_0);

    /* Set locality */
    ok = tpm_set_locality(&mgr, TPM_LOCALITY_4);
    RUN_TEST("Set locality to 4 (CPU TXT)", ok);
    RUN_TEST("Get locality returns 4",
             tpm_get_locality(&mgr) == TPM_LOCALITY_4);

    /* Invalid locality */
    ok = tpm_set_locality(&mgr, 99);
    RUN_TEST("Set invalid locality fails", !ok);

    /* Start auth session */
    ok = tpm_start_auth_session(&mgr, &session_handle,
                                 TPM_AUTH_HMAC, TPM_SE_HMAC);
    RUN_TEST("Start HMAC auth session", ok);
    RUN_TEST("Session count=1", mgr.session_count == 1);

    /* Policy PCR binding */
    sha256_hash((const uint8_t*)"pcr_test_value", 14, pcr_digest);
    ok = tpm_policy_pcr(&mgr, session_handle, pcr_digest, 1);
    RUN_TEST("Policy PCR binding succeeds", ok);

    /* Policy password */
    ok = tpm_policy_password(&mgr, session_handle);
    RUN_TEST("Policy password approval", ok);

    /* Authorization check */
    {
        TPMAuthCommand cmd;
        memset(&cmd, 0, sizeof(cmd));
        memcpy(cmd.auth_value, auth_value, 9);
        cmd.auth_value_size = 9;

        ok = tpm_authorize_command(&mgr, &cmd, auth_value, 9);
        RUN_TEST("Auth command with correct value", ok);

        ok = tpm_authorize_command(&mgr, &cmd,
                                    (const uint8_t*)"wrong", 5);
        RUN_TEST("Auth command with wrong value fails", !ok);
    }
}

/* ---- L5: HMAC-SHA256 correctness ---- */
static void test_hmac(void) {
    /* RFC 4231 Test Case 1 */
    uint8_t key[20] = {0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,
                       0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,
                       0x0b,0x0b,0x0b,0x0b};
    uint8_t data[9] = "Hi There";
    uint8_t mac[32];
    uint8_t expected[32] = {
        0xb0,0x34,0x4c,0x61,0xd8,0xdb,0x38,0x53,
        0x5c,0xa8,0xaf,0xce,0xaf,0x0b,0xf1,0x2b,
        0x88,0x1d,0xc2,0x00,0xc9,0x83,0x3d,0xa7,
        0x26,0xe9,0x37,0x6c,0x2e,0x32,0xcf,0xf7
    };

    tpm_hmac_sha256(key, 20, data, 8, mac);
    RUN_TEST("HMAC-SHA256 RFC 4231 Test Case 1",
             memcmp(mac, expected, 32) == 0);
}

/* ---- L2: SRTM/DRTM flow ---- */
static void test_srtm_drtm(void) {
    PCRBank bank;
    TCGEventLog log;
    SRTMFlow srtm;
    DRTMFlow drtm;
    uint8_t measurement[SHA256_DIGEST_SIZE];

    pcr_bank_init(&bank, TPM_ALG_SHA256);
    event_log_init(&log, 0x0200);

    /* SRTM init */
    srtm_flow_init(&srtm, &bank, &log);
    RUN_TEST("SRTM flow init", srtm.pcr_bank != NULL);

    /* SRTM measurement chain */
    sha256_hash((const uint8_t*)"bios", 4, measurement);
    {
        bool ok = srtm_measure_bios(&srtm, measurement);
        RUN_TEST("SRTM measure BIOS", ok);
    }

    sha256_hash((const uint8_t*)"bootloader", 10, measurement);
    {
        bool ok = srtm_measure_bootloader(&srtm, measurement);
        RUN_TEST("SRTM measure bootloader", ok);
    }

    /* DRTM init */
    drtm_flow_init(&drtm, &bank, &log);
    RUN_TEST("DRTM flow init", drtm.pcr_bank != NULL);

    sha256_hash((const uint8_t*)"sinit", 5, measurement);
    {
        bool ok = drtm_measure_sinit(&drtm, measurement);
        RUN_TEST("DRTM measure SINIT", ok);
    }

    sha256_hash((const uint8_t*)"mle_data", 8, measurement);
    {
        bool ok = drtm_measure_mle(&drtm, measurement);
        RUN_TEST("DRTM measure MLE", ok);
    }

    /* TXT verification */
    {
        bool ok = txt_verify_launch(&drtm);
        RUN_TEST("TXT launch verification", ok);
    }
}

/* ---- L1: TPM structure helpers ---- */
static void test_tpm_structs(void) {
    RUN_TEST("Hash size SHA256=32",
             tpm2_hash_size(TPM_ALG_SHA256) == 32);
    RUN_TEST("Hash size SHA1=20",
             tpm2_hash_size(TPM_ALG_SHA1) == 20);
    RUN_TEST("Hash size unknown=0",
             tpm2_hash_size(0xFFFF) == 0);

    RUN_TEST("alg_id_to_string SHA256",
             strcmp(tpm2_alg_id_to_string(TPM_ALG_SHA256), "SHA256") == 0);
    RUN_TEST("rc_to_string SUCCESS",
             strcmp(tpm2_rc_to_string(TPM_RC_SUCCESS), "SUCCESS") == 0);
}

/* ---- Main ---- */
int main(void) {
    printf("\n=== mini-measured-boot Test Suite ===\n\n");

    printf("[L1] TPM Structures:\n");
    test_tpm_structs();

    printf("\n[L1] SHA-256:\n");
    test_sha256();

    printf("\n[L2] PCR Bank:\n");
    test_pcr_extend();

    printf("\n[L3] Event Log:\n");
    test_event_log();

    printf("\n[L2] SRTM / DRTM:\n");
    test_srtm_drtm();

    printf("\n[L4] NV Storage:\n");
    test_nv_storage();

    printf("\n[L5] TPM Keys:\n");
    test_tpm_keys();

    printf("\n[L5] HMAC-SHA256:\n");
    test_hmac();

    printf("\n[L5] TPM Locality & Auth:\n");
    test_tpm_locality();

    printf("\n[L6] Remote Attestation:\n");
    test_attestation();

    printf("\n=== Results: %d passed, %d failed ===\n",
           tests_passed, tests_failed);

    return (tests_failed > 0) ? 1 : 0;
}
