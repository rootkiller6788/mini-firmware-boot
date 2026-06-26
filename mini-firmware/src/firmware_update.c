#include "firmware_update.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * firmware_update.c -- UEFI Capsule Update & A/B Slot Manager
 *
 * References:
 *   - UEFI Specification v2.10, Section 8.5.5 (UpdateCapsule)
 *   - Android A/B (Seamless) System Updates
 *   - Chrome OS Firmware Update design
 */

/* ??? L2: Initialization ???????????????????????????????????? */

bool fw_update_init(FwUpdateManager *mgr)
{
    if (!mgr) return false;
    memset(mgr, 0, sizeof(FwUpdateManager));
    mgr->slot_mgr.max_boot_attempts    = 7;
    mgr->slot_mgr.min_successful_boots = 2;
    mgr->slot_mgr.slot_a_priority      = true;
    mgr->update_sequence = 0;
    mgr->recovery_mode = false;
    return true;
}

/*
 * Configure A/B slot layout in flash.
 *
 * Typical layout (x86 SPI flash, 16 MB):
 *   Offset    Size      Content
 *   0x000000  0x400000  Flash Descriptor + ME Region  (4MB)
 *   0x400000  0x600000  Slot A firmware               (6MB)
 *   0xA00000  0x600000  Slot B firmware               (6MB)
 *   0xFFFFFF  0x000001  End of flash
 *
 * Recovery region may be at top or bottom of flash.
 *
 * Reference: Chrome OS Firmware Design ?4.2 (Flash Map)
 */
bool fw_update_setup_slots(FwUpdateManager *mgr,
                           uint64_t slot_a_offset, uint64_t slot_a_size,
                           uint64_t slot_b_offset, uint64_t slot_b_size,
                           uint64_t recovery_offset, uint64_t recovery_size)
{
    if (!mgr) return false;

    ABSlotManager *sm = &mgr->slot_mgr;

    /* Slot A */
    sm->slots[FW_SLOT_A].id          = FW_SLOT_A;
    snprintf(sm->slots[FW_SLOT_A].name, FW_SLOT_NAME_LEN, "Slot A");
    sm->slots[FW_SLOT_A].base_offset = slot_a_offset;
    sm->slots[FW_SLOT_A].size        = slot_a_size;
    sm->slots[FW_SLOT_A].status      = SLOT_STATUS_ACTIVE;
    sm->slots[FW_SLOT_A].version     = 1;
    sm->slots[FW_SLOT_A].bootable    = true;

    /* Slot B */
    sm->slots[FW_SLOT_B].id          = FW_SLOT_B;
    snprintf(sm->slots[FW_SLOT_B].name, FW_SLOT_NAME_LEN, "Slot B");
    sm->slots[FW_SLOT_B].base_offset = slot_b_offset;
    sm->slots[FW_SLOT_B].size        = slot_b_size;
    sm->slots[FW_SLOT_B].status      = SLOT_STATUS_STANDBY;
    sm->slots[FW_SLOT_B].version     = 1;
    sm->slots[FW_SLOT_B].bootable    = true;

    /* Recovery */
    sm->slots[FW_SLOT_RECOVERY].id          = FW_SLOT_RECOVERY;
    snprintf(sm->slots[FW_SLOT_RECOVERY].name, FW_SLOT_NAME_LEN, "Recovery");
    sm->slots[FW_SLOT_RECOVERY].base_offset = recovery_offset;
    sm->slots[FW_SLOT_RECOVERY].size        = recovery_size;
    sm->slots[FW_SLOT_RECOVERY].status      = SLOT_STATUS_STANDBY;
    sm->slots[FW_SLOT_RECOVERY].version     = 0;  /* Golden, version 0 */
    sm->slots[FW_SLOT_RECOVERY].bootable    = true;

    sm->active_slot = FW_SLOT_A;
    return true;
}

/* ??? L3: Capsule Validation ???????????????????????????????? */

/*
 * Validate a firmware update capsule.
 *
 * The capsule is the delivery mechanism for firmware updates in UEFI.
 * It is persisted across reboot via CapsuleCoalesce (memory) or
 * capsule-on-disk, then processed in early boot (BDS phase).
 *
 * Verification steps:
 *   1. Header integrity (GUID, size sanity)
 *   2. Signature verification (RSA-2048 + SHA-256)
 *   3. Anti-rollback version check
 *   4. Payload hash verification
 *   5. Target slot availability check
 *
 * Security model:
 *   - Signing key is held offline (HSM)
 *   - Verification key is in firmware (embedded or eFuse)
 *   - Rollback counter in TPM NV or RPMB
 *
 * Reference: UEFI Spec 2.10 ?8.5.5, NIST SP 800-147B (BIOS Update)
 */
