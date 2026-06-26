#include "smm_attacks.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

typedef struct {
    uint8_t magic[4];
    uint32_t command;
    uint32_t target_addr;
    uint32_t data_size;
} SMICommBuffer;

static void secure_smi_handler(SMMContext *ctx, SMMCall *call) {
    SMICommBuffer *cmd;
    uint8_t *data_ptr;

    if (call == NULL || call->comm_buffer_ptr == NULL)
        return;

    if (call->buffer_size < sizeof(SMICommBuffer))
        return;

    cmd = (SMICommBuffer *)call->comm_buffer_ptr;

    if (cmd->magic[0] == 0x53 && cmd->magic[1] == 0x4D &&
        cmd->magic[2] == 0x49 && cmd->magic[3] == 0x21) {

        if (!smm_validate_smrr_access(ctx, cmd->target_addr)) {
            return;
        }

        if (cmd->command == 0x01) {
            data_ptr = (uint8_t *)(uintptr_t)cmd->target_addr;
            *data_ptr = 0x42;
        }
    }
}

static void vulnerable_smi_handler(SMMContext *ctx, SMMCall *call) {
    SMICommBuffer *cmd;

    (void)ctx;

    if (call == NULL || call->comm_buffer_ptr == NULL)
        return;

    cmd = (SMICommBuffer *)call->comm_buffer_ptr;

    if (cmd->command == 0x01) {
        uint8_t *target = (uint8_t *)(uintptr_t)cmd->target_addr;
        *target = 0xFF;
    }
}

int main(void) {
    SMMContext smm_ctx;
    SMMCall call;
    SMICommBuffer legit_cmd, evil_cmd;
    uint8_t smram_byte;
    bool result;
    uint32_t i;

    printf("===== SMM Attack Demo =====\n\n");

    printf("[1] Initializing SMM context...\n");
    smm_init(&smm_ctx);
    printf("    SMBASE   = 0x%08X\n", smm_ctx.smm_core.smbase);
    printf("    SMRAM    = 0x%08X - 0x%08X\n",
           smm_ctx.smm_core.smbase,
           smm_ctx.smm_core.smbase + SMM_SMRAM_SIZE);
    printf("    SMRR     = %s\n", smm_ctx.smm_core.smrr_enabled ? "ON" : "OFF");

    printf("\n[2] Enabling SMRR protection...\n");
    smm_ctx.in_smm = true;
    result = smm_set_smrr(&smm_ctx, 0x30000, 0xFFFF0000);
    smm_ctx.in_smm = false;
    printf("    SMRR set: base=0x30000, mask=0xFFFF0000 => %s\n",
           result ? "OK" : "FAILED");

    printf("\n[3] Registering secure SMI handler...\n");
    result = smm_handler_register(&smm_ctx, 0, (void *)secure_smi_handler);
    printf("    Handler 0 registered => %s\n", result ? "OK" : "FAILED");

    printf("\n[4] Legitimate SMI call within SMRAM bounds...\n");
    memset(&legit_cmd, 0, sizeof(legit_cmd));
    legit_cmd.magic[0] = 0x53; legit_cmd.magic[1] = 0x4D;
    legit_cmd.magic[2] = 0x49; legit_cmd.magic[3] = 0x21;
    legit_cmd.command = 0x01;
    legit_cmd.target_addr = SMM_SMRAM_BASE + 0x2000;
    legit_cmd.data_size = 4;

    memset(&call, 0, sizeof(call));
    call.sw_smi_code = SW_SMI_CODE_COMM_BUFFER;
    call.handler_index = 0;
    call.comm_buffer_ptr = (uint8_t *)&legit_cmd;
    call.buffer_size = sizeof(legit_cmd);

    result = smm_handler_invoke(&smm_ctx, &call);
    printf("    Legit SMI call => %s\n", result ? "SUCCESS" : "DENIED");

    printf("\n[5] Confused Deputy Attack with crafted buffer...\n");
    memset(&evil_cmd, 0, sizeof(evil_cmd));
    evil_cmd.magic[0] = 0x53; evil_cmd.magic[1] = 0x4D;
    evil_cmd.magic[2] = 0x49; evil_cmd.magic[3] = 0x21;
    evil_cmd.command = 0x01;
    evil_cmd.target_addr = 0x00001000;
    evil_cmd.data_size = 1;

    result = smm_attack_confused_deputy(&smm_ctx, 0,
                                         (uint8_t *)&evil_cmd,
                                         sizeof(evil_cmd));
    printf("    Confused deputy attack => %s\n",
           result ? "EXECUTED (BAD!)" : "BLOCKED (GOOD)");

    printf("\n[6] SMM Callout Check...\n");
    result = smm_smm_callout_check(&smm_ctx, SMM_SMRAM_BASE + 0x100);
    printf("    Address 0x%08X (inside SMRAM) => %s\n",
           SMM_SMRAM_BASE + 0x100, result ? "VALID" : "CALL OUT!");

    result = smm_smm_callout_check(&smm_ctx, 0x00010000);
    printf("    Address 0x00010000 (outside SMRAM) => %s\n",
           result ? "VALID" : "CALL OUT!");

    printf("\n[7] Ring 3 to Ring -2 escalation attempt...\n");
    result = smm_ring3_to_ring2_attack(&smm_ctx, SMM_SMRAM_BASE + 0x5000);
    printf("    Access SMRAM at 0x%08X => %s\n",
           SMM_SMRAM_BASE + 0x5000,
           result ? "POSSIBLE (D_OPEN)" : "BLOCKED");

    smm_ctx.smm_core.d_lock = true;
    smm_ctx.smm_core.d_open = false;

    result = smm_ring3_to_ring2_attack(&smm_ctx, SMM_SMRAM_BASE + 0x5000);
    printf("    After D_LCK + D_CLS => %s\n",
           result ? "POSSIBLE" : "BLOCKED (D_LCK)");

    printf("\n[8] SMRR validation test...\n");
    for (i = 0; i < 5; i++) {
        uint32_t test_addr = 0x20000 + (i * 0x10000);
        result = smm_validate_smrr_access(&smm_ctx, test_addr);
        printf("    SMRR check 0x%08X => %s\n",
               test_addr, result ? "ALLOWED" : "BLOCKED");
    }

    printf("\n===== Demo Complete =====\n");
    return 0;
}
