#ifndef TRUST_CHAIN_H
#define TRUST_CHAIN_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "signature_verify.h"

#define SHA256_HASH_SIZE_TC       32
#define RSA_MAX_MODULUS_BYTES_TC  256

#define TC_MAX_COMPONENTS        16
#define TC_MAX_NAME_LEN          64
#define TC_MAX_DESC_LEN          256
#define TC_MAX_FIT_IMAGES        32
#define TC_MAX_FIT_CONFIGS       8
#define TC_MAX_COMPONENT_DATA    65536

typedef enum {
    BOOT_COMP_SPL = 0,
    BOOT_COMP_TPL,
    BOOT_COMP_U_BOOT,
    BOOT_COMP_LINUX_KERNEL,
    BOOT_COMP_INITRD,
    BOOT_COMP_DT_FIT,
    BOOT_COMP_FDT,
    BOOT_COMP_UNKNOWN,
    BOOT_COMP_COUNT
} BootComponent;

typedef enum {
    TC_STATUS_UNVERIFIED = 0,
    TC_STATUS_VERIFIED,
    TC_STATUS_REVOKED,
    TC_STATUS_MISSING,
    TC_STATUS_FAILED
} VerificationStatus;

typedef struct {
    BootComponent    type;
    char             name[TC_MAX_NAME_LEN];
    uint8_t          hash[SHA256_HASH_SIZE_TC];
    uint8_t          data[TC_MAX_COMPONENT_DATA];
    uint32_t         data_size;
    bool             present;
    VerificationStatus status;
} ChainComponent;

typedef struct {
    ChainComponent components[TC_MAX_COMPONENTS];
    uint32_t       component_count;
    uint32_t       verified_bitmap;
    bool           boot_allowed;
    char           error_message[TC_MAX_DESC_LEN];
} BootChain;

/* FIT Image structures */
#define FIT_MAGIC   0xD00DFEED
#define FIT_MAX_DESC_LEN 128

typedef struct {
    char        description[FIT_MAX_DESC_LEN];
    uint64_t    timestamp;
    uint32_t    image_count;
    uint32_t    config_count;
} FITImageHeader;

typedef struct {
    char        name[TC_MAX_NAME_LEN];
    char        type[TC_MAX_NAME_LEN];
    uint8_t     data[TC_MAX_COMPONENT_DATA];
    uint32_t    data_size;
    uint32_t    load_address;
    uint32_t    entry_point;
    char        compression[TC_MAX_NAME_LEN];
    uint8_t     hash_value[SHA256_HASH_SIZE_TC];
    bool        has_hash;
} FITSubImage;

typedef struct {
    char         description[FIT_MAX_DESC_LEN];
    uint32_t     kernel_idx;
    uint32_t     fdt_idx;
    uint32_t     initrd_idx;
    char         signature_type[TC_MAX_NAME_LEN];
    uint8_t      signature_data[RSA_MAX_MODULUS_BYTES_TC];
    uint32_t     sig_len;
    bool         verified;
} FITConfiguration;

typedef struct {
    FITImageHeader  header;
    FITSubImage     images[TC_MAX_FIT_IMAGES];
    FITConfiguration configs[TC_MAX_FIT_CONFIGS];
    uint32_t        total_size;
    bool            parsed;
} FITImage;

/* Boot chain */
void trust_chain_init(BootChain *chain);
bool trust_chain_add_component(BootChain *chain, BootComponent type,
                                const char *name, const uint8_t *data,
                                uint32_t data_size);
bool trust_chain_verify_component(BootChain *chain, uint32_t idx,
                                   const uint8_t *expected_hash);
bool trust_chain_verify_all(BootChain *chain,
                             const uint8_t expected_hashes[][SHA256_HASH_SIZE_TC]);
bool trust_chain_boot_authorized(const BootChain *chain);
void trust_chain_print_status(const BootChain *chain);

/* FIT image */
bool fit_parse(FITImage *fit, const uint8_t *data, uint32_t data_size);
bool fit_get_subimage(const FITImage *fit, uint32_t idx, const FITSubImage **out);
bool fit_get_config(const FITImage *fit, uint32_t idx, const FITConfiguration **out);
bool fit_verify_config(const FITImage *fit, uint32_t config_idx,
                        const RSAKey *pub_key);
bool fit_verify_all(const FITImage *fit, const RSAKey *pub_key);
void fit_print_components(const FITImage *fit);

#endif /* TRUST_CHAIN_H */
