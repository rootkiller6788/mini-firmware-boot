#ifndef BOOT_POLICY_H
#define BOOT_POLICY_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/*
 * Boot Policy Evaluation Engine
 *
 * Implements a configurable policy engine for secure boot authorization
 * decisions. Supports: AND/OR rule composition, version constraints,
 * time-based validity windows, multi-signer requirements, and
 * recovery/failsafe policies.
 *
 * Knowledge coverage:
 *   L2: Policy-based authorization ? declarative boot policy evaluation
 *   L3: UEFI Secure Boot Policy architecture (PK/KEK/db/dbx/dbt/dbr)
 *   L5: Policy evaluation algorithm (DFS rule tree evaluation)
 *   L7: Enterprise boot policy management for fleet devices
 */

#define BP_MAX_RULES             32
#define BP_MAX_NAME_LEN          64
#define BP_MAX_DESCRIPTION_LEN   256
#define BP_MAX_POLICY_NAME       64
#define BP_MAX_SIGNERS           8
#define BP_MAX_VERSION_RANGES    4
#define BP_MAX_VENDOR_IDS        8

/* ??? Rule Operators ??? */

typedef enum {
    BP_OP_ALL_OF = 0,        /* ALL rules must match (AND) */
    BP_OP_ANY_OF,            /* ANY rule must match (OR) */
    BP_OP_N_OF_M,            /* At least N of M rules match (threshold) */
    BP_OP_NOT,               /* Rule must NOT match */
    BP_OP_ALWAYS_TRUE,       /* Always passes */
    BP_OP_ALWAYS_FALSE       /* Always fails (permanently blocked) */
} BPRuleOp;

/* ??? Rule Conditions ??? */

typedef enum {
    BP_COND_SIGNER = 0,      /* Signed by a specific entity */
    BP_COND_HASH,            /* Specific hash value */
    BP_COND_VERSION_GE,      /* Version >= threshold */
    BP_COND_VERSION_RANGE,   /* Min <= version <= Max */
    BP_COND_TIME_BEFORE,     /* Must boot before deadline */
    BP_COND_TIME_AFTER,      /* Must boot after date */
    BP_COND_VENDOR_ID,       /* Specific vendor identifier */
    BP_COND_DEVICE_CLASS,    /* Device class constraint */
    BP_COND_SECURE_BOOT_ENABLED, /* SB must be enabled */
    BP_COND_RECOVERY_MODE,   /* Only in recovery mode */
    BP_COND_SETUP_MODE,      /* Only in setup mode */
    BP_COND_PCR_MATCH,       /* PCR value matches expected */
    BP_COND_COUNT
} BPConditionType;

/* ??? Version Range ??? */

typedef struct {
    uint32_t min_version;
    uint32_t max_version;
    bool     min_inclusive;
    bool     max_inclusive;
} BPVersionRange;

/* ??? Rule Condition ??? */

typedef struct {
    BPConditionType   cond_type;
    union {
        char     signer_name[BP_MAX_NAME_LEN];
        uint8_t  hash_value[32];
        uint32_t version_threshold;
        BPVersionRange version_range;
        uint64_t deadline;
        char     vendor_id[BP_MAX_NAME_LEN];
        uint32_t device_class;
        uint8_t  pcr_expected[32];
    } value;
    uint32_t          pcr_index;        /* For PCR_MATCH condition */
    bool              negated;          /* Invert the condition result */
} BPCondition;

/* ??? Boot Rule ??? */

typedef struct {
    char              rule_name[BP_MAX_NAME_LEN];
    char              description[BP_MAX_DESCRIPTION_LEN];
    BPRuleOp          operator;
    uint32_t          n_required;       /* For N_OF_M operator */
    BPCondition       conditions[BP_MAX_SIGNERS];
    uint32_t          condition_count;
    bool              enabled;
    uint32_t          rule_id;
    uint32_t          priority;         /* Higher = evaluated first */
} BPRule;

/* ??? Policy ??? */

typedef struct {
    char        policy_name[BP_MAX_POLICY_NAME];
    BPRule      rules[BP_MAX_RULES];
    uint32_t    rule_count;
    bool        default_allow;          /* Default if no rule matches */
    bool        audit_mode;            /* Log but don't enforce */
    uint32_t    policy_version;
    uint64_t    last_updated;
} BootPolicy;

