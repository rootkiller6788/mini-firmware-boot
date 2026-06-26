#ifndef SIGNATURE_VERIFY_H
#define SIGNATURE_VERIFY_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define RSA_MAX_MODULUS_WORDS    64
#define RSA_MAX_MODULUS_BYTES    256
#define RSA_MAX_EXPONENT_BYTES   256
#define SHA256_HASH_SIZE         32
#define X509_MAX_ISSUER_LEN      128
#define X509_MAX_SUBJECT_LEN     128
#define X509_MAX_CERT_CHAIN      8
#define SIGBIG_BITS_PER_WORD     32
#define SIGBIG_MAX_WORDS         64

typedef struct {
    uint32_t words[SIGBIG_MAX_WORDS];
    uint32_t num_words;
} BigInt;

typedef struct {
    uint8_t  modulus[RSA_MAX_MODULUS_BYTES];
    uint8_t  exponent[RSA_MAX_EXPONENT_BYTES];
    uint32_t mod_len;
    uint32_t exp_len;
} RSAKey;

typedef struct {
    uint8_t  issuer[X509_MAX_ISSUER_LEN];
    uint8_t  subject[X509_MAX_SUBJECT_LEN];
    RSAKey   public_key;
    uint8_t  signature[RSA_MAX_MODULUS_BYTES];
    uint32_t sig_len;
    uint64_t not_before;
    uint64_t not_after;
    uint32_t serial_number;
    bool     is_ca;
    bool     parsed;
} X509Cert;

typedef struct {
    X509Cert certs[X509_MAX_CERT_CHAIN];
    uint32_t count;
} X509Chain;

/* SHA-256 */
void sha256_init(void *ctx);
void sha256_update(void *ctx, const uint8_t *data, uint32_t len);
void sha256_final(void *ctx, uint8_t *digest);
void sha256_hash(const uint8_t *data, uint32_t len, uint8_t *digest);

/* Big integer helpers */
void bigint_init(BigInt *n);
void bigint_from_bytes(BigInt *n, const uint8_t *bytes, uint32_t len);
void bigint_to_bytes(const BigInt *n, uint8_t *bytes, uint32_t len);
void bigint_mod_exp(BigInt *result, const BigInt *base,
                    const BigInt *exp, const BigInt *mod);
bool bigint_is_zero(const BigInt *n);
bool bigint_equals(const BigInt *a, const BigInt *b);
void bigint_mul(BigInt *result, const BigInt *a, const BigInt *b);
void bigint_mod(BigInt *result, const BigInt *a, const BigInt *b);
int  bigint_compare(const BigInt *a, const BigInt *b);

/* RSA */
bool rsa_sha256_sign(const RSAKey *priv, const uint8_t *hash,
                     uint8_t *sig, uint32_t *sig_len);
bool rsa_sha256_verify(const RSAKey *pub, const uint8_t *hash,
                       const uint8_t *sig, uint32_t sig_len);
void rsa_generate_simple_keypair(RSAKey *pub, RSAKey *priv, uint32_t keybits);

/* X.509 */
bool x509_parse_cert(X509Cert *cert, const uint8_t *der_data, uint32_t der_len);
bool x509_verify_chain(const X509Chain *chain, const RSAKey *root_key);
bool x509_is_cert_valid(const X509Cert *cert, uint64_t current_time);

/* Authenticode / PE signature */
bool sig_verify_efi_image(const uint8_t *pe_image, uint32_t image_size,
                          const RSAKey *trusted_key);
bool sig_extract_auth_data(const uint8_t *pe_image, uint32_t image_size,
                           uint8_t *sig_data, uint32_t *sig_len);

/* Utility */
void sig_hexdump(const uint8_t *data, uint32_t len, char *out, uint32_t out_max);

#endif /* SIGNATURE_VERIFY_H */
