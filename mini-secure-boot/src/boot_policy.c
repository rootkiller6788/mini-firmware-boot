#include "boot_policy.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

/*
 * Boot Policy Evaluation Engine Implementation
 *
 * Implemented as a rule-based authorization engine for secure boot.
 * Rules are evaluated in priority order with first-match semantics.
 * Conditions support boolean operators: ALL_OF, ANY_OF, N_OF_M, NOT.
 *
 * Algorithm complexity: O(R * C) where R = rules, C = conditions per rule.
 */

/* ??? Static helpers ?????????????????????????????????????????????????? */

static int rule_priority_compare(const void *a, const void *b)
{
    const BPRule *ra = (const BPRule *)a;
    const BPRule *rb = (const BPRule *)b;
    /* Descending priority: higher priority first */
    if (ra->priority > rb->priority) return -1;
    if (ra->priority < rb->priority) return 1;
    return 0;
}

static void sort_rules_by_priority(BPRule *rules, uint32_t count)
{
    if (count > 1) {
        qsort(rules, count, sizeof(BPRule), rule_priority_compare);
    }
}

static bool evaluate_condition(const BPCondition *cond, const BPEvalContext *ctx)
{
    if (!cond || !ctx) return false;

    bool result = false;

    switch (cond->cond_type) {
        case BP_COND_SIGNER:
            /* Match signer name against context */
            if (ctx->signer_name) {
                result = (strncmp(cond->value.signer_name, ctx->signer_name,
                                  BP_MAX_NAME_LEN) == 0);
            }
            break;

        case BP_COND_HASH:
            /* Exact hash match */
            if (ctx->image_hash && ctx->hash_size == 32) {
                result = (memcmp(cond->value.hash_value, ctx->image_hash, 32) == 0);
            }
            break;

        case BP_COND_VERSION_GE:
            /* Version greater-than-or-equal */
            result = (ctx->image_version >= cond->value.version_threshold);
            break;

        case BP_COND_VERSION_RANGE:
            /* Version within range [min, max] with inclusive/exclusive bounds */
            {
                const BPVersionRange *r = &cond->value.version_range;
                bool above_min = r->min_inclusive ?
                    ctx->image_version >= r->min_version :
                    ctx->image_version > r->min_version;
                bool below_max = r->max_inclusive ?
                    ctx->image_version <= r->max_version :
                    ctx->image_version < r->max_version;
                result = above_min && below_max;
            }
            break;

        case BP_COND_TIME_BEFORE:
            /* Current time must be before deadline */
            result = (ctx->current_time < cond->value.deadline);
            break;

        case BP_COND_TIME_AFTER:
            /* Current time must be after the specified date */
            result = (ctx->current_time > cond->value.deadline);
            break;

        case BP_COND_VENDOR_ID:
            /* Vendor ID string match */
            if (ctx->vendor_id) {
                result = (strncmp(cond->value.vendor_id, ctx->vendor_id,
                                  BP_MAX_NAME_LEN) == 0);
            }
            break;

        case BP_COND_DEVICE_CLASS:
            /* Device class match (bitmask) */
            result = (ctx->device_class == cond->value.device_class);
            break;

        case BP_COND_SECURE_BOOT_ENABLED:
            /* Require Secure Boot to be enabled */
            result = ctx->secure_boot_enabled;
            break;

        case BP_COND_RECOVERY_MODE:
            /* Only allow in recovery mode */
            result = ctx->recovery_mode;
            break;

        case BP_COND_SETUP_MODE:
            /* Only allow in setup mode */
            result = ctx->setup_mode;
            break;

        case BP_COND_PCR_MATCH:
            /* PCR value at index must match expected */
            if (cond->pcr_index < 24 && ctx->pcr_values[cond->pcr_index]) {
                result = (memcmp(cond->value.pcr_expected,
                                 ctx->pcr_values[cond->pcr_index], 32) == 0);
            }
            break;

        default:
            result = false;
            break;
    }

    return cond->negated ? !result : result;
}

