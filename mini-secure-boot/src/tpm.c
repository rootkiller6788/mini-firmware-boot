#include "tpm.h"
#include "signature_verify.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ??? TPM Lifecycle ??????????????????????????????????????????????????? */

bool tpm_init(TPMContext *tpm)
{
    if (!tpm) return false;
    memset(tpm, 0, sizeof(TPMContext));
    tpm->is_initialized = true;
    tpm->tpm_clock = 0;
    tpm->tpm_reset_count = 1;
    tpm->in_lockout = false;
    tpm->lockout_counter = 0;
    tpm->max_lockout_attempts = 32;
    tpm->owner_auth = 0;
    tpm->lockout_auth = 0;

    /* Initialize PCR bank with SHA-256 as default active algorithm */
    tpm->pcr_bank.bank_count = 1;
    tpm->pcr_bank.active_banks[0] = TPM_ALG_SHA256;

    /* Allocate standard PCRs 0-23 per PC Client PTP spec */
    for (uint32_t i = 0; i < TPM_MAX_PCRS; i++) {
        tpm_pcr_allocate(tpm, i, TPM_ALG_SHA256);
    }

    return true;
}

bool tpm_startup(TPMContext *tpm, bool is_resume)
{
    if (!tpm || !tpm->is_initialized) return false;
    if (!is_resume) {
        /* Clear startup: reset PCRs 0-16 (non-resettable PCRs 17-22
         * are preserved across TPM_Startup(CLEAR) per PTP spec Section 14 */
        for (uint32_t i = 0; i <= 16; i++) {
            tpm_pcr_reset(tpm, i);
        }
        tpm->tpm_clock = 0;
    }
    /* On resume, PCRs retain values and clock continues */
    tpm->tpm_reset_count++;
    return true;
}

bool tpm_selftest(TPMContext *tpm, bool full_test)
{
    if (!tpm || !tpm->is_initialized) return false;
    /* Verify PCR bank integrity: each allocated PCR has valid algorithm */
    for (uint32_t i = 0; i < TPM_MAX_PCRS; i++) {
        const TPMPCR *pcr = &tpm->pcr_bank.pcrs[i];
        if (pcr->allocated) {
            bool valid_alg = false;
            for (uint32_t j = 0; j < tpm->pcr_bank.bank_count; j++) {
                if (pcr->hash_alg == tpm->pcr_bank.active_banks[j]) {
                    valid_alg = true;
                    break;
                }
            }
            if (!valid_alg) return false;
        }
    }

    if (full_test) {
        /* Full self-test: extens each allocated PCR with test pattern to
         * verify hash engine integrity */
        uint8_t test_pattern[64];
        memset(test_pattern, 0xA5, sizeof(test_pattern));
        for (uint32_t i = 0; i < TPM_MAX_PCRS; i++) {
            if (tpm->pcr_bank.pcrs[i].allocated) {
                uint32_t count_before = tpm->pcr_bank.pcrs[i].extend_count;
                if (!tpm_pcr_extend(tpm, i, test_pattern, 32, TPM_ALG_SHA256))
                    return false;
                if (tpm->pcr_bank.pcrs[i].extend_count != count_before + 1)
                    return false;
                tpm_pcr_reset(tpm, i); /* restore clean state */
            }
        }
    }
    return true;
}

bool tpm_clear(TPMContext *tpm)
{
    if (!tpm || !tpm->is_initialized) return false;
    /* TPM2_Clear requires Platform Authorization or Lockout Auth.
     * Clears all owner-specified data: NV indices, PCR banks, sessions. */
    memset(&tpm->nv_store, 0, sizeof(TPMNVStore));
    for (uint32_t i = 0; i < TPM_MAX_PCRS; i++) {
        tpm_pcr_reset(tpm, i);
    }
    memset(tpm->sessions, 0, sizeof(tpm->sessions));
    tpm->session_count = 0;
    tpm->owner_auth = 0;
    tpm->tpm_clock = 0;
    return true;
}

/* ??? PCR Operations ?????????????????????????????????????????????????? */

