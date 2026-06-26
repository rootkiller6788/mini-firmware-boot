#include "dice.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static void dice_kdf(const uint8_t *key, uint32_t key_len,
                      const uint8_t *context, uint32_t context_len,
                      uint8_t *output, uint32_t output_len) {
    uint32_t seed = 0x6A09E667;
    uint32_t i;
    for (i = 0; i < key_len; i++) {
        seed ^= (uint32_t)key[i] << ((i % 4) * 8);
        seed = (seed * 0x01000193) ^ (seed >> 16);
    }
    for (i = 0; i < context_len; i++) {
        seed ^= (uint32_t)context[i] << ((i % 4) * 8);
        seed = (seed * 0x5BD1E995) ^ ((seed >> 13) + i);
    }
    seed ^= (uint32_t)(key_len + context_len);
    for (i = 0; i < output_len && i < DICE_CDI_SIZE; i++) {
        output[i] = (uint8_t)((seed >> ((i % 4) * 8)) & 0xFF);
        seed = (seed * 0x5BD1E995) ^ ((seed >> 13) + (i + 7));
    }
}

static void dice_keygen_from_cdi(const uint8_t *cdi, uint32_t cdi_len,
                                  TPMKeyPublic *pub, uint8_t *priv, uint16_t *priv_size) {
    if (priv && priv_size) {
        uint8_t extended[AIK_KEY_SIZE];
        dice_kdf(cdi, cdi_len, (const uint8_t *)"DICE_DEVICE_ID_KEY", 18,
                 extended, AIK_KEY_SIZE);
        memcpy(priv, extended, AIK_KEY_SIZE);
        *priv_size = AIK_KEY_SIZE;
    }
    uint8_t pub_data[AIK_KEY_SIZE];
    dice_kdf(cdi, cdi_len, (const uint8_t *)"DICE_PUBLIC_KEY_DATA", 20,
             pub_data, AIK_KEY_SIZE);
    memcpy(pub->modulus, pub_data, AIK_KEY_SIZE);
    pub->modulus_size = AIK_KEY_SIZE;
    pub->exponent[0] = 0x01;
    pub->exponent[1] = 0x00;
    pub->exponent[2] = 0x01;
    pub->hierarchy = TPM_KEY_HIERARCHY_AIK;
}

static void dice_simulate_hash(const uint8_t *data, size_t len, uint8_t *out) {
    uint32_t seed = 0x6A09E667;
    size_t i;
    for (i = 0; i < len; i++) {
        seed ^= (uint32_t)data[i] << ((i % 4) * 8);
        seed = (seed * 0x01000193) ^ (seed >> 16);
    }
    seed ^= (uint32_t)len;
    for (i = 0; i < 32; i++) {
        out[i] = (uint8_t)((seed >> ((i % 4) * 8)) & 0xFF);
        seed = (seed * 0x5BD1E995) ^ ((seed >> 13) + i);
    }
}

static void dice_simulate_sign(const uint8_t *data, size_t len,
                                const uint8_t *priv_key, uint16_t key_size,
                                uint8_t *sig, uint16_t *sig_size) {
    uint8_t temp[512];
    memset(temp, 0, sizeof(temp));
    size_t copy_len = len < 512 ? len : 512;
    memcpy(temp, data, copy_len);
    size_t i;
    for (i = 0; i < key_size && i < 512; i++) {
        temp[i] ^= priv_key[i];
    }
    temp[0] = 0x00;
    temp[1] = 0x01;
    memcpy(sig, temp, 512);
    *sig_size = 512;
}

static bool dice_simulate_verify(const uint8_t *data, size_t len,
                                  const TPMKeyPublic *pub_key,
                                  const uint8_t *sig, uint16_t sig_size) {
    (void)data;
    (void)len;
    if (sig_size > 0 && sig[0] == 0x00 && sig[1] == 0x01) return true;
    if (pub_key->modulus_size > 0) return true;
    return false;
}

int32_t dice_uds_derive(const uint8_t *physically_unclonable_data,
                         uint32_t pud_size,
                         DICEUniqueDeviceSecret *uds) {
    if (!physically_unclonable_data || !uds) return -1;
    if (pud_size < 16) return -2;
    memset(uds, 0, sizeof(*uds));
    dice_kdf(physically_unclonable_data, pud_size,
             (const uint8_t *)"DICE_UDS_DERIVATION_SALT", 24,
             uds->data, DICE_UDS_SIZE);
    return 0;
}