static BPEvalResult evaluate_rule(const BPRule *rule, const BPEvalContext *ctx,
                                   BPEvalOutput *output)
{
    if (!rule || !ctx || !output || !rule->enabled) return BP_RESULT_NEED_MORE_INFO;

    bool rule_result = false;

    switch (rule->operator) {
        case BP_OP_ALWAYS_TRUE:
            rule_result = true;
            break;

        case BP_OP_ALWAYS_FALSE:
            rule_result = false;
            break;

        case BP_OP_ALL_OF: {
            /* ALL conditions must be true */
            rule_result = true;
            for (uint32_t i = 0; i < rule->condition_count; i++) {
                if (!evaluate_condition(&rule->conditions[i], ctx)) {
                    rule_result = false;
                    break;
                }
            }
            break;
        }

        case BP_OP_ANY_OF: {
            /* At least one condition must be true */
            rule_result = false;
            for (uint32_t i = 0; i < rule->condition_count; i++) {
                if (evaluate_condition(&rule->conditions[i], ctx)) {
                    rule_result = true;
                    break;
                }
            }
            break;
        }

        case BP_OP_N_OF_M: {
            /* At least N of M conditions must be true */
            uint32_t matched = 0;
            for (uint32_t i = 0; i < rule->condition_count; i++) {
                if (evaluate_condition(&rule->conditions[i], ctx)) {
                    matched++;
                }
            }
            rule_result = (matched >= rule->n_required);
            break;
        }

        case BP_OP_NOT: {
            /* Negate the result of the first condition */
            if (rule->condition_count > 0) {
                rule_result = !evaluate_condition(&rule->conditions[0], ctx);
            } else {
                rule_result = false;
            }
            break;
        }

        default:
            rule_result = false;
            break;
    }

    if (rule_result) {
        /* Rule matched: determine if it's an ALLOW or DENY rule.
         * By convention: rules with ALWAYS_TRUE or ANY_OF/ALL_OF with
         * positive conditions are ALLOW rules. Rules with NOT or
         * ALWAYS_FALSE are DENY rules.
         * Additionally, in production systems, rules can be tagged as
         * 'allow' or 'deny' rules. Here we use a heuristic: if the
         * rule's operator is NOT or ALWAYS_FALSE, it's a DENY. */
        if (rule->operator == BP_OP_NOT || rule->operator == BP_OP_ALWAYS_FALSE) {
            output->result = BP_RESULT_DENY;
            snprintf(output->reason, BP_MAX_DESCRIPTION_LEN,
                     "Blocked by deny-rule [%s]: %s",
                     rule->rule_name, rule->description);
        } else {
            output->result = BP_RESULT_ALLOW;
            snprintf(output->reason, BP_MAX_DESCRIPTION_LEN,
                     "Allowed by rule [%s]: %s",
                     rule->rule_name, rule->description);
        }
        snprintf(output->matched_rule, BP_MAX_NAME_LEN, "%s", rule->rule_name);
        output->rule_id_matched = rule->rule_id;
        return output->result;
    }

    return BP_RESULT_NEED_MORE_INFO;
}

/* ??? Policy Management ??????????????????????????????????????????????? */

bool bp_policy_init(BootPolicy *policy, const char *name, bool default_allow)
{
    if (!policy || !name) return false;
    memset(policy, 0, sizeof(BootPolicy));
    snprintf(policy->policy_name, BP_MAX_POLICY_NAME, "%s", name);
    policy->default_allow = default_allow;
    policy->audit_mode = false;
    policy->policy_version = 1;
    policy->last_updated = (uint64_t)time(NULL);
    return true;
}

