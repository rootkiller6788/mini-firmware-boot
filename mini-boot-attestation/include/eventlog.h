#ifndef EVENTLOG_H
#define EVENTLOG_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "tpm_quote.h"
#include "attest_protocol.h"

/*
 * TCG EFI Platform Specification for TPM Family 1.1/1.2/2.0
 * TCG PC Client Platform Firmware Profile Specification
 * Crypto Agile Log format (TCG CEL)
 *
 * EventLog = sequence of TCG_EVENT or TCG_EVENT2 structures
 * Each event records a measurement: PCR[index] = H(PCR[index] || digest)
 *
 * Theorem (Hash Chain Integrity):
 *   If H is collision-resistant, then any tampering with event_log[i]
 *   will cause recomputed_final_pcr != attested_pcr with probability
 *   >= 1 - epsilon where epsilon = Adv^coll_H(adversary).
 *
 * Reference: NIST SP 800-155 Section 4.2 BIOS Integrity Measurement
 */

#define TCG_EVENT_LOG_MAX_ENTRIES  64
#define TCG_MAX_DIGEST_COUNT       4
#define TCG_EVENT_DATA_MAX_SIZE    512
#define TCG_EVENT_TYPE_SIZE         4
#define TCG_SHA1_DIGEST_SIZE       20
#define TCG_SHA384_DIGEST_SIZE     48
#define TCG_SHA512_DIGEST_SIZE     64

#define TCG_ALG_SHA1             0x0004
#define TCG_ALG_SHA256           0x000B
#define TCG_ALG_SHA384           0x000C
#define TCG_ALG_SHA512           0x000D
#define TCG_ALG_SM3_256          0x0012

#define EV_PREBOOT_CERT          0x00000000
#define EV_POST_CODE             0x00000001
#define EV_NO_ACTION             0x00000003
#define EV_SEPARATOR             0x00000004
#define EV_ACTION                0x00000005
#define EV_EVENT_TAG             0x00000006
#define EV_S_CRTM_CONTENTS       0x00000007
#define EV_S_CRTM_VERSION        0x00000008
#define EV_CPU_MICROCODE         0x00000009
#define EV_PLATFORM_CONFIG_FLAGS 0x0000000A
#define EV_TABLE_OF_DEVICES      0x0000000B
#define EV_COMPACT_HASH          0x0000000C
#define EV_IPL                   0x0000000D
#define EV_IPL_PARTITION_DATA    0x0000000E
#define EV_NONHOST_CODE          0x0000000F
#define EV_NONHOST_CONFIG        0x00000010
#define EV_NONHOST_INFO          0x00000011
#define EV_OMIT_BOOT_DEVICE      0x00000012
#define EV_EFI_EVENT_BASE        0x80000000
#define EV_EFI_VARIABLE_DRIVER   0x80000001
#define EV_EFI_VARIABLE_BOOT     0x80000002
#define EV_EFI_BOOT_SERVICES     0x80000003
#define EV_EFI_BOOT_MANAGER      0x80000004
#define EV_EFI_PLATFORM_FIRMWARE 0x800000E0

typedef struct {
    uint16_t algorithm_id;
    union {
        uint8_t sha1[TCG_SHA1_DIGEST_SIZE];
        uint8_t sha256[TPM_SHA256_DIGEST_SIZE];
        uint8_t sha384[TCG_SHA384_DIGEST_SIZE];
        uint8_t sha512[TCG_SHA512_DIGEST_SIZE];
    } digest;
} TCGDigest;

typedef struct {
    uint32_t  count;
    TCGDigest digests[TCG_MAX_DIGEST_COUNT];
} TCGDigestList;

typedef struct {
    uint32_t     pcr_index;
    uint32_t     event_type;
    TCGDigestList digests;
    uint8_t      event_data[TCG_EVENT_DATA_MAX_SIZE];
    uint32_t     event_data_size;
} TCGEvent;

typedef struct {
    TCGEvent entries[TCG_EVENT_LOG_MAX_ENTRIES];
    uint32_t entry_count;
    uint32_t log_hash_algorithm;
    uint64_t boot_counter;
    bool     finalized;
} TCGEventLog;

typedef struct {
    uint8_t  pcr_index;
    uint32_t event_type;
    uint8_t  digest[TPM_SHA256_DIGEST_SIZE];
    uint8_t  data[TCG_EVENT_DATA_MAX_SIZE];
    uint32_t data_size;
} TCGEventSHA256;

typedef struct {
    TCGEventSHA256 entries[TCG_EVENT_LOG_MAX_ENTRIES];
    uint32_t entry_count;
    uint32_t boot_cycle;
    bool     sealed;
} TCGEventLogSHA256;

typedef struct {
    TPMHash   state;
    bool      started;
    uint32_t  data_hashed;
} TCGHashSequence;

int32_t  tcg_pcr_extend(TPMHash *pcr_value,
                        const uint8_t *digest, uint32_t digest_size);

int32_t  tcg_pcr_extend_with_data(TPMHash *pcr_value,
                                   const uint8_t *data, uint32_t data_size);

int32_t  tcg_pcr_extend_multi(TCGDigestList *pcr_values,
                               uint32_t pcr_index,
                               const TCGDigestList *digests);

void     tcg_eventlog_init(TCGEventLog *log, uint32_t hash_algorithm);
int32_t  tcg_eventlog_add(TCGEventLog *log,
                           uint32_t pcr_index, uint32_t event_type,
                           const TCGDigestList *digests,
                           const uint8_t *event_data, uint32_t data_size);
int32_t  tcg_eventlog_get(const TCGEventLog *log, uint32_t index, TCGEvent *out);
void     tcg_eventlog_finalize(TCGEventLog *log);

void     tcg_eventlog_sha256_init(TCGEventLogSHA256 *log);
int32_t  tcg_eventlog_sha256_add(TCGEventLogSHA256 *log,
                                  uint8_t pcr_index, uint32_t event_type,
                                  const uint8_t *data, uint32_t data_size);
int32_t  tcg_eventlog_sha256_seal(TCGEventLogSHA256 *log);

int32_t  tcg_eventlog_replay_sha256(const TCGEventLogSHA256 *log,
                                     TPMPcrComposite *recomputed_pcr,
                                     bool *match,
                                     const TPMPcrComposite *expected_pcr,
                                     uint16_t pcr_mask);

int32_t  tcg_eventlog_integrity_check(const TCGEventLogSHA256 *log,
                                       const TPMHash *expected_final_hash,
                                       bool *valid);

int32_t  tcg_pcr_bank_compare(const TPMPcrComposite *bank_a,
                               const TPMPcrComposite *bank_b,
                               uint8_t pcr_index, bool *match);

void     tcg_hash_sequence_start(TCGHashSequence *seq);
int32_t  tcg_hash_sequence_update(TCGHashSequence *seq,
                                   const uint8_t *data, uint32_t len);
int32_t  tcg_hash_sequence_end(TCGHashSequence *seq, TPMHash *result);

void     tcg_event_dump(const TCGEvent *event);
void     tcg_eventlog_dump(const TCGEventLog *log);
void     tcg_eventlog_sha256_dump(const TCGEventLogSHA256 *log);

#endif