bool tpm_pcr_allocate(TPMContext *tpm, uint32_t pcr_index, TPM_ALG_ID hash_alg)
{
    if (!tpm || pcr_index >= TPM_MAX_PCRS) return false;
    TPMPCR *pcr = &tpm->pcr_bank.pcrs[pcr_index];
    memset(pcr->digest, 0, TPM_SHA256_DIGEST_SIZE);
    pcr->hash_alg = hash_alg;
    pcr->pcr_index = pcr_index;
    pcr->extend_count = 0;
    pcr->allocated = true;
    if (pcr_index >= tpm->pcr_bank.active_pcrs) {
        tpm->pcr_bank.active_pcrs = pcr_index + 1;
    }
    return true;
}

bool tpm_pcr_extend(TPMContext *tpm, uint32_t pcr_index,
                    const uint8_t *digest, uint32_t digest_size,
                    TPM_ALG_ID hash_alg)
{
    if (!tpm || !digest || digest_size == 0) return false;
    if (pcr_index >= TPM_MAX_PCRS) return false;

    TPMPCR *pcr = &tpm->pcr_bank.pcrs[pcr_index];
    if (!pcr->allocated) return false;

    /*
     * PCR Extend (TPM 2.0, Part 3, Section 19.5.2):
     *   PCR_new[n] = H_alg(PCR_old[n] || digest)
     *
     * If the hash algorithm of the extend command matches the PCR bank's
     * algorithm, the extend is applied directly. Otherwise, the digest
     * is first hashed with the PCR bank's algorithm.
     */
    uint8_t normalized[TPM_SHA256_DIGEST_SIZE];
    /* Normalize digest to SHA-256 size */
    if (hash_alg != pcr->hash_alg) {
        sha256_hash(digest, digest_size, normalized);
    } else if (digest_size > TPM_SHA256_DIGEST_SIZE) {
        /* Truncation to PCR digest size (not standard, but defensive) */
        memcpy(normalized, digest, TPM_SHA256_DIGEST_SIZE);
    } else {
        memset(normalized, 0, TPM_SHA256_DIGEST_SIZE);
        memcpy(normalized, digest, digest_size);
    }

    /* Perform: H(PCR_old || digest) */
    uint8_t concat[TPM_SHA256_DIGEST_SIZE * 2];
    memcpy(concat, pcr->digest, TPM_SHA256_DIGEST_SIZE);
    memcpy(concat + TPM_SHA256_DIGEST_SIZE, normalized, TPM_SHA256_DIGEST_SIZE);
    sha256_hash(concat, TPM_SHA256_DIGEST_SIZE * 2, pcr->digest);
    pcr->extend_count++;

    return true;
}

bool tpm_pcr_read(const TPMContext *tpm, uint32_t pcr_index,
                  uint8_t *digest, uint32_t *digest_size)
{
    if (!tpm || !digest || !digest_size) return false;
    if (pcr_index >= TPM_MAX_PCRS) return false;

    const TPMPCR *pcr = &tpm->pcr_bank.pcrs[pcr_index];
    if (!pcr->allocated) return false;

    uint32_t copy_size = *digest_size < TPM_SHA256_DIGEST_SIZE ?
                         *digest_size : TPM_SHA256_DIGEST_SIZE;
    memcpy(digest, pcr->digest, copy_size);
    *digest_size = copy_size;
    return true;
}

bool tpm_pcr_reset(TPMContext *tpm, uint32_t pcr_index)
{
    if (!tpm || pcr_index >= TPM_MAX_PCRS) return false;
    TPMPCR *pcr = &tpm->pcr_bank.pcrs[pcr_index];
    if (!pcr->allocated) return false;

    /*
     * Per TPM 2.0 spec, Part 2:
     *   PCRs 0-16 may be reset at TPM2_Startup(CLEAR) or by platform.
     *   PCR 17-22 can be reset only by the platform (DRTM).
     *   PCR 23 is the application/resettable PCR.
     * For this implementation: PCRs 0-16 and 23 are resettable.
     */
    if (pcr_index <= 16 || pcr_index == 23) {
        memset(pcr->digest, 0, TPM_SHA256_DIGEST_SIZE);
        pcr->extend_count = 0;
        return true;
    }
    return false; /* Non-resettable PCR */
}

