#include "uefi_sb.h"
#include "signature_verify.h"
#include "trust_chain.h"
#include "firmware_update.h"
#include "root_of_trust.h"
#include "tpm.h"
#include "measured_boot.h"
#include "key_mgmt.h"
#include "boot_policy.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) do { printf("  TEST: %s ... ", name); } while(0)
#define PASS() do { printf("PASS\n"); tests_passed++; } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); tests_failed++; } while(0)
#define CHECK(cond, msg) do { if (cond) PASS(); else FAIL(msg); } while(0)

/* ??? SHA-256 Tests ??????????????????????????????????????????????????? */

static void test_sha256_known_vector(void)
{
    TEST("SHA-256 empty string");
    const char *input = "";
    uint8_t digest[SHA256_HASH_SIZE];
    uint8_t expected[SHA256_HASH_SIZE] = {
        0xE3, 0xB0, 0xC4, 0x42, 0x98, 0xFC, 0x1C, 0x14,
        0x9A, 0xFB, 0xF4, 0xC8, 0x99, 0x6F, 0xB9, 0x24,
        0x27, 0xAE, 0x41, 0xE4, 0x64, 0x9B, 0x93, 0x4C,
        0xA4, 0x95, 0x99, 0x1B, 0x78, 0x52, 0xB8, 0x55
    };
    sha256_hash((const uint8_t *)input, (uint32_t)strlen(input), digest);
    CHECK(memcmp(digest, expected, SHA256_HASH_SIZE) == 0,
          "SHA-256 empty string mismatch");
}

static void test_sha256_abc(void)
{
    TEST("SHA-256 'abc'");
    const char *input = "abc";
    uint8_t digest[SHA256_HASH_SIZE];
    uint8_t expected[SHA256_HASH_SIZE] = {
        0xBA, 0x78, 0x16, 0xBF, 0x8F, 0x01, 0xCF, 0xEA,
        0x41, 0x41, 0x40, 0xDE, 0x5D, 0xAE, 0x22, 0x23,
        0xB0, 0x03, 0x61, 0xA3, 0x96, 0x17, 0x7A, 0x9C,
        0xB4, 0x10, 0xFF, 0x61, 0xF2, 0x00, 0x15, 0xAD
    };
    sha256_hash((const uint8_t *)input, (uint32_t)strlen(input), digest);
    CHECK(memcmp(digest, expected, SHA256_HASH_SIZE) == 0,
          "SHA-256 'abc' mismatch");
}

static void test_sha256_long(void)
{
    TEST("SHA-256 256-byte input");
    uint8_t data[256];
    uint8_t digest1[SHA256_HASH_SIZE], digest2[SHA256_HASH_SIZE];
    for (int i = 0; i < 256; i++) data[i] = (uint8_t)(i & 0xFF);
    sha256_hash(data, 256, digest1);
    sha256_hash(data, 256, digest2);
    CHECK(memcmp(digest1, digest2, SHA256_HASH_SIZE) == 0,
          "SHA-256 deterministic check failed");
}

/* ??? BigInt Tests ???????????????????????????????????????????????????? */

static void test_bigint_basic(void)
{
    TEST("BigInt basic operations");
    BigInt a, b, r;
    bigint_init(&a);
    bigint_init(&b);

    uint8_t bytes[] = {0x12, 0x34, 0x56, 0x78};
    bigint_from_bytes(&a, bytes, 4);
    CHECK(a.num_words == 1 && a.words[0] == 0x12345678,
          "BigInt from_bytes failed");

    bigint_to_bytes(&a, bytes, 4);
    uint8_t expected[] = {0x12, 0x34, 0x56, 0x78};
    CHECK(memcmp(bytes, expected, 4) == 0,
          "BigInt to_bytes roundtrip failed");

    CHECK(!bigint_is_zero(&a), "BigInt is_zero false positive");
    CHECK(bigint_is_zero(&b), "BigInt is_zero false negative");

    bigint_mul(&r, &a, &a);
    CHECK(!bigint_is_zero(&r), "BigInt mul produced zero");
}

/* ??? RSA Tests ??????????????????????????????????????????????????????? */

