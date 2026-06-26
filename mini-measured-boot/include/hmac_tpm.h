/* TPM 2.0 HMAC-SHA256 Module
 * Reference: RFC 2104 (HMAC), FIPS 198-1
 *
 * Knowledge coverage:
 *   L1: HMAC context and parameter structures
 *   L2: HMAC concept ? keyed hash for message authentication
 *   L4: HMAC security: PRF under weak collision resistance of SHA-256
 *   L5: HMAC-SHA256 implementation for TPM authorization
 *
 * Theorem (Bellare 2006): HMAC is a PRF assuming the underlying
 * compression function is a PRF. This is the best-possible result
 * for HMAC-like constructions and justifies its use in TPM 2.0
 * for session key derivation and command authorization.
 */
#ifndef HMAC_TPM_H
#define HMAC_TPM_H

#include <stdint.h>
#include <stddef.h>

#define HMAC_SHA256_SIZE  32
#define HMAC_BLOCK_SIZE   64

/* TPM-specific HMAC-SHA256.
 * key:     secret key material
 * key_len: key length in bytes (if > 64, hashed first per RFC 2104)
 * data:    message to authenticate
 * data_len: message length
 * mac:     output MAC (32 bytes)
 *
 * Algorithm (RFC 2104 section 2):
 *   HMAC(K, m) = H((K' xor opad) || H((K' xor ipad) || m))
 *   where K' = H(K) if |K| > block_size, else K */
void tpm_hmac_sha256(const uint8_t* key, uint32_t key_len,
                     const uint8_t* data, uint32_t data_len,
                     uint8_t* mac);

#endif
