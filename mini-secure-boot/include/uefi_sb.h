#ifndef UEFI_SB_H
#define UEFI_SB_H

#include <stdbool.h>
#include <stdint.h>

#define SB_MAX_SIGNATURES_PER_DB 64
#define SB_MAX_CERT_SIZE         2048
#define SB_MAX_SIG_SIZE          512
#define SB_MAX_GUID_STR          40
#define SB_MAX_OWNER_STR         64

/* EFI_GUID byte representation */
#define EFI_GLOBAL_VARIABLE_GUID \
    {0x8BE4DF61,0x93CA,0x11D2, {0xAA,0x0D,0x00,0xE0,0x98,0x03,0x2B,0x8C}}
#define EFI_IMAGE_SECURITY_DATABASE_GUID \
    {0xD719B2CB,0x3D3A,0x4596, {0xA3,0xBC,0xDA,0xD0,0x0E,0x67,0x65,0x6F}}
#define EFI_CERT_X509_GUID \
    {0xA5C059A1,0x94E4,0x4AA7, {0x87,0xB5,0xAB,0x15,0x5C,0x2B,0xF0,0x72}}
#define EFI_CERT_SHA256_GUID \
    {0xC1C41626,0x504C,0x4092, {0xAC,0xA9,0x41,0xF9,0x36,0x93,0x43,0x28}}
#define EFI_CERT_RSA2048_GUID \
    {0x3C5766E8,0x269C,0x4E34, {0xAA,0x14,0xED,0x77,0x6E,0x85,0xB3,0xB6}}

#define SB_EFI_VARIABLE_NON_VOLATILE         0x00000001
#define SB_EFI_VARIABLE_BOOTSERVICE_ACCESS   0x00000002
#define SB_EFI_VARIABLE_RUNTIME_ACCESS       0x00000004
#define SB_EFI_VARIABLE_TIME_BASED_AUTH      0x00000020
#define SB_EFI_VARIABLE_AUTHENTICATED_WRITE  0x00000010

typedef enum {
    SB_SIG_TYPE_NONE = 0,
    SB_SIG_TYPE_X509_CERT,
    SB_SIG_TYPE_SHA256_HASH,
    SB_SIG_TYPE_RSA2048,
    SB_SIG_TYPE_SHA1,
    SB_SIG_TYPE_COUNT
} SignatureType;

typedef struct {
    uint32_t data1;
    uint16_t data2;
    uint16_t data3;
    uint8_t  data4[8];
} EFI_GUID;

typedef struct {
    EFI_GUID     owner;
    SignatureType type;
    uint8_t       signature_data[SB_MAX_SIG_SIZE];
    uint32_t      signature_size;
} EFISignature;

typedef struct {
    EFISignature entries[SB_MAX_SIGNATURES_PER_DB];
    uint32_t     count;
} SignatureDB;

typedef struct {
    bool        setup_mode;
    bool        secure_boot;
    uint32_t    variable_attributes;
    SignatureDB pk;
    SignatureDB kek;
    SignatureDB db;
    SignatureDB dbx;
    SignatureDB dbt;
    SignatureDB dbr;
    uint64_t    pktimestamp;
    uint64_t    kek_timestamp;
    uint64_t    db_timestamp;
    uint64_t    dbx_timestamp;
} SecureBootVars;

bool sb_init(SecureBootVars *sb);
bool sb_enroll_pk(SecureBootVars *sb, const EFISignature *pk_entry);
bool sb_enroll_kek(SecureBootVars *sb, const EFISignature *kek_entry);
bool sb_enroll_db(SecureBootVars *sb, const EFISignature *db_entry);
bool sb_enroll_dbx(SecureBootVars *sb, const EFISignature *dbx_entry);
bool sb_enroll_dbt(SecureBootVars *sb, const EFISignature *dbt_entry);
bool sb_enroll_dbr(SecureBootVars *sb, const EFISignature *dbr_entry);
bool sb_verify_image(const SecureBootVars *sb, const uint8_t *image_hash,
                     uint32_t hash_size, const uint8_t *signature,
                     uint32_t sig_size);
bool sb_is_in_dbx(const SecureBootVars *sb, const uint8_t *hash, uint32_t hash_size);
bool sb_delete_pk(SecureBootVars *sb);
void sb_print_state(const SecureBootVars *sb);

#endif /* UEFI_SB_H */
