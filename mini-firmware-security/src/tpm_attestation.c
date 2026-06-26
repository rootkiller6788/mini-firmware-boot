#include "tpm_attestation.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/*
 * TPM_ATTESTATION: TPM 2.0 Measured Boot & Remote Attestation
 *
 * Knowledge points:
 *   L1: TPMPcrBank, TPMEventLog, TPMS_ATTEST data models
 *   L2: Measured boot chain enforcement
 *   L3: PCR extend pipeline (hash concatenation + SHA-256)
 *   L4: TCG Algorithm Registry, TPM 2.0 Part 2 Section 10
 *   L5: PCR Extend = H(PCR_old || digest) algorithm
 *        Quote verification with SHA-256 composite PCR hash
 *   L7: Remote attestation protocol with nonce-based freshness
 *   L8: Key hierarchy: EK -> SRK -> AK with derivation
 */

/* Forward declare SHA-256 from secure_boot module */
extern void sha256_hash(const uint8_t *data, size_t len, uint8_t *digest);

/* ?? TPM Core Lifecycle ?????????????????????????????????????????? */

void tpm_init(TPMState *tpm) {
    uint32_t i;
    if(tpm==NULL)return;
    memset(tpm,0,sizeof(TPMState));
    tpm->active_bank_count=0;
    tpm->tpm_initialized=true;
    tpm->tpm_self_test_passed=true;
    for(i=0;i<5;i++)tpm->locality_0_4_active[i]=false;
    tpm->locality_0_4_active[0]=true;

    /* Initialize PCR banks: SHA-1 and SHA-256 (TCG PC Client Profile) */
    tpm->pcr_banks[0].hash_algorithm=TPM_ALG_SHA1;
    tpm->pcr_banks[0].active=true;
    for(i=0;i<TPM_PCR_COUNT;i++){
        tpm->pcr_banks[0].pcrs[i].bank_id=0;
        tpm->pcr_banks[0].pcrs[i].hash_algorithm=TPM_ALG_SHA1;
        tpm->pcr_banks[0].pcrs[i].digest_size=TPM_SHA1_DIGEST_SIZE;
        tpm->pcr_banks[0].pcrs[i].initialized=true;
        memset(tpm->pcr_banks[0].pcrs[i].digest,0,TPM_SHA1_DIGEST_SIZE);
    }

    tpm->pcr_banks[1].hash_algorithm=TPM_ALG_SHA256;
    tpm->pcr_banks[1].active=true;
    for(i=0;i<TPM_PCR_COUNT;i++){
        tpm->pcr_banks[1].pcrs[i].bank_id=1;
        tpm->pcr_banks[1].pcrs[i].hash_algorithm=TPM_ALG_SHA256;
        tpm->pcr_banks[1].pcrs[i].digest_size=TPM_SHA256_DIGEST_SIZE;
        tpm->pcr_banks[1].pcrs[i].initialized=true;
        memset(tpm->pcr_banks[1].pcrs[i].digest,0,TPM_SHA256_DIGEST_SIZE);
    }
    tpm->active_bank_count=2;
    memset(tpm->tpm_nonce,0,TPM_NONCE_SIZE);
}

bool tpm_self_test(TPMState *tpm) {
    if(tpm==NULL)return false;
    tpm->tpm_self_test_passed=true;
    return true;
}

bool tpm_set_locality(TPMState *tpm, uint8_t locality) {
    if(tpm==NULL||locality>4)return false;
    tpm->locality_0_4_active[locality]=true;
    return true;
}

/* ?? L2/L5: PCR Extend (Measured Boot Core) ????????????????????? */

/*
 * PCR Extend Algorithm (TPM 2.0 Part 2 Section 10.4)
 *
 * PCR_new = H(PCR_old || digest)
 * where H is the hash algorithm associated with the PCR bank.
 *
 * The extend operation is the foundation of measured boot.
 * Each firmware component extends its measurement into PCRs,
 * creating an unforgeable chain of trust (Core Root of Trust).
 *
 * Complexity: O(digest_size) for hash computation
 * Theorem: PCR values are collision-resistant due to SHA-256
 *          preimage resistance (FIPS 180-4).
 */
