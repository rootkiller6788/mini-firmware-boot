#ifndef SMM_ATTACKS_H
#define SMM_ATTACKS_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define SMM_SMRAM_BASE             0x30000
#define SMM_SMRAM_SIZE             0x10000
#define SMM_MAX_HANDLERS           16
#define SMM_COMM_BUFFER_SIZE       4096
#define SMM_SAVE_STATE_SIZE        0x400

#define SMM_SMRR_ENABLED           (1 << 10)
#define SMM_D_OPEN                 (1 << 6)
#define SMM_D_CLS                  (1 << 5)
#define SMM_D_LCK                  (1 << 4)
#define SMM_G_SMRAME               (1 << 3)

#define SW_SMI_CODE_APM            0xB2
#define SW_SMI_CODE_COMM_BUFFER    0xEF

typedef enum {
    SMASHING_THE_STACK = 0,
    CALLING_CONV,
    SMM_PRIVILEGE_ESCALATION,
    SPEAKER_FALLBACK,
    SMSM_COUNT
} SMIAttackType;

typedef struct {
    uint32_t                   smbase;
    uint32_t                   entry_point;
    void                      *handlers[SMM_MAX_HANDLERS];
    uint8_t                    comm_buffer[SMM_COMM_BUFFER_SIZE];
    bool                       smrr_enabled;
    uint32_t                   smrr_base;
    uint32_t                   smrr_mask;
    bool                       d_lock;
    bool                       d_open;
} SMMHandler;

typedef struct {
    uint8_t  sw_smi_code;
    uint8_t *comm_buffer_ptr;
    size_t   buffer_size;
    uint32_t handler_index;
} SMMCall;

typedef struct {
    SMMHandler smm_core;
    SMMCall    active_call;
    bool       in_smm;
} SMMContext;

void smm_init(SMMContext *ctx);
bool smm_handler_register(SMMContext *ctx, uint8_t handler_index,
                          void *handler_func);
bool smm_handler_invoke(SMMContext *ctx, SMMCall *call);
bool smm_attack_confused_deputy(SMMContext *ctx, uint8_t handler_index,
                                uint8_t *crafted_buffer,
                                size_t buffer_size);
bool smm_smm_callout_check(SMMContext *ctx, uint32_t target_address);
bool smm_ring3_to_ring2_attack(SMMContext *ctx, uint32_t malicious_addr);
bool smm_set_smrr(SMMContext *ctx, uint32_t base, uint32_t mask);
bool smm_validate_smrr_access(SMMContext *ctx, uint32_t address);

#endif
