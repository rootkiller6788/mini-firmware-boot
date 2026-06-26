#ifndef MEASURED_BOOT_H
#define MEASURED_BOOT_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/*
 * Measured Boot Module
 *
 * Implements: TCG EFI Platform Specification (Event Log Format)
 * Reference:  NIST SP 800-155 (BIOS Integrity Measurement Guidelines)
 *             TCG PC Client Specific Implementation Specification
 *
 * Knowledge coverage:
 *   L2: Measured Boot vs Verified Boot ? measure vs enforce
 *   L3: TCG Crypto Agile Event Log Format (SHA-256 family)
 *   L4: NIST SP 800-155, TCG EFI Platform Specification
 *   L5: Event log reconstruction, PCR validation against log
 *   L6: SRTM (Static Root of Trust for Measurement) full implementation
 */

#define MB_MAX_EVENTS            256
#define MB_MAX_EVENT_DATA        1024
#define MB_MAX_DIGEST_SIZE       48     /* SHA-384 max */
#define MB_PCR_COUNT             24
#define MB_EVENT_LOG_MAGIC       0x454C4F47  /* "ELOG" */

/* ??? TCG Event Types (EV_ constants per TCG PC Client) ??? */

typedef enum {
    EV_PREBOOT_CERT       = 0x00000000,
    EV_POST_CODE          = 0x00000001,
    EV_UNUSED             = 0x00000002,
    EV_NO_ACTION          = 0x00000003,
    EV_SEPARATOR          = 0x00000004,
    EV_ACTION             = 0x00000005,
    EV_EVENT_TAG          = 0x00000006,
    EV_S_CRTM_CONTENTS    = 0x00000007,
    EV_S_CRTM_VERSION     = 0x00000008,
    EV_CPU_MICROCODE      = 0x00000009,
    EV_PLATFORM_CONFIG_FLAGS = 0x0000000A,
    EV_TABLE_OF_DEVICES   = 0x0000000B,
    EV_COMPACT_HASH       = 0x0000000C,
    EV_IPL                = 0x0000000D,
    EV_IPL_PARTITION_DATA = 0x0000000E,
    EV_NONHOST_CODE       = 0x0000000F,
    EV_NONHOST_CONFIG     = 0x00000010,
    EV_NONHOST_INFO       = 0x00000011,
    EV_OMIT_BOOT_DEVICE_EVENTS = 0x00000012,
    EV_EFI_EVENT_BASE     = 0x80000000,
    EV_EFI_VARIABLE_DRIVER_CONFIG = 0x80000001,
    EV_EFI_VARIABLE_BOOT         = 0x80000002,
    EV_EFI_BOOT_SERVICES_APPLICATION = 0x80000003,
    EV_EFI_BOOT_SERVICES_DRIVER  = 0x80000004,
    EV_EFI_RUNTIME_SERVICES_DRIVER = 0x80000005,
    EV_EFI_GPT_EVENT        = 0x80000006,
    EV_EFI_ACTION           = 0x80000007,
    EV_EFI_PLATFORM_FIRMWARE_BLOB  = 0x80000008,
    EV_EFI_HANDOFF_TABLES   = 0x80000009,
    EV_EFI_PLATFORM_FIRMWARE_BLOB2 = 0x8000000A,
    EV_EFI_HANDOFF_TABLES2  = 0x8000000B,
    EV_EFI_VARIABLE_BOOT2   = 0x8000000C
} MBEventType;

/* ??? TCG Algorithm Registry IDs for Crypto Agile format ??? */

typedef enum {
    TCG_ALG_SHA1      = 0x0004,
    TCG_ALG_SHA256    = 0x000B,
    TCG_ALG_SHA384    = 0x000C,
    TCG_ALG_SHA512    = 0x000D,
    TCG_ALG_SM3_256   = 0x0012
} TCGAlgID;

/* ??? SHA-256 Digest (as used in event log) ??? */
#define SHA256_DIGEST_SIZE 32

/* ??? Crypto Agile Digest ??? */

typedef struct {
    TCGAlgID  algorithm_id;
    uint8_t   digest[MB_MAX_DIGEST_SIZE];
} TCGDigest;

/* ??? Event Log Entry (TCG EFI Platform Spec, Section 5.2) ??? */

