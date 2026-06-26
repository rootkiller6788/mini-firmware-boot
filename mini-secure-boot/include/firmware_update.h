#ifndef FIRMWARE_UPDATE_H
#define FIRMWARE_UPDATE_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define FW_CAPSULE_FLAGS_PERSIST_ACROSS_RESET   0x00010000
#define FW_CAPSULE_FLAGS_POPULATE_SYSTEM_TABLE  0x00020000
#define FW_CAPSULE_FLAGS_INITIATE_RESET         0x00040000

#define FW_UPDATE_MODE_NORMAL      0
#define FW_UPDATE_MODE_STAGING     1
#define FW_UPDATE_MODE_COMMIT      2

#define FW_MAX_CAPSULE_SIZE        (4 * 1024 * 1024)
#define FW_MAX_IMAGE_SIZE          (2 * 1024 * 1024)
#define FW_MAX_VERSION_STR         32
#define FW_MAX_GUID_STR            40

/* EFI_FIRMWARE_MANAGEMENT_CAPSULE_ID_GUID */
#define FW_CAPSULE_GUID_DATA \
    {0x6DCBD5BA,0xE82A,0x4C44, {0xBF,0xD6,0x3F,0x63,0xD8,0x2C,0x5A,0x33}}

typedef struct {
    uint32_t data1;
    uint16_t data2;
    uint16_t data3;
    uint8_t  data4[8];
} FWGUID;

typedef struct {
    FWGUID     capsule_guid;
    uint32_t   header_size;
    uint32_t   flags;
    uint32_t   capsule_image_size;
} FWCapsule;

typedef struct {
    uint32_t version;
    uint32_t lowest_supported_version;
    uint32_t update_image_size;
    uint32_t update_image_offset;
    uint8_t  vendor_code[32];
    uint32_t checksum;
} FWImageHeader;

typedef enum {
    FW_VERIFY_OK = 0,
    FW_VERIFY_SIG_FAIL,
    FW_VERIFY_ROLLBACK,
    FW_VERIFY_CORRUPT,
    FW_VERIFY_INCOMPATIBLE,
    FW_VERIFY_SIZE_LIMIT
} FWVerifyResult;

typedef struct {
    uint8_t  staging_area[FW_MAX_CAPSULE_SIZE];
    uint32_t staging_size;
    uint32_t update_mode;
    uint32_t current_version;
    uint32_t lowest_supported_version;
    bool     update_pending;
} FWUpdateContext;

bool fw_capsule_init(FWUpdateContext *ctx);
bool fw_capsule_set_image(FWUpdateContext *ctx, const FWCapsule *capsule,
                           const uint8_t *payload, uint32_t payload_size);
FWVerifyResult fw_capsule_validate(const FWUpdateContext *ctx,
                                    const RSAKey_256 *trusted_key);
bool fw_capsule_process(FWUpdateContext *ctx);
bool fw_capsule_commit(FWUpdateContext *ctx);
bool fw_capsule_rollback(FWUpdateContext *ctx);
uint32_t fw_capsule_get_version(const FWUpdateContext *ctx);
const char *fw_verify_result_str(FWVerifyResult result);

#define RSA_MAX_MODULUS_BYTES_256  256
typedef struct {
    uint8_t  modulus[RSA_MAX_MODULUS_BYTES_256];
    uint8_t  exponent[RSA_MAX_MODULUS_BYTES_256];
    uint32_t mod_len;
    uint32_t exp_len;
} RSAKey_256;

#endif /* FIRMWARE_UPDATE_H */