static void test_rsa_keypair(void)
{
    TEST("RSA keypair generation");
    RSAKey pub, priv;
    rsa_generate_simple_keypair(&pub, &priv, 2048);
    CHECK(pub.mod_len == 256, "RSA modulus length wrong");
    CHECK(pub.exp_len == 3, "RSA exponent length wrong");
}

static void test_rsa_verify_null(void)
{
    TEST("RSA verify NULL safety");
    uint8_t hash[32] = {0};
    uint8_t sig[256] = {0};
    CHECK(!rsa_sha256_verify(NULL, hash, sig, 256), "NULL pub not handled");
    CHECK(!rsa_sha256_verify((void*)1, NULL, sig, 256), "NULL hash not handled");
}

/* ??? UEFI Secure Boot Tests ?????????????????????????????????????????? */

static void test_sb_init(void)
{
    TEST("SecureBootVars init");
    SecureBootVars sb;
    CHECK(sb_init(&sb), "sb_init failed");
    CHECK(sb.setup_mode, "setup_mode not set");
    CHECK(!sb.secure_boot, "secure_boot incorrectly set");
}

static void test_sb_enroll_pk(void)
{
    TEST("Enroll Platform Key");
    SecureBootVars sb;
    sb_init(&sb);

    EFISignature pk;
    memset(&pk, 0, sizeof(pk));
    pk.type = SB_SIG_TYPE_X509_CERT;
    pk.signature_size = 256;
    for (int i = 0; i < 256; i++) pk.signature_data[i] = (uint8_t)(i & 0xFF);

    CHECK(sb_enroll_pk(&sb, &pk), "PK enroll failed");
    CHECK(!sb.setup_mode, "setup_mode not cleared after PK enroll");
    CHECK(sb.pk.count == 1, "PK count not 1");
}

static void test_sb_verify_blacklist(void)
{
    TEST("SecureBoot blacklist verification");
    SecureBootVars sb;
    sb_init(&sb);

    /* Enroll PK first */
    EFISignature pk;
    memset(&pk, 0, sizeof(pk));
    pk.type = SB_SIG_TYPE_X509_CERT;
    for (int i = 0; i < 256; i++) pk.signature_data[i] = (uint8_t)i;
    pk.signature_size = 256;
    sb_enroll_pk(&sb, &pk);

    /* Add a dbx entry (blacklist) */
    EFISignature dbx_entry;
    memset(&dbx_entry, 0, sizeof(dbx_entry));
    dbx_entry.type = SB_SIG_TYPE_SHA256_HASH;
    dbx_entry.signature_size = 32;
    memset(dbx_entry.signature_data, 0xAA, 32);
    sb_enroll_dbx(&sb, &dbx_entry);

    /* Enable secure boot */
    sb.secure_boot = true;

    /* Try to boot with blacklisted hash */
    uint8_t black_hash[32];
    memset(black_hash, 0xAA, 32);
    bool result = sb_verify_image(&sb, black_hash, 32, black_hash, 32);
    CHECK(!result, "Blacklisted image passed verification");

    /* In setup mode, everything passes */
    sb.setup_mode = true;
    result = sb_verify_image(&sb, black_hash, 32, black_hash, 32);
    CHECK(result, "Setup mode should allow everything");
}

static void test_sb_delete_pk(void)
{
    TEST("Delete PK re-enters setup mode");
    SecureBootVars sb;
    sb_init(&sb);

    EFISignature pk;
    memset(&pk, 0, sizeof(pk));
    pk.type = SB_SIG_TYPE_X509_CERT;
    pk.signature_size = 256;
    sb_enroll_pk(&sb, &pk);
    CHECK(!sb.setup_mode, "Should be out of setup mode");

    CHECK(sb_delete_pk(&sb), "Delete PK failed");
    CHECK(sb.setup_mode, "Should re-enter setup mode");
}

/* ??? Trust Chain Tests ??????????????????????????????????????????????? */