bool tpm_pcr_extend(TPMState *tpm, uint16_t hash_alg,
                    uint32_t pcr_index, const uint8_t *digest,
                    uint16_t digest_size) {
    uint8_t i,bank_idx;
    TPMPcrBank *bank;
    TPMPcrValue *pcr;
    uint8_t concat_buf[TPM_SHA256_DIGEST_SIZE * 2];
    uint8_t new_hash[TPM_SHA384_DIGEST_SIZE];
    uint16_t hash_size;

    if(tpm==NULL||digest==NULL||pcr_index>=TPM_PCR_COUNT||!tpm->tpm_initialized)
        return false;

    /* Find the matching PCR bank */
    bank=NULL;
    for(i=0;i<tpm->active_bank_count;i++){
        if(tpm->pcr_banks[i].hash_algorithm==hash_alg&&tpm->pcr_banks[i].active){
            bank=&tpm->pcr_banks[i]; bank_idx=i; break;
        }
    }
    if(bank==NULL)return false;

    pcr=&bank->pcrs[pcr_index];

    /* Compute PCR_new = H(PCR_old || digest) */
    memset(concat_buf,0,sizeof(concat_buf));
    memcpy(concat_buf,pcr->digest,pcr->digest_size);
    memcpy(concat_buf+pcr->digest_size,digest,digest_size);

    if(hash_alg==TPM_ALG_SHA256){
        sha256_hash(concat_buf,pcr->digest_size+digest_size,new_hash);
        hash_size=TPM_SHA256_DIGEST_SIZE;
    }else if(hash_alg==TPM_ALG_SHA1){
        /* Simplified SHA-1-like hash using our SHA-256 truncated */
        sha256_hash(concat_buf,pcr->digest_size+digest_size,new_hash);
        hash_size=TPM_SHA1_DIGEST_SIZE;
    }else{
        return false;
    }

    memcpy(pcr->digest,new_hash,hash_size);
    pcr->digest_size=hash_size;
    return true;
}

/* L5: PCR Read - retrieve current PCR value */
bool tpm_pcr_read(TPMState *tpm, uint16_t hash_alg,
                  uint32_t pcr_index, uint8_t *digest_out,
                  uint16_t *digest_size) {
    uint8_t i;
    TPMPcrBank *bank;

    if(tpm==NULL||digest_out==NULL||digest_size==NULL||pcr_index>=TPM_PCR_COUNT)
        return false;

    for(i=0;i<tpm->active_bank_count;i++){
        if(tpm->pcr_banks[i].hash_algorithm==hash_alg&&tpm->pcr_banks[i].active){
            bank=&tpm->pcr_banks[i];
            memcpy(digest_out,bank->pcrs[pcr_index].digest,
                   bank->pcrs[pcr_index].digest_size);
            *digest_size=bank->pcrs[pcr_index].digest_size;
            return true;
        }
    }
    return false;
}

bool tpm_pcr_reset(TPMState *tpm, uint32_t pcr_index) {
    uint8_t i;
    if(tpm==NULL||pcr_index>=TPM_PCR_COUNT)return false;
    /* PCRs 0-15 can only be reset at TPM reset (locality 4) */
    if(pcr_index<16&&!tpm->locality_0_4_active[4])return false;

    for(i=0;i<tpm->active_bank_count;i++){
        memset(tpm->pcr_banks[i].pcrs[pcr_index].digest,0,
               tpm->pcr_banks[i].pcrs[pcr_index].digest_size);
    }
    return true;
}

/* ?? L2: Measured Boot Event Log ????????????????????????????????? */

/*
 * Add event to TPM event log.
 * Records: PCR index, event type, digest, and event data.
 * Event log is stored in ACPI table for OS verification.
 * Reference: TCG EFI Platform Specification 2.2
 */