typedef struct {
    uint32_t    pcr_index;                  /* PCR index extended (0-23) */
    MBEventType event_type;                 /* TCG event type */
    TCGDigest   digests[4];                /* up to 4 digest algorithms */
    uint32_t    digest_count;
    uint8_t     event_data[MB_MAX_EVENT_DATA];
    uint32_t    event_size;
    uint32_t    event_number;              /* monotonic sequence number */
} MBEvent;

/* ??? Event Log ??? */

typedef struct {
    MBEvent    events[MB_MAX_EVENTS];
    uint32_t   event_count;
    uint32_t   event_log_magic;
    bool       finalized;                  /* log closed after boot */
    bool       hash_log_extended;          /* HashLogExtend event recorded */
} MBEventLog;

/* ??? SRTM Measurement Context ??? */

typedef struct {
    uint8_t    current_pcrs[MB_PCR_COUNT][SHA256_DIGEST_SIZE];
    MBEventLog event_log;
    uint32_t   active_pcr_count;
    bool       srtm_complete;
} SRTMContext;

/* ??? Measured Boot States ??? */

typedef enum {
    MB_STATE_PRE_INIT = 0,
    MB_STATE_CRTM_ACTIVE,
    MB_STATE_MEASURING,
    MB_STATE_OS_PRESENT,
    MB_STATE_ERROR
} MBState;

typedef struct {
    SRTMContext srtm;
    MBState     state;
    uint8_t     crtm_version[16];
    bool        drtm_supported;
    bool        locality_3_available;
} MeasuredBoot;

/* ??? SRTM Lifecycle ??? */

bool mb_init(MeasuredBoot *mb, const uint8_t *crtm_version,
             uint32_t crtm_version_len);
bool mb_measure_firmware(MeasuredBoot *mb, uint32_t pcr_index,
                          MBEventType event_type,
                          const uint8_t *firmware_data, uint32_t data_size,
                          const char *description);
bool mb_measure_boot_variable(MeasuredBoot *mb,
                               const uint8_t *var_data, uint32_t var_size,
                               const char *var_name);
bool mb_extend_pcr_with_event(MeasuredBoot *mb, uint32_t pcr_index,
                               MBEventType event_type,
                               const uint8_t *digest, uint32_t digest_size);

/* ??? Event Log Operations ??? */

bool mb_event_log_append(MBEventLog *log, const MBEvent *event);
const MBEvent *mb_event_log_get(const MBEventLog *log, uint32_t event_number);
bool mb_event_log_validate(const MBEventLog *log,
                            const uint8_t expected_pcrs[][SHA256_DIGEST_SIZE],
                            uint32_t pcr_count);

/*
 * Event Log Validation (per NIST SP 800-155, Section 3.3):
 *   To validate integrity:
 *   1. Replay the event log by computing PCR_extend for each event
 *   2. Compare the resulting PCR values against expected golden values
 *   3. Verify event log sequence numbers are monotonic
 *   4. Check that EV_SEPARATOR events occur at correct transitions
 *
 *   PCR[n]_computed = H(H(H(0x00 || event_0_data) || event_1_data) || ...)
 */

/* ??? PCR Validation Against Event Log ??? */

bool mb_pcr_validate(const SRTMContext *srtm,
                     const uint8_t golden_pcrs[][SHA256_DIGEST_SIZE],
                     uint32_t pcr_count);
bool mb_check_separators(const MBEventLog *log);
uint32_t mb_get_pcr_value(const SRTMContext *srtm, uint32_t pcr_index,
                          uint8_t *digest);

/* ??? DRTM (Late Launch) Support ??? */

bool mb_drtm_launch(MeasuredBoot *mb);
bool mb_drtm_measure(MeasuredBoot *mb, const uint8_t *mle_data,
                     uint32_t mle_size);

/*
 * DRTM (Dynamic Root of Trust for Measurement):
 *   Uses CPU instructions (SKINIT/SENTER) to start a measured environment
 *   from an untrusted OS, resetting PCRs 17-22 for the DRTM locality.
 *
 *   PCR 17: DRTM and launch control policy
 *   PCR 18: MLE (Measured Launch Environment) code
 *   PCR 19: MLE configuration
 *   PCR 20: OS kernel
 *   PCR 21: OS configuration
 *   PCR 22: OS application
 */

/* ??? Utility ??? */

const char *mb_event_type_str(MBEventType event_type);
void mb_print_event_log(const MBEventLog *log);
void mb_print_pcr_summary(const SRTMContext *srtm);

#endif /* MEASURED_BOOT_H */
