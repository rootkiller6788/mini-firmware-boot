#ifndef TPM_LOCALITY_H
#define TPM_LOCALITY_H

#include <stdbool.h>
#include <stdint.h>
#include "tpm2_structs.h"

#define MAX_SESSION_HANDLES  8
#define NONCE_SIZE           32
#define MAX_AUTH_VALUE_SIZE  32
#define PCR_POLICY_MAX       8

typedef enum {
    TPM_LOCALITY_0 = 0,
    TPM_LOCALITY_1 = 1,
    TPM_LOCALITY_2 = 2,
    TPM_LOCALITY_3 = 3,
    TPM_LOCALITY_4 = 4,
} TPMLocality;

typedef enum {
    TPM_AUTH_PASSWORD = 0,
    TPM_AUTH_POLICY   = 1,
    TPM_AUTH_HMAC     = 2,
    TPM_AUTH_SESS     = 3,
} TPMAuthType;

typedef enum {
    TPM_SE_HMAC    = 0x00,
    TPM_SE_POLICY  = 0x01,
    TPM_SE_TRIAL   = 0x03,
} TPM_SE;

typedef struct {
    uint32_t      session_handle;
    TPMAuthType   auth_type;
    uint8_t       nonce_caller[NONCE_SIZE];
    uint8_t       nonce_tpm[NONCE_SIZE];
    uint32_t      bind_entity;
    uint8_t       session_key[32];
    bool          is_active;
    bool          is_trial;
    uint8_t       policy_digest[SHA256_DIGEST_SIZE];
    bool          policy_approved;
} TPMSession;

typedef struct {
    uint32_t     session_handle;
    uint8_t      nonce[NONCE_SIZE];
    uint8_t      session_attributes;
    uint8_t      auth_value[MAX_AUTH_VALUE_SIZE];
    uint16_t     auth_value_size;
} TPMAuthCommand;

typedef struct {
    uint8_t  policy[SHA256_DIGEST_SIZE];
    bool     is_set;
} TPMSPolicy;

typedef struct {
    TPMLocality   current_locality;
    TPMSession    sessions[MAX_SESSION_HANDLES];
    uint32_t      session_count;
    TPMSPolicy    policy;
    bool          lockout_active;
    uint32_t      lockout_counter;
    uint32_t      max_auth_fail;
} TPMAuthManager;

void     tpm_auth_manager_init(TPMAuthManager* mgr);
bool     tpm_start_auth_session(TPMAuthManager* mgr, uint32_t* session_handle,
                                TPMAuthType auth_type, TPM_SE session_type);
bool     tpm_policy_pcr(TPMAuthManager* mgr, uint32_t session_handle,
                        const uint8_t* pcr_digest, uint32_t pcr_count);
bool     tpm_policy_secret(TPMAuthManager* mgr, uint32_t session_handle,
                           const uint8_t* policy_ref, size_t ref_size);
bool     tpm_policy_password(TPMAuthManager* mgr, uint32_t session_handle);
bool     tpm_policy_or(TPMAuthManager* mgr, uint32_t session_handle,
                       const uint8_t* policy_digest, uint32_t count);
bool     tpm_flush_context(TPMAuthManager* mgr, uint32_t session_handle);
bool     tpm_check_auth_value(const uint8_t* expected, size_t expected_len,
                              const uint8_t* provided, size_t provided_len);
bool     tpm_set_locality(TPMAuthManager* mgr, TPMLocality locality);
TPMLocality tpm_get_locality(const TPMAuthManager* mgr);
bool     tpm_authorize_command(TPMAuthManager* mgr, const TPMAuthCommand* cmd,
                               const uint8_t* required_auth, size_t auth_len);
void     tpm_session_print(const TPMSession* session);
void     tpm_auth_manager_print(const TPMAuthManager* mgr);
const char* tpm_locality_to_string(TPMLocality locality);
const char* tpm_auth_type_to_string(TPMAuthType auth_type);

#endif
