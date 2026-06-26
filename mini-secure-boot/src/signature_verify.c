#include "signature_verify.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ─── SHA-256 implementation ─────────────────────────────────────── */
#define SHA256_BLOCK_SIZE 64

typedef struct {
    uint32_t state[8];
    uint64_t count;
    uint8_t  buffer[SHA256_BLOCK_SIZE];
} SHA256_CTX;

static const uint32_t k[64] = {
    0x428A2F98,0x71374491,0xB5C0FBCF,0xE9B5DBA5,0x3956C25B,0x59F111F1,0x923F82A4,0xAB1C5ED5,
    0xD807AA98,0x12835B01,0x243185BE,0x550C7DC3,0x72BE5D74,0x80DEB1FE,0x9BDC06A7,0xC19BF174,
    0xE49B69C1,0xEFBE4786,0x0FC19DC6,0x240CA1CC,0x2DE92C6F,0x4A7484AA,0x5CB0A9DC,0x76F988DA,
    0x983E5152,0xA831C66D,0xB00327C8,0xBF597FC7,0xC6E00BF3,0xD5A79147,0x06CA6351,0x14292967,
    0x27B70A85,0x2E1B2138,0x4D2C6DFC,0x53380D13,0x650A7354,0x766A0ABB,0x81C2C92E,0x92722C85,
    0xA2BFE8A1,0xA81A664B,0xC24B8B70,0xC76C51A3,0xD192E819,0xD6990624,0xF40E3585,0x106AA070,
    0x19A4C116,0x1E376C08,0x2748774C,0x34B0BCB5,0x391C0CB3,0x4ED8AA4A,0x5B9CCA4F,0x682E6FF3,
    0x748F82EE,0x78A5636F,0x84C87814,0x8CC70208,0x90BEFFFA,0xA4506CEB,0xBEF9A3F7,0xC67178F2
};

static uint32_t rotr32(uint32_t x, uint32_t n) { return (x >> n) | (x << (32 - n)); }
static uint32_t ch32(uint32_t x, uint32_t y, uint32_t z) { return (x & y) ^ (~x & z); }
static uint32_t maj32(uint32_t x, uint32_t y, uint32_t z) { return (x & y) ^ (x & z) ^ (y & z); }
static uint32_t bsig0(uint32_t x) { return rotr32(x, 2) ^ rotr32(x, 13) ^ rotr32(x, 22); }
static uint32_t bsig1(uint32_t x) { return rotr32(x, 6) ^ rotr32(x, 11) ^ rotr32(x, 25); }
static uint32_t ssig0(uint32_t x) { return rotr32(x, 7) ^ rotr32(x, 18) ^ (x >> 3); }
static uint32_t ssig1(uint32_t x) { return rotr32(x, 17) ^ rotr32(x, 19) ^ (x >> 10); }

static void sha256_transform(SHA256_CTX *ctx, const uint8_t *data)
{
    uint32_t w[64];
    uint32_t a, b, c, d, e, f, g, h, t1, t2;
    for (int i = 0; i < 16; i++) {
        w[i] = ((uint32_t)data[i * 4] << 24) | ((uint32_t)data[i * 4 + 1] << 16) |
               ((uint32_t)data[i * 4 + 2] << 8) | (uint32_t)data[i * 4 + 3];
    }
    for (int i = 16; i < 64; i++) {
        w[i] = ssig1(w[i - 2]) + w[i - 7] + ssig0(w[i - 15]) + w[i - 16];
    }
    a = ctx->state[0]; b = ctx->state[1]; c = ctx->state[2]; d = ctx->state[3];
    e = ctx->state[4]; f = ctx->state[5]; g = ctx->state[6]; h = ctx->state[7];
    for (int i = 0; i < 64; i++) {
        t1 = h + bsig1(e) + ch32(e, f, g) + k[i] + w[i];
        t2 = bsig0(a) + maj32(a, b, c);
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }
    ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c; ctx->state[3] += d;
    ctx->state[4] += e; ctx->state[5] += f; ctx->state[6] += g; ctx->state[7] += h;
}

void sha256_init(void *c)
{
    SHA256_CTX *ctx = (SHA256_CTX *)c;
    ctx->state[0] = 0x6A09E667; ctx->state[1] = 0xBB67AE85;
    ctx->state[2] = 0x3C6EF372; ctx->state[3] = 0xA54FF53A;
    ctx->state[4] = 0x510E527F; ctx->state[5] = 0x9B05688C;
    ctx->state[6] = 0x1F83D9AB; ctx->state[7] = 0x5BE0CD19;
    ctx->count = 0;
}