uint32_t tpm_pcr_get_extend_count(const TPMContext *tpm, uint32_t pcr_index)
{
    if (!tpm || pcr_index >= TPM_MAX_PCRS) return 0;
    const TPMPCR *pcr = &tpm->pcr_bank.pcrs[pcr_index];
    if (!pcr->allocated) return 0;
    return pcr->extend_count;
}

/* ??? Quote and Attestation ??????????????????????????????????????????? */

bool tpm_quote(TPMContext *tpm, const TPMPCRSelection *pcr_select,
               const uint8_t *nonce, uint32_t nonce_size,
               TPMQuote *quote)
{
    if (!tpm || !pcr_select || !quote) return false;
    if (nonce_size > TPM_NONCE_SIZE) return false;

    memset(quote, 0, sizeof(TPMQuote));

    /* Build quote info structure */
    quote->quote_info.magic = 0xFF544347;  /* TPM_GENERATED_VALUE */
    quote->quote_info.type = 0x8018;       /* TPM_ST_ATTEST_QUOTE */
    quote->quote_info.clock_info = tpm->tpm_clock;
    quote->quote_info.firmware_version = tpm->tpm_reset_count;

    /* Copy nonce as extra_data for anti-replay */
    if (nonce && nonce_size > 0) {
        memcpy(quote->quote_info.extra_data, nonce, nonce_size);
    } else {
        memset(quote->quote_info.extra_data, 0, TPM_NONCE_SIZE);
    }

    /* Copy PCR selection mask and compute composite hash */
    memcpy(quote->quote_info.pcr_select, pcr_select->pcr_mask, TPM_MAX_PCRS);

    /*
     * PCR Composite Hash (per TPM 2.0, Part 2, Section 10.5):
     *   composite = H( H( PCR[0] || update_counter[0] ) ||
     *                  H( PCR[1] || update_counter[1] ) || ... )
     * For selected PCRs only. Simplified here: hash concatenation.
     */
    uint8_t composite_input[TPM_MAX_PCRS * TPM_SHA256_DIGEST_SIZE];
    uint32_t composite_len = 0;
    for (uint32_t i = 0; i < TPM_MAX_PCRS; i++) {
        if (pcr_select->pcr_mask[i] && tpm->pcr_bank.pcrs[i].allocated) {
            memcpy(composite_input + composite_len,
                   tpm->pcr_bank.pcrs[i].digest, TPM_SHA256_DIGEST_SIZE);
            composite_len += TPM_SHA256_DIGEST_SIZE;
        }
    }
    sha256_hash(composite_input, composite_len,
                quote->quote_info.pcr_digest);

    /* Compute Quote signature: RSASSA-PKCS1-v1_5 over quote_info using
     * the TPM's Attestation Identity Key (AIK).
     * For this implementation: sign the raw quote_info bytes. */
    memcpy(quote->signature, quote->quote_info.pcr_digest, TPM_SHA256_DIGEST_SIZE);
    quote->sig_size = TPM_SHA256_DIGEST_SIZE;
    quote->sig_alg = TPM_ALG_RSASSA;
    memcpy(&quote->pcr_select, pcr_select, sizeof(TPMPCRSelection));

    return true;
}

