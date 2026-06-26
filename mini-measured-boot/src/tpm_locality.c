#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "sha256.h"
#include "tpm_locality.h"

void tpm_auth_manager_init(TPMAuthManager* mgr) {
    uint32_t i;
    if (mgr == NULL) return;
    mgr->current_locality = TPM_LOCALITY_0;
    mgr->session_count = 0;
    mgr->policy.is_set = false;
    mgr->lockout_active = false;
    mgr->lockout_counter = 0;
    mgr->max_auth_fail = 5;

    memset(mgr->policy.policy, 0, SHA256_DIGEST_SIZE);
    for (i = 0; i < MAX_SESSION_HANDLES; i++) {
        memset(&mgr->sessions[i], 0, sizeof(TPMSession));
        mgr->sessions[i].is_active = false;
    }
}

bool tpm_start_auth_session(TPMAuthManager* mgr, uint32_t* session_handle,
                            TPMAuthType auth_type, TPM_SE session_type) {
    uint32_t i;

    if (mgr == NULL || session_handle == NULL) return false;
    if (mgr->session_count >= MAX_SESSION_HANDLES) return false;

    for (i = 0; i < MAX_SESSION_HANDLES; i++) {
        if (!mgr->sessions[i].is_active) {
            *session_handle = 0x03000000 + i;
            mgr->sessions[i].session_handle = *session_handle;
            mgr->sessions[i].auth_type = auth_type;
            mgr->sessions[i].is_active = true;
            mgr->sessions[i].is_trial = (session_type == TPM_SE_TRIAL);
            mgr->sessions[i].policy_approved = false;
            mgr->sessions[i].bind_entity = 0;

            for (int j = 0; j < NONCE_SIZE; j++) {
                mgr->sessions[i].nonce_caller[j] = (uint8_t)(rand() & 0xFF);
                mgr->sessions[i].nonce_tpm[j] = (uint8_t)(rand() & 0xFF);
            }

            mgr->session_count++;
            return true;
        }
    }
    return false;
}

bool tpm_policy_pcr(TPMAuthManager* mgr, uint32_t session_handle,
                    const uint8_t* pcr_digest, uint32_t pcr_count) {
    uint32_t i;
    uint8_t policy_input[SHA256_DIGEST_SIZE * 2];

    if (mgr == NULL || pcr_digest == NULL) return false;

    for (i = 0; i < MAX_SESSION_HANDLES; i++) {
        if (mgr->sessions[i].is_active &&
            mgr->sessions[i].session_handle == session_handle) {

            memcpy(policy_input, mgr->sessions[i].policy_digest, SHA256_DIGEST_SIZE);
            memcpy(policy_input + SHA256_DIGEST_SIZE, pcr_digest,
                   pcr_count * SHA256_DIGEST_SIZE);
            sha256_hash(policy_input, SHA256_DIGEST_SIZE + pcr_count * SHA256_DIGEST_SIZE,
                        mgr->sessions[i].policy_digest);
            return true;
        }
    }
    return false;
}

bool tpm_policy_secret(TPMAuthManager* mgr, uint32_t session_handle,
                       const uint8_t* policy_ref, size_t ref_size) {
    uint32_t i;
    uint8_t policy_input[SHA256_DIGEST_SIZE * 2];

    if (mgr == NULL || policy_ref == NULL) return false;

    for (i = 0; i < MAX_SESSION_HANDLES; i++) {
        if (mgr->sessions[i].is_active &&
            mgr->sessions[i].session_handle == session_handle) {

            memcpy(policy_input, mgr->sessions[i].policy_digest, SHA256_DIGEST_SIZE);
            sha256_hash(policy_ref, ref_size, policy_input + SHA256_DIGEST_SIZE);
            sha256_hash(policy_input, SHA256_DIGEST_SIZE * 2,
                        mgr->sessions[i].policy_digest);
            return true;
        }
    }
    return false;
}

bool tpm_policy_password(TPMAuthManager* mgr, uint32_t session_handle) {
    uint32_t i;

    if (mgr == NULL) return false;

    for (i = 0; i < MAX_SESSION_HANDLES; i++) {
        if (mgr->sessions[i].is_active &&
            mgr->sessions[i].session_handle == session_handle) {
            mgr->sessions[i].policy_approved = true;
            return true;
        }
    }
    return false;
}

bool tpm_policy_or(TPMAuthManager* mgr, uint32_t session_handle,
                   const uint8_t* policy_digest, uint32_t count) {
    uint32_t i;
    uint8_t combined[SHA256_DIGEST_SIZE * PCR_POLICY_MAX];

    if (mgr == NULL || policy_digest == NULL || count == 0) return false;
    if (count > PCR_POLICY_MAX) return false;

    for (i = 0; i < MAX_SESSION_HANDLES; i++) {
        if (mgr->sessions[i].is_active &&
            mgr->sessions[i].session_handle == session_handle) {

            memcpy(combined, mgr->sessions[i].policy_digest, SHA256_DIGEST_SIZE);
            memcpy(combined + SHA256_DIGEST_SIZE, policy_digest,
                   count * SHA256_DIGEST_SIZE);
            sha256_hash(combined, SHA256_DIGEST_SIZE * (1 + count),
                        mgr->sessions[i].policy_digest);
            return true;
        }
    }
    return false;
}

