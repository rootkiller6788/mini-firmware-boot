#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>

#include "spi_protection.h"
#include "smm_attacks.h"
#include "dma_attacks.h"
#include "bmc_me.h"
#include "firmware_resiliency.h"
#include "secure_boot.h"
#include "tpm_attestation.h"
#include "fw_cross_integration.h"

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { \
    tests_run++; \
    printf("  TEST %s... ", name); \
} while(0)

#define PASS() do { \
    tests_passed++; \
    printf("PASSED\n"); \
} while(0)

#define FAIL(msg) do { \
    printf("FAILED: %s\n", msg); \
} while(0)

#define ASSERT_TRUE(cond, msg) do { \
    if (!(cond)) { FAIL(msg); return; } \
} while(0)

#define ASSERT_FALSE(cond, msg) do { \
    if (cond) { FAIL(msg); return; } \
} while(0)

#define ASSERT_EQ(a, b, msg) do { \
    if ((a) != (b)) { FAIL(msg); return; } \
} while(0)

/* ?? SPI Protection Tests ???????????????????????????????????????? */

static void test_spi_init(void) {
    SPIController ctrl;
    TEST("SPI init");
    spi_protect_init(&ctrl);
    ASSERT_TRUE(ctrl.initialized, "SPI not initialized");
    ASSERT_TRUE(ctrl.descriptor.regions[SPI_DESC_REGION_BIOS].base == 0x00000000,
                "BIOS region base wrong");
    ASSERT_TRUE(ctrl.descriptor.regions[SPI_DESC_REGION_ME].base == 0x00600000,
                "ME region base wrong");
    PASS();
}

static void test_spi_protected_range(void) {
    SPIController ctrl;
    TEST("SPI set protected range");
    spi_protect_init(&ctrl);
    bool ok = spi_set_protected_range(&ctrl, 0, 0x00000000, 0x0000FFFF, SPI_ACCESS_READ);
    ASSERT_TRUE(ok, "Failed to set PR0");
    ASSERT_TRUE(ctrl.protected_ranges.ranges[0].write_protect,
                "PR0 should be write-protected");
    PASS();
}

static void test_spi_lock_config(void) {
    SPIController ctrl;
    TEST("SPI lock config");
    spi_protect_init(&ctrl);
    bool ok = spi_lock_config(&ctrl, SPI_LOCK_FLOCKDN | SPI_LOCK_BLE);
    ASSERT_TRUE(ok, "Failed to lock config");
    ASSERT_TRUE(ctrl.lock_state.flockdn, "FLOCKDN not set");
    ASSERT_TRUE(ctrl.lock_state.ble, "BLE not set");
    PASS();
}

static void test_spi_access_control(void) {
    SPIController ctrl;
    uint8_t buf[64];
    TEST("SPI access control");
    spi_protect_init(&ctrl);
    spi_set_protected_range(&ctrl, 0, 0x00000000, 0x0000FFFF, SPI_ACCESS_READ);
    spi_lock_config(&ctrl, SPI_LOCK_FLOCKDN | SPI_LOCK_SMM_BWP);

    /* BIOS master should be able to read */
    bool ok = spi_check_access(&ctrl, SPI_MASTER_BIOS, 0x00001000, 64, false);
    ASSERT_TRUE(ok, "BIOS master should read");

    /* HOST should NOT be able to write to BIOS region */
    ok = spi_check_access(&ctrl, SPI_MASTER_HOST, 0x00000100, 64, true);
    ASSERT_FALSE(ok, "HOST should not write to BIOS");

    PASS();
}

static void test_spi_null_checks(void) {
    TEST("SPI null checks");
    ASSERT_FALSE(spi_set_protected_range(NULL, 0, 0, 0, 0), "NULL ctrl");
    ASSERT_FALSE(spi_lock_config(NULL, 0), "NULL ctrl lock");
    ASSERT_FALSE(spi_check_access(NULL, 0, 0, 0, false), "NULL ctrl access");
    PASS();
}

/* ?? SMM Attack Tests ???????????????????????????????????????????? */

