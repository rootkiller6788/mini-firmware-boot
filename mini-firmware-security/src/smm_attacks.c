#include "smm_attacks.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

typedef void (*smi_handler_func)(SMMContext *, SMMCall *);

static void default_smi_handler(SMMContext *ctx, SMMCall *call) {
    (void)ctx;
    (void)call;
}

void smm_init(SMMContext *ctx) {
    uint32_t i;

    if (ctx == NULL)
        return;

    memset(ctx, 0, sizeof(SMMContext));

    ctx->smm_core.smbase = SMM_SMRAM_BASE;
    ctx->smm_core.entry_point = SMM_SMRAM_BASE + 0x8000;
    ctx->smm_core.smrr_enabled = false;
    ctx->smm_core.smrr_base = 0;
    ctx->smm_core.smrr_mask = 0;
    ctx->smm_core.d_lock = false;
    ctx->smm_core.d_open = true;
    ctx->in_smm = false;

    for (i = 0; i < SMM_MAX_HANDLERS; i++) {
        ctx->smm_core.handlers[i] = NULL;
        ctx->smm_core.comm_buffer[i] = 0;
    }

    for (i = 0; i < SMM_COMM_BUFFER_SIZE; i++) {
        ctx->smm_core.comm_buffer[i] = 0;
    }
}

bool smm_handler_register(SMMContext *ctx, uint8_t handler_index,
                          void *handler_func) {
    if (ctx == NULL)
        return false;

    if (handler_index >= SMM_MAX_HANDLERS)
        return false;

    if (ctx->smm_core.d_lock)
        return false;

    ctx->smm_core.handlers[handler_index] = handler_func;
    return true;
}

bool smm_handler_invoke(SMMContext *ctx, SMMCall *call) {
    smi_handler_func handler;

    if (ctx == NULL || call == NULL)
        return false;

    if (call->handler_index >= SMM_MAX_HANDLERS)
        return false;

    handler = (smi_handler_func)ctx->smm_core.handlers[call->handler_index];
    if (handler == NULL)
        return false;

    if (call->comm_buffer_ptr != NULL && call->buffer_size > 0) {
        if (call->buffer_size > SMM_COMM_BUFFER_SIZE)
            return false;

        memcpy(ctx->smm_core.comm_buffer,
               call->comm_buffer_ptr,
               call->buffer_size);
    }

    ctx->in_smm = true;
    handler(ctx, call);
    ctx->in_smm = false;

    return true;
}

bool smm_attack_confused_deputy(SMMContext *ctx, uint8_t handler_index,
                                uint8_t *crafted_buffer,
                                size_t buffer_size) {
    SMMCall call;
    uint8_t *overwrite_target;

    if (ctx == NULL || crafted_buffer == NULL)
        return false;

    if (buffer_size > SMM_COMM_BUFFER_SIZE)
        return false;

    memset(&call, 0, sizeof(SMMCall));
    call.sw_smi_code = SW_SMI_CODE_COMM_BUFFER;
    call.handler_index = handler_index;
    call.comm_buffer_ptr = crafted_buffer;
    call.buffer_size = buffer_size;

    if (!smm_validate_smrr_access(ctx,
            (uint32_t)(uintptr_t)crafted_buffer)) {
        return false;
    }

    overwrite_target = (uint8_t *)0xDEAD0000;
    memcpy(overwrite_target, crafted_buffer, buffer_size);

    return smm_handler_invoke(ctx, &call);
}

bool smm_smm_callout_check(SMMContext *ctx, uint32_t target_address) {
    uint32_t smm_base, smm_limit;

    if (ctx == NULL)
        return false;

    smm_base = ctx->smm_core.smbase;
    smm_limit = smm_base + SMM_SMRAM_SIZE;

    if (target_address >= smm_base && target_address < smm_limit) {
        return true;
    }

    if (ctx->smm_core.smrr_enabled) {
        uint32_t smrr_base = ctx->smm_core.smrr_base;
        uint32_t smrr_limit = smrr_base + (~ctx->smm_core.smrr_mask + 1);
        if (target_address >= smrr_base && target_address < smrr_limit)
            return true;
    }

    return false;
}

bool smm_ring3_to_ring2_attack(SMMContext *ctx, uint32_t malicious_addr) {
    uint32_t smm_base, smm_limit;

    if (ctx == NULL)
        return false;

    smm_base = ctx->smm_core.smbase;
    smm_limit = smm_base + SMM_SMRAM_SIZE;

    if (malicious_addr >= smm_base && malicious_addr < smm_limit) {
        if (ctx->smm_core.d_open && !ctx->smm_core.d_lock) {
            return true;
        }
    }

    if (ctx->smm_core.smrr_enabled) {
        uint32_t smrr_base = ctx->smm_core.smrr_base;
        uint32_t smrr_limit = smrr_base + (~ctx->smm_core.smrr_mask + 1);
        if (malicious_addr >= smrr_base && malicious_addr < smrr_limit) {
            return true;
        }
    }

    return false;
}

bool smm_set_smrr(SMMContext *ctx, uint32_t base, uint32_t mask) {
    if (ctx == NULL)
        return false;

    if (!ctx->in_smm)
        return false;

    ctx->smm_core.smrr_enabled = true;
    ctx->smm_core.smrr_base = base;
    ctx->smm_core.smrr_mask = mask;

    return true;
}

bool smm_validate_smrr_access(SMMContext *ctx, uint32_t address) {
    uint32_t smrr_base, smrr_limit;

    if (ctx == NULL)
        return false;

    if (!ctx->smm_core.smrr_enabled)
        return true;

    smrr_base = ctx->smm_core.smrr_base;
    smrr_limit = smrr_base + (~ctx->smm_core.smrr_mask + 1);

    if (address >= smrr_base && address < smrr_limit)
        return true;

    return false;
}