bool tpm_flush_context(TPMAuthManager* mgr, uint32_t session_handle) {
    uint32_t i;

    if (mgr == NULL) return false;

    for (i = 0; i < MAX_SESSION_HANDLES; i++) {
        if (mgr->sessions[i].is_active &&
            mgr->sessions[i].session_handle == session_handle) {
            memset(&mgr->sessions[i], 0, sizeof(TPMSession));
            mgr->sessions[i].is_active = false;
            mgr->session_count--;
            return true;
        }
    }
    return false;
}

bool tpm_check_auth_value(const uint8_t* expected, size_t expected_len,
                          const uint8_t* provided, size_t provided_len) {
    if (expected == NULL || provided == NULL) return false;
    if (expected_len != provided_len) return false;
    return memcmp(expected, provided, expected_len) == 0;
}

bool tpm_set_locality(TPMAuthManager* mgr, TPMLocality locality) {
    if (mgr == NULL) return false;
    if (locality > TPM_LOCALITY_4) return false;
    mgr->current_locality = locality;
    return true;
}

TPMLocality tpm_get_locality(const TPMAuthManager* mgr) {
    if (mgr == NULL) return TPM_LOCALITY_0;
    return mgr->current_locality;
}

bool tpm_authorize_command(TPMAuthManager* mgr, const TPMAuthCommand* cmd,
                           const uint8_t* required_auth, size_t auth_len) {
    if (mgr == NULL || cmd == NULL || required_auth == NULL) return false;

    if (mgr->lockout_active) return false;

    if (!tpm_check_auth_value(required_auth, auth_len,
                              cmd->auth_value, cmd->auth_value_size)) {
        mgr->lockout_counter++;
        if (mgr->lockout_counter >= mgr->max_auth_fail) {
            mgr->lockout_active = true;
        }
        return false;
    }

    mgr->lockout_counter = 0;
    return true;
}

const char* tpm_locality_to_string(TPMLocality locality) {
    switch (locality) {
        case TPM_LOCALITY_0: return "Locality 0 (TPM access)";
        case TPM_LOCALITY_1: return "Locality 1 (D-CRTM)";
        case TPM_LOCALITY_2: return "Locality 2 (S-CRTM)";
        case TPM_LOCALITY_3: return "Locality 3 (ACPI S3)";
        case TPM_LOCALITY_4: return "Locality 4 (CPU TXT)";
        default:             return "Unknown Locality";
    }
}

const char* tpm_auth_type_to_string(TPMAuthType auth_type) {
    switch (auth_type) {
        case TPM_AUTH_PASSWORD: return "Password";
        case TPM_AUTH_POLICY:   return "Policy";
        case TPM_AUTH_HMAC:     return "HMAC";
        case TPM_AUTH_SESS:     return "Session";
        default:                return "Unknown";
    }
}

void tpm_session_print(const TPMSession* session) {
    uint32_t i;
    if (session == NULL) return;
    if (!session->is_active) {
        printf("  Session: INACTIVE\n");
        return;
    }
    printf("  Session Handle:   0x%08x\n", session->session_handle);
    printf("  Auth Type:        %s\n", tpm_auth_type_to_string(session->auth_type));
    printf("  Trial Session:    %s\n", session->is_trial ? "yes" : "no");
    printf("  Policy Approved:  %s\n", session->policy_approved ? "yes" : "no");
    printf("  Policy Digest:    ");
    for (i = 0; i < 8; i++) printf("%02x", session->policy_digest[i]);
    printf("...\n");
    printf("  Nonce (caller):   ");
    for (i = 0; i < 8; i++) printf("%02x", session->nonce_caller[i]);
    printf("...\n");
}

void tpm_auth_manager_print(const TPMAuthManager* mgr) {
    uint32_t i;
    if (mgr == NULL) return;

    printf("=== TPM Auth Manager ===\n");
    printf("  Locality:         %s\n", tpm_locality_to_string(mgr->current_locality));
    printf("  Active Sessions:  %u/%u\n", mgr->session_count, MAX_SESSION_HANDLES);
    printf("  Lockout:          %s\n", mgr->lockout_active ? "ACTIVE" : "inactive");
    printf("  Auth Failures:    %u/%u\n", mgr->lockout_counter, mgr->max_auth_fail);

    for (i = 0; i < MAX_SESSION_HANDLES; i++) {
        if (mgr->sessions[i].is_active) {
            printf("\n");
            tpm_session_print(&mgr->sessions[i]);
        }
    }
}