static void test_smm_init(void) {
    SMMContext ctx;
    TEST("SMM init");
    smm_init(&ctx);
    ASSERT_TRUE(ctx.smm_core.smbase == SMM_SMRAM_BASE, "SMBASE wrong");
    ASSERT_FALSE(ctx.smm_core.smrr_enabled, "SMRR should be off initially");
    ASSERT_TRUE(ctx.smm_core.d_open, "D_OPEN should be set initially");
    PASS();
}

static void test_smm_smrr(void) {
    SMMContext ctx;
    TEST("SMM SMRR");
    smm_init(&ctx);
    ctx.in_smm = true;
    bool ok = smm_set_smrr(&ctx, 0x30000, 0xFFFF0000);
    ASSERT_TRUE(ok, "Failed to set SMRR");
    ASSERT_TRUE(ctx.smm_core.smrr_enabled, "SMRR not enabled");
    ctx.in_smm = false;
    /* SMRR should only be settable in SMM */
    ok = smm_set_smrr(&ctx, 0x40000, 0xFFFF0000);
    ASSERT_FALSE(ok, "SMRR set outside SMM should fail");
    PASS();
}

static void test_smm_smrr_validate(void) {
    SMMContext ctx;
    TEST("SMM SMRR validate");
    smm_init(&ctx);
    ASSERT_TRUE(smm_validate_smrr_access(&ctx, 0x00035000),
                "Without SMRR, all access allowed");
    ctx.in_smm = true;
    smm_set_smrr(&ctx, 0x30000, 0xFFFF0000);
    ctx.in_smm = false;
    ASSERT_TRUE(smm_validate_smrr_access(&ctx, SMM_SMRAM_BASE + 0x100),
                "Access within SMRR range should pass");
    ASSERT_FALSE(smm_validate_smrr_access(&ctx, 0x00001000),
                 "Access outside SMRR range should fail");
    PASS();
}

static void test_smm_ring3_to_ring2(void) {
    SMMContext ctx;
    TEST("SMM ring-3 to ring-2 attack");
    smm_init(&ctx);
    bool ok = smm_ring3_to_ring2_attack(&ctx, SMM_SMRAM_BASE + 0x5000);
    ASSERT_TRUE(ok, "With D_OPEN, attack should succeed");
    ctx.smm_core.d_lock = true;
    ctx.smm_core.d_open = false;
    ok = smm_ring3_to_ring2_attack(&ctx, SMM_SMRAM_BASE + 0x5000);
    ASSERT_FALSE(ok, "With D_LCK, attack should be blocked");
    PASS();
}

/* ?? DMA/IOMMU Tests ????????????????????????????????????????????? */

static void test_iommu_init(void) {
    IOMMU iommu;
    TEST("IOMMU init");
    iommu_init(&iommu);
    ASSERT_TRUE(iommu.enabled, "IOMMU not enabled");
    ASSERT_TRUE(iommu.capabilities & DMA_REMAPPING_ENABLED,
                "DMA remapping capability missing");
    PASS();
}

static void test_iommu_map_device(void) {
    IOMMU iommu;
    TEST("IOMMU map device");
    iommu_init(&iommu);
    bool ok = iommu_map_device(&iommu, 0x0010, 0x1000, 1);
    ASSERT_TRUE(ok, "Failed to map device");
    ASSERT_TRUE(iommu.device_table[0x0010].valid, "Device not valid");
    ASSERT_TRUE(iommu.device_table[0x0010].present, "Device not present");
    ok = iommu_map_device(NULL, 0, 0, 0);
    ASSERT_FALSE(ok, "NULL iommu should fail");
    ok = iommu_map_device(&iommu, IOMMU_MAX_DEVICES + 1, 0, 0);
    ASSERT_FALSE(ok, "Invalid requester ID should fail");
    PASS();
}

static void test_iommu_create_domain(void) {
    IOMMU iommu;
    TEST("IOMMU create domain");
    iommu_init(&iommu);
    uint8_t dummy_mem[4096];
    bool ok = iommu_create_domain(&iommu, 1, (uint64_t)(uintptr_t)dummy_mem,
                                   0x10000000, 0x10000,
                                   IOMMU_FLAG_READ | IOMMU_FLAG_WRITE);
    ASSERT_TRUE(ok, "Failed to create domain");
    ASSERT_EQ(iommu.domain_count, (size_t)1, "Domain count should be 1");
    ASSERT_EQ(iommu.domains[0]->domain_id, (uint16_t)1, "Domain ID wrong");
    PASS();
}

