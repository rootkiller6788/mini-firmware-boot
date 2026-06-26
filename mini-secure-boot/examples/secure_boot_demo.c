#include "uefi_sb.h"
#include "signature_verify.h"
#include <stdio.h>
#include <string.h>

int main(void)
{
    SecureBootVars sb;
    EFISignature pk, kek, db_entry, dbx_entry;
    uint8_t bootloader[4096];
    uint8_t bootloader_hash[SHA256_HASH_SIZE];
    uint8_t evil_bootloader[4096];

    printf("╔══════════════════════════════════════════════╗\n");
    printf("║   UEFI Secure Boot Demo — Full Lifecycle     ║\n");
    printf("╚══════════════════════════════════════════════╝\n\n");

    /* 1. Initialize: enters Setup Mode */
    printf(">>> Step 1: Initialize Secure Boot\n");
    sb_init(&sb);
    sb_print_state(&sb);

    /* 2. In Setup Mode, enroll Platform Key */
    printf("\n>>> Step 2: Enroll Platform Key (PK)\n");
    memset(&pk, 0, sizeof(pk));
    pk.type = SB_SIG_TYPE_X509_CERT;
    pk.signature_size = 256;
    for (int i = 0; i < 256; i++) pk.signature_data[i] = (uint8_t)(i & 0xFF);
    printf("  PK type=%d size=%u\n", pk.type, pk.signature_size);

    if (sb_enroll_pk(&sb, &pk)) {
        printf("  PK enrolled. Setup Mode EXITED.\n");
    } else {
        printf("  FAILED to enroll PK.\n");
    }
    sb_print_state(&sb);

    /* 3. Enroll KEK */
    printf("\n>>> Step 3: Enroll Key Exchange Key (KEK)\n");
    memset(&kek, 0, sizeof(kek));
    kek.type = SB_SIG_TYPE_X509_CERT;
    kek.signature_size = 256;
    for (int i = 0; i < 256; i++) kek.signature_data[i] = (uint8_t)((i + 128) & 0xFF);

    if (sb_enroll_kek(&sb, &kek)) {
        printf("  KEK enrolled successfully.\n");
    } else {
        printf("  FAILED to enroll KEK.\n");
    }

    /* 4. Enroll db (allowed signature) — SHA256 hash of bootloader */
    printf("\n>>> Step 4: Enroll Allowed Signature (db)\n");
    memset(bootloader, 0xAA, sizeof(bootloader));
    sha256_hash(bootloader, sizeof(bootloader), bootloader_hash);

    memset(&db_entry, 0, sizeof(db_entry));
    db_entry.type = SB_SIG_TYPE_SHA256_HASH;
    db_entry.signature_size = SHA256_HASH_SIZE;
    memcpy(db_entry.signature_data, bootloader_hash, SHA256_HASH_SIZE);

    if (sb_enroll_db(&sb, &db_entry)) {
        printf("  db entry enrolled (SHA256 of bootloader).\n");
    } else {
        printf("  FAILED to enroll db.\n");
    }

    /* 5. Enroll dbx (blacklisted hash) — evil bootloader */
    printf("\n>>> Step 5: Enroll Forbidden Signature (dbx)\n");
    memset(evil_bootloader, 0xDE, sizeof(evil_bootloader));
    uint8_t evil_hash[SHA256_HASH_SIZE];
    sha256_hash(evil_bootloader, sizeof(evil_bootloader), evil_hash);

    memset(&dbx_entry, 0, sizeof(dbx_entry));
    dbx_entry.type = SB_SIG_TYPE_SHA256_HASH;
    dbx_entry.signature_size = SHA256_HASH_SIZE;
    memcpy(dbx_entry.signature_data, evil_hash, SHA256_HASH_SIZE);

    if (sb_enroll_dbx(&sb, &dbx_entry)) {
        printf("  dbx entry enrolled (SHA256 of evil bootloader).\n");
    } else {
        printf("  FAILED to enroll dbx.\n");
    }

    sb_print_state(&sb);

    /* 6. Attempt to boot with signed image (should pass) */
    printf("\n>>> Step 6: Verify GOOD Bootloader\n");
    sb.secure_boot = true;

    /* Simulate valid signature matching the hash in db */
    uint8_t good_signature[SHA256_HASH_SIZE];
    memcpy(good_signature, bootloader_hash, SHA256_HASH_SIZE);

    bool pass = sb_verify_image(&sb, bootloader_hash, SHA256_HASH_SIZE,
                                good_signature, SHA256_HASH_SIZE);
    printf("  Bootloader verification: %s\n", pass ? "PASS — Allowed" : "BLOCKED");

    /* 7. Attempt to boot with unsigned image (should fail) */
    printf("\n>>> Step 7: Verify UNSIGNED Bootloader\n");
    uint8_t unknown_hash[SHA256_HASH_SIZE];
    memset(unknown_hash, 0x42, SHA256_HASH_SIZE);
    uint8_t unknown_sig[SHA256_HASH_SIZE];
    memset(unknown_sig, 0x13, SHA256_HASH_SIZE);

    pass = sb_verify_image(&sb, unknown_hash, SHA256_HASH_SIZE,
                           unknown_sig, SHA256_HASH_SIZE);
    printf("  Unsigned bootloader: %s\n", pass ? "PASS" : "BLOCKED — No matching db entry");

    /* 8. Attempt to boot with blacklisted image (should be blocked) */
    printf("\n>>> Step 8: Verify BLACKLISTED Bootloader\n");
    uint8_t evil_sig[SHA256_HASH_SIZE];
    memcpy(evil_sig, evil_hash, SHA256_HASH_SIZE);

    pass = sb_verify_image(&sb, evil_hash, SHA256_HASH_SIZE,
                           evil_sig, SHA256_HASH_SIZE);
    printf("  Blacklisted bootloader: %s\n", pass ? "PASS" : "BLOCKED — Found in dbx");

    /* 9. Delete PK — re-enter Setup Mode */
    printf("\n>>> Step 9: Delete PK (re-enter Setup Mode)\n");
    if (sb_delete_pk(&sb)) {
        printf("  PK deleted. Re-entered Setup Mode.\n");
    } else {
        printf("  FAILED to delete PK.\n");
    }
    sb_print_state(&sb);

    printf("\n=== Demo Complete ===\n");
    return 0;
}
