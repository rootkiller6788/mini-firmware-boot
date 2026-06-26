#ifndef BOOT_SECURE_H
#define BOOT_SECURE_H

#include <stdbool.h>
#include <stdint.h>

/*
 * L8: Secure Boot — cryptographic verification of boot chain.
 *     Covers SHA-256 hashing, signature verification concepts,
 *     and measured boot (TPM PCR extension emulation).
 *
 * L4: SHA-256 standard (FIPS 180-4).
 *     TPM 2.0 PCR operations.
 */

#define SHA256_BLOCK_SIZE  64
#define SHA256_DIGEST_SIZE 32
#define SHA256_HASH_SIZE   32

#define BOOTSEC_MAX_PCR     24
#define BOOTSEC_PCR_SIZE    32
#define BOOTSEC_MAX_EVENTS  128

/* SHA-256 context */
typedef struct {
    uint32_t state[8];
    uint64_t count;
    uint8_t  buffer[SHA256_BLOCK_SIZE];
} SHA256_CTX;

/* Measured boot event (TPM-style) */
typedef struct {
    uint32_t pcr_index;
    uint8_t  digest[SHA256_DIGEST_SIZE];
    char     event_desc[128];
    uint32_t event_type;
} BootMeasureEvent;

/* PCR bank */
typedef struct {
    uint8_t  pcrs[BOOTSEC_MAX_PCR][BOOTSEC_PCR_SIZE];
    bool     initialized[BOOTSEC_MAX_PCR];
} PCRBank;

/* Boot measurement log */
typedef struct {
    BootMeasureEvent events[BOOTSEC_MAX_EVENTS];
    uint32_t         event_count;
    PCRBank          pcr_bank;
} BootMeasureLog;

/* Signature structure (simplified RSA-2048 style) */
typedef struct {
    uint8_t  sig_r[256];
    uint8_t  sig_s[256];
    uint32_t sig_len;
} BootSignature;

/* Certificate / key */
typedef struct {
    uint8_t  modulus[256];
    uint8_t  exponent[4];
    uint32_t key_size;
    char     subject[128];
    char     issuer[128];
    bool     trusted;
} BootCertificate;

/* ── API ────────────────────────────────────────────────── */
/* SHA-256 */
void     sha256_init(SHA256_CTX *ctx);
void     sha256_update(SHA256_CTX *ctx, const uint8_t *data, uint32_t len);
void     sha256_final(SHA256_CTX *ctx, uint8_t digest[SHA256_DIGEST_SIZE]);
void     sha256_hash(const uint8_t *data, uint32_t len,
                     uint8_t digest[SHA256_DIGEST_SIZE]);
bool     sha256_verify(const uint8_t *data, uint32_t len,
                       const uint8_t expected[SHA256_DIGEST_SIZE]);
void     sha256_print_digest(const uint8_t digest[SHA256_DIGEST_SIZE]);

/* PCR operations */
void     pcr_bank_init(PCRBank *bank);
bool     pcr_extend(PCRBank *bank, uint32_t pcr_index,
                    const uint8_t digest[SHA256_DIGEST_SIZE]);
bool     pcr_verify(const PCRBank *bank, uint32_t pcr_index,
                    const uint8_t expected[SHA256_DIGEST_SIZE]);
void     pcr_bank_print(const PCRBank *bank);

/* Measured boot */
void     boot_measure_init(BootMeasureLog *log);
bool     boot_measure_event(BootMeasureLog *log, uint32_t pcr_index,
                            const uint8_t *data, uint32_t data_len,
                            const char *desc, uint32_t event_type);
bool     boot_measure_verify(BootMeasureLog *log, uint32_t pcr_index,
                             const uint8_t expected[SHA256_DIGEST_SIZE]);
void     boot_measure_print(const BootMeasureLog *log);

/* Certificate & verification */
void     boot_cert_init(BootCertificate *cert);
bool     boot_cert_verify_signature(const BootCertificate *cert,
                                    const uint8_t *hash,
                                    const BootSignature *sig);
bool     boot_cert_check_chain(const BootCertificate *chain, uint32_t count);
void     boot_cert_print(const BootCertificate *cert);

/* File integrity */
bool     boot_verify_file(const char *path, const uint8_t expected_hash[SHA256_DIGEST_SIZE]);
bool     boot_verify_memory_range(const uint8_t *data, uint32_t len,
                                  const uint8_t expected[SHA256_DIGEST_SIZE]);

#endif