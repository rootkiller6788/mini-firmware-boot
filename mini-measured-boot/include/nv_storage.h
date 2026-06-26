#ifndef NV_STORAGE_H
#define NV_STORAGE_H
#include <stdbool.h>
#include <stdint.h>
#include "tpm2_structs.h"

#define NV_MAX_INDICES        32
#define NV_MAX_DATA_SIZE      1024
#define NV_INDEX_BASE         0x01000000
#define NV_COUNTER_MAX        0xFFFFFFFF

typedef enum {
    NV_TYPE_ORDINARY   = 0x0,
    NV_TYPE_COUNTER    = 0x1,
    NV_TYPE_BITFIELD   = 0x2,
    NV_TYPE_EXTEND     = 0x4,
    NV_TYPE_PIN_PASS   = 0x8,
    NV_TYPE_PIN_FAIL   = 0x9,
} NVType;

typedef enum {
    NV_ATTR_OWNERREAD     = 0x00000001,
    NV_ATTR_OWNERWRITE    = 0x00000002,
    NV_ATTR_AUTHREAD      = 0x00000004,
    NV_ATTR_AUTHWRITE     = 0x00000008,
    NV_ATTR_POLICYREAD    = 0x00000010,
    NV_ATTR_POLICYWRITE   = 0x00000020,
    NV_ATTR_POLICY_DELETE = 0x00000400,
    NV_ATTR_WRITELOCKED   = 0x00000800,
    NV_ATTR_WRITEALL      = 0x00001000,
    NV_ATTR_WRITEDEFINE   = 0x00002000,
    NV_ATTR_WRITE_STCLEAR = 0x00004000,
    NV_ATTR_GLOBALLOCK    = 0x00008000,
    NV_ATTR_PPREAD        = 0x00010000,
    NV_ATTR_PPWRITE       = 0x00020000,
    NV_ATTR_PLATFORMCREATE = 0x00040000,
    NV_ATTR_READ_STCLEAR  = 0x00080000,
    NV_ATTR_READLOCKED    = 0x00100000,
    NV_ATTR_ORDERLY       = 0x00200000,
    NV_ATTR_CLEAR_STCLEAR = 0x00400000,
    NV_ATTR_WRITTEN       = 0x00800000,
    NV_ATTR_NO_DA         = 0x02000000,
} NVAttr;

typedef enum {
    NV_AUTH_OWNER    = 0,
    NV_AUTH_PLATFORM = 1,
    NV_AUTH_INDEX    = 2,
} NVAuthType;

typedef struct {
    uint32_t  handle;
    NVType    nv_type;
    uint32_t  attributes;
    uint16_t  data_size;
    uint8_t   auth_value[32];
    uint16_t  auth_value_size;
    uint8_t   policy_digest[32];
    bool      policy_defined;
    bool      defined;
    bool      read_locked;
    bool      write_locked;
    uint8_t   data[1024];
    uint16_t  data_len;
    uint32_t  write_count;
} NVIndex;

typedef struct {
    NVIndex   indices[32];
    uint32_t  index_count;
    bool      global_lock;
    uint32_t  owner_auth_handle;
} NVStorage;

void      nv_storage_init(NVStorage* nv);
bool      nv_define_space(NVStorage* nv, uint32_t handle, NVType nv_type, uint32_t attributes, uint16_t data_size, const uint8_t* auth_value, uint16_t auth_size, const uint8_t* policy, uint32_t* out_slot);
bool      nv_undefine_space(NVStorage* nv, uint32_t handle, NVAuthType auth);
bool      nv_read(NVStorage* nv, uint32_t handle, uint8_t* data_out, uint16_t* data_len_out, NVAuthType auth, const uint8_t* auth_value, uint16_t auth_size);
bool      nv_write(NVStorage* nv, uint32_t handle, const uint8_t* data, uint16_t data_len, NVAuthType auth, const uint8_t* auth_value, uint16_t auth_size);
bool      nv_counter_increment(NVStorage* nv, uint32_t handle, uint32_t* new_value);
bool      nv_extend(NVStorage* nv, uint32_t handle, const uint8_t* data, uint16_t data_len, NVAuthType auth, const uint8_t* auth_value, uint16_t auth_size);
bool      nv_setbits(NVStorage* nv, uint32_t handle, uint64_t bits, NVAuthType auth, const uint8_t* auth_value, uint16_t auth_size);
void      nv_global_lock(NVStorage* nv);
bool      nv_read_lock(NVStorage* nv, uint32_t handle);
bool      nv_write_lock(NVStorage* nv, uint32_t handle);
bool      nv_change_auth(NVStorage* nv, uint32_t handle, const uint8_t* old_auth, uint16_t old_size, const uint8_t* new_auth, uint16_t new_size);
bool      nv_define_uefi_variable(NVStorage* nv, uint32_t handle, const uint8_t* name_hash, uint16_t data_size);
uint32_t  nv_get_index_count(const NVStorage* nv);
const NVIndex* nv_get_index(const NVStorage* nv, uint32_t slot);
void      nv_storage_print(const NVStorage* nv);
const char* nv_type_to_string(NVType t);
const char* nv_auth_type_to_string(NVAuthType a);
#endif