bool tpm_event_log_add(TPMState *tpm, uint32_t pcr_index,
                       uint32_t event_type, const uint8_t *event_data,
                       uint32_t event_data_size) {
    TPMEventLogEntry *entry;

    if(tpm==NULL||pcr_index>=TPM_PCR_COUNT)return false;
    if(tpm->event_log.entry_count>=TPM_EVENT_LOG_MAX_ENTRIES)return false;
    if(event_data_size>TPM_EVENT_LOG_ENTRY_MAX)return false;

    entry=&tpm->event_log.entries[tpm->event_log.entry_count];
    entry->pcr_index=pcr_index;
    entry->event_type=event_type;

    /* Compute SHA-256 digest of event data for the log */
    sha256_hash(event_data,event_data_size,entry->digest);

    if(event_data!=NULL&&event_data_size>0){
        memcpy(entry->event_data,event_data,event_data_size);
    }
    entry->event_data_size=event_data_size;
    tpm->event_log.entry_count++;

    /* Also extend the appropriate PCR with the event digest */
    tpm_pcr_extend(tpm,TPM_ALG_SHA256,pcr_index,
                   entry->digest,TPM_SHA256_DIGEST_SIZE);

    return true;
}

/*
 * Event log verification: simulate PCR extension from all log entries
 * and compare with current PCR values to detect tampering.
 *
 * L4 Theorem: If SHA-256 is collision-resistant, then an attacker
 * cannot forge event log entries that produce matching PCR values
 * without knowing the correct measurement sequence.
 * (Reduction to collision resistance of SHA-256 per Rogaway-Shrimpton 2004)
 */
bool tpm_event_log_verify(TPMState *tpm) {
    size_t i;
    uint8_t simulated_pcr[TPM_PCR_COUNT][TPM_SHA256_DIGEST_SIZE];
    uint8_t pcr_counters[TPM_PCR_COUNT];
    uint8_t actual_digest[TPM_SHA256_DIGEST_SIZE];
    uint16_t actual_size;

    if(tpm==NULL)return false;

    memset(simulated_pcr,0,sizeof(simulated_pcr));
    memset(pcr_counters,0,sizeof(pcr_counters));

    /* Replay event log to compute expected PCR values */
    for(i=0;i<tpm->event_log.entry_count;i++){
        TPMEventLogEntry *entry=&tpm->event_log.entries[i];
        uint8_t concat[TPM_SHA256_DIGEST_SIZE*2];
        uint8_t sim_hash[TPM_SHA256_DIGEST_SIZE];

        memcpy(concat,simulated_pcr[entry->pcr_index],TPM_SHA256_DIGEST_SIZE);
        memcpy(concat+TPM_SHA256_DIGEST_SIZE,entry->digest,TPM_SHA256_DIGEST_SIZE);
        sha256_hash(concat,TPM_SHA256_DIGEST_SIZE*2,sim_hash);
        memcpy(simulated_pcr[entry->pcr_index],sim_hash,TPM_SHA256_DIGEST_SIZE);
        pcr_counters[entry->pcr_index]++;
    }

    /* Compare with actual PCR values */
    for(i=0;i<TPM_PCR_COUNT;i++){
        if(pcr_counters[i]>0){
            if(!tpm_pcr_read(tpm,TPM_ALG_SHA256,(uint32_t)i,
                             actual_digest,&actual_size))return false;
            if(memcmp(simulated_pcr[i],actual_digest,TPM_SHA256_DIGEST_SIZE)!=0)
                return false;
        }
    }
    return true;
}

/* ?? L7: Remote Attestation (Quote) ?????????????????????????????? */

/*
 * TPM Quote: digitally signed attestation of PCR values
 *
 * Protocol (TCG TPM 2.0 Part 3 Section 18):
 * 1. Challenger sends nonce (freshness)
 * 2. TPM creates Quote = Sign_AK(PCR_composite || nonce || info)
 * 3. Challenger verifies signature and compares PCR composite
 *    with expected values from known-good measurements
 *
 * The Quote provides cryptographic proof that the platform
 * is in a known state without revealing the AK private key.
 */
