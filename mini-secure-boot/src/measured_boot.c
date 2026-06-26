#include "measured_boot.h"
#include "signature_verify.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/*
 * Measured Boot Implementation
 *
 * Core concept: Trusted Computing Group (TCG) Static Root of Trust
 * for Measurement (SRTM). Every firmware component is measured
 * (hashed) and extended into a PCR before execution. The event
 * log records what was measured, allowing later verification.
 *
 * Difference from Secure Boot:
 *   - Secure Boot: ENFORCES policy (blocks untrusted code)
 *   - Measured Boot: RECORDS what ran (detects tampering afterward)
 *   - They complement each other: Secure Boot for enforcement,
 *     Measured Boot + Remote Attestation for audit.
 */

/* ??? Static helpers ?????????????????????????????????????????????????? */

static void compute_sha256_digest(const uint8_t *data, uint32_t size,
                                   uint8_t *digest_out)
{
    sha256_hash(data, size, digest_out);
}

/*
 * PCR Extend: PCR_new = SHA-256(PCR_old || hash_of_event)
 * This is the fundamental SRTM operation defined in TCG PC Client Spec.
 */
static void pcr_extend_sha256(uint8_t *pcr_value,
                               const uint8_t *event_digest)
{
    uint8_t concat[SHA256_DIGEST_SIZE * 2];
    memcpy(concat, pcr_value, SHA256_DIGEST_SIZE);
    memcpy(concat + SHA256_DIGEST_SIZE, event_digest, SHA256_DIGEST_SIZE);
    compute_sha256_digest(concat, SHA256_DIGEST_SIZE * 2, pcr_value);
}

/* ??? SRTM Lifecycle ?????????????????????????????????????????????????? */

bool mb_init(MeasuredBoot *mb, const uint8_t *crtm_version,
             uint32_t crtm_version_len)
{
    if (!mb) return false;
    memset(mb, 0, sizeof(MeasuredBoot));

    mb->state = MB_STATE_PRE_INIT;
    mb->drtm_supported = false;
    mb->locality_3_available = false;

    /* Record CRTM version (Core Root of Trust for Measurement).
     * CRTM is the first piece of code executed at reset ? typically
     * part of the BIOS/UEFI boot block, immutable by design. */
    if (crtm_version && crtm_version_len > 0 && crtm_version_len < 16) {
        memcpy(mb->crtm_version, crtm_version, crtm_version_len);
    } else {
        memcpy(mb->crtm_version, "CRTM-v1.0", 9);
    }

    /* Initialize event log */
    mb->srtm.event_log.event_log_magic = MB_EVENT_LOG_MAGIC;
    mb->srtm.event_log.event_count = 0;
    mb->srtm.event_log.finalized = false;
    mb->srtm.event_log.hash_log_extended = false;

    /* Initialize all PCRs to zero (initial value per TCG spec) */
    for (uint32_t i = 0; i < MB_PCR_COUNT; i++) {
        memset(mb->srtm.current_pcrs[i], 0, SHA256_DIGEST_SIZE);
    }
    mb->srtm.active_pcr_count = MB_PCR_COUNT;
    mb->srtm.srtm_complete = false;

    /* Record S_CRTM_VERSION event in PCR 0
     * (Per TCG EFI Platform Spec, Section 5.3.1) */
    MBEvent crtm_event;
    memset(&crtm_event, 0, sizeof(MBEvent));
    crtm_event.pcr_index = 0;
    crtm_event.event_type = EV_S_CRTM_VERSION;
    crtm_event.digest_count = 1;
    crtm_event.digests[0].algorithm_id = TCG_ALG_SHA256;
    crtm_event.event_size = crtm_version_len < MB_MAX_EVENT_DATA ?
                            crtm_version_len : MB_MAX_EVENT_DATA;
    memcpy(crtm_event.event_data, mb->crtm_version, crtm_event.event_size);
    crtm_event.event_number = 0;

    compute_sha256_digest(crtm_event.event_data, crtm_event.event_size,
                          crtm_event.digests[0].digest);

    /* Extend PCR 0 with CRTM version hash */
    pcr_extend_sha256(mb->srtm.current_pcrs[0],
                       crtm_event.digests[0].digest);

    mb_event_log_append(&mb->srtm.event_log, &crtm_event);

    /* Record S_CRTM_CONTENTS event in PCR 0 */
    MBEvent crtm_contents_event;
    memset(&crtm_contents_event, 0, sizeof(MBEvent));
    crtm_contents_event.pcr_index = 0;
    crtm_contents_event.event_type = EV_S_CRTM_CONTENTS;
    crtm_contents_event.digest_count = 1;
    crtm_contents_event.digests[0].algorithm_id = TCG_ALG_SHA256;
    /* CRTM contents = immutable firmware descriptor */
    const char crtm_desc[] = "CRTM Boot Block V1.0 (Immutable)";
    crtm_contents_event.event_size = sizeof(crtm_desc);
    memcpy(crtm_contents_event.event_data, crtm_desc, sizeof(crtm_desc));
    crtm_contents_event.event_number = 1;

    compute_sha256_digest(crtm_contents_event.event_data,
                          crtm_contents_event.event_size,
                          crtm_contents_event.digests[0].digest);

    pcr_extend_sha256(mb->srtm.current_pcrs[0],
                       crtm_contents_event.digests[0].digest);
    mb_event_log_append(&mb->srtm.event_log, &crtm_contents_event);

    mb->state = MB_STATE_CRTM_ACTIVE;
    return true;
}