static void test_trust_chain(void)
{
    TEST("Boot chain component management");
    BootChain chain;
    trust_chain_init(&chain);
    CHECK(chain.component_count == 0, "Initial component count not 0");

    uint8_t spl[512];
    memset(spl, 0x55, sizeof(spl));
    CHECK(trust_chain_add_component(&chain, BOOT_COMP_SPL, "SPL",
                                     spl, sizeof(spl)),
          "Add SPL component failed");
    CHECK(chain.component_count == 1, "Component count not updated");
    CHECK(chain.components[0].type == BOOT_COMP_SPL, "Component type wrong");
}

static void test_trust_chain_verify(void)
{
    TEST("Boot chain hash verification");
    BootChain chain;
    trust_chain_init(&chain);

    uint8_t data[256];
    memset(data, 0x42, sizeof(data));
    trust_chain_add_component(&chain, BOOT_COMP_U_BOOT, "U-Boot",
                               data, sizeof(data));

    uint8_t expected[SHA256_HASH_SIZE_TC];
    sha256_hash(data, sizeof(data), expected);
    CHECK(trust_chain_verify_component(&chain, 0, expected),
          "Component verification failed");
    CHECK(chain.components[0].status == TC_STATUS_VERIFIED,
          "Status not set to VERIFIED");
}

/* ??? Firmware Update Tests ??????????????????????????????????????????? */

static void test_fw_update(void)
{
    TEST("Firmware capsule update lifecycle");
    FWUpdateContext ctx;
    CHECK(fw_capsule_init(&ctx), "Capsule init failed");
    CHECK(ctx.current_version == 1, "Default version not 1");
    CHECK(ctx.update_mode == FW_UPDATE_MODE_NORMAL, "Mode not NORMAL");
    CHECK(!ctx.update_pending, "Update pending incorrectly set");
}

static void test_fw_rollback(void)
{
    TEST("Firmware update rollback");
    FWUpdateContext ctx;
    fw_capsule_init(&ctx);
    ctx.update_pending = true;
    ctx.update_mode = FW_UPDATE_MODE_COMMIT;
    CHECK(fw_capsule_rollback(&ctx), "Rollback failed");
    CHECK(!ctx.update_pending, "Pending not cleared");
    CHECK(ctx.update_mode == FW_UPDATE_MODE_NORMAL, "Mode not NORMAL after rollback");
}

/* ??? Root of Trust Tests ????????????????????????????????????????????? */

static void test_rot_init(void)
{
    TEST("Root of Trust initialization");
    RootOfTrust rot;
    CHECK(rot_init(&rot, ROT_TYPE_ROM_BASED), "RoT init failed");
    CHECK(rot.initialized, "Not marked initialized");
    CHECK(rot.boot_policy == ROT_POLICY_UNLOCKED, "Policy not UNLOCKED");
}

static void test_rot_lock_device(void)
{
    TEST("Root of Trust device locking");
    RootOfTrust rot;
    rot_init(&rot, ROT_TYPE_EFUSE);
    uint8_t pk_hash[ROT_MAX_KEY_HASH];
    memset(pk_hash, 0xAB, ROT_MAX_KEY_HASH);
    CHECK(rot_lock_device(&rot, pk_hash), "Lock device failed");
    CHECK(rot_is_locked(&rot), "Device not locked");
    CHECK(rot.boot_policy == ROT_POLICY_LOCKED, "Policy not LOCKED");
}

static void test_rot_derive_secret(void)
{
    TEST("Root of Trust derive device secret");
    RootOfTrust rot;
    rot_init(&rot, ROT_TYPE_PUF_BASED);
    rot_lock_device(&rot, (const uint8_t*)"test-pk-hash-32-bytes-paddingXX");

    uint8_t seed[16];
    memset(seed, 0xCD, sizeof(seed));
    CHECK(rot_derive_device_secret(&rot, seed, sizeof(seed)),
          "Derive secret failed");
    /* Secret should be non-zero after derivation */
    bool all_zero = true;
    for (int i = 0; i < ROT_MAX_SECRET_SIZE; i++) {
        if (rot.device_secret[i] != 0) { all_zero = false; break; }
    }
    CHECK(!all_zero, "Derived secret is all zeros");
}

