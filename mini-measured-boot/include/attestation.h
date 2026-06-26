/* TPM 2.0 Remote Attestation Protocol
 * Reference: TPM 2.0 Part 1 section 29 (Attestation)
 *            TCG Trusted Attestation Protocol (TAP) Information Model
 *
 * Knowledge coverage:
 *   L1: Attestation structures (Quote, Challenge, Response)
 *   L2: Remote attestation concept and trust model
 *   L3: Quote signing flow and verification pipeline
 *   L4: Attestation identity keys (AIK) vs endorsement keys (EK)
 *   L5: Challenge-response protocol with nonce freshness
 *   L6: End-to-end attestation with PCR composite verification
 *   L8: Formal attestation protocol properties (freshness, binding)
 */
#ifndef ATTESTATION_H
#define ATTESTATION_H

#include <stdbool.h>
#include <stdint.h>
#include "tpm2_structs.h"
#include "pcr_bank.h"
#include "tpm_keys.h"

#define ATT_NONCE_SIZE         32
#define ATT_MAX_PCR_COUNT      8
#define ATT_SIGNATURE_SIZE     64
#define ATT_CHALLENGE_ID_SIZE  16

/* L1: Attestation challenge (verifier -> prover) */
typedef struct {
    uint8_t   challenge_id[ATT_CHALLENGE_ID_SIZE];
    uint8_t   nonce[ATT_NONCE_SIZE];
    uint32_t  pcr_indices[ATT_MAX_PCR_COUNT];
    uint32_t  pcr_count;
    uint32_t  timestamp;
} AttestChallenge;

/* L1: PCR composite within a quote */
typedef struct {
    uint32_t  pcr_indices[ATT_MAX_PCR_COUNT];
    uint32_t  pcr_count;
    uint8_t   pcr_values[ATT_MAX_PCR_COUNT][SHA256_DIGEST_SIZE];
    uint8_t   pcr_composite[SHA256_DIGEST_SIZE];
} AttestPCRComposite;

/* L1: Attestation quote (prover -> verifier) */
typedef struct {
    uint8_t            quote_nonce[ATT_NONCE_SIZE];
    AttestPCRComposite pcr_comp;
    uint32_t           signing_key_handle;
    uint8_t            signature[ATT_SIGNATURE_SIZE];
    uint32_t           signature_len;
    uint8_t            qualifying_data[64];
    uint16_t           qualifying_data_len;
    bool               sig_verified;
} AttestQuote;

/* L1: Attestation response = quote + attestation key certificate */
typedef struct {
    AttestQuote     quote;
    TPMKeyPublic    aik_public;
    uint8_t         aik_name[SHA256_DIGEST_SIZE];
    bool            verified;
} AttestResponse;

/* L1: Attestation verifier state */
typedef struct {
    AttestChallenge  last_challenge;
    AttestResponse   last_response;
    bool             challenge_sent;
    bool             response_received;
    uint8_t          expected_nonce[ATT_NONCE_SIZE];
    uint32_t         expected_pcr_indices[ATT_MAX_PCR_COUNT];
    uint32_t         expected_pcr_count;
    /* Trusted PCR reference values (golden measurements) */
    uint8_t          golden_pcrs[PCR_COUNT][SHA256_DIGEST_SIZE];
    bool             golden_defined[PCR_COUNT];
} AttestVerifier;

/* ---- API ---- */

/* L5: Generate a fresh attestation challenge with random nonce.
 * The nonce provides freshness: the prover must include this exact
 * nonce in the quote to prevent replay attacks. */
void attest_generate_challenge(AttestVerifier* verifier,
                               const uint32_t* pcr_indices, uint32_t pcr_count);

/* L5: Create a quote from the current PCR state using an attestation key.
 * The quote is signed by the AIK so the verifier can trust the
 * integrity and authenticity of PCR values. */
bool attest_create_quote(const PCRBank* pcr_bank,
                         const AttestChallenge* challenge,
                         const TPMKeyManager* key_mgr,
                         uint32_t signing_key_handle,
                         AttestQuote* quote);

/* L5: Verify a quote against a challenge.
 * Checks: 1) nonce matches, 2) signature valid, 3) PCR indices correct. */
bool attest_verify_quote(const AttestVerifier* verifier,
                         const AttestQuote* quote,
                         const TPMKeyPublic* aik_public);

/* L6: End-to-end attestation: challenge -> quote -> verify.
 * This is the canonical remote attestation protocol flow.
 * Returns true if the platform integrity is verified. */
bool attest_verify_integrity(const AttestVerifier* verifier,
                             const AttestResponse* response);

/* L8: Formal attestation property check.
 * Verifies that the attestation protocol satisfies:
 * 1. Freshness: nonce in response matches challenge
 * 2. Binding: PCR values bound to AIK by signature
 * 3. Trustworthiness: PCR values match golden references
 * Returns a bitmask of which properties hold. */
uint32_t attest_check_properties(const AttestVerifier* verifier,
                                  const AttestResponse* response);
#define ATT_PROP_FRESHNESS  0x01
#define ATT_PROP_BINDING    0x02
#define ATT_PROP_TRUST      0x04

/* L4: Set golden PCR reference values for verification */
void attest_set_golden_pcr(AttestVerifier* verifier, uint32_t pcr_index,
                           const uint8_t* golden_value);

void attest_print_challenge(const AttestChallenge* challenge);
void attest_print_quote(const AttestQuote* quote);
void attest_print_response(const AttestResponse* response);
void attest_print_verifier(const AttestVerifier* verifier);

#endif