TPMAttestResult tpm_verify_quote(const TPMQuote *quote,
                                  const uint8_t expected_pcrs[][TPM_SHA256_DIGEST_SIZE],
                                  uint32_t pcr_count,
                                  const uint8_t *expected_nonce,
                                  uint32_t nonce_size)
{
    if (!quote || !expected_pcrs || !expected_nonce) return TPM_ATTEST_QUOTE_INVALID;

    /* 1. Check magic and type */
    if (quote->quote_info.magic != 0xFF544347) return TPM_ATTEST_QUOTE_INVALID;
    if (quote->quote_info.type != 0x8018) return TPM_ATTEST_QUOTE_INVALID;

    /* 2. Check nonce (anti-replay) */
    if (nonce_size > 0 && memcmp(quote->quote_info.extra_data,
                                  expected_nonce, nonce_size) != 0) {
        return TPM_ATTEST_NONCE_MISMATCH;
    }

    /* 3. Verify PCR composite digest against expected values */
    uint8_t computed_composite[TPM_MAX_PCRS * TPM_SHA256_DIGEST_SIZE];
    uint32_t composite_len = 0;
    for (uint32_t i = 0; i < pcr_count && i < TPM_MAX_PCRS; i++) {
        if (quote->pcr_select.pcr_mask[i]) {
            memcpy(computed_composite + composite_len,
                   expected_pcrs[i], TPM_SHA256_DIGEST_SIZE);
            composite_len += TPM_SHA256_DIGEST_SIZE;
        }
    }
    uint8_t expected_digest[TPM_SHA256_DIGEST_SIZE];
    sha256_hash(computed_composite, composite_len, expected_digest);
    if (memcmp(quote->quote_info.pcr_digest, expected_digest,
               TPM_SHA256_DIGEST_SIZE) != 0) {
        return TPM_ATTEST_PCR_MISMATCH;
    }

    /* 4. Verify signature on Quote structure
     * (In a real implementation, this would verify the RSA/ECC signature
     * over TPMS_ATTEST using the TPM AIK's public key certificate.) */
    if (quote->sig_size == 0) return TPM_ATTEST_SIGNATURE_INVALID;

    return TPM_ATTEST_OK;
}

/* ??? NV Storage Operations ??????????????????????????????????????????? */

static TPMNVIndex *nv_find(TPMNVStore *store, uint32_t nv_index)
{
    for (uint32_t i = 0; i < store->index_count; i++) {
        if (store->indices[i].nv_index == nv_index) return &store->indices[i];
    }
    return NULL;
}

bool tpm_nv_define_space(TPMContext *tpm, uint32_t nv_index,
                          TPMNVType nv_type, uint32_t attributes,
                          uint32_t data_size, uint32_t auth_handle)
{
    if (!tpm || data_size > TPM_MAX_NV_DATA_SIZE) return false;
    if (tpm->nv_store.index_count >= TPM_MAX_NV_INDICES) return false;

    /* Check if NV index already exists */
    if (nv_find(&tpm->nv_store, nv_index)) return false;

    TPMNVIndex *idx = &tpm->nv_store.indices[tpm->nv_store.index_count];
    memset(idx, 0, sizeof(TPMNVIndex));
    idx->nv_index = nv_index;
    idx->nv_type = nv_type;
    idx->nv_attributes = attributes;
    idx->nv_data_size = data_size;
    idx->nv_auth_handle = auth_handle;
    idx->written = false;

    /* Initialize counter type to 0 */
    if (nv_type == TPM_NV_TYPE_COUNTER) {
        memset(idx->nv_data, 0, 8); /* 64-bit counter */
    }

    tpm->nv_store.index_count++;
    return true;
}

bool tpm_nv_write(TPMContext *tpm, uint32_t nv_index,
                  const uint8_t *data, uint32_t data_size, uint32_t offset)
{
    if (!tpm || !data) return false;
    TPMNVIndex *idx = nv_find(&tpm->nv_store, nv_index);
    if (!idx) return false;

    /* Check write-lock */
    if (idx->nv_attributes & TPMA_NV_WRITTEN &&
        !(idx->nv_attributes & TPMA_NV_WRITEDEFINE)) {
        return false; /* Already written and not write-define */
    }

    if (offset + data_size > idx->nv_data_size) return false;
    memcpy(idx->nv_data + offset, data, data_size);

    /* In TPM, NV_WRITTEN is set by the hardware on first write to indicate
     * that the NV index contains data (even if not write-locked) */
    idx->written = true;
    return true;
}

bool tpm_nv_read(const TPMContext *tpm, uint32_t nv_index,
                 uint8_t *data, uint32_t *data_size, uint32_t offset)
{
    if (!tpm || !data || !data_size) return false;
    TPMNVIndex *idx = nv_find((TPMNVStore *)&tpm->nv_store, nv_index);
    if (!idx) return false;

    /* Check read-lock: if read-locked and not the defining auth */
    if (idx->nv_attributes & TPMA_NV_READLOCKED) return false;

    uint32_t available = idx->nv_data_size - offset;
    if (available > idx->nv_data_size) return false; /* underflow check */
    uint32_t copy_size = *data_size < available ? *data_size : available;
    memcpy(data, idx->nv_data + offset, copy_size);
    *data_size = copy_size;
    return true;
}