static void test_iommu_translate(void) {
    IOMMU iommu;
    uint8_t mem[128 * 4096];
    uint64_t phys;
    TEST("IOMMU translate");
    iommu_init(&iommu);
    /* Create domain and map device */
    iommu_create_domain(&iommu, 1, (uint64_t)(uintptr_t)mem,
                        0x10000000, 128 * 4096,
                        IOMMU_FLAG_READ | IOMMU_FLAG_WRITE);
    iommu_map_device(&iommu, 0x0200, (uint64_t)(uintptr_t)mem, 1);
    /* Translate IOVA to physical */
    bool ok = iommu_translate(&iommu, 0x0200, 0x10000000, &phys);
    ASSERT_TRUE(ok, "Translation should succeed");
    ASSERT_TRUE(phys >= (uint64_t)(uintptr_t)mem &&
                phys < (uint64_t)(uintptr_t)mem + 128 * 4096,
                "Physical address out of range");
    /* Unknown device should fail */
    ok = iommu_translate(&iommu, 0xDEAD, 0x10000000, &phys);
    ASSERT_FALSE(ok, "Unknown device should fail");
    PASS();
}

/* ?? BMC/ME Tests ???????????????????????????????????????????????? */

static void test_bmc_init(void) {
    BMCController bmc;
    TEST("BMC init");
    bmc_init(&bmc);
    ASSERT_TRUE(bmc.ipmi_interface, "IPMI interface not active");
    ASSERT_EQ(bmc.kcs_data_port, (uint16_t)0xCA2, "KCS data port wrong");
    ASSERT_EQ(bmc.last_status, (uint8_t)0x00, "Status should be normal");
    PASS();
}

static void test_bmc_ipmi_get_device_id(void) {
    BMCController bmc;
    uint8_t resp[256];
    size_t resp_len;
    TEST("BMC IPMI Get Device ID");
    bmc_init(&bmc);
    bool ok = bmc_ipmi_command(&bmc, BMC_IPMI_NETFN_APP,
                                BMC_IPMI_CMD_GET_DEVICE_ID,
                                NULL, 0, resp, &resp_len);
    ASSERT_TRUE(ok, "IPMI command failed");
    ASSERT_TRUE(resp_len >= 5, "Response too short");
    PASS();
}

static void test_me_init(void) {
    IntelME me;
    TEST("ME init");
    me_init(&me);
    ASSERT_FALSE(me.manufacturing_mode, "Should not be in manufacturing mode");
    ASSERT_TRUE(me.fw_init_complete, "FW init should be complete");
    ASSERT_FALSE(me_check_manufacturing_mode(&me), "MFG mode check");
    PASS();
}

static void test_me_lock_jtag(void) {
    IntelME me;
    TEST("ME lock JTAG");
    me_init(&me);
    bool ok = me_lock_jtag(&me);
    ASSERT_TRUE(ok, "JTAG lock should succeed");
    ASSERT_TRUE(me.jtag_disable, "JTAG not disabled");
    /* In manufacturing mode, JTAG lock should fail */
    me.manufacturing_mode = true;
    ok = me_lock_jtag(&me);
    ASSERT_FALSE(ok, "JTAG lock in mfg mode should fail");
    PASS();
}

static void test_me_supply_chain_risk(void) {
    IntelME me;
    TEST("ME supply chain risk detect");
    me_init(&me);
    /* Clean ME should have no risk */
    bool risk = me_detect_supply_chain_risk(&me);
    ASSERT_FALSE(risk, "Clean ME should have no risk");
    /* Manufacturing mode = risk */
    me.manufacturing_mode = true;
    risk = me_detect_supply_chain_risk(&me);
    ASSERT_TRUE(risk, "Mfg mode should be detected");
    PASS();
}