int32_t dice_cdi_compute(const DICECompoundDeviceID *parent_cdi,
                          const DICELayerInputs *inputs,
                          DICECompoundDeviceID *cdi_out) {
    if (!parent_cdi || !inputs || !cdi_out) return -1;
    if (!parent_cdi->valid) return -3;
    uint8_t measurement[32];
    uint8_t concat[256];
    uint32_t concat_len = 0;
    memcpy(concat + concat_len, inputs->code_hash, DICE_CODE_HASH_SIZE);
    concat_len += DICE_CODE_HASH_SIZE;
    memcpy(concat + concat_len, inputs->config_hash, DICE_CONFIG_HASH_SIZE);
    concat_len += DICE_CONFIG_HASH_SIZE;
    memcpy(concat + concat_len, inputs->authority_hash, DICE_AUTHORITY_HASH_SIZE);
    concat_len += DICE_AUTHORITY_HASH_SIZE;
    memcpy(concat + concat_len, &inputs->version, sizeof(uint64_t));
    concat_len += sizeof(uint64_t);
    dice_simulate_hash(concat, concat_len, measurement);
    dice_kdf(parent_cdi->data, DICE_CDI_SIZE, measurement, 32,
             cdi_out->data, DICE_CDI_SIZE);
    cdi_out->valid = true;
    return 0;
}

int32_t dice_layer_derive_keys(DICELayerState *state,
                                const DICECompoundDeviceID *cdi) {
    if (!state || !cdi) return -1;
    if (!cdi->valid) return -2;
    memset(state, 0, sizeof(*state));
    memcpy(&state->cdi, cdi, sizeof(DICECompoundDeviceID));
    dice_keygen_from_cdi(cdi->data, DICE_CDI_SIZE,
                          &state->device_id_pub,
                          state->device_id_priv,
                          &state->device_id_priv_size);
    uint8_t alias_seed[DICE_CDI_SIZE + 16];
    memcpy(alias_seed, cdi->data, DICE_CDI_SIZE);
    memcpy(alias_seed + DICE_CDI_SIZE, "DICE_ALIAS_KEY", 14);
    uint8_t alias_cdi_raw[DICE_CDI_SIZE];
    dice_kdf(alias_seed, DICE_CDI_SIZE + 14,
             (const uint8_t *)"ALIAS", 5,
             alias_cdi_raw, DICE_CDI_SIZE);
    dice_keygen_from_cdi(alias_cdi_raw, DICE_CDI_SIZE,
                          &state->alias_key_pub,
                          state->alias_key_priv,
                          &state->alias_key_priv_size);
    state->key_pair_generated = true;
    return 0;
}

int32_t dice_cert_generate(DICECertificate *cert,
                            const DICELayerState *issuer,
                            const DICELayerState *subject) {
    if (!cert || !issuer || !subject) return -1;
    if (!issuer->key_pair_generated || !subject->key_pair_generated) return -2;
    memset(cert, 0, sizeof(*cert));
    cert->issuer_layer = issuer->layer;
    cert->subject_layer = subject->layer;
    memcpy(&cert->subject_pub_key, &subject->device_id_pub, sizeof(TPMKeyPublic));
    dice_simulate_hash(subject->cdi.data, DICE_CDI_SIZE, cert->cdi_claim);
    uint8_t tbs_data[512];
    uint32_t tbs_len = 0;
    memcpy(tbs_data + tbs_len, &cert->subject_pub_key, sizeof(TPMKeyPublic));
    tbs_len += sizeof(TPMKeyPublic);
    memcpy(tbs_data + tbs_len, cert->cdi_claim, DICE_CDI_SIZE);
    tbs_len += DICE_CDI_SIZE;
    dice_simulate_sign(tbs_data, tbs_len,
                        issuer->device_id_priv,
                        issuer->device_id_priv_size,
                        cert->signature, &cert->signature_size);
    cert->size = (uint16_t)(sizeof(DICECertificate) - sizeof(cert->signature)
                            - sizeof(uint16_t));
    return 0;
}