bool mb_measure_firmware(MeasuredBoot *mb, uint32_t pcr_index,
                          MBEventType event_type,
                          const uint8_t *firmware_data, uint32_t data_size,
                          const char *description)
{
    if (!mb || !firmware_data || !description) return false;
    if (pcr_index >= MB_PCR_COUNT) return false;
    if (mb->state < MB_STATE_CRTM_ACTIVE) return false;

    /* Compute SHA-256 hash of the firmware component */
    uint8_t digest[SHA256_DIGEST_SIZE];
    compute_sha256_digest(firmware_data, data_size, digest);

    /* Create event log entry */
    MBEvent event;
    memset(&event, 0, sizeof(MBEvent));
    event.pcr_index = pcr_index;
    event.event_type = event_type;
    event.digest_count = 1;
    event.digests[0].algorithm_id = TCG_ALG_SHA256;
    memcpy(event.digests[0].digest, digest, SHA256_DIGEST_SIZE);

    uint32_t desc_len = (uint32_t)strlen(description);
    if (desc_len >= MB_MAX_EVENT_DATA) desc_len = MB_MAX_EVENT_DATA - 1;
    memcpy(event.event_data, description, desc_len);
    event.event_size = desc_len;
    event.event_number = mb->srtm.event_log.event_count;

    /* Extend the PCR: PCR_new = SHA-256(PCR_old || digest) */
    pcr_extend_sha256(mb->srtm.current_pcrs[pcr_index], digest);

    /* Append to event log */
    if (!mb_event_log_append(&mb->srtm.event_log, &event)) return false;

    mb->state = MB_STATE_MEASURING;
    return true;
}

bool mb_measure_boot_variable(MeasuredBoot *mb,
                               const uint8_t *var_data, uint32_t var_size,
                               const char *var_name)
{
    if (!mb || !var_data || !var_name) return false;

    /* Boot variables are measured into PCR 1 (Platform Configuration)
     * per TCG EFI Protocol Specification. This includes:
     *   - BootOrder, Boot#### variables
     *   - Secure Boot configuration (PK, KEK, db, dbx)
     *   - Platform configuration settings */
    return mb_measure_firmware(mb, 1, EV_EFI_VARIABLE_BOOT,
                                var_data, var_size, var_name);
}