static void test_me_verify_firmware(void) {
    IntelME me;
    uint8_t expected[32];
    TEST("ME verify firmware");
    me_init(&me);

    /* Compute expected hash from the ME state */
    {
        uint32_t i;
        uint32_t hash_state = 0x9E3779B9;
        uint8_t fw_bytes[4];
        fw_bytes[0] = (uint8_t)(me.firmware_version & 0xFF);
        fw_bytes[1] = (uint8_t)((me.firmware_version >> 8) & 0xFF);
        fw_bytes[2] = (uint8_t)(me.hfs & 0xFF);
        fw_bytes[3] = (uint8_t)((me.hfs >> 8) & 0xFF);
        for (i = 0; i < 4; i++) {
            hash_state ^= (uint32_t)fw_bytes[i];
            hash_state += (hash_state << 6) + (hash_state >> 2);
        }
        hash_state ^= (uint32_t)(me.hfs & 0xFFFF);
        hash_state += (hash_state << 5) + (hash_state >> 3);
        for (i = 0; i < 32; i++) {
            hash_state = hash_state * 1103515245 + 12345;
            expected[i] = (uint8_t)((hash_state >> 16) & 0xFF);
        }
    }

    bool ok = me_verify_firmware(&me, expected);
    ASSERT_TRUE(ok, "Firmware verification should pass with correct hash");
    /* Tampered hash should fail */
    expected[0] ^= 0xFF;
    ok = me_verify_firmware(&me, expected);
    ASSERT_FALSE(ok, "Firmware verification should fail with wrong hash");
    PASS();
}

/* ?? Firmware Resiliency Tests ??????????????????????????????????? */

static void test_resilient_fw_init(void) {
    ResilientFW fw;
    TEST("Resilient FW init");
    resilient_fw_init(&fw);
    ASSERT_TRUE(fw.auto_recovery, "Auto recovery should be enabled");
    ASSERT_EQ(fw.max_boot_attempts, (uint8_t)3, "Max boot attempts wrong");
    ASSERT_TRUE(fw.golden_hash.valid, "Golden hash should be valid");
    PASS();
}

static void test_resilient_fw_detect_corruption(void) {
    ResilientFW fw;
    uint8_t good_hash[RESILIENT_FW_HASH_SIZE];
    uint8_t bad_hash[RESILIENT_FW_HASH_SIZE];
    FWCorruptionType ctype;
    TEST("Resilient FW detect corruption");
    resilient_fw_init(&fw);
    fw.active_hash.valid = true;
    /* Set active hash to known value */
    memset(fw.active_hash.hash, 0xAB, RESILIENT_FW_HASH_SIZE);
    memcpy(good_hash, fw.active_hash.hash, RESILIENT_FW_HASH_SIZE);

    /* Good hash should show no corruption */
    bool corrupt = resilient_fw_detect_corruption(&fw, good_hash, &ctype);
    ASSERT_FALSE(corrupt, "Good hash should not trigger corruption");

    /* Bit flip should be detected */
    memcpy(bad_hash, good_hash, RESILIENT_FW_HASH_SIZE);
    bad_hash[0] ^= 0x01;
    corrupt = resilient_fw_detect_corruption(&fw, bad_hash, &ctype);
    ASSERT_TRUE(corrupt, "Bit flip should be detected");
    ASSERT_EQ((int)ctype, (int)RANDOM_BIT_FLIP, "Should be random bit flip");

    /* Multiple changes = malicious */
    bad_hash[1] ^= 0x03;
    corrupt = resilient_fw_detect_corruption(&fw, bad_hash, &ctype);
    ASSERT_TRUE(corrupt, "Multiple changes should be detected");
    ASSERT_EQ((int)ctype, (int)MALICIOUS_MODIFICATION,
              "Should be malicious modification");
    PASS();
}

static void test_resilient_fw_recover(void) {
    ResilientFW fw;
    TEST("Resilient FW recover to golden");
    resilient_fw_init(&fw);
    bool ok = resilient_fw_recover_to_golden(&fw);
    ASSERT_TRUE(ok, "Recovery should succeed");
    ASSERT_EQ(fw.active_fw_slot, fw.golden_fw_slot,
              "Active slot should be golden after recovery");
    /* Compare hashes: after recovery, active hash should match golden */
    ASSERT_TRUE(memcmp(fw.active_hash.hash, fw.golden_hash.hash,
                       RESILIENT_FW_HASH_SIZE) == 0,
                "Active hash should equal golden hash after recovery");
    PASS();
}