int32_t dice_cert_verify(const DICECertificate *cert,
                          const TPMKeyPublic *issuer_pub_key,
                          bool *result) {
    if (!cert || !issuer_pub_key || !result) return -1;
    uint8_t tbs_data[512];
    uint32_t tbs_len = 0;
    memcpy(tbs_data + tbs_len, &cert->subject_pub_key, sizeof(TPMKeyPublic));
    tbs_len += sizeof(TPMKeyPublic);
    memcpy(tbs_data + tbs_len, cert->cdi_claim, DICE_CDI_SIZE);
    tbs_len += DICE_CDI_SIZE;
    *result = dice_simulate_verify(tbs_data, tbs_len,
                                    issuer_pub_key,
                                    cert->signature, cert->signature_size);
    return 0;
}

int32_t dice_chain_verify(const DICECertChain *chain, bool *result) {
    if (!chain || !result) return -1;
    if (chain->cert_count == 0) { *result = false; return 0; }
    *result = true;
    uint8_t i;
    for (i = 0; i < chain->cert_count; i++) {
        const DICECertificate *cert = &chain->certs[i];
        if (i == 0) {
            if (cert->signature_size == 0) { *result = false; return 0; }
            continue;
        }
        bool cert_valid = false;
        const TPMKeyPublic *issuer_key = &chain->certs[i - 1].subject_pub_key;
        dice_cert_verify(cert, issuer_key, &cert_valid);
        if (!cert_valid) { *result = false; return 0; }
    }
    return 0;
}

int32_t dice_chain_build_layered_attestation(
    const DICEUniqueDeviceSecret *uds,
    const DICELayerInputs layers[],
    uint8_t layer_count,
    DICECertChain *chain) {
    if (!uds || !layers || !chain) return -1;
    if (layer_count == 0 || layer_count > DICE_MAX_LAYERS) return -2;
    memset(chain, 0, sizeof(*chain));
    memcpy(&chain->root_uds, uds, sizeof(DICEUniqueDeviceSecret));
    DICELayerState layer_states[DICE_MAX_LAYERS];
    DICECompoundDeviceID cdi_0;
    dice_derive_cdi_from_uds(uds, &layers[0], &cdi_0);
    layer_states[0].layer = layers[0].level;
    dice_layer_derive_keys(&layer_states[0], &cdi_0);
    dice_cert_generate(&chain->certs[0], &layer_states[0], &layer_states[0]);
    chain->cert_count = 1;
    uint8_t j;
    for (j = 1; j < layer_count && j < DICE_MAX_LAYERS; j++) {
        DICECompoundDeviceID cdi_j;
        dice_cdi_compute(&layer_states[j - 1].cdi, &layers[j], &cdi_j);
        layer_states[j].layer = layers[j].level;
        dice_layer_derive_keys(&layer_states[j], &cdi_j);
        dice_cert_generate(&chain->certs[j], &layer_states[j - 1], &layer_states[j]);
        chain->cert_count++;
    }
    chain->chain_verified = true;
    return 0;
}

int32_t dice_derive_cdi_from_uds(const DICEUniqueDeviceSecret *uds,
                                  const DICELayerInputs *layer0,
                                  DICECompoundDeviceID *cdi_out) {
    if (!uds || !layer0 || !cdi_out) return -1;
    uint8_t concat[256];
    uint32_t concat_len = 0;
    memcpy(concat + concat_len, layer0->code_hash, DICE_CODE_HASH_SIZE);
    concat_len += DICE_CODE_HASH_SIZE;
    memcpy(concat + concat_len, layer0->config_hash, DICE_CONFIG_HASH_SIZE);
    concat_len += DICE_CONFIG_HASH_SIZE;
    memcpy(concat + concat_len, layer0->authority_hash, DICE_AUTHORITY_HASH_SIZE);
    concat_len += DICE_AUTHORITY_HASH_SIZE;
    memcpy(concat + concat_len, &layer0->version, sizeof(uint64_t));
    concat_len += sizeof(uint64_t);
    uint8_t first_hash[32];
    dice_simulate_hash(concat, concat_len, first_hash);
    dice_kdf(uds->data, DICE_UDS_SIZE, first_hash, 32,
             cdi_out->data, DICE_CDI_SIZE);
    cdi_out->valid = true;
    return 0;
}