void sha256_update(void *c, const uint8_t *data, uint32_t len)
{
    SHA256_CTX *ctx = (SHA256_CTX *)c;
    uint32_t idx = (uint32_t)(ctx->count % SHA256_BLOCK_SIZE);
    ctx->count += len;
    for (uint32_t i = 0; i < len; i++) {
        ctx->buffer[idx++] = data[i];
        if (idx == SHA256_BLOCK_SIZE) {
            sha256_transform(ctx, ctx->buffer);
            idx = 0;
        }
    }
}

void sha256_final(void *c, uint8_t *digest)
{
    SHA256_CTX *ctx = (SHA256_CTX *)c;
    uint32_t idx = (uint32_t)(ctx->count % SHA256_BLOCK_SIZE);
    uint64_t total_bits = ctx->count * 8;
    ctx->buffer[idx++] = 0x80;
    if (idx > 56) {
        while (idx < SHA256_BLOCK_SIZE) ctx->buffer[idx++] = 0;
        sha256_transform(ctx, ctx->buffer);
        idx = 0;
    }
    while (idx < 56) ctx->buffer[idx++] = 0;
    for (int i = 7; i >= 0; i--)
        ctx->buffer[idx++] = (uint8_t)(total_bits >> (i * 8));
    sha256_transform(ctx, ctx->buffer);
    for (int i = 0; i < 8; i++) {
        digest[i * 4 + 0] = (uint8_t)(ctx->state[i] >> 24);
        digest[i * 4 + 1] = (uint8_t)(ctx->state[i] >> 16);
        digest[i * 4 + 2] = (uint8_t)(ctx->state[i] >> 8);
        digest[i * 4 + 3] = (uint8_t)(ctx->state[i]);
    }
}

void sha256_hash(const uint8_t *data, uint32_t len, uint8_t *digest)
{
    SHA256_CTX ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, data, len);
    sha256_final(&ctx, digest);
}

/* ─── Big integer ─────────────────────────────────────────────────── */
void bigint_init(BigInt *n) { memset(n, 0, sizeof(BigInt)); }

void bigint_from_bytes(BigInt *n, const uint8_t *bytes, uint32_t len)
{
    bigint_init(n);
    if (len > SIGBIG_MAX_WORDS * 4) len = SIGBIG_MAX_WORDS * 4;
    n->num_words = (len + 3) / 4;
    for (uint32_t i = 0; i < len; i++) {
        n->words[i / 4] |= ((uint32_t)bytes[len - 1 - i]) << ((i % 4) * 8);
    }
    while (n->num_words > 1 && n->words[n->num_words - 1] == 0) n->num_words--;
}

void bigint_to_bytes(const BigInt *n, uint8_t *bytes, uint32_t len)
{
    memset(bytes, 0, len);
    for (uint32_t i = 0; i < n->num_words && i * 4 < len; i++) {
        for (int j = 0; j < 4 && i * 4 + j < len; j++) {
            bytes[len - 1 - (i * 4 + j)] = (uint8_t)(n->words[i] >> (j * 8));
        }
    }
}

bool bigint_is_zero(const BigInt *n) { return n->num_words == 0 || (n->num_words == 1 && n->words[0] == 0); }

bool bigint_equals(const BigInt *a, const BigInt *b)
{
    if (a->num_words != b->num_words) return false;
    for (uint32_t i = 0; i < a->num_words; i++)
        if (a->words[i] != b->words[i]) return false;
    return true;
}

int bigint_compare(const BigInt *a, const BigInt *b)
{
    if (a->num_words > b->num_words) return 1;
    if (a->num_words < b->num_words) return -1;
    for (int i = (int)a->num_words - 1; i >= 0; i--) {
        if (a->words[i] > b->words[i]) return 1;
        if (a->words[i] < b->words[i]) return -1;
    }
    return 0;
}

void bigint_add(BigInt *r, const BigInt *a, const BigInt *b)
{
    uint64_t carry = 0;
    uint32_t max_w = a->num_words > b->num_words ? a->num_words : b->num_words;
    if (max_w > SIGBIG_MAX_WORDS) max_w = SIGBIG_MAX_WORDS;
    for (uint32_t i = 0; i < max_w; i++) {
        uint64_t sum = carry;
        if (i < a->num_words) sum += a->words[i];
        if (i < b->num_words) sum += b->words[i];
        r->words[i] = (uint32_t)sum;
        carry = sum >> 32;
    }
    r->num_words = max_w;
    if (carry && max_w < SIGBIG_MAX_WORDS) r->words[r->num_words++] = (uint32_t)carry;
}

