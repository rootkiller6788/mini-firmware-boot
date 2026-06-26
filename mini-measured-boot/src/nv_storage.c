/* TPM 2.0 NV Storage Implementation
 * L1: NV index descriptor allocation and management
 * L2: NV authorization model: three-tier access
 * L3: NV index lifecycle with attribute evolution
 * L4: Access control matrix enforcement per TCG PTP 8.4
 * L5: NV counter monotonic increment, NV extend chained hash
 * L7: UEFI variable emulation using NV storage
 */
#include <string.h>
#include <stdio.h>
#include "sha256.h"
#include "nv_storage.h"

const char* nv_type_to_string(NVType t) {
    switch (t) {
        case NV_TYPE_ORDINARY: return "ORDINARY";
        case NV_TYPE_COUNTER:  return "COUNTER";
        case NV_TYPE_BITFIELD: return "BITFIELD";
        case NV_TYPE_EXTEND:   return "EXTEND";
        case NV_TYPE_PIN_PASS: return "PIN_PASS";
        case NV_TYPE_PIN_FAIL: return "PIN_FAIL";
        default:               return "UNKNOWN";
    }
}

const char* nv_auth_type_to_string(NVAuthType a) {
    switch (a) {
        case NV_AUTH_OWNER:    return "OWNER";
        case NV_AUTH_PLATFORM: return "PLATFORM";
        case NV_AUTH_INDEX:    return "INDEX";
        default:               return "UNKNOWN";
    }
}
static int nv_find_index(const NVStorage* nv, uint32_t handle) {
    uint32_t i;
    if (nv == NULL) return -1;
    for (i = 0; i < nv->index_count; i++) {
        if (nv->indices[i].defined && nv->indices[i].handle == handle) {
            return (int)i;
        }
    }
    return -1;
}

static bool nv_check_read_access(const NVIndex* idx, NVAuthType auth,
                                  const uint8_t* auth_value, uint16_t auth_size) {
    if (idx->read_locked) return false;
    if (auth == NV_AUTH_OWNER && (idx->attributes & NV_ATTR_OWNERREAD))
        return true;
    if (auth == NV_AUTH_PLATFORM && (idx->attributes & NV_ATTR_PPREAD))
        return true;
    if (auth == NV_AUTH_INDEX && (idx->attributes & NV_ATTR_AUTHREAD)) {
        if (auth_value && idx->auth_value_size == auth_size &&
            memcmp(idx->auth_value, auth_value, auth_size) == 0)
            return true;
    }
    return false;
}

static bool nv_check_write_access(const NVIndex* idx, NVAuthType auth,
                                   const uint8_t* auth_value, uint16_t auth_size) {
    if (idx->write_locked) return false;
    if (idx->attributes & NV_ATTR_WRITELOCKED) return false;
    if (auth == NV_AUTH_OWNER && (idx->attributes & NV_ATTR_OWNERWRITE))
        return true;
    if (auth == NV_AUTH_PLATFORM && (idx->attributes & NV_ATTR_PPWRITE))
        return true;
    if (auth == NV_AUTH_INDEX && (idx->attributes & NV_ATTR_AUTHWRITE)) {
        if (auth_value && idx->auth_value_size == auth_size &&
            memcmp(idx->auth_value, auth_value, auth_size) == 0)
            return true;
    }
    return false;
}

void nv_storage_init(NVStorage* nv) {
    uint32_t i;
    if (nv == NULL) return;
    memset(nv, 0, sizeof(NVStorage));
    nv->index_count = 0;
    nv->global_lock = false;
    nv->owner_auth_handle = 0x40000001;
    for (i = 0; i < NV_MAX_INDICES; i++) {
        nv->indices[i].defined = false;
    }
}

