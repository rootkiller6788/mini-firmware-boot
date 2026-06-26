#ifndef ROOT_OF_TRUST_H
#define ROOT_OF_TRUST_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define ROT_MAX_KEY_HASH       32
#define ROT_MAX_DEVICE_ID      32
#define ROT_MAX_SECRET_SIZE    32
#define ROT_MAX_STAGE_SIZE     (64 * 1024)
#define ROT_ANTI_ROLLBACK_MAX  0xFFFFFFFF

typedef enum {
    ROT_TYPE_ROM_BASED = 0,
    ROT_TYPE_EFUSE,
    ROT_TYPE_PUF_BASED,
    ROT_TYPE_COUNT
} RoTType;

typedef enum {
    ROT_POLICY_LOCKED = 0,
    ROT_POLICY_UNLOCKED,
    ROT_POLICY_TAMPERED
} BootPolicy;

typedef enum {
    ROT_VERIFY_OK = 0,
    ROT_VERIFY_SIG_MISMATCH,
    ROT_VERIFY_ROLLBACK_BLOCKED,
    ROT_VERIFY_HASH_MISMATCH,
    ROT_VERIFY_DEVICE_LOCKED,
    ROT_VERIFY_IMAGE_CORRUPT
} RoTVerifyResult;

typedef struct {
    uint8_t  public_key_hash[ROT_MAX_KEY_HASH];
    BootPolicy boot_policy;
    uint32_t anti_rollback_counter;
    uint8_t  device_secret[ROT_MAX_SECRET_SIZE];
    uint8_t  device_id[ROT_MAX_DEVICE_ID];
    RoTType  rot_type;
    bool     initialized;
} RootOfTrust;

bool rot_init(RootOfTrust *rot, RoTType type);
RoTVerifyResult rot_verify_first_stage(const RootOfTrust *rot,
                                        const uint8_t *stage_data,
                                        uint32_t stage_size,
                                        const uint8_t *signature,
                                        uint32_t sig_size,
                                        uint32_t version);
bool rot_lock_device(RootOfTrust *rot, const uint8_t *pk_hash);
bool rot_is_locked(const RootOfTrust *rot);
void rot_get_device_id(const RootOfTrust *rot, char *id_str, uint32_t max_len);
bool rot_increment_counter(RootOfTrust *rot);
bool rot_check_counter(const RootOfTrust *rot, uint32_t min_required);
const char *rot_verify_result_str(RoTVerifyResult result);

bool rot_derive_device_secret(RootOfTrust *rot, const uint8_t *seed, uint32_t seed_len);
bool rot_verify_chained(RootOfTrust *rot, const uint8_t *stages[],
                         const uint32_t *stage_sizes, uint32_t stage_count);

#endif /* ROOT_OF_TRUST_H */