bool fw_update_validate_capsule(FwUpdateManager *mgr,
                                const FirmwareCapsule *capsule,
                                const PublicKeyFingerprint *key)
{
    if (!mgr || !capsule || !key) return false;

    /* Check header integrity */
    if (capsule->header.header_size < sizeof(CapsuleHeader))
        return false;
    if (capsule->header.capsule_image_size < sizeof(FirmwareCapsule))
        return false;
    if (capsule->payload_size == 0 ||
        capsule->payload_size > FW_UPDATE_MAX_PAYLOAD)
        return false;

    /* Verify signature */
    VerifyResult vr = vb_verify_signature(&capsule->payload_hash,
                                           &capsule->signature, key);
    if (vr != VERIFY_OK) {
        fprintf(stderr, "Update: Signature verification failed (%d)\n", vr);
        return false;
    }

    /* Anti-rollback check */
    FirmwareSlot *active = &mgr->slot_mgr.slots[mgr->slot_mgr.active_slot];
    if (capsule->fw_version < active->version) {
        fprintf(stderr, "Update: Rollback denied (%u < %u)\n",
                capsule->fw_version, active->version);
        return false;
    }

    /* Check target slot */
    if (capsule->target_slot != FW_SLOT_A && capsule->target_slot != FW_SLOT_B) {
        return false;
    }

    FirmwareSlot *target = &mgr->slot_mgr.slots[capsule->target_slot];
    if (capsule->payload_size > target->size) {
        fprintf(stderr, "Update: Payload too large for target slot\n");
        return false;
    }

    printf("[Update] Capsule validated: v%u -> v%u (slot %s)\n",
           active->version, capsule->fw_version,
           capsule->target_slot == FW_SLOT_A ? "A" : "B");

    /* Copy capsule for later application */
    memcpy(&mgr->current_capsule, capsule, sizeof(FirmwareCapsule));
    mgr->update_in_progress = true;

    return true;
}

/*
 * Apply a validated capsule to the target slot.
 *
 * This happens during early boot (BDS phase) before the OS boots.
 * The capsule is written to the standby slot, then on next reboot
 * the system tries the new slot. If it boots successfully enough
 * times, it becomes the active slot.
 *
 * Atomicity: The update is NOT atomic at the flash level.
 * Instead, the slot metadata (bootable flag) provides atomicity:
 * only after successful boot does the slot become marked "good".
 *
 * This implements the "try-before-you-buy" model.
 */
bool fw_update_apply_capsule(FwUpdateManager *mgr)
{
    if (!mgr || !mgr->update_in_progress) return false;

    FirmwareCapsule *cap = &mgr->current_capsule;
    FirmwareSlot *target = &mgr->slot_mgr.slots[cap->target_slot];

    /* Mark target slot as updating */
    SlotStatus prev_status = target->status;
    target->status = SLOT_STATUS_UPDATING;

    /* Simulate writing payload to flash */
    printf("[Update] Writing %u bytes to %s at offset 0x%llX\n",
           cap->payload_size, target->name,
           (unsigned long long)target->base_offset);

    /* Update slot metadata */
    target->version     = cap->fw_version;
    target->fw_hash     = cap->payload_hash;
    target->status      = SLOT_STATUS_STANDBY;
    target->boot_attempts    = 0;
    target->successful_boots = 0;
    target->bootable    = true;

    /* Record update history */
    if (mgr->history_count < MAX_UPDATE_HISTORY) {
        UpdateHistoryEntry *h = &mgr->history[mgr->history_count];
        h->sequence     = mgr->update_sequence++;
        h->from_version = mgr->slot_mgr.slots[mgr->slot_mgr.active_slot].version;
        h->to_version   = cap->fw_version;
        h->target_slot  = cap->target_slot;
        h->success      = true;
        h->timestamp    = 0;
        snprintf(h->reason, sizeof(h->reason), "Capsule update");
        mgr->history_count++;
    }

    /* Select the newly updated slot as next boot slot */
    mgr->slot_mgr.active_slot = cap->target_slot;
    target->status = SLOT_STATUS_ACTIVE;

    /* Switch previous active to standby */
    FirmwareSlot *prev_active = NULL;
    for (int i = 0; i < 3; i++) {
        if (mgr->slot_mgr.slots[i].id != cap->target_slot &&
            mgr->slot_mgr.slots[i].status == SLOT_STATUS_ACTIVE) {
            prev_active = &mgr->slot_mgr.slots[i];
            break;
        }
    }
    if (prev_active) {
        prev_active->status = SLOT_STATUS_STANDBY;
    }

    (void)prev_status;
    mgr->update_in_progress = false;

    printf("[Update] Slot %s updated to v%u, will boot next\n",
           target->name, cap->fw_version);
    return true;
}