bool nv_define_space(NVStorage* nv,
                     uint32_t handle, NVType nv_type,
                     uint32_t attributes, uint16_t data_size,
                     const uint8_t* auth_value, uint16_t auth_size,
                     const uint8_t* policy, uint32_t* out_slot) {
    int existing;
    uint32_t slot;

    if (nv == NULL) return false;
    if (data_size > NV_MAX_DATA_SIZE) return false;
    if (auth_size > 32) return false;
    if (nv->index_count >= NV_MAX_INDICES) return false;

    existing = nv_find_index(nv, handle);
    if (existing >= 0) return false;

    if ((handle & 0xFF000000) != NV_INDEX_BASE) return false;

    slot = nv->index_count;
    NVIndex* idx = &nv->indices[slot];
    memset(idx, 0, sizeof(NVIndex));

    idx->handle      = handle;
    idx->nv_type     = nv_type;
    idx->attributes  = attributes;
    idx->data_size   = data_size;
    idx->data_len    = 0;
    idx->write_count = 0;
    idx->read_locked = false;
    idx->write_locked = false;
    idx->defined     = true;

    if (auth_value && auth_size > 0) {
        idx->auth_value_size = auth_size;
        memcpy(idx->auth_value, auth_value, auth_size);
    }
    if (policy) {
        memcpy(idx->policy_digest, policy, SHA256_DIGEST_SIZE);
        idx->policy_defined = true;
    }

    nv->index_count++;
    if (out_slot) *out_slot = slot;
    return true;
}
bool nv_undefine_space(NVStorage* nv, uint32_t handle, NVAuthType auth) {
    int idx_pos;
    uint32_t i;

    if (nv == NULL) return false;
    idx_pos = nv_find_index(nv, handle);
    if (idx_pos < 0) return false;

    NVIndex* idx = &nv->indices[idx_pos];

    if (idx->attributes & NV_ATTR_POLICY_DELETE) return false;
    if ((idx->attributes & NV_ATTR_PLATFORMCREATE) && auth != NV_AUTH_PLATFORM)
        return false;

    memset(idx, 0, sizeof(NVIndex));
    for (i = (uint32_t)idx_pos; i < nv->index_count - 1; i++) {
        nv->indices[i] = nv->indices[i + 1];
    }
    memset(&nv->indices[nv->index_count - 1], 0, sizeof(NVIndex));
    nv->index_count--;
    return true;
}

bool nv_read(NVStorage* nv, uint32_t handle,
             uint8_t* data_out, uint16_t* data_len_out,
             NVAuthType auth, const uint8_t* auth_value, uint16_t auth_size) {
    int idx_pos;

    if (nv == NULL || data_out == NULL || data_len_out == NULL) return false;
    idx_pos = nv_find_index(nv, handle);
    if (idx_pos < 0) return false;

    NVIndex* idx = &nv->indices[idx_pos];
    if (!nv_check_read_access(idx, auth, auth_value, auth_size)) return false;

    if (idx->attributes & NV_ATTR_READ_STCLEAR) {
        if (idx->read_locked) return false;
        idx->read_locked = true;
    }

    *data_len_out = idx->data_len;
    memcpy(data_out, idx->data, idx->data_len);
    return true;
}

bool nv_write(NVStorage* nv, uint32_t handle,
              const uint8_t* data, uint16_t data_len,
              NVAuthType auth, const uint8_t* auth_value, uint16_t auth_size) {
    int idx_pos;

    if (nv == NULL || data == NULL) return false;
    idx_pos = nv_find_index(nv, handle);
    if (idx_pos < 0) return false;

    NVIndex* idx = &nv->indices[idx_pos];

    if (idx->nv_type == NV_TYPE_COUNTER ||
        idx->nv_type == NV_TYPE_EXTEND ||
        idx->nv_type == NV_TYPE_BITFIELD) return false;

    if (nv->global_lock && (idx->attributes & NV_ATTR_GLOBALLOCK)) return false;
    if ((idx->attributes & NV_ATTR_WRITEALL) && data_len != idx->data_size)
        return false;
    if (data_len > idx->data_size) return false;
    if (!nv_check_write_access(idx, auth, auth_value, auth_size)) return false;
    if ((idx->attributes & NV_ATTR_WRITEDEFINE) && idx->write_count > 0)
        return false;

    memcpy(idx->data, data, data_len);
    idx->data_len = data_len;
    idx->write_count++;
    idx->attributes |= NV_ATTR_WRITTEN;
    return true;
}
bool nv_counter_increment(NVStorage* nv, uint32_t handle, uint32_t* new_value) {
    int idx_pos;
    uint32_t old_val;

    if (nv == NULL || new_value == NULL) return false;
    idx_pos = nv_find_index(nv, handle);
    if (idx_pos < 0) return false;

    NVIndex* idx = &nv->indices[idx_pos];
    if (idx->nv_type != NV_TYPE_COUNTER) return false;
    if (idx->write_locked) return false;

    old_val = 0;
    if (idx->data_len >= 4) {
        old_val = ((uint32_t)idx->data[0] << 24) |
                  ((uint32_t)idx->data[1] << 16) |
                  ((uint32_t)idx->data[2] << 8)  |
                  ((uint32_t)idx->data[3]);
    }

    if (old_val >= NV_COUNTER_MAX) return false;
    *new_value = old_val + 1;

    idx->data[0] = (uint8_t)((*new_value >> 24) & 0xFF);
    idx->data[1] = (uint8_t)((*new_value >> 16) & 0xFF);
    idx->data[2] = (uint8_t)((*new_value >> 8) & 0xFF);
    idx->data[3] = (uint8_t)(*new_value & 0xFF);
    idx->data_len = 4;
    idx->write_count++;
    idx->attributes |= NV_ATTR_WRITTEN;
    return true;
}

