#ifndef UEFI_IMAGE_AUTH_H
#define UEFI_IMAGE_AUTH_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* ================================================================
 * L1-L5: UEFI Image Authentication and Cryptography
 *
 * Knowledge points:
 *   L1: struct definitions (SHA-256 context, EFI_TIME, capsule header)
 *   L2: cryptographic hash functions, CRC error detection
 *   L3: UEFI Secure Boot chain (PK -> KEK -> db/dbx)
 *   L4: FIPS 180-4 (SHA-256), IEEE 802.3 (CRC32 polynomial)
 *   L5: Merkle-Damgard construction, table-driven CRC32,
 *       certificate chain validation flow
 * ================================================================ */

/* ---- CRC32 (IEEE 802.3 / PKZIP polynomial) ---- */
#define CRC32_POLYNOMIAL  0xEDB88320UL
#define CRC32_INITIAL     0xFFFFFFFFUL
#define CRC32_FINAL_XOR   0xFFFFFFFFUL

/* ---- SHA-256 (FIPS 180-4) ---- */
#define SHA256_BLOCK_SIZE  64
#define SHA256_DIGEST_SIZE 32
#define SHA256_HASH_SIZE   32

typedef struct {
    uint32_t state[8];
    uint64_t total_bytes;
    uint8_t  buffer[SHA256_BLOCK_SIZE];
    uint8_t  buffer_used;
    bool     finalized;
} SHA256Context;

/* ---- UEFI Time (for authenticated variables) ---- */
typedef struct {
    uint16_t year;
    uint8_t  month;
    uint8_t  day;
    uint8_t  hour;
    uint8_t  minute;
    uint8_t  second;
    uint8_t  pad1;
    uint32_t nanosecond;
    int16_t  timezone;
    uint8_t  daylight;
    uint8_t  pad2;
} EFITime;

/* ---- EFI Variable Authentication (Secure Boot) ---- */
#define EFI_VARIABLE_AUTHENTICATION_TYPE_TIMESTAMP 1
#define EFI_VARIABLE_AUTHENTICATION_TYPE_CERT      2

typedef struct {
    uint64_t monotonic_count;
    EFITime  timestamp;
} EFIVariableAuthTime;

typedef struct {
    uint8_t  type;
    uint8_t  subtype;
    uint16_t reserved;
    union {
        EFIVariableAuthTime timestamp;
        uint8_t             cert_data[2048];
    } auth_info;
} EFIVariableAuthDescriptor;

/* ---- EFI Capsule Update Header ---- */
#define EFI_CAPSULE_HEADER_FLAGS_PERSIST_ACROSS_RESET  0x00010000
#define EFI_CAPSULE_HEADER_FLAGS_POPULATE_SYSTEM_TABLE 0x00020000
#define EFI_CAPSULE_HEADER_FLAGS_INITIATE_RESET         0x00040000

#pragma pack(push, 1)
typedef struct {
    uint8_t  capsule_guid[16];
    uint32_t header_size;
    uint32_t flags;
    uint32_t capsule_image_size;
} EFICapsuleHeader;
#pragma pack(pop)

/* ---- Secure Boot Key Database ---- */
typedef enum {
    SB_VAR_PK,        /* Platform Key */
    SB_VAR_KEK,       /* Key Exchange Key */
    SB_VAR_DB,        /* Authorized Signature Database */
    SB_VAR_DBX,       /* Forbidden Signature Database */
    SB_VAR_DBT,       /* Authorized Timestamp Database */
    SB_VAR_DBR,       /* Recovery Database */
    SB_VAR_COUNT
} SecureBootVariableId;

typedef struct {
    SecureBootVariableId id;
    const char *name;
    const char *guid_str;
    bool        is_authenticated;
} SecureBootVarInfo;

typedef struct {
    uint8_t  pk_present;
    uint8_t  kek_present;
    uint8_t  db_present;
    uint8_t  dbx_present;
    bool     setup_mode;
    bool     secure_boot_enabled;
    bool     audit_mode;
    bool     deployed_mode;
} SecureBootState;

/* ---- Function declarations ---- */

/* CRC32 */
uint32_t crc32_init(void);
uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t len);
uint32_t crc32_finalize(uint32_t crc);
uint32_t crc32_compute(const uint8_t *data, size_t len);

/* SHA-256 */
void     sha256_init(SHA256Context *ctx);
void     sha256_update(SHA256Context *ctx, const uint8_t *data, size_t len);
void     sha256_finalize(SHA256Context *ctx, uint8_t digest[SHA256_DIGEST_SIZE]);
void     sha256_hash(const uint8_t *data, size_t len, uint8_t digest[SHA256_DIGEST_SIZE]);
void     sha256_hash_to_hex(const uint8_t *data, size_t len, char *hex_out, size_t hex_size);

/* Secure Boot */
void     secure_boot_init(SecureBootState *sb);
bool     secure_boot_verify_image(const SecureBootState *sb,
                                   const uint8_t *image, size_t size,
                                   const uint8_t *signature, size_t sig_size);
const char* secure_boot_variable_name(SecureBootVariableId id);
void     secure_boot_print_state(const SecureBootState *sb);

/* Capsule Update */
bool     capsule_parse_header(const uint8_t *data, size_t size, EFICapsuleHeader *hdr);
bool     capsule_validate_header(const EFICapsuleHeader *hdr, size_t total_size);
void     capsule_print_header(const EFICapsuleHeader *hdr);

/* EFI Time */
void     efi_time_init(EFITime *t, uint16_t year, uint8_t month, uint8_t day,
                        uint8_t hour, uint8_t minute, uint8_t second);

#endif /* UEFI_IMAGE_AUTH_H */