bool bp_policy_add_rule(BootPolicy *policy, const BPRule *rule)
{
    if (!policy || !rule) return false;
    if (policy->rule_count >= BP_MAX_RULES) return false;

    /* Check for duplicate rule_id */
    for (uint32_t i = 0; i < policy->rule_count; i++) {
        if (policy->rules[i].rule_id == rule->rule_id) return false;
    }

    memcpy(&policy->rules[policy->rule_count], rule, sizeof(BPRule));
    policy->rule_count++;
    policy->policy_version++;
    policy->last_updated = (uint64_t)time(NULL);

    /* Re-sort rules by priority */
    sort_rules_by_priority(policy->rules, policy->rule_count);
    return true;
}

bool bp_policy_remove_rule(BootPolicy *policy, uint32_t rule_id)
{
    if (!policy) return false;
    for (uint32_t i = 0; i < policy->rule_count; i++) {
        if (policy->rules[i].rule_id == rule_id) {
            /* Shift remaining rules down */
            for (uint32_t j = i; j < policy->rule_count - 1; j++) {
                memcpy(&policy->rules[j], &policy->rules[j + 1], sizeof(BPRule));
            }
            policy->rule_count--;
            policy->policy_version++;
            policy->last_updated = (uint64_t)time(NULL);
            return true;
        }
    }
    return false;
}

bool bp_policy_enable_rule(BootPolicy *policy, uint32_t rule_id, bool enable)
{
    if (!policy) return false;
    for (uint32_t i = 0; i < policy->rule_count; i++) {
        if (policy->rules[i].rule_id == rule_id) {
            policy->rules[i].enabled = enable;
            return true;
        }
    }
    return false;
}

const BPRule *bp_policy_find_rule(const BootPolicy *policy, uint32_t rule_id)
{
    if (!policy) return NULL;
    for (uint32_t i = 0; i < policy->rule_count; i++) {
        if (policy->rules[i].rule_id == rule_id) return &policy->rules[i];
    }
    return NULL;
}

/* ??? Policy Evaluation ??????????????????????????????????????????????? */

BPEvalOutput bp_policy_evaluate(const BootPolicy *policy,
                                 const BPEvalContext *ctx)
{
    BPEvalOutput output;
    memset(&output, 0, sizeof(BPEvalOutput));
    output.result = policy && policy->default_allow ?
                    BP_RESULT_ALLOW : BP_RESULT_DENY;
    snprintf(output.reason, BP_MAX_DESCRIPTION_LEN, "Default policy: %s",
             output.result == BP_RESULT_ALLOW ? "ALLOW" : "DENY");

    if (!policy || !ctx) return output;

    /*
     * Policy Evaluation Algorithm:
     *
     * evaluate(policy, ctx):
     *   1. Sort rules by priority (descending) [done at add time]
     *   2. For each enabled rule in priority order:
     *      a. Evaluate all conditions according to the rule's operator
     *      b. If rule matches (conditions satisfied):
     *         - ALLOW rule ? return BP_RESULT_ALLOW
     *         - DENY rule ? return BP_RESULT_DENY
     *   3. If no rules matched: return default policy
     */

    for (uint32_t i = 0; i < policy->rule_count; i++) {
        const BPRule *rule = &policy->rules[i];
        if (!rule->enabled) continue;

        BPEvalResult rule_result = evaluate_rule(rule, ctx, &output);
        if (rule_result == BP_RESULT_ALLOW || rule_result == BP_RESULT_DENY) {
            return output; /* First matching rule decides */
        }
    }

    /* No rule matched; apply default */
    if (policy->default_allow) {
        output.result = BP_RESULT_ALLOW;
        snprintf(output.reason, BP_MAX_DESCRIPTION_LEN,
                 "No rule matched ? default ALLOW (audit_mode=%s)",
                 policy->audit_mode ? "ON" : "OFF");
    } else {
        output.result = BP_RESULT_DENY;
        snprintf(output.reason, BP_MAX_DESCRIPTION_LEN,
                 "No rule matched ? default DENY (policy: %s)",
                 policy->policy_name);
    }
    return output;
}

/* ??? Predefined Policy Templates ????????????????????????????????????? */