bool mb_extend_pcr_with_event(MeasuredBoot *mb, uint32_t pcr_index,
                               MBEventType event_type,
                               const uint8_t *digest, uint32_t digest_size)
{
    if (!mb || !digest || digest_size == 0) return false;
    if (pcr_index >= MB_PCR_COUNT) return false;

    uint8_t hash[SHA256_DIGEST_SIZE];
    if (digest_size == SHA256_DIGEST_SIZE) {
        memcpy(hash, digest, SHA256_DIGEST_SIZE);
    } else {
        compute_sha256_digest(digest, digest_size, hash);
    }

    MBEvent event;
    memset(&event, 0, sizeof(MBEvent));
    event.pcr_index = pcr_index;
    event.event_type = event_type;
    event.digest_count = 1;
    event.digests[0].algorithm_id = TCG_ALG_SHA256;
    memcpy(event.digests[0].digest, hash, SHA256_DIGEST_SIZE);
    event.event_size = digest_size < MB_MAX_EVENT_DATA ?
                       digest_size : MB_MAX_EVENT_DATA;
    memcpy(event.event_data, digest, event.event_size);
    event.event_number = mb->srtm.event_log.event_count;

    pcr_extend_sha256(mb->srtm.current_pcrs[pcr_index], hash);
    return mb_event_log_append(&mb->srtm.event_log, &event);
}

/* ??? Event Log Operations ???????????????????????????????????????????? */

bool mb_event_log_append(MBEventLog *log, const MBEvent *event)
{
    if (!log || !event) return false;
    if (log->finalized) return false;
    if (log->event_count >= MB_MAX_EVENTS) return false;

    memcpy(&log->events[log->event_count], event, sizeof(MBEvent));
    log->events[log->event_count].event_number = log->event_count;
    log->event_count++;
    return true;
}

const MBEvent *mb_event_log_get(const MBEventLog *log, uint32_t event_number)
{
    if (!log || event_number >= log->event_count) return NULL;
    return &log->events[event_number];
}

bool mb_event_log_validate(const MBEventLog *log,
                            const uint8_t expected_pcrs[][SHA256_DIGEST_SIZE],
                            uint32_t pcr_count)
{
    if (!log || !expected_pcrs) return false;
    if (log->event_log_magic != MB_EVENT_LOG_MAGIC) return false;

    /*
     * Event Log Validation Algorithm (NIST SP 800-155, Section 3.3):
     *
     * 1. Replay: starting from PCR[n] = 0x00...00, apply each event
     *    sequentially: PCR[event.pcr_index] = SHA-256(PCR_current || event.digest)
     * 2. Compare: the resulting PCR values against the expected golden PCRs
     * 3. Integrity: verify sequence numbers are monotonic and consecutive
     */

    /* Step 1: Initialize all PCRs to zero and replay events */
    uint8_t computed_pcrs[MB_PCR_COUNT][SHA256_DIGEST_SIZE];
    memset(computed_pcrs, 0, sizeof(computed_pcrs));

    uint32_t last_event_number = 0;
    bool first_event = true;

    for (uint32_t i = 0; i < log->event_count; i++) {
        const MBEvent *event = &log->events[i];

        /* Verify monotonic sequence numbers */
        if (!first_event && event->event_number != last_event_number + 1) {
            return false; /* Gap in event sequence */
        }
        first_event = false;
        last_event_number = event->event_number;

        /* Apply PCR extend for this event */
        if (event->pcr_index < MB_PCR_COUNT && event->digest_count > 0) {
            pcr_extend_sha256(computed_pcrs[event->pcr_index],
                              event->digests[0].digest);
        }
    }

    /* Step 2: Compare computed PCRs against expected golden values */
    uint32_t check_count = pcr_count < MB_PCR_COUNT ? pcr_count : MB_PCR_COUNT;
    for (uint32_t i = 0; i < check_count; i++) {
        if (memcmp(computed_pcrs[i], expected_pcrs[i], SHA256_DIGEST_SIZE) != 0) {
            return false;
        }
    }

    return true;
}

/* ??? PCR Validation ?????????????????????????????????????????????????? */

bool mb_pcr_validate(const SRTMContext *srtm,
                     const uint8_t golden_pcrs[][SHA256_DIGEST_SIZE],
                     uint32_t pcr_count)
{
    if (!srtm || !golden_pcrs) return false;
    return mb_event_log_validate(&srtm->event_log, golden_pcrs, pcr_count);
}