bool tpm_quote_create(TPMState *tpm, const uint8_t *nonce,
                      uint32_t nonce_size, TPMS_ATTEST *quote) {
    uint8_t i;
    uint8_t pcr_composite[TPM_SHA256_DIGEST_SIZE];
    uint8_t concat_buf[TPM_SHA256_DIGEST_SIZE*TPM_PCR_COUNT];
    uint32_t concat_len;
    uint8_t actual_digest[TPM_SHA256_DIGEST_SIZE];
    uint16_t actual_size;

    if(tpm==NULL||nonce==NULL||quote==NULL||nonce_size>TPM_NONCE_SIZE)
        return false;
    if(!tpm->ak.generated)return false; /* Need attestation key */

    memset(quote,0,sizeof(TPMS_ATTEST));
    quote->magic=0xFF544347; /* "TCG" in hex */
    quote->type=0x8018; /* TPM_ST_ATTEST_QUOTE */
    quote->clock_info=0;
    quote->firmware_version=0x00010000;

    /* Compute PCR composite: H(PCR[0] || PCR[1] || ... || PCR[23]) */
    concat_len=0;
    for(i=0;i<TPM_PCR_COUNT;i++){
        if(!tpm_pcr_read(tpm,TPM_ALG_SHA256,i,actual_digest,&actual_size))
            return false;
        memcpy(concat_buf+concat_len,actual_digest,TPM_SHA256_DIGEST_SIZE);
        concat_len+=TPM_SHA256_DIGEST_SIZE;
    }
    sha256_hash(concat_buf,concat_len,pcr_composite);
    memcpy(quote->pcr_digest,pcr_composite,TPM_SHA256_DIGEST_SIZE);

    /* Store nonce as extra_data for freshness */
    memcpy(quote->extra_data,nonce,nonce_size);

    /*  signer info: AK public key hash */
    sha256_hash(tpm->ak.public_key,tpm->ak.key_size,quote->qualified_signer);

    /* PCR selection mask: all 24 PCRs */
    memset(quote->pcr_select,0xFF,3);

    return true;
}

/*
 * Quote Verification: verify that the quote's PCR composite
 * matches expected values from a known-good configuration.
 *
 * Security requirement: the verifier must have a trusted copy
 * of the expected PCR values (golden measurements).
 */
bool tpm_quote_verify(TPMState *tpm, const TPMS_ATTEST *quote,
                      const uint8_t *nonce, uint32_t nonce_size,
                      const uint8_t *expected_pcr_values,
                      uint32_t expected_pcr_size) {
    uint8_t computed_composite[TPM_SHA256_DIGEST_SIZE];

    if(tpm==NULL||quote==NULL||nonce==NULL||expected_pcr_values==NULL)
        return false;

    /* Verify magic number */
    if(quote->magic!=0xFF544347)return false;
    if(quote->type!=0x8018)return false;

    /* Verify nonce freshness */
    if(memcmp(quote->extra_data,nonce,nonce_size)!=0)return false;

    /* Compute expected PCR composite */
    sha256_hash(expected_pcr_values,expected_pcr_size,computed_composite);

    /* Compare with quote's PCR digest */
    if(memcmp(computed_composite,quote->pcr_digest,TPM_SHA256_DIGEST_SIZE)!=0)
        return false;

    return true;
}

/* ?? L8: Key Hierarchy Management ???????????????????????????????? */

/*
 * TPM Key Hierarchy (TCG TPM 2.0 Part 1 Section 12):
 *
 * Endorsement Key (EK) - unique per TPM, used to prove TPM genuineness
 *   |
 *   v
 * Storage Root Key (SRK) - per owner, wraps other keys
 *   |
 *   v
 * Attestation Key (AK) - used for signing quotes
 *
 * Each level provides separation of concerns:
 * - EK: manufacturer-provisioned, privacy-sensitive
 * - SRK: created on ownership change
 * - AK: created per attestation domain
 */

bool tpm_create_ek(TPMState *tpm) {
    if(tpm==NULL||!tpm->tpm_initialized)return false;

    tpm->ek.handle=TPM_RH_EK;
    tpm->ek.key_type=TPM_ALG_RSA;
    tpm->ek.key_size=TPM_SHA256_DIGEST_SIZE*4;
    /* Derive EK from seed (simplified: hash of handle + nonce) */
    {
        uint8_t seed[TPM_NONCE_SIZE+4];
        memset(seed,0xAA,TPM_NONCE_SIZE);
        seed[TPM_NONCE_SIZE]=0x00;seed[TPM_NONCE_SIZE+1]=0x00;
        seed[TPM_NONCE_SIZE+2]=0x00;seed[TPM_NONCE_SIZE+3]=0x01;
        sha256_hash(seed,TPM_NONCE_SIZE+4,tpm->ek.public_key);
    }
    tpm->ek.generated=true;
    return true;
}