bool bp_create_standard_sb_policy(BootPolicy *policy)
{
    if (!policy) return false;

    bp_policy_init(policy, "Standard-SecureBoot", false);

    /* Rule 1: ANY_OF(Signer in db, Hash in db) ? ALLOW */
    BPRule allow_rule;
    memset(&allow_rule, 0, sizeof(BPRule));
    snprintf(allow_rule.rule_name, BP_MAX_NAME_LEN, "db-whitelist");
    snprintf(allow_rule.description, BP_MAX_DESCRIPTION_LEN,
             "Allow if signed by trusted signer OR hash whitelisted");
    allow_rule.operator = BP_OP_ANY_OF;
    allow_rule.priority = 100;
    allow_rule.enabled = true;
    allow_rule.rule_id = 1;
    allow_rule.condition_count = 2;
    allow_rule.conditions[0].cond_type = BP_COND_SIGNER;
    snprintf(allow_rule.conditions[0].value.signer_name, BP_MAX_NAME_LEN,
             "OEM-SecureBoot-Signer");
    allow_rule.conditions[1].cond_type = BP_COND_HASH;
    memset(allow_rule.conditions[1].value.hash_value, 0, 32);
    /* In real use, hash value would be pre-computed.
     * Here we mark this condition as 'present but unset' */
    bp_policy_add_rule(policy, &allow_rule);

    /* Rule 2: NOT(Hash in dbx) ? DENY if blacklisted */
    BPRule deny_blacklist;
    memset(&deny_blacklist, 0, sizeof(BPRule));
    snprintf(deny_blacklist.rule_name, BP_MAX_NAME_LEN, "dbx-blacklist");
    snprintf(deny_blacklist.description, BP_MAX_DESCRIPTION_LEN,
             "Deny if image hash is in forbidden signature database");
    deny_blacklist.operator = BP_OP_NOT;
    deny_blacklist.priority = 90;
    deny_blacklist.enabled = true;
    deny_blacklist.rule_id = 2;
    deny_blacklist.condition_count = 1;
    deny_blacklist.conditions[0].cond_type = BP_COND_HASH;
    /* In production, the dbx hash is populated from UEFI variable */
    bp_policy_add_rule(policy, &deny_blacklist);

    /* Rule 3: NOT(Version < min_version) ? DENY if too old */
    BPRule deny_rollback;
    memset(&deny_rollback, 0, sizeof(BPRule));
    snprintf(deny_rollback.rule_name, BP_MAX_NAME_LEN, "anti-rollback");
    snprintf(deny_rollback.description, BP_MAX_DESCRIPTION_LEN,
             "Deny if image version is below minimum allowed");
    deny_rollback.operator = BP_OP_NOT;
    deny_rollback.priority = 80;
    deny_rollback.enabled = true;
    deny_rollback.rule_id = 3;
    deny_rollback.condition_count = 1;
    deny_rollback.conditions[0].cond_type = BP_COND_VERSION_GE;
    deny_rollback.conditions[0].value.version_threshold = 1;
    bp_policy_add_rule(policy, &deny_rollback);

    return true;
}