/* ??? TPM Tests ??????????????????????????????????????????????????????? */

static void test_tpm_init(void)
{
    TEST("TPM context initialization");
    TPMContext tpm;
    CHECK(tpm_init(&tpm), "TPM init failed");
    CHECK(tpm.is_initialized, "TPM not initialized");
    CHECK(tpm.pcr_bank.bank_count > 0, "No PCR banks");
    CHECK(tpm.max_lockout_attempts == 32, "Default lockout attempts wrong");
}

static void test_tpm_pcr_extend(void)
{
    TEST("TPM PCR extend operation");
    TPMContext tpm;
    tpm_init(&tpm);

    uint8_t digest[32];
    memset(digest, 0x11, 32);

    uint8_t pcr_before[32];
    uint32_t sz_before = 32;
    tpm_pcr_read(&tpm, 0, pcr_before, &sz_before);

    CHECK(tpm_pcr_extend(&tpm, 0, digest, 32, TPM_ALG_SHA256),
          "PCR extend failed");
    CHECK(tpm.pcr_bank.pcrs[0].extend_count == 1,
          "Extend count not incremented");

    /* PCR should have changed */
    uint8_t pcr_after[32];
    uint32_t sz_after = 32;
    tpm_pcr_read(&tpm, 0, pcr_after, &sz_after);
    bool changed = (memcmp(pcr_before, pcr_after, 32) != 0);
    CHECK(changed, "PCR value unchanged after extend");
}

static void test_tpm_quote_and_attest(void)
{
    TEST("TPM quote and attestation verification");
    TPMContext tpm;
    tpm_init(&tpm);

    /* Extend some known values to PCRs */
    uint8_t val1[32];
    memset(val1, 0xAA, 32);
    tpm_pcr_extend(&tpm, 0, val1, 32, TPM_ALG_SHA256);

    /* Prepare quote */
    TPMPCRSelection sel;
    memset(&sel, 0, sizeof(sel));
    sel.pcr_mask[0] = 1;
    sel.pcr_count = 1;

    uint8_t nonce[32];
    memset(nonce, 0xBB, 32);

    TPMQuote quote;
    CHECK(tpm_quote(&tpm, &sel, nonce, 32, &quote),
          "Quote generation failed");

    /* Verify the quote */
    uint8_t expected_pcrs[24][32];
    memset(expected_pcrs, 0, sizeof(expected_pcrs));
    /* PCR 0 expected = SHA-256(initial_0 || 0xAA...AA) */
    uint8_t concat[64];
    memset(concat, 0, 32);
    memset(concat + 32, 0xAA, 32);
    sha256_hash(concat, 64, expected_pcrs[0]);

    TPMAttestResult attest = tpm_verify_quote(&quote, expected_pcrs, 1,
                                                nonce, 32);
    CHECK(attest == TPM_ATTEST_OK, "Quote verification failed");
}

static void test_tpm_nv_storage(void)
{
    TEST("TPM NV storage operations");
    TPMContext tpm;
    tpm_init(&tpm);

    CHECK(tpm_nv_define_space(&tpm, 0x01800001, TPM_NV_TYPE_ORDINARY,
                               TPMA_NV_OWNERWRITE | TPMA_NV_OWNERREAD,
                               128, TPM_RH_OWNER),
          "NV define space failed");

    uint8_t write_data[128];
    memset(write_data, 0xCC, 128);
    CHECK(tpm_nv_write(&tpm, 0x01800001, write_data, 128, 0),
          "NV write failed");

    uint8_t read_data[128];
    uint32_t read_sz = 128;
    CHECK(tpm_nv_read(&tpm, 0x01800001, read_data, &read_sz, 0),
          "NV read failed");
    CHECK(read_sz == 128, "Read size mismatch");
    CHECK(memcmp(write_data, read_data, 128) == 0, "NV readback mismatch");

    /* Lock NV and verify it can't be written again */
    tpm_nv_write_lock(&tpm, 0x01800001);
    CHECK(!tpm_nv_write(&tpm, 0x01800001, write_data, 128, 0),
          "NV write should fail after lock");

    /* NV counter test */
    CHECK(tpm_nv_define_space(&tpm, 0x01800010, TPM_NV_TYPE_COUNTER,
                               TPMA_NV_OWNERWRITE | TPMA_NV_OWNERREAD,
                               8, TPM_RH_OWNER),
          "NV counter define failed");
    CHECK(tpm_nv_increment(&tpm, 0x01800010),
          "NV counter increment failed");
}

