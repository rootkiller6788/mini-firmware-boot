#ifndef FIRMWARE_UPDATE_H
#define FIRMWARE_UPDATE_H

/*
 * firmware_update.h -- UEFI Capsule Update & Recovery
 *
 * Implements firmware update mechanisms:
 *   - UEFI Capsule Update (UEFI Spec ?8.5.5)
 *   - A/B Slot updates (Android/chromeOS style)
 *   - Firmware recovery with golden image
 *
 * Knowledge coverage:
 *   L1: FwUpdateCapsule, UpdateManifest, ABSlotConfig structs
 *   L2: Capsule update, A/B slot, recovery concepts
 *   L3: Update pipeline with authentication and rollback protection
 *   L6: Firmware update canonical problem (safe updates)
 *   L7: UEFI capsule update, Android A/B updates
 *   L8: Firmware attestation after update
 */

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "bootblock.h"

#define FW_SLOT_NAME_LEN   32
#define FW_UPDATE_MAX_PAYLOAD (16 * 1024 * 1024)
#define CAPSULE_GUID_LEN   16
#define MAX_UPDATE_HISTORY 8

/* ??? L1: A/B Slot Configuration ??????????????????????????????????? */

typedef enum {
    FW_SLOT_A = 0,
    FW_SLOT_B = 1,
    FW_SLOT_RECOVERY = 2
} FirmwareSlotID;

typedef enum {
    SLOT_STATUS_ACTIVE      = 0,  /* Currently booted slot       */
    SLOT_STATUS_STANDBY     = 1,  /* Standby (for next boot)     */
    SLOT_STATUS_UPDATING    = 2,  /* Being written               */
    SLOT_STATUS_UNBOOTABLE  = 3,  /* Failed to boot (>N attempts)*/
    SLOT_STATUS_INVALID     = 4   /* No valid firmware           */
} SlotStatus;

typedef struct {
    FirmwareSlotID id;
    char           name[FW_SLOT_NAME_LEN];
    uint64_t       base_offset;     /* Offset in flash              */
    uint64_t       size;            /* Slot size in bytes           */
    SlotStatus     status;          /* Current slot status          */
    uint32_t       version;         /* Firmware version in slot     */
    uint32_t       boot_attempts;   /* Consecutive failed boots     */
    uint32_t       successful_boots;/* Consecutive successful boots */
    bool           bootable;        /* Slot contains valid fw?      */
    SHA256Digest   fw_hash;         /* SHA-256 of slot contents     */
} FirmwareSlot;

/* ??? L1: A/B Slot Manager ???????????????????????????????????????? */

typedef struct {
    FirmwareSlot slots[3];          /* A, B, Recovery              */
    FirmwareSlotID active_slot;     /* Currently active slot       */
    uint32_t      max_boot_attempts;/* Before marking unbootable   */
    uint32_t      min_successful_boots;/* Before marking good      */
    bool          slot_a_priority;  /* Try slot A first?           */
} ABSlotManager;

/* ??? L1: UEFI Capsule Update ????????????????????????????????????? */

typedef enum {
    CAPSULE_TYPE_RAW        = 0,   /* Raw firmware binary          */
    CAPSULE_TYPE_SIGNED     = 1,   /* Signed firmware blob         */
    CAPSULE_TYPE_ENCRYPTED  = 2,   /* Encrypted (confidential)     */
    CAPSULE_TYPE_DELTA      = 3    /* Binary delta (bsdiff)        */
} CapsuleType;

typedef struct {
    uint8_t  guid[CAPSULE_GUID_LEN];
    uint32_t header_size;
    uint32_t flags;                /* PersistAcrossReset, etc.     */
    uint32_t capsule_image_size;   /* Total size including header  */
} CapsuleHeader;

typedef struct {
    CapsuleHeader header;
    CapsuleType   type;
    uint32_t      payload_size;    /* Payload size after header    */
    uint32_t      target_slot;     /* Which slot to write          */
    uint32_t      fw_version;      /* Version of new firmware      */
    SHA256Digest  payload_hash;    /* SHA-256 of payload           */
    RSASignature  signature;       /* Capsule signature            */
    /* Payload follows */
} FirmwareCapsule;

/* ??? L1: Update History ??????????????????????????????????????????? */

typedef struct {
    uint32_t    sequence;          /* Monotonic update sequence #  */
    uint32_t    from_version;      /* Previous firmware version    */
    uint32_t    to_version;        /* New firmware version         */
    FirmwareSlotID target_slot;    /* Which slot was updated       */
    bool        success;           /* Did update succeed?          */
    uint32_t    timestamp;         /* When update occurred         */
    char        reason[64];        /* Why: security, feature, bug  */
} UpdateHistoryEntry;

/* ??? L1: Firmware Update Manager ????????????????????????????????? */

typedef struct {
    ABSlotManager     slot_mgr;
    FirmwareCapsule   current_capsule;
    UpdateHistoryEntry history[MAX_UPDATE_HISTORY];
    uint32_t          history_count;
    bool              update_in_progress;
    uint32_t          update_sequence;    /* Monotonic counter      */
    bool              recovery_mode;      /* Booted into recovery?  */
    SHA256Digest      golden_image_hash;  /* Known-good hash        */
} FwUpdateManager;

/* ??? L2/L3: Update Manager API ??????????????????????????????????? */

/* Initialize firmware update subsystem with A/B slots */
bool fw_update_init(FwUpdateManager *mgr);

/* Configure A/B slot layout */
bool fw_update_setup_slots(FwUpdateManager *mgr,
                           uint64_t slot_a_offset, uint64_t slot_a_size,
                           uint64_t slot_b_offset, uint64_t slot_b_size,
                           uint64_t recovery_offset, uint64_t recovery_size);

/* ??? L3: Capsule Processing ????????????????????????????????????? */

/*
 * Validate a capsule update (header, signature, hash).
 * Returns true if capsule is valid and trustworthy.
 *
 * Security requirements:
 *   1. Capsule signature must verify against trusted key
 *   2. Target slot must exist and be in valid state
 *   3. Version must satisfy anti-rollback check
 *   4. Payload hash must match capsule header hash
 *
 * Reference: UEFI Spec 2.10 ?8.5.5 (UpdateCapsule)
 */
bool fw_update_validate_capsule(FwUpdateManager *mgr,
                                const FirmwareCapsule *capsule,
                                const PublicKeyFingerprint *key);

/* Write capsule payload to target slot */
bool fw_update_apply_capsule(FwUpdateManager *mgr);

/* ??? L3: A/B Slot Management ???????????????????????????????????? */

/* Mark the current boot as successful (called by OS after boot) */
bool fw_update_mark_boot_success(FwUpdateManager *mgr);

/* Mark the current boot as failed (trigger slot switch) */
bool fw_update_mark_boot_failed(FwUpdateManager *mgr);

/* Select the best slot for next boot */
FirmwareSlotID fw_update_select_boot_slot(const FwUpdateManager *mgr);

/* ??? L6: Recovery ??????????????????????????????????????????????? */

/* Enter recovery mode using the golden image */
bool fw_update_enter_recovery(FwUpdateManager *mgr);

/* Verify the golden image hash matches stored hash */
bool fw_update_verify_golden_image(const FwUpdateManager *mgr);

/* ??? L7: Diagnostics ???????????????????????????????????????????? */

void fw_update_print_slots(const FwUpdateManager *mgr);
void fw_update_print_history(const FwUpdateManager *mgr);

#endif /* FIRMWARE_UPDATE_H */
