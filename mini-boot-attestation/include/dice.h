#ifndef DICE_H
#define DICE_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "tpm_quote.h"
#include "aik_identity.h"

/*
 * DICE (Device Identifier Composition Engine) �� TCG DICE Architecture
 *
 * DICE is a hardware-software layered attestation mechanism that chains
 * device identity from immutable ROM (Layer 0) through successive firmware
 * layers (Layers 1..N). Each layer derives its own asymmetric key pair from
 * the previous layer's Compound Device Identifier (CDI).
 *
 * Theorem (DICE Chain of Trust):
 *   Given a Unique Device Secret (UDS) known only to hardware, each layer
 *   L_i produces CDI_i = KDF(CDI_{i-1}, H(code_i, config_i, authority_i)).
 *   A verifier can validate the entire chain from leaf to root, establishing
 *   that each layer's code was unmodified.
 *
 * Reference:
 *   - TCG DICE Architecture Specification v1.1
 *   - TCG DICE Layering Architecture
 *   - TCG DICE Certificate Profiles
 *   - Mandt et al., "DICE: Device Identifier Composition Engine", 2015
 *
 * Stanford CS 144/244: Network Security �� Trusted Computing
 * CMU 15-410: OS �� Boot Integrity
 * Cambridge Part II: Concurrent Systems �� Trusted Boot
 */

#define DICE_UDS_SIZE            32
#define DICE_CDI_SIZE            32
#define DICE_MAX_LAYERS          8
#define DICE_CERT_SIZE           1024
#define DICE_AUTHORITY_HASH_SIZE 32
#define DICE_CONFIG_HASH_SIZE    32
#define DICE_CODE_HASH_SIZE      32
#define DICE_LAYER_LABEL_SIZE    48

typedef enum {
    DICE_LAYER_L0_HARDWARE    = 0,
    DICE_LAYER_L1_BOOT_ROM    = 1,
    DICE_LAYER_L2_BOOTLOADER  = 2,
    DICE_LAYER_L3_OS_KERNEL   = 3,
    DICE_LAYER_L4_INITRD      = 4,
    DICE_LAYER_L5_FILESYSTEM  = 5,
    DICE_LAYER_L6_APPLICATION = 6,
    DICE_LAYER_L7_RUNTIME     = 7
} DICELayerLevel;

typedef struct {
    uint8_t data[DICE_UDS_SIZE];
} DICEUniqueDeviceSecret;

typedef struct {
    uint8_t  data[DICE_CDI_SIZE];
    bool     valid;
} DICECompoundDeviceID;

typedef struct {
    uint8_t  code_hash[DICE_CODE_HASH_SIZE];
    uint8_t  config_hash[DICE_CONFIG_HASH_SIZE];
    uint8_t  authority_hash[DICE_AUTHORITY_HASH_SIZE];
    uint64_t version;
    DICELayerLevel level;
    uint8_t  label[DICE_LAYER_LABEL_SIZE];
} DICELayerInputs;

typedef struct {
    TPMKeyPublic device_id_pub;
    uint8_t      device_id_priv[AIK_KEY_SIZE];
    uint16_t     device_id_priv_size;
    TPMKeyPublic alias_key_pub;
    uint8_t      alias_key_priv[AIK_KEY_SIZE];
    uint16_t     alias_key_priv_size;
    DICECompoundDeviceID cdi;
    DICELayerLevel layer;
    bool         key_pair_generated;
} DICELayerState;

typedef struct {
    uint8_t       serialized_data[DICE_CERT_SIZE];
    uint16_t      size;
    DICELayerLevel issuer_layer;
    DICELayerLevel subject_layer;
    TPMKeyPublic  subject_pub_key;
    uint8_t       cdi_claim[DICE_CDI_SIZE];
    uint8_t       code_hash_claim[DICE_CODE_HASH_SIZE];
    uint8_t       config_hash_claim[DICE_CONFIG_HASH_SIZE];
    uint8_t       signature[DICE_CERT_SIZE / 2];
    uint16_t      signature_size;
} DICECertificate;

typedef struct {
    DICECertificate certs[DICE_MAX_LAYERS];
    uint8_t         cert_count;
    DICEUniqueDeviceSecret root_uds;
    bool            chain_verified;
} DICECertChain;

int32_t  dice_uds_derive(const uint8_t *physically_unclonable_data,
                          uint32_t pud_size,
                          DICEUniqueDeviceSecret *uds);

int32_t  dice_cdi_compute(const DICECompoundDeviceID *parent_cdi,
                           const DICELayerInputs *inputs,
                           DICECompoundDeviceID *cdi_out);

int32_t  dice_layer_derive_keys(DICELayerState *state,
                                 const DICECompoundDeviceID *cdi);

int32_t  dice_cert_generate(DICECertificate *cert,
                             const DICELayerState *issuer,
                             const DICELayerState *subject);

int32_t  dice_cert_verify(const DICECertificate *cert,
                           const TPMKeyPublic *issuer_pub_key,
                           bool *result);

int32_t  dice_chain_verify(const DICECertChain *chain, bool *result);

int32_t  dice_chain_build_layered_attestation(
    const DICEUniqueDeviceSecret *uds,
    const DICELayerInputs layers[],
    uint8_t layer_count,
    DICECertChain *chain);

int32_t  dice_derive_cdi_from_uds(const DICEUniqueDeviceSecret *uds,
                                   const DICELayerInputs *layer0,
                                   DICECompoundDeviceID *cdi_out);

int32_t  dice_export_device_id_public(const DICELayerState *state,
                                       TPMKeyPublic *pub_key);

int32_t  dice_export_alias_key_public(const DICELayerState *state,
                                       TPMKeyPublic *pub_key);

void     dice_layer_state_dump(const DICELayerState *state);
void     dice_cert_dump(const DICECertificate *cert);
void     dice_chain_dump(const DICECertChain *chain);
void     dice_layer_inputs_dump(const DICELayerInputs *inputs);

#endif