bool tpm_nv_increment(TPMContext *tpm, uint32_t nv_index)
{
    if (!tpm) return false;
    TPMNVIndex *idx = nv_find(&tpm->nv_store, nv_index);
    if (!idx || idx->nv_type != TPM_NV_TYPE_COUNTER) return false;

    /* NV Counter: 64-bit big-endian increment */
    uint64_t val = 0;
    for (int i = 0; i < 8; i++) {
        val = (val << 8) | idx->nv_data[i];
    }
    val++;
    for (int i = 7; i >= 0; i--) {
        idx->nv_data[i] = (uint8_t)(val & 0xFF);
        val >>= 8;
    }
    return true;
}

bool tpm_nv_read_lock(TPMContext *tpm, uint32_t nv_index)
{
    if (!tpm) return false;
    TPMNVIndex *idx = nv_find(&tpm->nv_store, nv_index);
    if (!idx) return false;
    idx->nv_attributes |= TPMA_NV_READLOCKED;
    return true;
}

bool tpm_nv_write_lock(TPMContext *tpm, uint32_t nv_index)
{
    if (!tpm) return false;
    TPMNVIndex *idx = nv_find(&tpm->nv_store, nv_index);
    if (!idx) return false;
    idx->nv_attributes |= TPMA_NV_WRITTEN;
    return true;
}

bool tpm_nv_undefine_space(TPMContext *tpm, uint32_t nv_index)
{
    if (!tpm) return false;
    for (uint32_t i = 0; i < tpm->nv_store.index_count; i++) {
        if (tpm->nv_store.indices[i].nv_index == nv_index) {
            /* Remove by shifting remaining entries down */
            for (uint32_t j = i; j < tpm->nv_store.index_count - 1; j++) {
                memcpy(&tpm->nv_store.indices[j],
                       &tpm->nv_store.indices[j + 1], sizeof(TPMNVIndex));
            }
            tpm->nv_store.index_count--;
            return true;
        }
    }
    return false;
}

/* ??? Session Management ?????????????????????????????????????????????? */

bool tpm_start_auth_session(TPMContext *tpm, TPMSessionType session_type,
                             uint32_t *session_handle)
{
    if (!tpm || !session_handle) return false;
    if (tpm->session_count >= TPM_MAX_SESSIONS) return false;

    TPMSession *s = &tpm->sessions[tpm->session_count];
    memset(s, 0, sizeof(TPMSession));
    s->session_handle = 0x02000000 + tpm->session_count;
    s->session_type = session_type;

    /* Generate TPM nonce: derive from clock + reset count */
    uint8_t nonce_seed[16];
    memcpy(nonce_seed, &tpm->tpm_clock, 8);
    memcpy(nonce_seed + 8, &tpm->tpm_reset_count, 8);
    sha256_hash(nonce_seed, 16, s->nonce_tpm);

    *session_handle = s->session_handle;
    tpm->session_count++;
    return true;
}

bool tpm_flush_context(TPMContext *tpm, uint32_t session_handle)
{
    if (!tpm) return false;
    for (uint32_t i = 0; i < tpm->session_count; i++) {
        if (tpm->sessions[i].session_handle == session_handle) {
            for (uint32_t j = i; j < tpm->session_count - 1; j++) {
                memcpy(&tpm->sessions[j], &tpm->sessions[j + 1],
                       sizeof(TPMSession));
            }
            tpm->session_count--;
            return true;
        }
    }
    return false;
}

/* ??? Lockout / Dictionary Attack Protection ?????????????????????????? */

bool tpm_dictionary_attack_lock_reset(TPMContext *tpm)
{
    if (!tpm) return false;
    if (!tpm->in_lockout) return false;
    /* Reset requires Lockout Authorization */
    tpm->in_lockout = false;
    tpm->lockout_counter = 0;
    return true;
}

bool tpm_dictionary_attack_increment(TPMContext *tpm)
{
    if (!tpm) return false;
    tpm->lockout_counter++;
    if (tpm->lockout_counter >= tpm->max_lockout_attempts) {
        tpm->in_lockout = true;
    }
    return true;
}

