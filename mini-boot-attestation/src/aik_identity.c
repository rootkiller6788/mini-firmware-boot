#include "aik_identity.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>

static uint32_t rng_seed = 0xDEADBEEF;

static void simulate_rng(uint8_t *buf, size_t len) {
    size_t i;
    for (i = 0; i < len; i++) {
        rng_seed = rng_seed * 1103515245 + 12345;
        buf[i] = (uint8_t)((rng_seed >> 16) & 0xFF);
    }
}

static void simulate_rsa_keygen(uint8_t *modulus, uint16_t *mod_size,
                                 uint8_t *priv_exp, uint8_t *pub_exp) {
    *mod_size = 256;
    simulate_rng(modulus, *mod_size);
    modulus[0] |= 0x80;
    modulus[255] |= 0x01;

    if (priv_exp) {
        simulate_rng(priv_exp, AIK_KEY_SIZE);
    }

    pub_exp[0] = 0x01;
    pub_exp[1] = 0x00;
    pub_exp[2] = 0x01;
}

static void simulate_rsa_encrypt(const uint8_t *plaintext, size_t pt_len,
                                  const TPMKeyPublic *pub_key,
                                  uint8_t *ciphertext, uint16_t *ct_size) {
    memset(ciphertext, 0, AIK_CREDENTIAL_SIZE);
    memcpy(ciphertext, plaintext, pt_len < AIK_CREDENTIAL_SIZE ? pt_len : AIK_CREDENTIAL_SIZE);

    size_t i;
    for (i = 0; i < pub_key->modulus_size && i < AIK_CREDENTIAL_SIZE; i++) {
        ciphertext[i] ^= pub_key->modulus[i] ^ 0xA5;
    }
    ciphertext[0] = 0x00;
    ciphertext[1] = 0x02;
    *ct_size = AIK_CREDENTIAL_SIZE;
}

static void simulate_rsa_decrypt(const uint8_t *ciphertext, size_t ct_len,
                                  const TPMKeyAIK *aik,
                                  uint8_t *plaintext, uint16_t *pt_size) {
    memset(plaintext, 0, AIK_CREDENTIAL_SIZE);

    memcpy(plaintext, ciphertext, ct_len < AIK_CREDENTIAL_SIZE ? ct_len : AIK_CREDENTIAL_SIZE);

    size_t i;
    for (i = 0; i < aik->aik_pub_modulus_size && i < AIK_CREDENTIAL_SIZE; i++) {
        plaintext[i] ^= aik->aik_pub_modulus[i] ^ 0xA5;
    }

    *pt_size = ct_len < AIK_CREDENTIAL_SIZE ? (uint16_t)ct_len : AIK_CREDENTIAL_SIZE;
}

