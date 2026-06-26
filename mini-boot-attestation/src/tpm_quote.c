#include "tpm_quote.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

static void simulate_sha256(const uint8_t *data, size_t len, uint8_t *out) {
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

static void simulate_rsa_sign(const uint8_t *hash, size_t hash_len,
                               const TPMKey *key, uint8_t *sig, uint16_t *sig_size) {
    uint8_t temp[256];
    memset(temp, 0, sizeof(temp));
    memcpy(temp, hash, hash_len < 256 ? hash_len : 256);
    size_t i;
    for (i = 0; i < key->modulus_size && i < 256; i++) {
        temp[i] ^= key->modulus[i];
    }
    temp[0] = 0x00;
    temp[1] = 0x01;
    memcpy(sig, temp, key->modulus_size > 256 ? 256 : key->modulus_size);
    *sig_size = key->modulus_size;
}

static bool simulate_rsa_verify(const uint8_t *hash, size_t hash_len,
                                 const TPMKey *key, const uint8_t *sig, uint16_t sig_size) {
    uint8_t expected[256];
    simulate_rsa_sign(hash, hash_len, key, expected, &sig_size);
    return memcmp(sig, expected, sig_size) == 0;
}

void tpm_hash_init(TPMHash *hash) {
    memset(hash->digest, 0, TPM_SHA256_DIGEST_SIZE);
    hash->digest[0] = 0xDA;
    hash->digest[1] = 0x39;
}

void tpm_hash_update(TPMHash *hash, const uint8_t *data, size_t len) {
    uint8_t temp[TPM_SHA256_DIGEST_SIZE];
    simulate_sha256(data, len, temp);
    size_t i;
    for (i = 0; i < TPM_SHA256_DIGEST_SIZE; i++) {
        hash->digest[i] ^= temp[i];
    }
}

void tpm_hash_final(TPMHash *hash) {
    hash->digest[31] ^= 0xA5;
}

void tpm_pcr_composite_init(TPMPcrComposite *comp) {
    memset(comp, 0, sizeof(*comp));
    comp->count = 0;
    comp->pcr_count = 0;
}

int32_t tpm_pcr_composite_add(TPMPcrComposite *comp,
                               uint8_t pcr_index, const TPMHash *value) {
    if (!comp || !value) return -1;
    if (pcr_index >= TPM_MAX_PCRS) return -2;
    if (comp->pcr_count >= TPM_MAX_PCRS) return -3;

    comp->pcr_selections[0].algorithm_id = TPM_ALG_SHA256;
    comp->pcr_selections[0].size_of_select = TPM_PCR_SELECT_SIZE;
    comp->pcr_selections[0].pcr_select[pcr_index / 8] |= (1 << (pcr_index % 8));

    memcpy(comp->pcr_digests[pcr_index].digest, value->digest, TPM_SHA256_DIGEST_SIZE);
    comp->pcr_count++;
    comp->count++;
    return 0;
}

int32_t tpm_pcr_composite_get(const TPMPcrComposite *comp,
                               uint8_t pcr_index, TPMHash *value) {
    if (!comp || !value) return -1;
    if (pcr_index >= TPM_MAX_PCRS) return -2;

    uint8_t byte_idx = pcr_index / 8;
    uint8_t bit_idx = pcr_index % 8;

    if (!(comp->pcr_selections[0].pcr_select[byte_idx] & (1 << bit_idx))) {
        return -3;
    }

    memcpy(value->digest, comp->pcr_digests[pcr_index].digest, TPM_SHA256_DIGEST_SIZE);
    return 0;
}

void tpm_pcr_composite_hash(const TPMPcrComposite *comp, TPMHash *out) {
    TPMHash temp;
    tpm_hash_init(&temp);

    size_t i;
    for (i = 0; i < TPM_MAX_PCRS; i++) {
        uint8_t byte_idx = (uint8_t)(i / 8);
        uint8_t bit_idx = (uint8_t)(i % 8);
        if (comp->pcr_selections[0].pcr_select[byte_idx] & (1 << bit_idx)) {
            tpm_hash_update(&temp, comp->pcr_digests[i].digest, TPM_SHA256_DIGEST_SIZE);
        }
    }
    tpm_hash_final(&temp);
    memcpy(out->digest, temp.digest, TPM_SHA256_DIGEST_SIZE);
}

int32_t tpm_quote_create(TPMQuote *quote,
                          const TPMPcrComposite *pcr_composite,
                          const uint8_t *nonce, uint32_t nonce_size,
                          const uint8_t *qualifying_data, uint32_t qdata_size,
                          uint64_t firmware_version) {
    if (!quote || !pcr_composite) return -1;

    memset(quote, 0, sizeof(*quote));

    quote->attest.magic = TPM_GENERATED_VALUE;
    quote->attest.type = TPM_ST_ATTEST_QUOTE;

    if (qualifying_data && qdata_size > 0) {
        uint32_t copy_size = qdata_size < TPM_SHA256_DIGEST_SIZE ? qdata_size : TPM_SHA256_DIGEST_SIZE;
        memcpy(quote->attest.qualified_signer, qualifying_data, copy_size);
    }

    if (nonce && nonce_size > 0) {
        uint32_t copy_size = nonce_size < TPM_EXTRA_DATA_SIZE ? nonce_size : TPM_EXTRA_DATA_SIZE;
        memcpy(quote->attest.extra_data, nonce, copy_size);
    }

    quote->attest.firmware_version = firmware_version;

    memset(quote->attest.clock_info, 0x00, TPM_CLOCK_INFO_SIZE);
    quote->attest.clock_info[0] = (uint8_t)((firmware_version >> 24) & 0xFF);
    quote->attest.clock_info[1] = (uint8_t)((firmware_version >> 16) & 0xFF);
    quote->attest.clock_info[2] = (uint8_t)((firmware_version >> 8) & 0xFF);
    quote->attest.clock_info[3] = (uint8_t)(firmware_version & 0xFF);
    quote->attest.clock_info[4] = 0x00;
    quote->attest.clock_info[5] = 0x00;
    quote->attest.clock_info[6] = 0x00;
    quote->attest.clock_info[7] = 0x00;

    memcpy(&quote->attest.pcr_select, &pcr_composite->pcr_selections[0], sizeof(TPMPcrSelection));

    tpm_pcr_composite_hash(pcr_composite, &quote->attest.pcr_digest);

    memcpy(&quote->pcr_composite, pcr_composite, sizeof(TPMPcrComposite));

    return 0;
}

int32_t tpm_quote_sign(TPMQuote *quote, const TPMKey *aik) {
    if (!quote || !aik) return -1;

    TPMHash attest_hash;
    tpm_hash_init(&attest_hash);
    tpm_hash_update(&attest_hash, (const uint8_t *)&quote->attest, sizeof(TPMAttest));
    tpm_hash_final(&attest_hash);

    simulate_rsa_sign(attest_hash.digest, TPM_SHA256_DIGEST_SIZE,
                      aik, quote->signature, &quote->signature_size);

    return 0;
}

int32_t tpm_quote_verify(const TPMQuote *quote,
                          const TPMKey *aik_pub,
                          const TPMPcrComposite *expected_pcr,
                          uint64_t expected_fw_version,
                          bool *result) {
    if (!quote || !aik_pub || !expected_pcr || !result) return -1;

    *result = false;

    if (quote->attest.magic != TPM_GENERATED_VALUE) {
        return -2;
    }
    if (quote->attest.type != TPM_ST_ATTEST_QUOTE) {
        return -3;
    }

    TPMHash attest_hash;
    tpm_hash_init(&attest_hash);
    tpm_hash_update(&attest_hash, (const uint8_t *)&quote->attest, sizeof(TPMAttest));
    tpm_hash_final(&attest_hash);

    bool sig_valid = simulate_rsa_verify(attest_hash.digest, TPM_SHA256_DIGEST_SIZE,
                                          aik_pub, quote->signature, quote->signature_size);
    if (!sig_valid) {
        *result = false;
        return 0;
    }

    if (quote->attest.firmware_version != expected_fw_version) {
        *result = false;
        return 0;
    }

    TPMHash composite_hash;
    tpm_pcr_composite_hash(&quote->pcr_composite, &composite_hash);

    TPMHash expected_hash;
    tpm_pcr_composite_hash(expected_pcr, &expected_hash);

    if (memcmp(composite_hash.digest, expected_hash.digest, TPM_SHA256_DIGEST_SIZE) != 0) {
        *result = false;
        return 0;
    }

    *result = true;
    return 0;
}

int32_t tpm_certify(const TPMKey *certifying_key,
                     const TPMAttest *attest,
                     uint8_t *signature_out, uint16_t *sig_size) {
    if (!certifying_key || !attest || !signature_out || !sig_size) return -1;

    TPMHash attest_hash;
    tpm_hash_init(&attest_hash);
    tpm_hash_update(&attest_hash, (const uint8_t *)attest, sizeof(TPMAttest));
    tpm_hash_final(&attest_hash);

    simulate_rsa_sign(attest_hash.digest, TPM_SHA256_DIGEST_SIZE,
                      certifying_key, signature_out, sig_size);

    return 0;
}

void tpm_quote_dump(const TPMQuote *quote) {
    if (!quote) {
        printf("TPMQuote: (null)\n");
        return;
    }
    printf("=== TPM Quote ===\n");
    printf("  Magic:          0x%08X\n", quote->attest.magic);
    printf("  Type:            0x%04X\n", quote->attest.type);
    printf("  Firmware Ver:    %llu\n", (unsigned long long)quote->attest.firmware_version);
    printf("  PCR Select:      [");
    int i;
    for (i = 0; i < TPM_PCR_SELECT_SIZE; i++) {
        printf("0x%02X%s", quote->attest.pcr_select.pcr_select[i],
               (i < TPM_PCR_SELECT_SIZE - 1) ? ", " : "");
    }
    printf("]\n");
    printf("  PCR Digest:      ");
    for (i = 0; i < 16; i++) {
        printf("%02X", quote->attest.pcr_digest.digest[i]);
    }
    printf("...\n");
    printf("  Signature Size:  %u\n", quote->signature_size);
    printf("  PCR Count:       %u\n", quote->pcr_composite.pcr_count);
    printf("==================\n");
}