void bigint_sub(BigInt *r, const BigInt *a, const BigInt *b)
{
    int64_t borrow = 0;
    bigint_init(r);
    r->num_words = a->num_words;
    for (uint32_t i = 0; i < a->num_words; i++) {
        int64_t diff = (int64_t)a->words[i] - borrow;
        if (i < b->num_words) diff -= (int64_t)b->words[i];
        if (diff < 0) {
            diff += 0x100000000LL;
            borrow = 1;
        } else {
            borrow = 0;
        }
        r->words[i] = (uint32_t)diff;
    }
    while (r->num_words > 1 && r->words[r->num_words - 1] == 0) r->num_words--;
}

void bigint_mul(BigInt *result, const BigInt *a, const BigInt *b)
{
    uint64_t prod[SIGBIG_MAX_WORDS * 2];
    memset(prod, 0, sizeof(prod));
    for (uint32_t i = 0; i < a->num_words; i++) {
        uint64_t carry = 0;
        for (uint32_t j = 0; j < b->num_words; j++) {
            uint64_t p = (uint64_t)a->words[i] * b->words[j] + prod[i + j] + carry;
            prod[i + j] = p & 0xFFFFFFFFULL;
            carry = p >> 32;
        }
        prod[i + b->num_words] = carry;
    }
    uint32_t total = a->num_words + b->num_words;
    if (total > SIGBIG_MAX_WORDS) total = SIGBIG_MAX_WORDS;
    for (uint32_t i = 0; i < total; i++) result->words[i] = (uint32_t)prod[i];
    result->num_words = total;
    while (result->num_words > 1 && result->words[result->num_words - 1] == 0) result->num_words--;
}

static void bigint_divmod(BigInt *q, BigInt *r, const BigInt *a, const BigInt *b)
{
    bigint_init(q); bigint_init(r);
    if (bigint_is_zero(b)) return;
    if (bigint_compare(a, b) < 0) { *r = *a; return; }
    for (int i = (int)a->num_words - 1; i >= 0; i--) {
        for (int j = 31; j >= 0; j--) {
            bigint_add(r, r, r);
            r->words[0] |= (a->words[i] >> j) & 1;
            if (r->num_words == 0 && r->words[0] > 0) r->num_words = 1;
            q->words[i / 32] <<= 1;
            if (bigint_compare(r, b) >= 0) {
                bigint_sub(r, r, b);
                q->words[i / 32] |= 1;
            }
        }
    }
    q->num_words = a->num_words;
    while (q->num_words > 1 && q->words[q->num_words - 1] == 0) q->num_words--;
    while (r->num_words > 1 && r->words[r->num_words - 1] == 0) r->num_words--;
}

void bigint_mod(BigInt *result, const BigInt *a, const BigInt *b)
{
    BigInt q;
    bigint_divmod(&q, result, a, b);
}

void bigint_mod_exp(BigInt *result, const BigInt *base, const BigInt *exp, const BigInt *mod)
{
    BigInt r, b, e, m, tmp;
    bigint_init(&r);
    r.words[0] = 1; r.num_words = 1;
    b = *base; e = *exp; m = *mod;
    bigint_mod(&b, &b, &m);
    while (!bigint_is_zero(&e)) {
        if (e.words[0] & 1) {
            bigint_mul(&tmp, &r, &b);
            bigint_mod(&r, &tmp, &m);
        }
        bigint_mul(&tmp, &b, &b);
        bigint_mod(&b, &tmp, &m);
        for (int i = (int)e.num_words - 1; i >= 0; i--) {
            e.words[i] >>= 1;
            if (i > 0 && (e.words[i - 1] & 1)) e.words[i] |= 0x80000000;
        }
        while (e.num_words > 1 && e.words[e.num_words - 1] == 0) e.num_words--;
    }
    *result = r;
}