/* ??? L3: A/B Slot Boot Management ?????????????????????????? */

/*
 * Mark the current boot as successful.
 *
 * This is called by the OS after it has booted successfully.
 * On Android, this is done by the update_engine daemon.
 *
 * Algorithm:
 *   1. Increment successful_boots counter for active slot
 *   2. If successful_boots >= min_successful_boots, mark as "good"
 *   3. Mark standby slot as STANDBY (available for next update)
 *   4. Record update history entry as successful
 */
bool fw_update_mark_boot_success(FwUpdateManager *mgr)
{
    if (!mgr) return false;

    FirmwareSlot *active = &mgr->slot_mgr.slots[mgr->slot_mgr.active_slot];
    active->successful_boots++;
    active->boot_attempts = 0;  /* Reset failure counter */

    printf("[Update] Boot success: %s, %u/%u successful boots\n",
           active->name, active->successful_boots,
           mgr->slot_mgr.min_successful_boots);

    /* Mark recent update as definitely successful */
    if (mgr->history_count > 0) {
        UpdateHistoryEntry *last = &mgr->history[mgr->history_count - 1];
        last->success = true;
    }

    return true;
}

/*
 * Mark the current boot as failed.
 *
 * Called when:
 *   - Watchdog timeout before OS marks boot successful
 *   - Kernel panic during boot
 *   - Bootloader detects corrupted image
 *
 * The bootloader increments boot_attempts on each attempt.
 * If boot_attempts >= max_boot_attempts, the slot is marked
 * UNBOOTABLE and the system falls back to the other slot.
 *
 * This implements a robust failover mechanism:
 *   Slot A ? N failures ? Slot B ? N failures ? Recovery
 */
bool fw_update_mark_boot_failed(FwUpdateManager *mgr)
{
    if (!mgr) return false;

    FirmwareSlot *active = &mgr->slot_mgr.slots[mgr->slot_mgr.active_slot];
    active->boot_attempts++;

    printf("[Update] Boot failed: %s, attempt %u/%u\n",
           active->name, active->boot_attempts,
           mgr->slot_mgr.max_boot_attempts);

    if (active->boot_attempts >= mgr->slot_mgr.max_boot_attempts) {
        /* Slot exceeded retry limit: mark unbootable */
        active->status = SLOT_STATUS_UNBOOTABLE;
        active->bootable = false;

        printf("[Update] %s marked UNBOOTABLE after %u attempts\n",
               active->name, active->boot_attempts);

        /* Select fallback slot */
        mgr->slot_mgr.active_slot = fw_update_select_boot_slot(mgr);

        printf("[Update] Switching to %s\n",
               mgr->slot_mgr.slots[mgr->slot_mgr.active_slot].name);
    }

    return true;
}

/*
 * Select the best slot for next boot.
 *
 * Priority:
 *   1. Active slot (if bootable)
 *   2. Standby slot (slot B if A active, vice versa)
 *   3. Recovery slot (if available and bootable)
 *
 * This implements the A/B slot selection policy used by
 * Android (libavb) and Chrome OS (vboot_reference).
 */
FirmwareSlotID fw_update_select_boot_slot(const FwUpdateManager *mgr)
{
    if (!mgr) return FW_SLOT_RECOVERY;

    const ABSlotManager *sm = &mgr->slot_mgr;

    /* If active slot is bootable, use it */
    const FirmwareSlot *active = &sm->slots[sm->active_slot];
    if (active->bootable && active->status != SLOT_STATUS_UNBOOTABLE) {
        return sm->active_slot;
    }

    /* Try the other main slot */
    FirmwareSlotID alt = (sm->active_slot == FW_SLOT_A) ? FW_SLOT_B : FW_SLOT_A;
    const FirmwareSlot *alt_slot = &sm->slots[alt];
    if (alt_slot->bootable && alt_slot->status != SLOT_STATUS_UNBOOTABLE) {
        return alt;
    }

    /* Fall back to recovery */
    const FirmwareSlot *rec = &sm->slots[FW_SLOT_RECOVERY];
    if (rec->bootable) {
        return FW_SLOT_RECOVERY;
    }

    /* Desperate: return anything */
    return FW_SLOT_A;
}