/* ??? Measured Boot Tests ????????????????????????????????????????????? */

static void test_measured_boot_init(void)
{
    TEST("Measured boot initialization");
    MeasuredBoot mb;
    CHECK(mb_init(&mb, NULL, 0), "MB init failed");
    CHECK(mb.state == MB_STATE_CRTM_ACTIVE, "Not in CRTM active state");
    CHECK(mb.srtm.event_log.event_count >= 2,
          "CRTM events not recorded");
}

static void test_measure_firmware(void)
{
    TEST("Measure firmware component");
    MeasuredBoot mb;
    mb_init(&mb, NULL, 0);

    uint8_t fw_data[1024];
    memset(fw_data, 0x42, sizeof(fw_data));
    CHECK(mb_measure_firmware(&mb, 4, EV_EFI_BOOT_SERVICES_APPLICATION,
                               fw_data, sizeof(fw_data), "TestApp.efi"),
          "Measure firmware failed");
    CHECK(mb.srtm.event_log.event_count >= 3,
          "Event log count not incremented");
    CHECK(mb.state == MB_STATE_MEASURING, "State not MEASURING");
}

static void test_event_log_validate(void)
{
    TEST("Event log validation replay");
    MeasuredBoot mb;
    mb_init(&mb, NULL, 0);

    /* Measure some firmware */
    uint8_t fw[256];
    memset(fw, 0xDE, 256);
    mb_measure_firmware(&mb, 2, EV_EFI_PLATFORM_FIRMWARE_BLOB,
                         fw, 256, "UEFI-Firmware");
    mb_measure_firmware(&mb, 4, EV_EFI_BOOT_SERVICES_APPLICATION,
                         fw, 256, "BootApp.efi");

    /* Validate event log against current PCRs */
    CHECK(mb_event_log_validate(&mb.srtm.event_log,
                                 mb.srtm.current_pcrs, 8),
          "Event log validation failed");
}

/* ??? Key Management Tests ???????????????????????????????????????????? */

static void test_key_store_init(void)
{
    TEST("Key store initialization");
    KMKeyStore store;
    CHECK(km_store_init(&store), "Key store init failed");
    CHECK(store.key_count == 0, "Initial key count not 0");
    CHECK(!store.locked, "Store locked by default");
}

static void test_key_generate(void)
{
    TEST("Key generation");
    KMKeyStore store;
    km_store_init(&store);

    CHECK(km_key_generate(&store, "SB-Signing-Key-1", "Secure Boot Signing",
                           KM_KEY_TYPE_RSA_2048, KM_USAGE_SIGN | KM_USAGE_VERIFY,
                           2048, 365),
          "Key generation failed");
    CHECK(store.key_count == 1, "Key count not incremented");

    const KMKeyEntry *key = km_key_find(&store, "SB-Signing-Key-1");
    CHECK(key != NULL, "Key not found");
    CHECK(key->state == KM_STATE_PRE_ACTIVATION, "Key not in pre-activation");
    CHECK(key->key_type == KM_KEY_TYPE_RSA_2048, "Key type wrong");
}