bool mb_check_separators(const MBEventLog *log)
{
    if (!log) return false;

    /*
     * Per TCG EFI Platform Spec:
     * EV_SEPARATOR events mark transitions between boot phases.
     * There should be separators at:
     *   1. After CRTM events (PCR 0 ? PCR 7 transition)
     *   2. Before ExitBootServices (PCR 0-7)
     *   3. At ReadyToBoot (PCR 0-7)
     *
     * Proper separator placement is critical for attestation: a missing
     * separator allows an attacker to inject events in an earlier phase.
     */
    uint32_t separator_count = 0;
    for (uint32_t i = 0; i < log->event_count; i++) {
        if (log->events[i].event_type == EV_SEPARATOR) {
            separator_count++;
        }
    }
    /* Minimum: at least one separator after CRTM and one at end of boot */
    return separator_count >= 2;
}

uint32_t mb_get_pcr_value(const SRTMContext *srtm, uint32_t pcr_index,
                          uint8_t *digest)
{
    if (!srtm || !digest || pcr_index >= MB_PCR_COUNT) return 0;
    memcpy(digest, srtm->current_pcrs[pcr_index], SHA256_DIGEST_SIZE);
    return SHA256_DIGEST_SIZE;
}

/* ??? DRTM (Late Launch) Support ?????????????????????????????????????? */

bool mb_drtm_launch(MeasuredBoot *mb)
{
    if (!mb) return false;
    if (!mb->drtm_supported) return false;

    /*
     * DRTM Launch Process (simplified):
     *
     * 1. CPU executes GETSEC[SENTER] or SKINIT
     * 2. CPU resets PCRs 17-22 in DRTM locality
     * 3. CPU measures and launches SINIT ACM or SKINIT loader
     * 4. MLE is measured into PCR 18
     * 5. Control transfers to MLE in a trusted environment
     *
     * PCR assignments in DRTM:
     *   17: DRTM + SINIT/ACM authority
     *   18: MLE (Measured Launch Environment)
     *   19: MLE configuration / policy
     *   20: Post-launch OS kernel
     *   21: OS configuration
     *   22: OS applications
     */

    /* Reset DRTM PCRs (17-22) for late launch */
    for (uint32_t i = 17; i <= 22 && i < MB_PCR_COUNT; i++) {
        memset(mb->srtm.current_pcrs[i], 0, SHA256_DIGEST_SIZE);
    }

    /* Record DRTM launch event in PCR 17 */
    MBEvent drtm_event;
    memset(&drtm_event, 0, sizeof(MBEvent));
    drtm_event.pcr_index = 17;
    drtm_event.event_type = EV_ACTION;
    drtm_event.digest_count = 1;
    drtm_event.digests[0].algorithm_id = TCG_ALG_SHA256;
    const char launch_msg[] = "DRTM Late Launch (GETSEC/SENTER)";
    drtm_event.event_size = sizeof(launch_msg);
    memcpy(drtm_event.event_data, launch_msg, sizeof(launch_msg));
    compute_sha256_digest(drtm_event.event_data,
                          drtm_event.event_size,
                          drtm_event.digests[0].digest);
    drtm_event.event_number = mb->srtm.event_log.event_count;

    pcr_extend_sha256(mb->srtm.current_pcrs[17],
                       drtm_event.digests[0].digest);
    mb_event_log_append(&mb->srtm.event_log, &drtm_event);

    return true;
}

bool mb_drtm_measure(MeasuredBoot *mb, const uint8_t *mle_data,
                     uint32_t mle_size)
{
    if (!mb || !mle_data || mle_size == 0) return false;

    /* Measure MLE into PCR 18 */
    return mb_measure_firmware(mb, 18, EV_NONHOST_CODE,
                                mle_data, mle_size,
                                "DRTM Measured Launch Environment (MLE)");
}

/* ??? Utility ????????????????????????????????????????????????????????? */