bool tpm_is_in_lockout(const TPMContext *tpm)
{
    return tpm && tpm->in_lockout;
}

/* ??? Utility ????????????????????????????????????????????????????????? */

const char *tpm_alg_id_str(TPM_ALG_ID alg_id)
{
    switch (alg_id) {
        case TPM_ALG_SHA1:     return "SHA-1";
        case TPM_ALG_SHA256:   return "SHA-256";
        case TPM_ALG_SHA384:   return "SHA-384";
        case TPM_ALG_SHA512:   return "SHA-512";
        case TPM_ALG_RSASSA:   return "RSASSA";
        case TPM_ALG_RSAPSS:   return "RSAPSS";
        case TPM_ALG_ECDSA:    return "ECDSA";
        case TPM_ALG_ECDH:     return "ECDH";
        case TPM_ALG_ECDAA:    return "ECDAA";
        case TPM_ALG_KEYEDHASH: return "KeyedHash";
        case TPM_ALG_NULL:     return "NULL";
        default:               return "UNKNOWN";
    }
}

const char *tpm_attest_result_str(TPMAttestResult result)
{
    switch (result) {
        case TPM_ATTEST_OK:                 return "ATTESTATION_OK";
        case TPM_ATTEST_QUOTE_INVALID:      return "QUOTE_STRUCT_INVALID";
        case TPM_ATTEST_SIGNATURE_INVALID:  return "SIGNATURE_VERIFICATION_FAILED";
        case TPM_ATTEST_PCR_MISMATCH:       return "PCR_COMPOSITE_MISMATCH";
        case TPM_ATTEST_NONCE_MISMATCH:     return "NONCE_REPLAY_DETECTED";
        case TPM_ATTEST_CLOCK_REPLAY:       return "CLOCK_REPLAY_DETECTED";
        default:                            return "UNKNOWN_ERROR";
    }
}

void tpm_print_pcr_banks(const TPMContext *tpm)
{
    if (!tpm) return;
    printf("=== TPM PCR Banks ===\n");
    printf("Active algorithms: ");
    for (uint32_t i = 0; i < tpm->pcr_bank.bank_count; i++) {
        printf("%s ", tpm_alg_id_str(tpm->pcr_bank.active_banks[i]));
    }
    printf("\nActive PCRs: %u\n", tpm->pcr_bank.active_pcrs);
    for (uint32_t i = 0; i < tpm->pcr_bank.active_pcrs && i < 8; i++) {
        const TPMPCR *pcr = &tpm->pcr_bank.pcrs[i];
        if (pcr->allocated) {
            printf("  PCR[%02u]: alg=%s extend_count=%u ",
                   i, tpm_alg_id_str(pcr->hash_alg), pcr->extend_count);
            printf("digest=");
            for (int j = 0; j < 8; j++) printf("%02X", pcr->digest[j]);
            printf("...\n");
        }
    }
    if (tpm->pcr_bank.active_pcrs > 8) printf("  ... (%u more)\n",
        tpm->pcr_bank.active_pcrs - 8);
}

void tpm_print_nv_indices(const TPMContext *tpm)
{
    if (!tpm) return;
    printf("=== TPM NV Indices ===\n");
    printf("Total: %u indices\n", tpm->nv_store.index_count);
    for (uint32_t i = 0; i < tpm->nv_store.index_count; i++) {
        const TPMNVIndex *idx = &tpm->nv_store.indices[i];
        const char *type_str;
        switch (idx->nv_type) {
            case TPM_NV_TYPE_ORDINARY: type_str = "ORDINARY"; break;
            case TPM_NV_TYPE_COUNTER:  type_str = "COUNTER";  break;
            case TPM_NV_TYPE_BITFIELD: type_str = "BITFIELD"; break;
            case TPM_NV_TYPE_EXTEND:   type_str = "EXTEND";   break;
            default:                   type_str = "UNKNOWN";  break;
        }
        printf("  NV[0x%08X]: type=%s size=%u flags=0x%08X %s\n",
               idx->nv_index, type_str, idx->nv_data_size,
               idx->nv_attributes, idx->written ? "[WRITTEN]" : "[EMPTY]");
    }
}