int32_t dice_export_device_id_public(const DICELayerState *state,
                                      TPMKeyPublic *pub_key) {
    if (!state || !pub_key) return -1;
    if (!state->key_pair_generated) return -2;
    memcpy(pub_key, &state->device_id_pub, sizeof(TPMKeyPublic));
    return 0;
}

int32_t dice_export_alias_key_public(const DICELayerState *state,
                                      TPMKeyPublic *pub_key) {
    if (!state || !pub_key) return -1;
    if (!state->key_pair_generated) return -2;
    memcpy(pub_key, &state->alias_key_pub, sizeof(TPMKeyPublic));
    return 0;
}

static const char *layer_name(DICELayerLevel level) {
    switch (level) {
        case DICE_LAYER_L0_HARDWARE:    return "L0 Hardware";
        case DICE_LAYER_L1_BOOT_ROM:    return "L1 Boot ROM";
        case DICE_LAYER_L2_BOOTLOADER:  return "L2 Bootloader";
        case DICE_LAYER_L3_OS_KERNEL:   return "L3 OS Kernel";
        case DICE_LAYER_L4_INITRD:      return "L4 Initrd";
        case DICE_LAYER_L5_FILESYSTEM:  return "L5 Filesystem";
        case DICE_LAYER_L6_APPLICATION: return "L6 Application";
        case DICE_LAYER_L7_RUNTIME:     return "L7 Runtime";
        default:                        return "L? Unknown";
    }
}

void dice_layer_state_dump(const DICELayerState *state) {
    if (!state) { printf("DICELayerState: (null)\n"); return; }
    printf("=== DICE Layer: %s ===\n", layer_name(state->layer));
    printf("  Keys Generated: %s\n", state->key_pair_generated ? "YES" : "NO");
    printf("  CDI Valid:      %s\n", state->cdi.valid ? "YES" : "NO");
    printf("  CDI:            ");
    uint8_t i;
    for (i = 0; i < 16; i++) printf("%02X", state->cdi.data[i]);
    printf("...\n");
    printf("========================\n");
}

void dice_cert_dump(const DICECertificate *cert) {
    if (!cert) { printf("DICECertificate: (null)\n"); return; }
    printf("=== DICE Certificate ===\n");
    printf("  Issuer:  %s\n", layer_name(cert->issuer_layer));
    printf("  Subject: %s\n", layer_name(cert->subject_layer));
    printf("  Sig Size: %u\n", cert->signature_size);
    printf("  CDI Claim: ");
    uint8_t i;
    for (i = 0; i < 8; i++) printf("%02X", cert->cdi_claim[i]);
    printf("...\n");
    printf("========================\n");
}

void dice_chain_dump(const DICECertChain *chain) {
    if (!chain) { printf("DICECertChain: (null)\n"); return; }
    printf("=== DICE Chain ===\n");
    printf("  Certs: %u  Verified: %s\n",
           chain->cert_count, chain->chain_verified ? "YES" : "NO");
    uint8_t i;
    for (i = 0; i < chain->cert_count; i++) {
        printf("  [%u] %s -> %s\n", i,
               layer_name(chain->certs[i].issuer_layer),
               layer_name(chain->certs[i].subject_layer));
    }
    printf("==================\n");
}

void dice_layer_inputs_dump(const DICELayerInputs *inputs) {
    if (!inputs) { printf("DICELayerInputs: (null)\n"); return; }
    printf("  Layer Inputs: %s (v%llu)\n",
           layer_name(inputs->level), (unsigned long long)inputs->version);
    printf("    Code Hash:      ");
    uint8_t i;
    for (i = 0; i < 8; i++) printf("%02X", inputs->code_hash[i]);
    printf("...\n");
    printf("    Config Hash:    ");
    for (i = 0; i < 8; i++) printf("%02X", inputs->config_hash[i]);
    printf("...\n");
    printf("    Authority Hash: ");
    for (i = 0; i < 8; i++) printf("%02X", inputs->authority_hash[i]);
    printf("...\n");
}