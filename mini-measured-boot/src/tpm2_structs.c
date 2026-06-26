#include <stdio.h>
#include <string.h>
#include "tpm2_structs.h"

const char* tpm2_alg_id_to_string(TPM2_ALG_ID alg) {
    switch (alg) {
        case TPM_ALG_RSA:      return "RSA";
        case TPM_ALG_SHA1:     return "SHA1";
        case TPM_ALG_HMAC:     return "HMAC";
        case TPM_ALG_AES:      return "AES";
        case TPM_ALG_MGF1:     return "MGF1";
        case TPM_ALG_KEYEDHASH: return "KEYEDHASH";
        case TPM_ALG_XOR:      return "XOR";
        case TPM_ALG_SHA256:   return "SHA256";
        case TPM_ALG_SHA384:   return "SHA384";
        case TPM_ALG_SHA512:   return "SHA512";
        case TPM_ALG_NULL:     return "NULL";
        case TPM_ALG_RSASSA:   return "RSASSA";
        case TPM_ALG_RSAPSS:   return "RSAPSS";
        case TPM_ALG_OAEP:     return "OAEP";
        case TPM_ALG_ECDSA:    return "ECDSA";
        case TPM_ALG_ECDH:     return "ECDH";
        case TPM_ALG_ECDAA:    return "ECDAA";
        case TPM_ALG_ECC:      return "ECC";
        default:               return "UNKNOWN";
    }
}

const char* tpm2_st_to_string(TPM_ST st) {
    switch (st) {
        case TPM_ST_ATTEST_QUOTE:    return "ATTEST_QUOTE";
        case TPM_ST_ATTEST_CREATION: return "ATTEST_CREATION";
        case TPM_ST_ATTEST_NV:       return "ATTEST_NV";
        case TPM_ST_ATTEST_TIME:     return "ATTEST_TIME";
        default:                     return "UNKNOWN";
    }
}

const char* tpm2_rc_to_string(TPM_RC rc) {
    switch (rc) {
        case TPM_RC_SUCCESS:     return "SUCCESS";
        case TPM_RC_BAD_TAG:     return "BAD_TAG";
        case TPM_RC_INITIALIZE:  return "INITIALIZE";
        case TPM_RC_FAILURE:     return "FAILURE";
        case TPM_RC_SEQUENCE:    return "SEQUENCE_ERROR";
        case TPM_RC_VALUE:       return "VALUE_ERROR";
        case TPM_RC_PCR_CHANGED: return "PCR_CHANGED";
        case TPM_RC_BAD_AUTH:    return "BAD_AUTH";
        case TPM_RC_LOCKOUT:     return "LOCKOUT";
        case TPM_RC_PCR:         return "PCR_ERROR";
        default:                 return "UNKNOWN_RC";
    }
}

uint16_t tpm2_hash_size(TPMI_ALG_HASH alg) {
    switch (alg) {
        case TPM_ALG_SHA1:   return SHA1_DIGEST_SIZE;
        case TPM_ALG_SHA256: return SHA256_DIGEST_SIZE;
        case TPM_ALG_SHA384: return SHA384_DIGEST_SIZE;
        case TPM_ALG_SHA512: return SHA512_DIGEST_SIZE;
        default:             return 0;
    }
}
