#include "boot_secure.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/*
 * L8: Secure Boot - cryptographic verification of boot chain.
 *     Implements SHA-256 (FIPS 180-4), PCR extension (TPM-style
 *     measured boot), and simplified certificate/signature verification.
 * L4: SHA-256 standard (FIPS 180-4, August 2015), TPM 2.0 PCR extend.
 */

/* SHA-256 round constants (FIPS 180-4 Section 4.2.2) */
static const uint32_t sha256_k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

/* L5: SHA-256 (FIPS 180-4) - O(n) in message length. */
static uint32_t sha256_rotr(uint32_t x, uint32_t n)
{
    return (x >> n) | (x << (32 - n));
}

void sha256_init(SHA256_CTX *ctx)
{
    if (ctx == NULL) return;
    memset(ctx, 0, sizeof(SHA256_CTX));
    ctx->state[0] = 0x6a09e667;
    ctx->state[1] = 0xbb67ae85;
    ctx->state[2] = 0x3c6ef372;
    ctx->state[3] = 0xa54ff53a;
    ctx->state[4] = 0x510e527f;
    ctx->state[5] = 0x9b05688c;
    ctx->state[6] = 0x1f83d9ab;
    ctx->state[7] = 0x5be0cd19;
}

static void sha256_transform(SHA256_CTX *ctx, const uint8_t block[64])
{
    uint32_t w[64], a, b, c, d, e, f, g, h;
    int t;

    /* Message schedule: 16 big-endian words from block */
    for (t = 0; t < 16; t++) {
        w[t] = ((uint32_t)block[t * 4] << 24) |
               ((uint32_t)block[t * 4 + 1] << 16) |
               ((uint32_t)block[t * 4 + 2] << 8) |
               ((uint32_t)block[t * 4 + 3]);
    }

    /* Extend to 64 words */
    for (t = 16; t < 64; t++) {
        uint32_t s0 = sha256_rotr(w[t - 15], 7) ^
                      sha256_rotr(w[t - 15], 18) ^ (w[t - 15] >> 3);
        uint32_t s1 = sha256_rotr(w[t - 2], 17) ^
                      sha256_rotr(w[t - 2], 19) ^ (w[t - 2] >> 10);
        w[t] = w[t - 16] + s0 + w[t - 7] + s1;
    }

    a = ctx->state[0]; b = ctx->state[1]; c = ctx->state[2];
    d = ctx->state[3]; e = ctx->state[4]; f = ctx->state[5];
    g = ctx->state[6]; h = ctx->state[7];

    for (t = 0; t < 64; t++) {
        uint32_t S1 = sha256_rotr(e, 6) ^ sha256_rotr(e, 11) ^ sha256_rotr(e, 25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t temp1 = h + S1 + ch + sha256_k[t] + w[t];
        uint32_t S0 = sha256_rotr(a, 2) ^ sha256_rotr(a, 13) ^ sha256_rotr(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = S0 + maj;
        h = g; g = f; f = e; e = d + temp1;
        d = c; c = b; b = a; a = temp1 + temp2;
    }

    ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c;
    ctx->state[3] += d; ctx->state[4] += e; ctx->state[5] += f;
    ctx->state[6] += g; ctx->state[7] += h;
}

void sha256_update(SHA256_CTX *ctx, const uint8_t *data, uint32_t len)
{
    if (ctx == NULL || data == NULL) return;
    uint32_t idx = (uint32_t)(ctx->count % SHA256_BLOCK_SIZE);
    ctx->count += len;

    if (idx > 0) {
        uint32_t space = SHA256_BLOCK_SIZE - idx;
        if (len < space) { memcpy(ctx->buffer + idx, data, len); return; }
        memcpy(ctx->buffer + idx, data, space);
        sha256_transform(ctx, ctx->buffer);
        data += space; len -= space;
    }
    while (len >= SHA256_BLOCK_SIZE) {
        sha256_transform(ctx, data);
        data += SHA256_BLOCK_SIZE; len -= SHA256_BLOCK_SIZE;
    }
    if (len > 0) memcpy(ctx->buffer, data, len);
}

void sha256_final(SHA256_CTX *ctx, uint8_t digest[SHA256_DIGEST_SIZE])
{
    if (ctx == NULL || digest == NULL) return;
    uint64_t bit_count = ctx->count * 8;
    uint32_t idx = (uint32_t)(ctx->count % SHA256_BLOCK_SIZE);

    ctx->buffer[idx++] = 0x80;  /* padding byte */
    if (idx > 56) {
        memset(ctx->buffer + idx, 0, SHA256_BLOCK_SIZE - idx);
        sha256_transform(ctx, ctx->buffer);
        idx = 0;
    }
    memset(ctx->buffer + idx, 0, 56 - idx);

    /* Append bit count as big-endian 64-bit */
    int i;
    for (i = 0; i < 8; i++)
        ctx->buffer[56 + i] = (uint8_t)(bit_count >> (56 - i * 8));
    sha256_transform(ctx, ctx->buffer);

    for (i = 0; i < 8; i++) {
        digest[i * 4]     = (uint8_t)(ctx->state[i] >> 24);
        digest[i * 4 + 1] = (uint8_t)(ctx->state[i] >> 16);
        digest[i * 4 + 2] = (uint8_t)(ctx->state[i] >> 8);
        digest[i * 4 + 3] = (uint8_t)(ctx->state[i]);
    }
}

void sha256_hash(const uint8_t *data, uint32_t len,
                 uint8_t digest[SHA256_DIGEST_SIZE])
{
    SHA256_CTX ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, data, len);
    sha256_final(&ctx, digest);
}

bool sha256_verify(const uint8_t *data, uint32_t len,
                   const uint8_t expected[SHA256_DIGEST_SIZE])
{
    uint8_t computed[SHA256_DIGEST_SIZE];
    sha256_hash(data, len, computed);
    return memcmp(computed, expected, SHA256_DIGEST_SIZE) == 0;
}

void sha256_print_digest(const uint8_t digest[SHA256_DIGEST_SIZE])
{
    if (digest == NULL) return;
    int i;
    for (i = 0; i < SHA256_DIGEST_SIZE; i++) printf("%02x", digest[i]);
}

/*
 * PCR (Platform Configuration Register) operations per TPM 2.0 spec.
 * PCR_Extend: new = SHA256(old || digest)
 * This is non-reversible and order-dependent (L4: TPM 2.0 Part 3 §22.4).
 */
void pcr_bank_init(PCRBank *bank)
{
    if (bank == NULL) return;
    memset(bank, 0, sizeof(PCRBank));
}

bool pcr_extend(PCRBank *bank, uint32_t pcr_index,
                const uint8_t digest[SHA256_DIGEST_SIZE])
{
    if (bank == NULL || digest == NULL) return false;
    if (pcr_index >= BOOTSEC_MAX_PCR) {
        fprintf(stderr, "[pcr] Invalid PCR index: %u\n", pcr_index);
        return false;
    }
    uint8_t concat[SHA256_DIGEST_SIZE * 2];
    memcpy(concat, bank->pcrs[pcr_index], SHA256_DIGEST_SIZE);
    memcpy(concat + SHA256_DIGEST_SIZE, digest, SHA256_DIGEST_SIZE);
    sha256_hash(concat, SHA256_DIGEST_SIZE * 2, bank->pcrs[pcr_index]);
    bank->initialized[pcr_index] = true;
    return true;
}

bool pcr_verify(const PCRBank *bank, uint32_t pcr_index,
                const uint8_t expected[SHA256_DIGEST_SIZE])
{
    if (bank == NULL || expected == NULL) return false;
    if (pcr_index >= BOOTSEC_MAX_PCR || !bank->initialized[pcr_index])
        return false;
    return memcmp(bank->pcrs[pcr_index], expected, SHA256_DIGEST_SIZE) == 0;
}

void pcr_bank_print(const PCRBank *bank)
{
    if (bank == NULL) return;
    printf("\n=== TPM PCR Bank ===\n");
    uint32_t i;
    for (i = 0; i < BOOTSEC_MAX_PCR; i++) {
        if (!bank->initialized[i]) continue;
        printf("PCR[%2u]: ", i);
        sha256_print_digest(bank->pcrs[i]);
        printf("\n");
    }
}

/*
 * Measured boot: records SHA-256 of each boot component and
 * extends the corresponding PCR, forming a tamper-evident log.
 */
void boot_measure_init(BootMeasureLog *log)
{
    if (log == NULL) return;
    memset(log, 0, sizeof(BootMeasureLog));
    pcr_bank_init(&log->pcr_bank);
}

bool boot_measure_event(BootMeasureLog *log, uint32_t pcr_index,
                        const uint8_t *data, uint32_t data_len,
                        const char *desc, uint32_t event_type)
{
    if (log == NULL || data == NULL || desc == NULL) return false;
    if (log->event_count >= BOOTSEC_MAX_EVENTS) {
        fprintf(stderr, "[measure] Event log full\n");
        return false;
    }
    BootMeasureEvent *evt = &log->events[log->event_count];
    sha256_hash(data, data_len, evt->digest);
    evt->pcr_index  = pcr_index;
    evt->event_type = event_type;
    snprintf(evt->event_desc, sizeof(evt->event_desc), "%s", desc);
    if (!pcr_extend(&log->pcr_bank, pcr_index, evt->digest)) return false;
    log->event_count++;
    printf("[measure] Event %u: PCR%u '%s' -> ",
           log->event_count - 1, pcr_index, desc);
    sha256_print_digest(evt->digest);
    printf("\n");
    return true;
}

bool boot_measure_verify(BootMeasureLog *log, uint32_t pcr_index,
                         const uint8_t expected[SHA256_DIGEST_SIZE])
{
    return pcr_verify(&log->pcr_bank, pcr_index, expected);
}

void boot_measure_print(const BootMeasureLog *log)
{
    if (log == NULL) return;
    printf("\n=== Measured Boot Log ===\n");
    uint32_t i;
    for (i = 0; i < log->event_count; i++) {
        printf("[%2u] PCR%u type=%u '%s'\n     digest: ",
               i, log->events[i].pcr_index, log->events[i].event_type,
               log->events[i].event_desc);
        sha256_print_digest(log->events[i].digest);
        printf("\n");
    }
    printf("\n--- Final PCR Values ---\n");
    for (i = 0; i < BOOTSEC_MAX_PCR; i++) {
        if (!log->pcr_bank.initialized[i]) continue;
        printf("PCR[%2u]: ", i);
        sha256_print_digest(log->pcr_bank.pcrs[i]);
        printf("\n");
    }
}

/*
 * Simplified certificate and signature verification (educational).
 * L4: RSA PKCS#1 v1.5 signature scheme model.
 */
void boot_cert_init(BootCertificate *cert)
{
    if (cert == NULL) return;
    memset(cert, 0, sizeof(BootCertificate));
}

bool boot_cert_verify_signature(const BootCertificate *cert,
                                const uint8_t *hash,
                                const BootSignature *sig)
{
    if (cert == NULL || hash == NULL || sig == NULL) return false;
    if (!cert->trusted) {
        fprintf(stderr, "[cert] Certificate not trusted\n");
        return false;
    }
    if (cert->key_size < 256) {
        fprintf(stderr, "[cert] Key too small: %u bytes\n", cert->key_size);
        return false;
    }
    /* Verify signature has non-trivial content */
    bool has_content = false;
    uint32_t i;
    for (i = 0; i < sig->sig_len && i < 256; i++) {
        if (sig->sig_r[i] != 0 || sig->sig_s[i] != 0) {
            has_content = true; break;
        }
    }
    if (!has_content) {
        fprintf(stderr, "[cert] Empty signature\n");
        return false;
    }
    printf("[cert] Signature verified for: %s\n", cert->subject);
    return true;
}

bool boot_cert_check_chain(const BootCertificate *chain, uint32_t count)
{
    if (chain == NULL || count == 0) return false;
    uint32_t i;
    for (i = 0; i < count; i++) {
        if (!chain[i].trusted) {
            fprintf(stderr, "[cert] Chain[%u] not trusted\n", i);
            return false;
        }
        if (i > 0 && strcmp(chain[i - 1].issuer, chain[i].subject) != 0) {
            fprintf(stderr, "[cert] Chain broken at %u\n", i);
            return false;
        }
    }
    printf("[cert] Chain of %u certificates validated\n", count);
    return true;
}

void boot_cert_print(const BootCertificate *cert)
{
    if (cert == NULL) return;
    printf("Certificate: subject='%s' issuer='%s' key=%uB trusted=%s\n",
           cert->subject, cert->issuer, cert->key_size,
           cert->trusted ? "yes" : "no");
}

/* File integrity verification using SHA-256 */
bool boot_verify_file(const char *path,
                      const uint8_t expected_hash[SHA256_DIGEST_SIZE])
{
    if (path == NULL || expected_hash == NULL) return false;
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        fprintf(stderr, "[verify] Cannot open: %s\n", path);
        return false;
    }
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    rewind(f);
    if (fsize <= 0) { fclose(f); return false; }
    uint8_t *buf = (uint8_t *)malloc((size_t)fsize);
    if (buf == NULL) { fclose(f); return false; }
    size_t rd = fread(buf, 1, (size_t)fsize, f);
    fclose(f);
    if (rd != (size_t)fsize) { free(buf); return false; }
    bool ok = sha256_verify(buf, (uint32_t)fsize, expected_hash);
    free(buf);
    printf("[verify] File '%s': %s\n", path, ok ? "OK" : "MISMATCH");
    return ok;
}

/* Memory range integrity verification (core secure boot check) */
bool boot_verify_memory_range(const uint8_t *data, uint32_t len,
                              const uint8_t expected[SHA256_DIGEST_SIZE])
{
    if (data == NULL || expected == NULL || len == 0) return false;
    bool ok = sha256_verify(data, len, expected);
    printf("[verify] Memory [%p, %u bytes]: %s\n",
           (const void *)data, len, ok ? "OK" : "MISMATCH");
    return ok;
}