static void simulate_hash(const uint8_t *data, size_t len, uint8_t *out) {
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

static void simulate_sign(const uint8_t *data, size_t len,
                           uint8_t *sig, uint16_t *sig_size) {
    (void)data;
    memset(sig, 0, AIK_CERT_SIZE / 2);
    simulate_rng(sig, len < (size_t)(AIK_CERT_SIZE / 2) ? len : (size_t)(AIK_CERT_SIZE / 2));
    *sig_size = AIK_CERT_SIZE / 2;
}

int32_t tpm_create_ek(TPMKeyPublic *ek_pub) {
    if (!ek_pub) return -1;

    memset(ek_pub, 0, sizeof(*ek_pub));
    ek_pub->hierarchy = TPM_KEY_HIERARCHY_EK;
    simulate_rsa_keygen(ek_pub->modulus, &ek_pub->modulus_size, NULL, ek_pub->exponent);

    return 0;
}

int32_t tpm_create_aik(TPMKeyAIK *aik,
                        const TPMKeyPublic *srk,
                        const TPMKeyPublic *ek_pub) {
    if (!aik || !srk || !ek_pub) return -1;

    memset(aik, 0, sizeof(*aik));
    aik->parent = TPM_KEY_HIERARCHY_SRK;

    simulate_rsa_keygen(aik->aik_pub_modulus, &aik->aik_pub_modulus_size,
                        aik->aik_priv_exponent, aik->aik_pub_exponent);

    memcpy(aik->aik_priv_modulus, aik->aik_pub_modulus, AIK_KEY_SIZE);

    uint8_t name_input[AIK_KEY_SIZE + 32];
    memcpy(name_input, aik->aik_pub_modulus, aik->aik_pub_modulus_size);
    memcpy(name_input + aik->aik_pub_modulus_size, ek_pub->modulus, 32);
    simulate_hash(name_input, aik->aik_pub_modulus_size + 32, (uint8_t *)aik->aik_name);
    memcpy(aik->aik_name, name_input, AIK_LABEL_SIZE > 64 ? 64 : AIK_LABEL_SIZE);

    return 0;
}

int32_t tpm_aik_certify(AIKCredential *credential,
                         const TPMKeyPublic *aik_pub,
                         const TPMKeyPublic *ek_pub,
                         const EKCertificate *ek_cert,
                         const uint8_t *privacy_ca_id) {
    if (!credential || !aik_pub || !ek_pub || !ek_cert || !privacy_ca_id) return -1;

    memset(credential, 0, sizeof(*credential));

    memcpy(credential->aik_pub_modulus, aik_pub->modulus, aik_pub->modulus_size);
    credential->aik_pub_modulus_size = aik_pub->modulus_size;
    memcpy(credential->aik_pub_exponent, aik_pub->exponent, 3);

    memcpy(credential->privacy_ca_id, privacy_ca_id, PRIVACY_CA_ID_SIZE);
    credential->issue_date = (uint32_t)time(NULL);
    credential->expiry_date = credential->issue_date + (365 * 24 * 3600);

    simulate_rng(credential->serial_number, 16);

    uint8_t name_seed[AIK_KEY_SIZE + PRIVACY_CA_ID_SIZE + 16];
    memcpy(name_seed, aik_pub->modulus, aik_pub->modulus_size);
    memcpy(name_seed + aik_pub->modulus_size, privacy_ca_id, PRIVACY_CA_ID_SIZE);
    memcpy(name_seed + aik_pub->modulus_size + PRIVACY_CA_ID_SIZE,
           credential->serial_number, 16);
    simulate_hash(name_seed, aik_pub->modulus_size + PRIVACY_CA_ID_SIZE + 16,
                  credential->aik_name);

    uint8_t cert_data[AIK_KEY_SIZE * 2 + AIK_LABEL_SIZE + 64];
    size_t cert_data_size = 0;
    memcpy(cert_data + cert_data_size, credential->aik_pub_modulus,
           credential->aik_pub_modulus_size);
    cert_data_size += credential->aik_pub_modulus_size;
    memcpy(cert_data + cert_data_size, credential->aik_name, AIK_LABEL_SIZE);
    cert_data_size += AIK_LABEL_SIZE;
    memcpy(cert_data + cert_data_size, credential->privacy_ca_id, PRIVACY_CA_ID_SIZE);
    cert_data_size += PRIVACY_CA_ID_SIZE;
    memcpy(cert_data + cert_data_size, &credential->issue_date, sizeof(uint32_t));
    cert_data_size += sizeof(uint32_t);
    memcpy(cert_data + cert_data_size, &credential->expiry_date, sizeof(uint32_t));
    cert_data_size += sizeof(uint32_t);

    simulate_sign(cert_data, cert_data_size,
                  credential->signature, &credential->signature_size);

    credential->active = true;

    return 0;
}

int32_t tpm_make_credential(AIKEncryptedCredential *enc_cred,
                             const AIKCredential *credential,
                             const TPMKeyPublic *ek_pub) {
    if (!enc_cred || !credential || !ek_pub) return -1;

    simulate_rsa_encrypt((const uint8_t *)credential, sizeof(AIKCredential),
                          ek_pub, enc_cred->data, &enc_cred->size);

    return 0;
}

int32_t tpm_activate_credential(AIKCredential *credential_out,
                                 const AIKEncryptedCredential *enc_cred,
                                 const TPMKeyAIK *aik) {
    if (!credential_out || !enc_cred || !aik) return -1;

    uint8_t plaintext[AIK_CREDENTIAL_SIZE];
    uint16_t pt_size;
    simulate_rsa_decrypt(enc_cred->data, enc_cred->size, aik, plaintext, &pt_size);

    if (pt_size >= sizeof(AIKCredential)) {
        memcpy(credential_out, plaintext, sizeof(AIKCredential));
    } else {
        memcpy(credential_out, plaintext, pt_size);
    }

    return 0;
}

int32_t tpm_verify_ek_certificate(const EKCertificate *cert,
                                   const TPMKeyPublic *ek_pub,
                                   bool *result) {
    if (!cert || !ek_pub || !result) return -1;

    uint8_t computed_hash[EK_PUB_HASH_SIZE];
    simulate_hash(ek_pub->modulus, ek_pub->modulus_size, computed_hash);

    *result = (memcmp(computed_hash, cert->ek_pub_hash, EK_PUB_HASH_SIZE) == 0);
    return 0;
}

int32_t tpm_verify_aik_credential(const AIKCredential *credential,
                                   const TPMKeyPublic *aik_pub,
                                   const uint8_t *privacy_ca_pub,
                                   bool *result) {
    if (!credential || !aik_pub || !privacy_ca_pub || !result) return -1;

    *result = credential->active;
    if (!*result) return 0;

    if (memcmp(credential->aik_pub_modulus, aik_pub->modulus,
               credential->aik_pub_modulus_size) != 0) {
        *result = false;
        return 0;
    }

    if (credential->expiry_date < (uint32_t)time(NULL)) {
        *result = false;
        return 0;
    }

    *result = true;
    return 0;
}

int32_t tpm_get_ek_pub_hash(const TPMKeyPublic *ek_pub,
                             uint8_t *hash_out) {
    if (!ek_pub || !hash_out) return -1;
    simulate_hash(ek_pub->modulus, ek_pub->modulus_size, hash_out);
    return 0;
}

void aik_credential_dump(const AIKCredential *credential) {
    if (!credential) {
        printf("AIKCredential: (null)\n");
        return;
    }
    printf("=== AIK Credential ===\n");
    printf("  Active:        %s\n", credential->active ? "YES" : "NO");
    printf("  AIK Name:      %.16s\n", credential->aik_name);
    printf("  Privacy CA ID: %.16s\n", credential->privacy_ca_id);
    printf("  Issued:        %u\n", credential->issue_date);
    printf("  Expires:       %u\n", credential->expiry_date);
    printf("  Serial:        ");
    int i;
    for (i = 0; i < 8; i++) printf("%02X", credential->serial_number[i]);
    printf("\n");
    printf("======================\n");
}