/* ??? Evaluation Context ??? */

typedef struct {
    const char *signer_name;
    const uint8_t *image_hash;
    uint32_t hash_size;
    uint32_t image_version;
    uint64_t current_time;
    const char *vendor_id;
    uint32_t device_class;
    bool secure_boot_enabled;
    bool recovery_mode;
    bool setup_mode;
    const uint8_t *pcr_values[24];     /* PCR 0-23 values */
} BPEvalContext;

/* ??? Evaluation Result ??? */

typedef enum {
    BP_RESULT_ALLOW = 0,
    BP_RESULT_DENY,
    BP_RESULT_DENY_PERMANENT,
    BP_RESULT_NEED_MORE_INFO,
    BP_RESULT_ERROR
} BPEvalResult;

typedef struct {
    BPEvalResult    result;
    char            matched_rule[BP_MAX_NAME_LEN]; /* Which rule matched */
    char            reason[BP_MAX_DESCRIPTION_LEN];/* Human-readable reason */
    uint32_t        rule_id_matched;
} BPEvalOutput;

/* ??? Policy Management ??? */

bool bp_policy_init(BootPolicy *policy, const char *name, bool default_allow);
bool bp_policy_add_rule(BootPolicy *policy, const BPRule *rule);
bool bp_policy_remove_rule(BootPolicy *policy, uint32_t rule_id);
bool bp_policy_enable_rule(BootPolicy *policy, uint32_t rule_id, bool enable);
const BPRule *bp_policy_find_rule(const BootPolicy *policy, uint32_t rule_id);

/* ??? Policy Evaluation ??? */

BPEvalOutput bp_policy_evaluate(const BootPolicy *policy,
                                 const BPEvalContext *ctx);

/*
 * Policy Evaluation Algorithm:
 *
 *   evaluate(policy, ctx):
 *     sort rules by priority (descending)
 *     for each rule in policy:
 *       result = evaluate_rule(rule, ctx)
 *       if result == ALLOW:  return ALLOW  (first match wins)
 *       if result == DENY:   return DENY
 *     return policy.default_allow ? ALLOW : DENY
 *
 *   evaluate_rule(rule, ctx):
 *     switch rule.operator:
 *       ALL_OF:  return ALL conditions evaluate to true
 *       ANY_OF:  return at least one condition evaluates to true
 *       N_OF_M:  return at least N conditions evaluate to true
 *       NOT:     return the negation of the sole condition
 *
 *   evaluate_condition(cond, ctx):
 *     value = evaluate_condition_value(cond, ctx)
 *     return cond.negated ? !value : value
 */

/* ??? Predefined Policy Templates ??? */

bool bp_create_standard_sb_policy(BootPolicy *policy);
/*
 * Standard Secure Boot Policy:
 *   1. [ANY_OF] Signer in db OR hash in db
 *   2. [NOT] Hash in dbx (blacklist)
 *   3. [NOT] Version below minimum
 *   4. Default: DENY
 */

bool bp_create_recovery_policy(BootPolicy *policy);
/*
 * Recovery Policy:
 *   1. [ANY_OF] Recovery signer OR physical presence asserted
 *   2. [ALL_OF] Recovery mode AND OEM signer
 *   3. Default: ALLOW (recovery is permissive)
 */

bool bp_create_enterprise_policy(BootPolicy *policy,
                                  const char *enterprise_signer,
                                  uint32_t min_version);
/*
 * Enterprise Policy:
 *   1. [ALL_OF] Enterprise signer AND version >= min_version
 *   2. [ALL_OF] OEM signer AND !enterprise_enrollment (fallback)
 *   3. Default: DENY
 */

/* ??? Utility ??? */

const char *bp_rule_op_str(BPRuleOp op);
const char *bp_cond_type_str(BPConditionType cond_type);
const char *bp_result_str(BPEvalResult result);
void bp_print_policy(const BootPolicy *policy);
void bp_print_eval_output(const BPEvalOutput *output);

#endif /* BOOT_POLICY_H */