static void test_resilient_fw_rollback(void) {
    ResilientFW fw;
    TEST("Resilient FW rollback protection");
    resilient_fw_init(&fw);
    fw.active_hash.version = 5;
    fw.recovery_hash.version = 3;
    fw.golden_hash.version = 1;
    bool ok = resilient_fw_rollback_protection(&fw, 4);
    ASSERT_FALSE(ok, "Recovery version 3 < min 4 should block");
    ok = resilient_fw_rollback_protection(&fw, 2);
    ASSERT_TRUE(ok, "All versions >= 2 should pass");
    PASS();
}

/* ?? Secure Boot Tests ??????????????????????????????????????????? */

static void test_sb_init(void) {
    SecureBootPolicy sb;
    TEST("Secure Boot init");
    sb_init(&sb);
    ASSERT_TRUE(sb.setup_mode, "Should start in setup mode");
    ASSERT_FALSE(sb.secure_boot_enabled, "SB should be off initially");
    ASSERT_TRUE(sb_check_setup_mode(&sb), "Setup mode check");
    PASS();
}

static void test_sb_enroll_pk(void) {
    SecureBootPolicy sb;
    EFI_GUID owner = {0x12345678, 0xABCD, 0xEF01, {0,1,2,3,4,5,6,7}};
    uint8_t cert[128];
    memset(cert, 0xAA, sizeof(cert));
    TEST("Secure Boot enroll PK");
    sb_init(&sb);
    bool ok = sb_enroll_pk(&sb, &owner, cert, sizeof(cert));
    ASSERT_TRUE(ok, "PK enrollment should succeed");
    ASSERT_FALSE(sb.setup_mode, "Should exit setup mode after PK enrolled");
    ASSERT_TRUE(sb.secure_boot_enabled, "SB should be enabled");
    /* Cannot enroll PK again */
    ok = sb_enroll_pk(&sb, &owner, cert, sizeof(cert));
    ASSERT_FALSE(ok, "Second PK enrollment should fail");
    PASS();
}

static void test_sb_enroll_kek_db(void) {
    SecureBootPolicy sb;
    EFI_GUID owner = {0x12345678, 0xABCD, 0xEF01, {0,1,2,3,4,5,6,7}};
    uint8_t cert[128], sig[64];
    memset(cert, 0xAA, sizeof(cert));
    memset(sig, 0xCC, sizeof(sig));
    TEST("Secure Boot enroll KEK/DB");
    sb_init(&sb);
    /* Enroll PK first */
    sb_enroll_pk(&sb, &owner, cert, sizeof(cert));
    /* Enroll KEK */
    bool ok = sb_enroll_kek(&sb, &owner, cert, sizeof(cert));
    ASSERT_TRUE(ok, "KEK enrollment should succeed");
    ASSERT_EQ(sb.kek_count, (uint8_t)1, "Should have 1 KEK");
    /* Enroll DB */
    ok = sb_enroll_db(&sb, &owner, sig, sizeof(sig));
    ASSERT_TRUE(ok, "DB enrollment should succeed");
    ASSERT_EQ(sb.db_count, (uint8_t)1, "Should have 1 DB entry");
    /* Enroll DBX */
    ok = sb_enroll_dbx(&sb, &owner, sig, sizeof(sig));
    ASSERT_TRUE(ok, "DBX enrollment should succeed");
    ASSERT_EQ(sb.dbx_count, (uint8_t)1, "Should have 1 DBX entry");
    PASS();
}

static void test_sb_query(void) {
    SecureBootPolicy sb;
    EFI_GUID owner = {0x12345678, 0xABCD, 0xEF01, {0,1,2,3,4,5,6,7}};
    uint8_t cert[128], sig[64], wrong[64];
    memset(cert, 0xAA, sizeof(cert));
    memset(sig, 0xCC, sizeof(sig));
    memset(wrong, 0xDD, sizeof(wrong));
    TEST("Secure Boot DB query");
    sb_init(&sb);
    sb_enroll_pk(&sb, &owner, cert, sizeof(cert));
    sb_enroll_kek(&sb, &owner, cert, sizeof(cert));
    sb_enroll_db(&sb, &owner, sig, sizeof(sig));
    /* Query for known signature */
    bool ok = sb_query_db(&sb, sig, sizeof(sig));
    ASSERT_TRUE(ok, "Known signature should be found");
    /* Query for unknown signature */
    ok = sb_query_db(&sb, wrong, sizeof(wrong));
    ASSERT_FALSE(ok, "Unknown signature should not be found");
    PASS();
}