const char *mb_event_type_str(MBEventType event_type)
{
    switch (event_type) {
        case EV_PREBOOT_CERT:       return "PREBOOT_CERT";
        case EV_POST_CODE:          return "POST_CODE";
        case EV_NO_ACTION:          return "NO_ACTION";
        case EV_SEPARATOR:          return "SEPARATOR";
        case EV_ACTION:             return "ACTION";
        case EV_S_CRTM_CONTENTS:    return "S_CRTM_CONTENTS";
        case EV_S_CRTM_VERSION:     return "S_CRTM_VERSION";
        case EV_CPU_MICROCODE:      return "CPU_MICROCODE";
        case EV_PLATFORM_CONFIG_FLAGS: return "PLATFORM_CONFIG_FLAGS";
        case EV_TABLE_OF_DEVICES:   return "TABLE_OF_DEVICES";
        case EV_COMPACT_HASH:       return "COMPACT_HASH";
        case EV_IPL:                return "IPL";
        case EV_NONHOST_CODE:       return "NONHOST_CODE";
        case EV_NONHOST_CONFIG:     return "NONHOST_CONFIG";
        case EV_NONHOST_INFO:       return "NONHOST_INFO";
        case EV_EFI_VARIABLE_DRIVER_CONFIG: return "EFI_VARIABLE_DRIVER_CONFIG";
        case EV_EFI_VARIABLE_BOOT:  return "EFI_VARIABLE_BOOT";
        case EV_EFI_BOOT_SERVICES_APPLICATION: return "EFI_BOOT_SERVICES_APP";
        case EV_EFI_BOOT_SERVICES_DRIVER:  return "EFI_BOOT_SERVICES_DRIVER";
        case EV_EFI_RUNTIME_SERVICES_DRIVER: return "EFI_RUNTIME_SERVICES_DRIVER";
        case EV_EFI_GPT_EVENT:      return "EFI_GPT_EVENT";
        case EV_EFI_ACTION:         return "EFI_ACTION";
        case EV_EFI_PLATFORM_FIRMWARE_BLOB:  return "EFI_PLATFORM_FIRMWARE_BLOB";
        case EV_EFI_HANDOFF_TABLES:  return "EFI_HANDOFF_TABLES";
        case EV_EFI_PLATFORM_FIRMWARE_BLOB2: return "EFI_PLATFORM_FW_BLOB2";
        case EV_EFI_HANDOFF_TABLES2: return "EFI_HANDOFF_TABLES2";
        case EV_EFI_VARIABLE_BOOT2:  return "EFI_VARIABLE_BOOT2";
        case EV_EFI_EVENT_BASE:     return "EFI_EVENT_BASE";
        default:                    return "UNKNOWN";
    }
}

void mb_print_event_log(const MBEventLog *log)
{
    if (!log) return;
    printf("????????????????????????????????????????????????\n");
    printf("?       TCG MEASURED BOOT EVENT LOG            ?\n");
    printf("????????????????????????????????????????????????\n");
    printf("? Magic: 0x%08X  Events: %-4u  %s         ?\n",
           log->event_log_magic, log->event_count,
           log->finalized ? "FINALIZED" : "OPEN");
    printf("????????????????????????????????????????????????\n");

    for (uint32_t i = 0; i < log->event_count && i < 32; i++) {
        const MBEvent *e = &log->events[i];
        printf("? [%03u] PCR%02u %-24s ", e->event_number,
               e->pcr_index, mb_event_type_str(e->event_type));
        printf("digest=");
        for (int j = 0; j < 4; j++) printf("%02X", e->digests[0].digest[j]);
        printf("...");
        if (e->event_size > 0) {
            printf(" data=%uB", e->event_size);
        }
        printf(" ?\n");
    }
    if (log->event_count > 32) {
        printf("? ... (%u more events)                       ?\n",
               log->event_count - 32);
    }
    printf("????????????????????????????????????????????????\n");
}

void mb_print_pcr_summary(const SRTMContext *srtm)
{
    if (!srtm) return;
    printf("=== SRTM PCR Summary ===\n");
    for (uint32_t i = 0; i < 8 && i < MB_PCR_COUNT; i++) {
        printf("  PCR[%02u]: ", i);
        for (int j = 0; j < 8; j++) {
            printf("%02X", srtm->current_pcrs[i][j]);
        }
        printf("...\n");
    }
    if (MB_PCR_COUNT > 8) printf("  ... (PCRs 8-23 omitted)\n");
}