bool nv_extend(NVStorage* nv, uint32_t handle,
               const uint8_t* data, uint16_t data_len,
               NVAuthType auth, const uint8_t* auth_value, uint16_t auth_size) {
    int idx_pos;
    uint8_t combined[SHA256_DIGEST_SIZE * 2];
    uint8_t data_hash[SHA256_DIGEST_SIZE];
    uint8_t result[SHA256_DIGEST_SIZE];

    if (nv == NULL || data == NULL) return false;
    idx_pos = nv_find_index(nv, handle);
    if (idx_pos < 0) return false;

    NVIndex* idx = &nv->indices[idx_pos];
    if (idx->nv_type != NV_TYPE_EXTEND) return false;
    if (!nv_check_write_access(idx, auth, auth_value, auth_size)) return false;
    if (nv->global_lock && (idx->attributes & NV_ATTR_GLOBALLOCK)) return false;

    memcpy(combined, idx->data, SHA256_DIGEST_SIZE);
    sha256_hash(data, data_len, data_hash);
    memcpy(combined + SHA256_DIGEST_SIZE, data_hash, SHA256_DIGEST_SIZE);
    sha256_hash(combined, SHA256_DIGEST_SIZE * 2, result);

    memcpy(idx->data, result, SHA256_DIGEST_SIZE);
    idx->data_len = SHA256_DIGEST_SIZE;
    idx->write_count++;
    idx->attributes |= NV_ATTR_WRITTEN;
    return true;
}

bool nv_setbits(NVStorage* nv, uint32_t handle, uint64_t bits,
                NVAuthType auth, const uint8_t* auth_value, uint16_t auth_size) {
    int idx_pos;
    uint64_t current;
    uint32_t i;

    if (nv == NULL) return false;
    idx_pos = nv_find_index(nv, handle);
    if (idx_pos < 0) return false;

    NVIndex* idx = &nv->indices[idx_pos];
    if (idx->nv_type != NV_TYPE_BITFIELD) return false;
    if (!nv_check_write_access(idx, auth, auth_value, auth_size)) return false;
    if (nv->global_lock && (idx->attributes & NV_ATTR_GLOBALLOCK)) return false;

    current = 0;
    if (idx->data_len >= 8) {
        for (i = 0; i < 8; i++) {
            current = (current << 8) | idx->data[i];
        }
    }

    current |= bits;

    {
        uint64_t tmp = current;
        for (i = 0; i < 8; i++) {
            idx->data[7 - i] = (uint8_t)(tmp & 0xFF);
            tmp >>= 8;
        }
    }
    idx->data_len = 8;
    idx->write_count++;
    idx->attributes |= NV_ATTR_WRITTEN;
    return true;
}
void nv_global_lock(NVStorage* nv) {
    if (nv == NULL) return;
    nv->global_lock = true;
}

bool nv_read_lock(NVStorage* nv, uint32_t handle) {
    int idx_pos;
    if (nv == NULL) return false;
    idx_pos = nv_find_index(nv, handle);
    if (idx_pos < 0) return false;
    nv->indices[idx_pos].read_locked = true;
    nv->indices[idx_pos].attributes |= NV_ATTR_READLOCKED;
    return true;
}