static void test_sha256(void) {
    uint8_t data[] = "Hello, Firmware Security!";
    uint8_t digest[32];
    TEST("SHA-256 hash");
    sha256_hash(data, strlen((char*)data), digest);
    /* Verify digest is non-zero */
    int zero_count = 0;
    for (int i = 0; i < 32; i++) {
        if (digest[i] == 0) zero_count++;
    }
    ASSERT_TRUE(zero_count < 32, "Hash should not be all zeros");
    /* Verify deterministic: same input -> same output */
    uint8_t digest2[32];
    sha256_hash(data, strlen((char*)data), digest2);
    ASSERT_TRUE(memcmp(digest, digest2, 32) == 0, "SHA-256 should be deterministic");
    PASS();
}

/* ?? TPM Attestation Tests ??????????????????????????????????????? */

static void test_tpm_init(void) {
    TPMState tpm;
    TEST("TPM init");
    tpm_init(&tpm);
    ASSERT_TRUE(tpm.tpm_initialized, "TPM not initialized");
    ASSERT_TRUE(tpm.tpm_self_test_passed, "Self test should pass");
    ASSERT_EQ(tpm.active_bank_count, (uint8_t)2, "Should have 2 banks");
    PASS();
}

static void test_tpm_pcr_extend(void) {
    TPMState tpm;
    uint8_t digest[32];
    uint8_t read_digest[48];
    uint16_t read_size;
    TEST("TPM PCR extend");
    tpm_init(&tpm);
    memset(digest, 0x42, 32);
    /* Extend PCR 0 with known digest */
    bool ok = tpm_pcr_extend(&tpm, TPM_ALG_SHA256, 0, digest, 32);
    ASSERT_TRUE(ok, "PCR extend should succeed");
    /* Read PCR 0 back */
    ok = tpm_pcr_read(&tpm, TPM_ALG_SHA256, 0, read_digest, &read_size);
    ASSERT_TRUE(ok, "PCR read should succeed");
    ASSERT_EQ(read_size, (uint16_t)32, "Digest size should be 32");
    /* PCR should no longer be all zeros */
    int zero_count = 0;
    for (int i = 0; i < 32; i++) {
        if (read_digest[i] == 0) zero_count++;
    }
    ASSERT_TRUE(zero_count < 32, "PCR should not be all zeros after extend");
    PASS();
}

static void test_tpm_event_log(void) {
    TPMState tpm;
    uint8_t event_data[] = "UEFI POST Phase";
    TEST("TPM event log");
    tpm_init(&tpm);
    bool ok = tpm_event_log_add(&tpm, 0, 0x00000001,
                                 event_data, sizeof(event_data));
    ASSERT_TRUE(ok, "Event log add should succeed");
    ASSERT_EQ(tpm.event_log.entry_count, (size_t)1, "Should have 1 entry");
    /* Event log should verify against PCRs */
    ok = tpm_event_log_verify(&tpm);
    ASSERT_TRUE(ok, "Event log should verify against PCRs");
    PASS();
}

static void test_tpm_key_hierarchy(void) {
    TPMState tpm;
    TEST("TPM key hierarchy");
    tpm_init(&tpm);
    /* Create EK */
    bool ok = tpm_create_ek(&tpm);
    ASSERT_TRUE(ok, "EK creation should succeed");
    ASSERT_TRUE(tpm.ek.generated, "EK should be generated");
    /* Create SRK (depends on EK) */
    ok = tpm_create_srk(&tpm);
    ASSERT_TRUE(ok, "SRK creation should succeed");
    ASSERT_TRUE(tpm.srk.generated, "SRK should be generated");
    /* Create AK (depends on SRK) */
    ok = tpm_create_ak(&tpm);
    ASSERT_TRUE(ok, "AK creation should succeed");
    ASSERT_TRUE(tpm.ak.generated, "AK should be generated");
    PASS();
}