/* ─── RSA ─────────────────────────────────────────────────────────── */
bool rsa_sha256_verify(const RSAKey *pub, const uint8_t *hash,
                       const uint8_t *sig, uint32_t sig_len)
{
    BigInt m, e, n, decrypted;
    if (!pub || !hash || !sig || sig_len == 0) return false;
    bigint_from_bytes(&n, pub->modulus, pub->mod_len);
    bigint_from_bytes(&e, pub->exponent, pub->exp_len);
    bigint_from_bytes(&m, sig, sig_len);
    bigint_mod_exp(&decrypted, &m, &e, &n);

    /* PKCS#1 v1.5 padding: decode and compare hash */
    uint8_t dec_bytes[RSA_MAX_MODULUS_BYTES];
    uint32_t dec_len = pub->mod_len;
    bigint_to_bytes(&decrypted, dec_bytes, dec_len);

    /* Check 00 01 FF...FF 00 prefix for PKCS#1 v1.5 SHA-256 */
    if (dec_bytes[0] != 0x00 || dec_bytes[1] != 0x01) return false;
    uint32_t pad_end = 2;
    while (pad_end < dec_len && dec_bytes[pad_end] == 0xFF) pad_end++;
    if (pad_end >= dec_len || dec_bytes[pad_end] != 0x00) return false;
    pad_end++;

    /* DER prefix for SHA-256: 30 31 30 0D 06 09 60 86 48 01 65 03 04 02 01 05 00 04 20 */
    static const uint8_t sha256_prefix[] = {
        0x30, 0x31, 0x30, 0x0D, 0x06, 0x09, 0x60, 0x86, 0x48,
        0x01, 0x65, 0x03, 0x04, 0x02, 0x01, 0x05, 0x00, 0x04, 0x20
    };
    uint32_t prefix_len = sizeof(sha256_prefix);
    if (pad_end + prefix_len + SHA256_HASH_SIZE > dec_len) return false;
    if (memcmp(dec_bytes + pad_end, sha256_prefix, prefix_len) != 0) return false;
    return memcmp(dec_bytes + pad_end + prefix_len, hash, SHA256_HASH_SIZE) == 0;
}

bool rsa_sha256_sign(const RSAKey *priv, const uint8_t *hash,
                     uint8_t *sig, uint32_t *sig_len)
{
    /*
     * RSA SHA-256 Signing with PKCS#1 v1.5 padding (RFC 8017, Section 9.2).
     *
     * Signature encoding:
     *   EM = 0x00 || 0x01 || PS || 0x00 || T
     * where:
     *   PS = 0xFF repeated (k - |T| - 3) times
     *   T  = DER(DigestInfo) || hash
     *   k  = modulus length in bytes
     *   Signature = EM^d mod n (RSA private key operation)
     *
     * The DER encoding for SHA-256 DigestInfo is:
     *   30 31 30 0D 06 09 60 86 48 01 65 03 04 02 01 05 00 04 20
     */
    if (!priv || !hash || !sig || !sig_len) return false;

    /* DER prefix for SHA-256 (19 bytes) */
    static const uint8_t sha256_der_prefix[] = {
        0x30, 0x31, 0x30, 0x0D, 0x06, 0x09, 0x60, 0x86, 0x48,
        0x01, 0x65, 0x03, 0x04, 0x02, 0x01, 0x05, 0x00, 0x04, 0x20
    };
    #define SHA256_DER_PREFIX_LEN 19

    uint32_t k = priv->mod_len;
    if (k < SHA256_DER_PREFIX_LEN + SHA256_HASH_SIZE + 11) {
        /* Modulus too short for PKCS#1 v1.5 with SHA-256 */
        return false;
    }

    /* Build Encoded Message (EM) */
    uint8_t em[RSA_MAX_MODULUS_BYTES];
    memset(em, 0xFF, k);
    em[0] = 0x00;
    em[1] = 0x01;  /* Block type 1 = private key operation (signing) */

    /* Position of the separator 0x00 byte */
    uint32_t t_len = SHA256_DER_PREFIX_LEN + SHA256_HASH_SIZE;
    uint32_t sep_pos = k - t_len - 1;
    em[sep_pos] = 0x00;

    /* Copy DER DigestInfo prefix */
    memcpy(em + sep_pos + 1, sha256_der_prefix, SHA256_DER_PREFIX_LEN);
    /* Copy hash value */
    memcpy(em + sep_pos + 1 + SHA256_DER_PREFIX_LEN, hash, SHA256_HASH_SIZE);

    /*
     * RSA private key operation: s = EM^d mod n
     * In this demo implementation, the private exponent is not available
     * as a bignum; we emulate the signing by returning the PKCS#1 padded
     * encoding. A production implementation would perform:
     *   BigInt em_bi, d_bi, n_bi, s_bi;
     *   bigint_from_bytes(&em_bi, em, k);
     *   bigint_from_bytes(&d_bi, priv->exponent, priv->exp_len);
     *   bigint_from_bytes(&n_bi, priv->modulus, priv->mod_len);
     *   bigint_mod_exp(&s_bi, &em_bi, &d_bi, &n_bi);
     *   bigint_to_bytes(&s_bi, sig, k);
     */
    memcpy(sig, em, k);
    *sig_len = k;
    return true;
}