bool bp_create_recovery_policy(BootPolicy *policy)
{
    if (!policy) return false;

    bp_policy_init(policy, "Recovery-Policy", true);

    /* Rule 1: ANY_OF(Recovery signer OR setup mode) ? ALLOW */
    BPRule rule1;
    memset(&rule1, 0, sizeof(BPRule));
    snprintf(rule1.rule_name, BP_MAX_NAME_LEN, "recovery-access");
    snprintf(rule1.description, BP_MAX_DESCRIPTION_LEN,
             "Allow recovery-signed images or setup-mode operations");
    rule1.operator = BP_OP_ANY_OF;
    rule1.priority = 100;
    rule1.enabled = true;
    rule1.rule_id = 1;
    rule1.condition_count = 2;
    rule1.conditions[0].cond_type = BP_COND_SIGNER;
    snprintf(rule1.conditions[0].value.signer_name, BP_MAX_NAME_LEN,
             "OEM-Recovery-Signer");
    rule1.conditions[1].cond_type = BP_COND_SETUP_MODE;
    bp_policy_add_rule(policy, &rule1);

    /* Rule 2: ALL_OF(Recovery mode AND OEM signer) ? ALLOW */
    BPRule rule2;
    memset(&rule2, 0, sizeof(BPRule));
    snprintf(rule2.rule_name, BP_MAX_NAME_LEN, "recovery-oem");
    snprintf(rule2.description, BP_MAX_DESCRIPTION_LEN,
             "Allow OEM-signed images in recovery mode");
    rule2.operator = BP_OP_ALL_OF;
    rule2.priority = 50;
    rule2.enabled = true;
    rule2.rule_id = 2;
    rule2.condition_count = 2;
    rule2.conditions[0].cond_type = BP_COND_RECOVERY_MODE;
    rule2.conditions[1].cond_type = BP_COND_SIGNER;
    snprintf(rule2.conditions[1].value.signer_name, BP_MAX_NAME_LEN,
             "OEM-SecureBoot-Signer");
    bp_policy_add_rule(policy, &rule2);

    return true;
}

bool bp_create_enterprise_policy(BootPolicy *policy,
                                  const char *enterprise_signer,
                                  uint32_t min_version)
{
    if (!policy || !enterprise_signer) return false;

    char pol_name[BP_MAX_POLICY_NAME];
    snprintf(pol_name, BP_MAX_POLICY_NAME, "Enterprise-%s", enterprise_signer);
    bp_policy_init(policy, pol_name, false);

    /* Rule 1: ALL_OF(Enterprise signer AND version >= min) ? ALLOW */
    BPRule rule1;
    memset(&rule1, 0, sizeof(BPRule));
    snprintf(rule1.rule_name, BP_MAX_NAME_LEN, "enterprise-primary");
    snprintf(rule1.description, BP_MAX_DESCRIPTION_LEN,
             "Allow enterprise-signed images meeting minimum version");
    rule1.operator = BP_OP_ALL_OF;
    rule1.priority = 100;
    rule1.enabled = true;
    rule1.rule_id = 1;
    rule1.condition_count = 2;
    rule1.conditions[0].cond_type = BP_COND_SIGNER;
    snprintf(rule1.conditions[0].value.signer_name, BP_MAX_NAME_LEN,
             "%s", enterprise_signer);
    rule1.conditions[1].cond_type = BP_COND_VERSION_GE;
    rule1.conditions[1].value.version_threshold = min_version;
    bp_policy_add_rule(policy, &rule1);

    /* Rule 2: ALL_OF(OEM signer AND Secure Boot enabled) ? ALLOW */
    BPRule rule2;
    memset(&rule2, 0, sizeof(BPRule));
    snprintf(rule2.rule_name, BP_MAX_NAME_LEN, "oem-fallback");
    snprintf(rule2.description, BP_MAX_DESCRIPTION_LEN,
             "Fallback: allow OEM-signed images if SB is enabled");
    rule2.operator = BP_OP_ALL_OF;
    rule2.priority = 50;
    rule2.enabled = true;
    rule2.rule_id = 2;
    rule2.condition_count = 2;
    rule2.conditions[0].cond_type = BP_COND_SIGNER;
    snprintf(rule2.conditions[0].value.signer_name, BP_MAX_NAME_LEN,
             "OEM-SecureBoot-Signer");
    rule2.conditions[1].cond_type = BP_COND_SECURE_BOOT_ENABLED;
    bp_policy_add_rule(policy, &rule2);

    return true;
}

/* ??? Utility ????????????????????????????????????????????????????????? */