static void test_tpm_quote(void) {
    TPMState tpm;
    uint8_t nonce[32];
    TPMS_ATTEST quote;
    TEST("TPM quote");
    tpm_init(&tpm);
    tpm_create_ek(&tpm);
    tpm_create_srk(&tpm);
    tpm_create_ak(&tpm);
    memset(nonce, 0xAA, 32);
    bool ok = tpm_quote_create(&tpm, nonce, 32, &quote);
    ASSERT_TRUE(ok, "Quote creation should succeed");
    ASSERT_EQ(quote.magic, (uint32_t)0xFF544347, "Quote magic wrong");
    ASSERT_TRUE(memcmp(quote.extra_data, nonce, 32) == 0, "Nonce mismatch");
    /* Verify quote */
    uint8_t expected_pcr[24 * 32];
    memset(expected_pcr, 0, sizeof(expected_pcr));
    ok = tpm_quote_verify(&tpm, &quote, nonce, 32,
                          expected_pcr, sizeof(expected_pcr));
    ASSERT_TRUE(ok, "Quote verification should succeed");
    PASS();
}

/* ?? Cross-Integration Tests ???????????????????????????????????? */

static void test_cross_audit_init(void) {
    FwCrossAudit audit;
    TEST("Cross audit init");
    fw_audit_init(&audit);
    ASSERT_TRUE(audit.audit_active, "Audit should be active");
    ASSERT_EQ(audit.entry_count, (uint32_t)0, "Entry count should be 0");
    ASSERT_EQ(audit.total_violations, (uint32_t)0, "No violations initially");
    PASS();
}

static void test_cross_network_audit(void) {
    FwCrossAudit audit;
    FwNetworkPacket pkt;
    TEST("Cross network audit");
    fw_audit_init(&audit);

    /* Valid data flow: data-engine(7) -> backend(8) */
    memset(&pkt, 0, sizeof(pkt));
    pkt.packet_id = 1;
    pkt.src_module = FW_SRC_DATA_ENGINE;
    pkt.dst_module = FW_SRC_BACKEND;
    pkt.payload_size = 128;
    pkt.direction = FW_NET_DIR_INGRESS;
    pkt.timestamp = 1000;
    bool ok = fw_audit_network_packet(&audit, &pkt);
    ASSERT_TRUE(ok, "Valid data-engine flow should pass");
    ASSERT_EQ(audit.entry_count, (uint32_t)1, "Should have 1 audit entry");

    /* Valid response flow: backend(8) -> frontend(9) */
    pkt.packet_id = 2;
    pkt.src_module = FW_SRC_BACKEND;
    pkt.dst_module = FW_SRC_FRONTEND;
    pkt.payload_size = 256;
    ok = fw_audit_network_packet(&audit, &pkt);
    ASSERT_TRUE(ok, "Valid backend->frontend flow should pass");
    ASSERT_EQ(audit.entry_count, (uint32_t)2, "Should have 2 audit entries");

    ASSERT_EQ(audit.total_violations, (uint32_t)0, "No violations expected");
    PASS();
}

static void test_cross_backend_audit(void) {
    FwCrossAudit audit;
    FwBackendEvent evt;
    TEST("Cross backend audit");
    fw_audit_init(&audit);

    /* Authorized read */
    memset(&evt, 0, sizeof(evt));
    evt.event_id = 1;
    evt.src_module = FW_SRC_DATA_ENGINE;
    evt.operation = FW_BACKEND_OP_READ;
    evt.request_size = 64;
    evt.authorized = true;
    evt.timestamp = 2000;
    memcpy(evt.resource_path, "/data/config", 12);
    bool ok = fw_audit_backend_event(&audit, &evt);
    ASSERT_TRUE(ok, "Authorized read should pass");
    ASSERT_EQ(audit.total_backend_events, (uint32_t)1, "Should log 1 event");

    /* Unauthorized write = violation */
    evt.event_id = 2;
    evt.operation = FW_BACKEND_OP_WRITE;
    evt.authorized = false;
    evt.timestamp = 2001;
    ok = fw_audit_backend_event(&audit, &evt);
    ASSERT_FALSE(ok, "Unauthorized write should fail");
    ASSERT_EQ(audit.total_violations, (uint32_t)1, "Should have 1 violation");
    PASS();
}