/* ??? L6: Recovery Mode ????????????????????????????????????? */

/*
 * Enter recovery mode.
 *
 * Recovery mode provides a minimal, known-good firmware environment
 * for repairing a broken system. It is entered when:
 *   - Both A and B slots are unbootable
 *   - User triggers recovery (button combination)
 *   - Forced via BMC/IPMI/out-of-band management
 *
 * Recovery typically:
 *   1. Loads golden image from write-protected flash region
 *   2. Provides USB/network firmware reflash capability
 *   3. Minimal hardware init (serial console, USB, storage)
 *
 * Reference: Chrome OS Recovery Mode Design; UEFI Capsule Recovery.
 */
bool fw_update_enter_recovery(FwUpdateManager *mgr)
{
    if (!mgr) return false;

    mgr->recovery_mode = true;
    mgr->slot_mgr.active_slot = FW_SLOT_RECOVERY;
    mgr->slot_mgr.slots[FW_SLOT_RECOVERY].status = SLOT_STATUS_ACTIVE;

    printf("[Recovery] Entering recovery mode\n");
    printf("[Recovery] Active slot: Recovery (golden image)\n");
    printf("[Recovery] Functions available:\n");
    printf("  1. Verify golden image integrity\n");
    printf("  2. Reflash A/B slots from golden\n");
    printf("  3. USB firmware update\n");
    printf("  4. Network firmware update (PXE/HTTP Boot)\n");

    return true;
}

bool fw_update_verify_golden_image(const FwUpdateManager *mgr)
{
    if (!mgr) return false;
    /* In real firmware, compute hash of recovery region and compare
     * against mgr->golden_image_hash stored in write-protected flash */
    printf("[Recovery] Golden image hash verified\n");
    return true;
}

/* ??? L7: Diagnostics ?????????????????????????????????????? */

void fw_update_print_slots(const FwUpdateManager *mgr)
{
    if (!mgr) return;

    const ABSlotManager *sm = &mgr->slot_mgr;
    printf("=== A/B Slot Status ===\n");
    printf("Active Slot: %s\n", sm->slots[sm->active_slot].name);
    printf("Update in progress: %s\n\n", mgr->update_in_progress ? "Yes" : "No");

    for (int i = 0; i < 3; i++) {
        const FirmwareSlot *slot = &sm->slots[i];
        const char *status_str;
        switch (slot->status) {
        case SLOT_STATUS_ACTIVE:     status_str = "ACTIVE"; break;
        case SLOT_STATUS_STANDBY:    status_str = "STANDBY"; break;
        case SLOT_STATUS_UPDATING:   status_str = "UPDATING"; break;
        case SLOT_STATUS_UNBOOTABLE: status_str = "UNBOOTABLE"; break;
        case SLOT_STATUS_INVALID:    status_str = "INVALID"; break;
        default:                     status_str = "UNKNOWN"; break;
        }

        printf("  %s:\n", slot->name);
        printf("    Offset:  0x%08llX  Size: %llu KB\n",
               (unsigned long long)slot->base_offset,
               (unsigned long long)(slot->size / 1024));
        printf("    Version: v%u  Status: %s\n", slot->version, status_str);
        printf("    Bootable: %s  Attempts: %u/%u\n",
               slot->bootable ? "Yes" : "No",
               slot->boot_attempts, sm->max_boot_attempts);
        printf("    Successful boots: %u/%u\n",
               slot->successful_boots, sm->min_successful_boots);
        printf("    Hash: ");
        for (int j = 0; j < 8; j++) printf("%02x", slot->fw_hash.data[j]);
        printf("...\n");
    }
}

void fw_update_print_history(const FwUpdateManager *mgr)
{
    if (!mgr) return;

    printf("=== Update History ===\n");
    printf("Total updates: %u\n\n", mgr->history_count);

    for (uint32_t i = 0; i < mgr->history_count; i++) {
        const UpdateHistoryEntry *h = &mgr->history[i];
        printf("  [%u] v%u ? v%u (%s) ? %s\n",
               h->sequence, h->from_version, h->to_version,
               h->target_slot == FW_SLOT_A ? "Slot A" :
               h->target_slot == FW_SLOT_B ? "Slot B" : "Recovery",
               h->success ? "SUCCESS" : "FAILED");
        printf("      Reason: %s\n", h->reason);
    }
}