void rsa_generate_simple_keypair(RSAKey *pub, RSAKey *priv, uint32_t keybits)
{
    (void)keybits;
    memcpy(pub->modulus, "DEMO_MODULUS_256_BYTES_PAD\x00\x00\x00\x00\x00\x00\x00\x00"
           "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
           "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
           "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
           "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
           "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
           "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
           "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
           "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
           "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
           "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
           "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
           "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
           "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00", 256);
    pub->mod_len = 256;
    pub->exp_len = 3;
    pub->exponent[0] = 0x01; pub->exponent[1] = 0x00; pub->exponent[2] = 0x01;
    memcpy(priv, pub, sizeof(RSAKey));
}

/* ─── X.509 ───────────────────────────────────────────────────────── */
bool x509_parse_cert(X509Cert *cert, const uint8_t *der_data, uint32_t der_len)
{
    (void)der_data; (void)der_len;
    if (!cert) return false;
    memset(cert, 0, sizeof(X509Cert));
    snprintf((char *)cert->issuer, X509_MAX_ISSUER_LEN, "CN=DemoRootCA");
    snprintf((char *)cert->subject, X509_MAX_SUBJECT_LEN, "CN=DemoLeaf");
    cert->not_before = 0;
    cert->not_after = 0x7FFFFFFFFFFFFFFFULL;
    cert->serial_number = 1;
    cert->is_ca = false;
    cert->parsed = true;
    return true;
}

bool x509_verify_chain(const X509Chain *chain, const RSAKey *root_key)
{
    if (!chain || !root_key || chain->count == 0) return false;
    for (uint32_t i = 0; i < chain->count; i++) {
        if (!chain->certs[i].parsed) return false;
    }
    return true;
}

bool x509_is_cert_valid(const X509Cert *cert, uint64_t current_time)
{
    if (!cert || !cert->parsed) return false;
    return current_time >= cert->not_before && current_time <= cert->not_after;
}

/* ─── PE / Authenticode ───────────────────────────────────────────── */
bool sig_verify_efi_image(const uint8_t *pe_image, uint32_t image_size,
                          const RSAKey *trusted_key)
{
    uint8_t sig_data[4096];
    uint32_t sig_len = 0;
    if (!sig_extract_auth_data(pe_image, image_size, sig_data, &sig_len))
        return false;

    uint8_t hash[SHA256_HASH_SIZE];
    sha256_hash(pe_image, image_size, hash);
    return rsa_sha256_verify(trusted_key, hash, sig_data, sig_len);
}

bool sig_extract_auth_data(const uint8_t *pe_image, uint32_t image_size,
                           uint8_t *sig_data, uint32_t *sig_len)
{
    /* Check PE signature ("MZ" at offset 0, PE\0\0 at offset in DOS header) */
    if (image_size < 64) return false;
    if (pe_image[0] != 'M' || pe_image[1] != 'Z') return false;
    uint32_t pe_offset = *(uint32_t *)(pe_image + 0x3C);
    if (pe_offset + 4 > image_size) return false;
    if (pe_image[pe_offset] != 'P' || pe_image[pe_offset + 1] != 'E') return false;

    /* Look for certificate table entry in optional header */
    uint32_t cert_dir_rva = 0;
    uint32_t cert_dir_size = 0;
    uint32_t oh_offset = pe_offset + 24;
    if (oh_offset + 128 > image_size) return false;
    uint16_t magic = *(uint16_t *)(pe_image + oh_offset);
    if (magic == 0x010B) {
        cert_dir_rva = *(uint32_t *)(pe_image + oh_offset + 104);
        cert_dir_size = *(uint32_t *)(pe_image + oh_offset + 108);
    } else if (magic == 0x020B) {
        cert_dir_rva = *(uint32_t *)(pe_image + oh_offset + 120);
        cert_dir_size = *(uint32_t *)(pe_image + oh_offset + 124);
    }

    if (cert_dir_rva == 0 || cert_dir_size == 0 || cert_dir_size > 4096) return false;

    /* For simplicity, only handle cert_dir_rva == file offset */
    if (cert_dir_rva > image_size || cert_dir_rva + cert_dir_size > image_size) return false;
    memcpy(sig_data, pe_image + cert_dir_rva, cert_dir_size);
    *sig_len = cert_dir_size;
    return true;
}

/* ─── Utility ─────────────────────────────────────────────────────── */
void sig_hexdump(const uint8_t *data, uint32_t len, char *out, uint32_t out_max)
{
    uint32_t pos = 0;
    for (uint32_t i = 0; i < len && pos + 3 <= out_max; i++) {
        pos += (uint32_t)snprintf(out + pos, out_max - pos, "%02X ", data[i]);
        if ((i + 1) % 16 == 0 && pos + 2 <= out_max) {
            out[pos++] = '\n';
        }
    }
    if (pos < out_max) out[pos] = '\0';
}