static void test_cross_integrity(void) {
    FwCrossAudit audit;
    uint8_t hash1[FW_AUDIT_HASH_SIZE], hash2[FW_AUDIT_HASH_SIZE];
    FwNetworkPacket pkt;
    TEST("Cross audit integrity");
    fw_audit_init(&audit);

    /* Add some entries */
    memset(&pkt, 0, sizeof(pkt));
    pkt.src_module = FW_SRC_DATA_ENGINE;
    pkt.dst_module = FW_SRC_BACKEND;
    pkt.payload_size = 64;
    fw_audit_network_packet(&audit, &pkt);

    /* Compute integrity hash */
    bool ok = fw_audit_compute_integrity(&audit, hash1);
    ASSERT_TRUE(ok, "Integrity hash compute should succeed");

    /* Compute again - should be same */
    ok = fw_audit_compute_integrity(&audit, hash2);
    ASSERT_TRUE(ok, "Second integrity hash should succeed");
    ASSERT_TRUE(memcmp(hash1, hash2, FW_AUDIT_HASH_SIZE) == 0,
                "Integrity hash should be deterministic");
    PASS();
}

static void test_cross_export(void) {
    FwCrossAudit audit;
    FwNetworkPacket pkt;
    uint8_t log_buf[4096];
    uint32_t log_size;
    TEST("Cross audit export");
    fw_audit_init(&audit);

    /* Add entries */
    memset(&pkt, 0, sizeof(pkt));
    pkt.src_module = FW_SRC_DATA_ENGINE;
    pkt.dst_module = FW_SRC_BACKEND;
    pkt.payload_size = 64;
    fw_audit_network_packet(&audit, &pkt);
    pkt.src_module = FW_SRC_BACKEND;
    pkt.dst_module = FW_SRC_FRONTEND;
    pkt.payload_size = 128;
    fw_audit_network_packet(&audit, &pkt);

    /* Export log */
    log_size = sizeof(log_buf);
    bool ok = fw_audit_export_log(&audit, log_buf, &log_size);
    ASSERT_TRUE(ok, "Log export should succeed");
    ASSERT_TRUE(log_size > 8, "Export should have header + entries");
    PASS();
}

/* ?? Main Test Runner ???????????????????????????????????????????? */

int main(void) {
    printf("===== Firmware Security Module Test Suite =====\n\n");

    printf("[SPI Protection]\n");
    test_spi_init();
    test_spi_protected_range();
    test_spi_lock_config();
    test_spi_access_control();
    test_spi_null_checks();

    printf("\n[SMM Attacks]\n");
    test_smm_init();
    test_smm_smrr();
    test_smm_smrr_validate();
    test_smm_ring3_to_ring2();

    printf("\n[DMA / IOMMU]\n");
    test_iommu_init();
    test_iommu_map_device();
    test_iommu_create_domain();
    test_iommu_translate();

    printf("\n[BMC / ME]\n");
    test_bmc_init();
    test_bmc_ipmi_get_device_id();
    test_me_init();
    test_me_lock_jtag();
    test_me_supply_chain_risk();
    test_me_verify_firmware();

    printf("\n[Firmware Resiliency]\n");
    test_resilient_fw_init();
    test_resilient_fw_detect_corruption();
    test_resilient_fw_recover();
    test_resilient_fw_rollback();

    printf("\n[Secure Boot]\n");
    test_sb_init();
    test_sb_enroll_pk();
    test_sb_enroll_kek_db();
    test_sb_query();
    test_sha256();

    printf("\n[TPM Attestation]\n");
    test_tpm_init();
    test_tpm_pcr_extend();
    test_tpm_event_log();
    test_tpm_key_hierarchy();
    test_tpm_quote();

    printf("\n[Cross-Module Integration]\n");
    test_cross_audit_init();
    test_cross_network_audit();
    test_cross_backend_audit();
    test_cross_integrity();
    test_cross_export();

    printf("\n===== Results: %d/%d tests passed =====\n",
           tests_passed, tests_run);

    return (tests_passed == tests_run) ? 0 : 1;
}