const char *bp_rule_op_str(BPRuleOp op)
{
    switch (op) {
        case BP_OP_ALL_OF:       return "ALL_OF (AND)";
        case BP_OP_ANY_OF:       return "ANY_OF (OR)";
        case BP_OP_N_OF_M:       return "N_OF_M";
        case BP_OP_NOT:          return "NOT";
        case BP_OP_ALWAYS_TRUE:  return "ALWAYS_TRUE";
        case BP_OP_ALWAYS_FALSE: return "ALWAYS_FALSE";
        default:                 return "UNKNOWN";
    }
}

const char *bp_cond_type_str(BPConditionType cond_type)
{
    switch (cond_type) {
        case BP_COND_SIGNER:              return "SIGNER";
        case BP_COND_HASH:                return "HASH";
        case BP_COND_VERSION_GE:          return "VERSION_GE";
        case BP_COND_VERSION_RANGE:       return "VERSION_RANGE";
        case BP_COND_TIME_BEFORE:         return "TIME_BEFORE";
        case BP_COND_TIME_AFTER:          return "TIME_AFTER";
        case BP_COND_VENDOR_ID:           return "VENDOR_ID";
        case BP_COND_DEVICE_CLASS:        return "DEVICE_CLASS";
        case BP_COND_SECURE_BOOT_ENABLED: return "SB_ENABLED";
        case BP_COND_RECOVERY_MODE:       return "RECOVERY_MODE";
        case BP_COND_SETUP_MODE:          return "SETUP_MODE";
        case BP_COND_PCR_MATCH:           return "PCR_MATCH";
        default:                          return "UNKNOWN";
    }
}

const char *bp_result_str(BPEvalResult result)
{
    switch (result) {
        case BP_RESULT_ALLOW:           return "ALLOW";
        case BP_RESULT_DENY:            return "DENY";
        case BP_RESULT_DENY_PERMANENT:  return "DENY_PERMANENT";
        case BP_RESULT_NEED_MORE_INFO:  return "NEED_MORE_INFO";
        case BP_RESULT_ERROR:           return "ERROR";
        default:                        return "UNKNOWN";
    }
}

void bp_print_policy(const BootPolicy *policy)
{
    if (!policy) return;
    printf("????????????????????????????????????????????????????\n");
    printf("? BOOT POLICY: %-35s ?\n", policy->policy_name);
    printf("????????????????????????????????????????????????????\n");
    printf("? Version: %-4u  Default: %-6s  Audit: %-4s  ?\n",
           policy->policy_version,
           policy->default_allow ? "ALLOW" : "DENY",
           policy->audit_mode ? "ON" : "OFF");
    printf("? Rules: %-3u                                  ?\n",
           policy->rule_count);
    printf("????????????????????????????????????????????????????\n");

    for (uint32_t i = 0; i < policy->rule_count; i++) {
        const BPRule *r = &policy->rules[i];
        printf("? [%u] ID=%-4u %-15s Pri=%-3u %-3s ?\n",
               i, r->rule_id, r->rule_name, r->priority,
               r->enabled ? "ON" : "OFF");
        printf("?     Op: %-20s N=%u               ?\n",
               bp_rule_op_str(r->operator), r->n_required);
        printf("?     Conditions: %u                             ?\n",
               r->condition_count);
        for (uint32_t j = 0; j < r->condition_count; j++) {
            printf("?       [%u] %-20s %s              ?\n",
                   j, bp_cond_type_str(r->conditions[j].cond_type),
                   r->conditions[j].negated ? "(NOT)" : "");
        }
        if (i < policy->rule_count - 1) {
            printf("????????????????????????????????????????????????????\n");
        }
    }
    printf("????????????????????????????????????????????????????\n");
}

void bp_print_eval_output(const BPEvalOutput *output)
{
    if (!output) return;
    printf("=== Policy Evaluation Result ===\n");
    printf("Result: %s\n", bp_result_str(output->result));
    if (output->matched_rule[0]) {
        printf("Matched Rule: %s (ID=%u)\n",
               output->matched_rule, output->rule_id_matched);
    }
    printf("Reason: %s\n", output->reason);
}