bool nv_write_lock(NVStorage* nv, uint32_t handle) {
    int idx_pos;
    if (nv == NULL) return false;
    idx_pos = nv_find_index(nv, handle);
    if (idx_pos < 0) return false;
    nv->indices[idx_pos].write_locked = true;
    nv->indices[idx_pos].attributes |= NV_ATTR_WRITELOCKED;
    return true;
}

bool nv_change_auth(NVStorage* nv, uint32_t handle,
                    const uint8_t* old_auth, uint16_t old_size,
                    const uint8_t* new_auth, uint16_t new_size) {
    int idx_pos;

    if (nv == NULL || old_auth == NULL || new_auth == NULL) return false;
    if (new_size > 32) return false;

    idx_pos = nv_find_index(nv, handle);
    if (idx_pos < 0) return false;

    NVIndex* idx = &nv->indices[idx_pos];
    if (idx->auth_value_size != old_size) return false;
    if (memcmp(idx->auth_value, old_auth, old_size) != 0) return false;

    memcpy(idx->auth_value, new_auth, new_size);
    idx->auth_value_size = new_size;
    return true;
}

bool nv_define_uefi_variable(NVStorage* nv, uint32_t handle,
                             const uint8_t* name_hash, uint16_t data_size) {
    uint32_t attrs;
    uint32_t slot;

    if (nv == NULL || name_hash == NULL) return false;

    attrs = NV_ATTR_PPREAD | NV_ATTR_AUTHWRITE |
            NV_ATTR_WRITTEN | NV_ATTR_GLOBALLOCK | NV_ATTR_ORDERLY;

    return nv_define_space(nv, handle, NV_TYPE_ORDINARY, attrs,
                           data_size, name_hash, SHA256_DIGEST_SIZE,
                           NULL, &slot);
}

uint32_t nv_get_index_count(const NVStorage* nv) {
    if (nv == NULL) return 0;
    return nv->index_count;
}

const NVIndex* nv_get_index(const NVStorage* nv, uint32_t slot) {
    if (nv == NULL || slot >= nv->index_count) return NULL;
    if (!nv->indices[slot].defined) return NULL;
    return &nv->indices[slot];
}
void nv_storage_print(const NVStorage* nv) {
    uint32_t i, j;
    if (nv == NULL) return;

    printf("=== TPM NV Storage (%u indices) ===\n", nv->index_count);
    printf("  Global Lock: %s\n", nv->global_lock ? "ACTIVE" : "inactive");

    for (i = 0; i < nv->index_count; i++) {
        const NVIndex* idx = &nv->indices[i];
        if (!idx->defined) continue;

        printf("\n  [%u] NV Index 0x%08X\n", i, idx->handle);
        printf("      Type:       %s\n", nv_type_to_string(idx->nv_type));
        printf("      Data Size:  %u / %u\n", idx->data_len, idx->data_size);
        printf("      Written:    %s (count=%u)\n",
               (idx->attributes & NV_ATTR_WRITTEN) ? "yes" : "no",
               idx->write_count);
        printf("      Read Lock:  %s\n", idx->read_locked ? "LOCKED" : "open");
        printf("      Write Lock: %s\n", idx->write_locked ? "LOCKED" : "open");
        printf("      Policy:     %s\n", idx->policy_defined ? "defined" : "none");

        if (idx->data_len > 0) {
            printf("      Data:       ");
            for (j = 0; j < idx->data_len && j < 16; j++) {
                printf("%02x", idx->data[j]);
            }
            if (idx->data_len > 16) printf("...");
            printf("\n");
        }

        printf("      Attrs:      ");
        if (idx->attributes & NV_ATTR_OWNERREAD)  printf("OR ");
        if (idx->attributes & NV_ATTR_OWNERWRITE) printf("OW ");
        if (idx->attributes & NV_ATTR_AUTHREAD)   printf("AR ");
        if (idx->attributes & NV_ATTR_AUTHWRITE)  printf("AW ");
        if (idx->attributes & NV_ATTR_POLICYREAD) printf("PR ");
        if (idx->attributes & NV_ATTR_POLICYWRITE) printf("PW ");
        if (idx->attributes & NV_ATTR_GLOBALLOCK) printf("GL ");
        if (idx->attributes & NV_ATTR_ORDERLY)    printf("ORD ");
        if (idx->attributes & NV_ATTR_PLATFORMCREATE) printf("PC ");
        printf("\n");
    }
}