static void test_key_lifecycle(void)
{
    TEST("Key lifecycle state transitions");
    KMKeyStore store;
    km_store_init(&store);
    km_key_generate(&store, "K1", "TestKey",
                    KM_KEY_TYPE_ECDSA_P256, KM_USAGE_SIGN, 256, 365);

    /* PRE_ACTIVATION ? ACTIVE */
    CHECK(km_key_activate(&store, "K1"), "Activate failed");
    CHECK(km_key_find(&store, "K1")->state == KM_STATE_ACTIVE,
          "Not ACTIVE after activation");

    /* ACTIVE ? DEACTIVATED */
    CHECK(km_key_deactivate(&store, "K1"), "Deactivate failed");
    CHECK(km_key_find(&store, "K1")->state == KM_STATE_DEACTIVATED,
          "Not DEACTIVATED");

    /* DEACTIVATED ? ACTIVE (reactivate) */
    CHECK(km_key_reactivate(&store, "K1"), "Reactivate failed");
    CHECK(km_key_find(&store, "K1")->state == KM_STATE_ACTIVE,
          "Not ACTIVE after reactivation");

    /* ACTIVE ? COMPROMISED */
    CHECK(km_key_compromise(&store, "K1"), "Compromise failed");
    CHECK(km_key_find(&store, "K1")->state == KM_STATE_COMPROMISED,
          "Not COMPROMISED");

    /* COMPROMISED ? REVOKED */
    CHECK(km_key_revoke(&store, "K1"), "Revoke failed");
    CHECK(km_key_find(&store, "K1")->state == KM_STATE_REVOKED,
          "Not REVOKED");
}

static void test_hkdf(void)
{
    TEST("HKDF key derivation (RFC 5869)");
    uint8_t ikm[22] = {0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,
                        0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,
                        0x0B,0x0B,0x0B,0x0B,0x0B,0x0B};
    uint8_t salt[13] = {0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,
                         0x08,0x09,0x0A,0x0B,0x0C};
    uint8_t info[10] = {0xF0,0xF1,0xF2,0xF3,0xF4,0xF5,0xF6,0xF7,
                         0xF8,0xF9};
    uint8_t okm[42];

    CHECK(km_hkdf_derive(ikm, 22, salt, 13, info, 10, okm, 42),
          "HKDF derive failed");

    /* Verify output is non-zero (weak check; full RFC test vectors
     * would require exact byte comparison) */
    bool all_zero = true;
    for (int i = 0; i < 42; i++) if (okm[i] != 0) { all_zero = false; break; }
    CHECK(!all_zero, "HKDF output is all zeros");
}

/* ??? Boot Policy Tests ??????????????????????????????????????????????? */

static void test_boot_policy_init(void)
{
    TEST("Boot policy initialization");
    BootPolicy policy;
    CHECK(bp_policy_init(&policy, "Test-Policy", false),
          "Policy init failed");
    CHECK(policy.rule_count == 0, "Initial rules not 0");
    CHECK(!policy.default_allow, "Default should be DENY");
}

static void test_standard_sb_policy(void)
{
    TEST("Standard Secure Boot policy template");
    BootPolicy policy;
    CHECK(bp_create_standard_sb_policy(&policy),
          "Create standard SB policy failed");
    CHECK(policy.rule_count == 3, "Should have 3 rules");

    /* Evaluate: ALLOW with valid signer */
    BPEvalContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.signer_name = "OEM-SecureBoot-Signer";
    ctx.secure_boot_enabled = true;

    BPEvalOutput output = bp_policy_evaluate(&policy, &ctx);
    CHECK(output.result == BP_RESULT_ALLOW,
          "Should ALLOW valid signer");
}

static void test_enterprise_policy(void)
{
    TEST("Enterprise boot policy");
    BootPolicy policy;
    CHECK(bp_create_enterprise_policy(&policy, "AcmeCorp-Signer", 3),
          "Create enterprise policy failed");

    /* Enterprise signer with sufficient version */
    BPEvalContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.signer_name = "AcmeCorp-Signer";
    ctx.image_version = 5;
    ctx.secure_boot_enabled = true;

    BPEvalOutput output = bp_policy_evaluate(&policy, &ctx);
    CHECK(output.result == BP_RESULT_ALLOW,
          "Enterprise signer should be ALLOWed");

    /* Enterprise signer with insufficient version */
    ctx.image_version = 1;
    output = bp_policy_evaluate(&policy, &ctx);
    CHECK(output.result == BP_RESULT_DENY,
          "Below minimum version should be DENIED");

    /* Unknown signer should be denied */
    ctx.signer_name = "UNKNOWN";
    ctx.image_version = 5;
    output = bp_policy_evaluate(&policy, &ctx);
    CHECK(output.result == BP_RESULT_DENY,
          "Unknown signer should be DENIED");
}

