#ifndef FW_CROSS_INTEGRATION_H
#define FW_CROSS_INTEGRATION_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/*
 * FW_CROSS_INTEGRATION: Firmware Security Cross-Module Integration
 *
 * Implements L7: Cross-module security auditing for:
 *   - network(5) entry points (packet inspection at firmware level)
 *   - backend(8) entry points (storage/config audit trail)
 *
 * Reference: NIST SP 800-193 3.2 (Detection)
 *            Intel CSME Runtime Security
 *            OCP Cerberus Firmware Attestation
 *
 * Knowledge:
 *   L1: AuditEntry, NetworkFwPacket, BackendFwEvent structs
 *   L2: Security audit pipeline for cross-module data flow
 *   L3: Firmware-level packet inspection engine
 *        Firmware-level backend access audit trail
 *   L7: end-to-end demo: data-engine(7) -> backend(8) -> frontend(9)
 *        security(13) auditing network(5) + backend(8) entries
 */

/* ?? Constants ???????????????????????????????????????????????????? */

#define FW_AUDIT_ENTRY_MAX          128
#define FW_NET_PACKET_MAX           64
#define FW_BACKEND_EVENT_MAX        64
#define FW_NET_PAYLOAD_MAX          512
#define FW_BACKEND_PAYLOAD_MAX      256
#define FW_AUDIT_HASH_SIZE          32

/* Audit severity levels */
#define FW_AUDIT_INFO               0
#define FW_AUDIT_WARNING            1
#define FW_AUDIT_ERROR              2
#define FW_AUDIT_CRITICAL           3

/* Network packet direction */
#define FW_NET_DIR_INGRESS          0
#define FW_NET_DIR_EGRESS           1

/* Backend operation types */
#define FW_BACKEND_OP_READ          0
#define FW_BACKEND_OP_WRITE         1
#define FW_BACKEND_OP_DELETE        2
#define FW_BACKEND_OP_EXECUTE       3
#define FW_BACKEND_OP_CONFIGURE     4

/* Cross-module source identifiers */
#define FW_SRC_NETWORK              5
#define FW_SRC_DATA_ENGINE          7
#define FW_SRC_BACKEND              8
#define FW_SRC_FRONTEND             9
#define FW_SRC_SECURITY             13
#define FW_SRC_AI                   14

/* ?? L1: Core Data Structures ???????????????????????????????????? */

/* Network packet intercepted at firmware level */
typedef struct {
    uint32_t packet_id;
    uint8_t  src_module;
    uint8_t  dst_module;
    uint8_t  src_address[16];
    uint8_t  dst_address[16];
    uint16_t src_port;
    uint16_t dst_port;
    uint8_t  protocol;
    uint8_t  direction;
    uint8_t  payload[FW_NET_PAYLOAD_MAX];
    uint16_t payload_size;
    uint32_t timestamp;
    bool     allowed;
    uint8_t  audit_result;
} FwNetworkPacket;

/* Backend access event intercepted at firmware level */
typedef struct {
    uint32_t event_id;
    uint8_t  src_module;
    uint8_t  operation;
    uint8_t  resource_path[128];
    uint8_t  request_data[FW_BACKEND_PAYLOAD_MAX];
    uint16_t request_size;
    uint8_t  response_hash[FW_AUDIT_HASH_SIZE];
    uint32_t timestamp;
    bool     authorized;
    uint8_t  audit_result;
} FwBackendEvent;

/* Audit trail entry */
typedef struct {
    uint32_t entry_id;
    uint8_t  severity;
    uint8_t  source_module;
    uint8_t  target_module;
    uint32_t timestamp;
    uint8_t  description[64];
    uint8_t  evidence_hash[FW_AUDIT_HASH_SIZE];
    bool     violation_detected;
    uint8_t  recommendation;
} FwAuditEntry;

/* Cross-module audit state machine */
typedef struct {
    FwAuditEntry    entries[FW_AUDIT_ENTRY_MAX];
    uint32_t        entry_count;
    uint32_t        total_violations;
    uint32_t        total_packets_inspected;
    uint32_t        total_backend_events;
    bool            audit_active;
    uint32_t        session_id;
} FwCrossAudit;

/*
 * Data flow pipeline (L3):
 *   data-engine(7) -> backend(8) -> frontend(9)
 *   security(13) watches network(5) + backend(8) entries
 */

/* ?? L1: API Declarations ???????????????????????????????????????? */

/* Audit Engine Lifecycle */
void     fw_audit_init(FwCrossAudit *audit);
bool     fw_audit_enable(FwCrossAudit *audit);
bool     fw_audit_disable(FwCrossAudit *audit);

/* L7: Network Packet Security Audit */
bool     fw_audit_network_packet(FwCrossAudit *audit,
                                 const FwNetworkPacket *packet);
bool     fw_audit_network_whitelist(FwCrossAudit *audit,
                                     const uint8_t *src_addr,
                                     uint16_t src_port);
bool     fw_audit_network_blacklist(FwCrossAudit *audit,
                                     const uint8_t *src_addr,
                                     uint16_t src_port);

/* L7: Backend Access Security Audit */
bool     fw_audit_backend_event(FwCrossAudit *audit,
                                const FwBackendEvent *event);
bool     fw_audit_backend_authorize(FwCrossAudit *audit,
                                     uint32_t event_id);
bool     fw_audit_backend_revoke(FwCrossAudit *audit, uint32_t event_id);

/* L7: End-to-End Data Flow Security Verification */
bool     fw_audit_data_flow_verify(FwCrossAudit *audit,
                                    uint8_t src_module,
                                    uint8_t dst_module,
                                    const uint8_t *flow_hash);

/* Cross-Module Violation Detection */
uint32_t fw_audit_violation_count(FwCrossAudit *audit);
bool     fw_audit_get_entry(FwCrossAudit *audit, uint32_t entry_id,
                            FwAuditEntry *out);
bool     fw_audit_export_log(FwCrossAudit *audit,
                             uint8_t *log_buffer, uint32_t *log_size);

/* Integrity: Secure Audit Log */
bool     fw_audit_compute_integrity(FwCrossAudit *audit,
                                     uint8_t *integrity_hash);

#endif