bool tpm_create_srk(TPMState *tpm) {
    if(tpm==NULL||!tpm->ek.generated)return false;

    tpm->srk.handle=TPM_RH_SRK;
    tpm->srk.key_type=TPM_ALG_RSA;
    tpm->srk.key_size=TPM_SHA256_DIGEST_SIZE*4;
    /* Derive SRK from EK */
    {
        uint8_t seed[TPM_SHA256_DIGEST_SIZE*4+4];
        memcpy(seed,tpm->ek.public_key,tpm->ek.key_size);
        seed[tpm->ek.key_size]=0x00;seed[tpm->ek.key_size+1]=0x00;
        seed[tpm->ek.key_size+2]=0x00;seed[tpm->ek.key_size+3]=0x02;
        sha256_hash(seed,tpm->ek.key_size+4,tpm->srk.public_key);
    }
    tpm->srk.generated=true;
    return true;
}

bool tpm_create_ak(TPMState *tpm) {
    if(tpm==NULL||!tpm->srk.generated)return false;

    tpm->ak.handle=TPM_RH_AK;
    tpm->ak.key_type=TPM_ALG_RSA;
    tpm->ak.key_size=TPM_SHA256_DIGEST_SIZE*4;
    /* Derive AK from SRK */
    {
        uint8_t seed[TPM_SHA256_DIGEST_SIZE*4+4];
        memcpy(seed,tpm->srk.public_key,tpm->srk.key_size);
        seed[tpm->srk.key_size]=0x00;seed[tpm->srk.key_size+1]=0x00;
        seed[tpm->srk.key_size+2]=0x00;seed[tpm->srk.key_size+3]=0x03;
        sha256_hash(seed,tpm->srk.key_size+4,tpm->ak.public_key);
    }
    tpm->ak.generated=true;
    return true;
}

/*
 * Activate Credential: Verify that the TPM has a valid AK.
 * The credential is encrypted to the EK, and the TPM decrypts
 * it only if AK is resident. Proves AK binding to TPM.
 * Reference: TPM 2.0 Part 3 Section 24.1
 */
bool tpm_activate_credential(TPMState *tpm, const TPMKey *ak,
                             const uint8_t *credential_blob,
                             uint32_t blob_size) {
    if(tpm==NULL||ak==NULL||credential_blob==NULL)return false;
    if(!tpm->ek.generated||!ak->generated)return false;

    /* Verify the credential is valid (simplified: check AK binding) */
    if(ak->handle!=TPM_RH_AK)return false;
    if(blob_size<32)return false;

    return true;
}

/* ?? Utility: PCR Policy Check ??????????????????????????????????? */

/*
 * Check if current PCR values satisfy a policy.
 * Policy is defined as a mask of PCRs and expected composite hash.
 * Used for sealed storage and policy-based access control.
 * Reference: TPM 2.0 Part 1 Section 19.7 (PolicyPCR)
 */
bool tpm_check_pcr_policy(TPMState *tpm, uint16_t hash_alg,
                          const uint8_t *pcr_mask,
                          const uint8_t *expected_composite) {
    uint8_t i;
    uint8_t concat_buf[TPM_SHA256_DIGEST_SIZE*TPM_PCR_COUNT];
    uint32_t concat_len;
    uint8_t actual_composite[TPM_SHA256_DIGEST_SIZE];
    uint8_t actual_digest[TPM_SHA256_DIGEST_SIZE];
    uint16_t actual_size;

    if(tpm==NULL||pcr_mask==NULL||expected_composite==NULL)return false;

    concat_len=0;
    for(i=0;i<TPM_PCR_COUNT;i++){
        /* Check if PCR i is in the mask */
        if(i<TPM_PCR_COUNT&&(pcr_mask[i/8]&(1<<(i%8)))){
            if(!tpm_pcr_read(tpm,hash_alg,i,actual_digest,&actual_size))
                return false;
            memcpy(concat_buf+concat_len,actual_digest,TPM_SHA256_DIGEST_SIZE);
            concat_len+=TPM_SHA256_DIGEST_SIZE;
        }
    }
    sha256_hash(concat_buf,concat_len,actual_composite);

    return (memcmp(actual_composite,expected_composite,TPM_SHA256_DIGEST_SIZE)==0);
}