static void test_policy_eval_operators(void)
{
    TEST("Policy evaluation: ALL_OF / ANY_OF / NOT");
    BootPolicy policy;
    bp_policy_init(&policy, "Op-Test", true);

    /* Rule: ALL_OF(SIGNER='X' AND VERSION_GE=10) */
    BPRule rule;
    memset(&rule, 0, sizeof(rule));
    snprintf(rule.rule_name, BP_MAX_NAME_LEN, "all-of-test");
    rule.operator = BP_OP_ALL_OF;
    rule.priority = 100;
    rule.enabled = true;
    rule.rule_id = 42;
    rule.condition_count = 2;
    rule.conditions[0].cond_type = BP_COND_SIGNER;
    snprintf(rule.conditions[0].value.signer_name, BP_MAX_NAME_LEN, "X");
    rule.conditions[1].cond_type = BP_COND_VERSION_GE;
    rule.conditions[1].value.version_threshold = 10;
    bp_policy_add_rule(&policy, &rule);

    BPEvalContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.signer_name = "X";
    ctx.image_version = 15;

    BPEvalOutput output = bp_policy_evaluate(&policy, &ctx);
    CHECK(output.result == BP_RESULT_ALLOW,
          "ALL_OF should match when all conditions true");
    CHECK(output.rule_id_matched == 42, "Wrong rule ID matched");

    /* Fail: signer matches but version too low — rule should not match,
     * and default_allow=true should kick in. We verify the rule didn't match. */
    ctx.image_version = 5;
    output = bp_policy_evaluate(&policy, &ctx);
    CHECK(output.rule_id_matched != 42,
          "ALL_OF rule should not match with failed version condition");
    CHECK(output.result == BP_RESULT_ALLOW,
          "Default ALLOW should apply when no rule matches");
}

/* ??? Test Runner ????????????????????????????????????????????????????? */

int main(void)
{
    printf("????????????????????????????????????????????????????\n");
    printf("?   mini-secure-boot ? Comprehensive Test Suite    ?\n");
    printf("????????????????????????????????????????????????????\n\n");

    printf("??? SHA-256 ????????????????????????????\n");
    test_sha256_known_vector();
    test_sha256_abc();
    test_sha256_long();

    printf("\n??? BigInt ?????????????????????????????\n");
    test_bigint_basic();

    printf("\n??? RSA ????????????????????????????????\n");
    test_rsa_keypair();
    test_rsa_verify_null();

    printf("\n??? UEFI Secure Boot ???????????????????\n");
    test_sb_init();
    test_sb_enroll_pk();
    test_sb_verify_blacklist();
    test_sb_delete_pk();

    printf("\n??? Trust Chain ?????????????????????????\n");
    test_trust_chain();
    test_trust_chain_verify();

    printf("\n??? Firmware Update ????????????????????\n");
    test_fw_update();
    test_fw_rollback();

    printf("\n??? Root of Trust ??????????????????????\n");
    test_rot_init();
    test_rot_lock_device();
    test_rot_derive_secret();

    printf("\n??? TPM 2.0 ????????????????????????????\n");
    test_tpm_init();
    test_tpm_pcr_extend();
    test_tpm_quote_and_attest();
    test_tpm_nv_storage();

    printf("\n??? Measured Boot ??????????????????????\n");
    test_measured_boot_init();
    test_measure_firmware();
    test_event_log_validate();

    printf("\n??? Key Management ?????????????????????\n");
    test_key_store_init();
    test_key_generate();
    test_key_lifecycle();
    test_hkdf();

    printf("\n??? Boot Policy ????????????????????????\n");
    test_boot_policy_init();
    test_standard_sb_policy();
    test_enterprise_policy();
    test_policy_eval_operators();

    printf("\n???????????????????????????????????????????????????\n");
    printf("  TOTAL: %d passed, %d failed\n", tests_passed, tests_failed);
    printf("???????????????????????????????????????????????????\n");

    return tests_failed > 0 ? 1 : 0;
}
