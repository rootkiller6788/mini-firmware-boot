#include "uefi_sb.h"
#include "signature_verify.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static bool db_add_entry(SignatureDB *db, const EFISignature *entry)
{
    if (!db || !entry) return false;
    if (db->count >= SB_MAX_SIGNATURES_PER_DB) return false;
    memcpy(&db->entries[db->count], entry, sizeof(EFISignature));
    db->count++;
    return true;
}

static bool db_remove_entry(SignatureDB *db, const EFISignature *entry)
{
    uint32_t target;
    bool found = false;
    if (!db || !entry) return false;
    for (uint32_t i = 0; i < db->count; i++) {
        if (db->entries[i].type == entry->type &&
            db->entries[i].signature_size == entry->signature_size &&
            memcmp(db->entries[i].signature_data, entry->signature_data,
                   entry->signature_size) == 0) {
            target = i;
            found = true;
            break;
        }
    }
    if (!found) return false;
    for (uint32_t i = target; i < db->count - 1; i++) {
        memcpy(&db->entries[i], &db->entries[i + 1], sizeof(EFISignature));
    }
    db->count--;
    return true;
}

static bool db_find_entry(const SignatureDB *db, const EFISignature *entry)
{
    if (!db || !entry) return false;
    for (uint32_t i = 0; i < db->count; i++) {
        if (db->entries[i].type == entry->type &&
            db->entries[i].signature_size == entry->signature_size &&
            memcmp(db->entries[i].signature_data, entry->signature_data,
                   entry->signature_size) == 0) {
            return true;
        }
    }
    return false;
}

bool sb_init(SecureBootVars *sb)
{
    if (!sb) return false;
    memset(sb, 0, sizeof(SecureBootVars));
    sb->setup_mode = true;
    sb->secure_boot = false;
    sb->variable_attributes = SB_EFI_VARIABLE_NON_VOLATILE |
                              SB_EFI_VARIABLE_BOOTSERVICE_ACCESS |
                              SB_EFI_VARIABLE_RUNTIME_ACCESS;
    return true;
}

bool sb_enroll_pk(SecureBootVars *sb, const EFISignature *pk_entry)
{
    if (!sb || !pk_entry) return false;
    if (!sb->setup_mode && sb->pk.count > 0) return false;
    sb->pk.count = 0;
    if (!db_add_entry(&sb->pk, pk_entry)) return false;
    sb->setup_mode = false;
    sb->pktimestamp++;
    return true;
}

bool sb_enroll_kek(SecureBootVars *sb, const EFISignature *kek_entry)
{
    if (!sb || !kek_entry) return false;
    if (sb->setup_mode) return true;
    if (!db_add_entry(&sb->kek, kek_entry)) return false;
    sb->kek_timestamp++;
    return true;
}

bool sb_enroll_db(SecureBootVars *sb, const EFISignature *db_entry)
{
    if (!sb || !db_entry) return false;
    if (sb->setup_mode) return true;
    if (!db_add_entry(&sb->db, db_entry)) return false;
    sb->db_timestamp++;
    return true;
}

bool sb_enroll_dbx(SecureBootVars *sb, const EFISignature *dbx_entry)
{
    if (!sb || !dbx_entry) return false;
    if (sb->setup_mode) return true;
    if (!db_add_entry(&sb->dbx, dbx_entry)) return false;
    sb->dbx_timestamp++;
    return true;
}

bool sb_enroll_dbt(SecureBootVars *sb, const EFISignature *dbt_entry)
{
    if (!sb || !dbt_entry) return false;
    if (!db_add_entry(&sb->dbt, dbt_entry)) return false;
    return true;
}

bool sb_enroll_dbr(SecureBootVars *sb, const EFISignature *dbr_entry)
{
    if (!sb || !dbr_entry) return false;
    if (!db_add_entry(&sb->dbr, dbr_entry)) return false;
    return true;
}

bool sb_is_in_dbx(const SecureBootVars *sb, const uint8_t *hash, uint32_t hash_size)
{
    if (!sb || !hash) return false;
    EFISignature lookup;
    memset(&lookup, 0, sizeof(lookup));
    lookup.signature_size = hash_size;
    memcpy(lookup.signature_data, hash, hash_size > SB_MAX_SIG_SIZE ? SB_MAX_SIG_SIZE : hash_size);
    if (db_find_entry(&sb->dbx, &lookup)) return true;
    if (db_find_entry(&sb->dbr, &lookup)) return true;
    return false;
}

bool sb_verify_image(const SecureBootVars *sb, const uint8_t *image_hash,
                     uint32_t hash_size, const uint8_t *signature,
                     uint32_t sig_size)
{
    if (!sb || !image_hash || !signature) return false;
    if (sb->setup_mode) return true;
    if (!sb->secure_boot) return true;

    /* Check dbx/dbr blacklist first */
    if (sb_is_in_dbx(sb, image_hash, hash_size)) return false;
    if (sb_is_in_dbx(sb, signature, sig_size)) return false;

    /* Check db whitelist */
    EFISignature lookup;
    memset(&lookup, 0, sizeof(lookup));
    lookup.type = SB_SIG_TYPE_SHA256_HASH;
    lookup.signature_size = hash_size;
    memcpy(lookup.signature_data, image_hash,
           hash_size > SB_MAX_SIG_SIZE ? SB_MAX_SIG_SIZE : hash_size);
    if (db_find_entry(&sb->db, &lookup)) return true;
    if (db_find_entry(&sb->dbt, &lookup)) return true;

    return false;
}

bool sb_remove_db(SecureBootVars *sb, const EFISignature *entry)
{
    if (!sb || !entry) return false;
    if (sb->setup_mode) return true;
    return db_remove_entry(&sb->db, entry);
}

bool sb_remove_dbx(SecureBootVars *sb, const EFISignature *entry)
{
    if (!sb || !entry) return false;
    if (sb->setup_mode) return true;
    return db_remove_entry(&sb->dbx, entry);
}

bool sb_delete_pk(SecureBootVars *sb)
{
    if (!sb) return false;
    /* In setup mode: PK can be freely cleared (platform unowned) */
    if (sb->setup_mode) {
        memset(&sb->pk, 0, sizeof(SignatureDB));
        return true;
    }
    /* PK is present: delete it and re-enter setup mode.
     * In production, this requires signing with the current PK,
     * but for the demo/API we allow direct deletion. */
    if (sb->pk.count > 0) {
        memset(&sb->pk, 0, sizeof(SignatureDB));
        sb->setup_mode = true;
        sb->secure_boot = false;
        return true;
    }
    /* No PK present: already in effective setup mode */
    sb->setup_mode = true;
    sb->secure_boot = false;
    return true;
}

void sb_print_state(const SecureBootVars *sb)
{
    if (!sb) return;
    printf("=== UEFI Secure Boot State ===\n");
    printf("Setup Mode   : %s\n", sb->setup_mode ? "YES (1)" : "NO (0)");
    printf("Secure Boot  : %s\n", sb->secure_boot ? "ENABLED" : "DISABLED");
    printf("PK entries   : %u\n", sb->pk.count);
    printf("KEK entries  : %u\n", sb->kek.count);
    printf("db entries   : %u\n", sb->db.count);
    printf("dbx entries  : %u\n", sb->dbx.count);
    printf("dbt entries  : %u\n", sb->dbt.count);
    printf("dbr entries  : %u\n", sb->dbr.count);
    printf("==============================\n");
}
