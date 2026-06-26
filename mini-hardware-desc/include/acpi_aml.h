#ifndef ACPI_AML_H
#define ACPI_AML_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define AML_MAX_SCOPE_DEPTH  32
#define AML_MAX_VARS         128
#define AML_MAX_NAME_LEN     64
#define AML_MAX_METHOD_DEPTH 16
#define AML_MAX_PACKAGE_SIZE 256
#define AML_MAX_STACK_DEPTH  64

/* AML Opcodes */
#define AML_ZERO_OP          0x00
#define AML_ONE_OP           0x01
#define AML_ALIAS_OP         0x06
#define AML_NAME_OP          0x08
#define AML_BYTE_PREFIX      0x0A
#define AML_WORD_PREFIX      0x0B
#define AML_DWORD_PREFIX     0x0C
#define AML_STRING_PREFIX    0x0D
#define AML_QWORD_PREFIX     0x0E
#define AML_SCOPE_OP         0x10
#define AML_BUFFER_OP        0x11
#define AML_PACKAGE_OP       0x12
#define AML_VAR_PACKAGE_OP   0x13
#define AML_METHOD_OP        0x14
#define AML_EXTERNAL_OP      0x15
#define AML_DUAL_NAME_PREFIX 0x2E
#define AML_MULTI_NAME_PREFIX 0x2F
#define AML_NAME_CHAR_A      0x41
#define AML_ROOT_CHAR        0x5C
#define AML_PARENT_CHAR      0x5E
#define AML_LOCAL0           0x60
#define AML_LOCAL7           0x67
#define AML_ARG0             0x68
#define AML_ARG6             0x6E
#define AML_STORE_OP         0x70
#define AML_REF_OF_OP        0x71
#define AML_ADD_OP           0x72
#define AML_CONCAT_OP        0x73
#define AML_SUBTRACT_OP      0x74
#define AML_INCREMENT_OP     0x75
#define AML_DECREMENT_OP     0x76
#define AML_MULTIPLY_OP      0x77
#define AML_DIVIDE_OP        0x78
#define AML_SHIFT_LEFT_OP    0x79
#define AML_SHIFT_RIGHT_OP   0x7A
#define AML_AND_OP           0x7B
#define AML_NAND_OP          0x7C
#define AML_OR_OP            0x7D
#define AML_NOR_OP           0x7E
#define AML_XOR_OP           0x7F
#define AML_NOT_OP           0x80
#define AML_FIND_SET_LEFT_BIT_OP 0x81
#define AML_FIND_SET_RIGHT_BIT_OP 0x82
#define AML_DEREF_OF_OP      0x83
#define AML_CONCAT_RES_OP    0x84
#define AML_MOD_OP           0x85
#define AML_NOTIFY_OP        0x86
#define AML_SIZE_OF_OP       0x87
#define AML_INDEX_OP         0x88
#define AML_MATCH_OP         0x89
#define AML_CREATE_DWORD_FIELD_OP 0x8A
#define AML_CREATE_WORD_FIELD_OP  0x8B
#define AML_CREATE_BYTE_FIELD_OP  0x8C
#define AML_CREATE_BIT_FIELD_OP   0x8D
#define AML_OBJECT_TYPE_OP   0x8E
#define AML_CREATE_QWORD_FIELD_OP 0x8F
#define AML_LAND_OP          0x90
#define AML_LOR_OP           0x91
#define AML_LNOT_OP          0x92
#define AML_LEQUAL_OP        0x93
#define AML_LGREATER_OP      0x94
#define AML_LLESS_OP         0x95
#define AML_TO_BUFFER_OP     0x96
#define AML_TO_DEC_STRING_OP 0x97
#define AML_TO_HEX_STRING_OP 0x98
#define AML_TO_INTEGER_OP    0x99
#define AML_TO_STRING_OP     0x9C
#define AML_COPY_OBJECT_OP   0x9D
#define AML_MID_OP           0x9E
#define AML_CONTINUE_OP      0x9F
#define AML_IF_OP            0xA0
#define AML_ELSE_OP          0xA1
#define AML_WHILE_OP         0xA2
#define AML_NOOP_OP          0xA3
#define AML_RETURN_OP        0xA4
#define AML_BREAK_OP         0xA5
#define AML_BREAK_POINT_OP   0xCC
#define AML_ONES_OP          0xFF

/* AML value types */
typedef enum {
    AML_VAL_NONE = 0,
    AML_VAL_INTEGER,
    AML_VAL_STRING,
    AML_VAL_BUFFER,
    AML_VAL_PACKAGE,
    AML_VAL_BUFFER_FIELD,
    AML_VAL_DEVICE,
    AML_VAL_EVENT,
    AML_VAL_METHOD,
    AML_VAL_MUTEX,
    AML_VAL_OP_REGION,
    AML_VAL_POWER_RES,
    AML_VAL_PROCESSOR,
    AML_VAL_THERMAL_ZONE,
    AML_VAL_BUFFER_FIELD_OBJ
} AMLValueType;

typedef struct {
    AMLValueType type;
    union {
        uint64_t integer;
        struct {
            char   data[512];
            size_t length;
        } string_or_buffer;
        struct {
            size_t count;
            struct AMLValue *elements[AML_MAX_PACKAGE_SIZE];
        } package;
    };
} AMLValue;

typedef struct {
    char     name[AML_MAX_NAME_LEN];
    AMLValue value;
    bool     is_arg;
    bool     is_local;
    uint8_t  index;
} AMLVariable;

typedef struct {
    char  name[AML_MAX_NAME_LEN];
    AMLValue value;
} AMLField;

typedef struct {
    bool    valid;
    uint8_t opcode;
    char    name[AML_MAX_NAME_LEN];
    size_t  start_offset;
    size_t  end_offset;
    uint8_t arg_count;
    bool    needs_package;
} AMLMethod;

typedef struct {
    char            name[AML_MAX_NAME_LEN];
    AMLVariable     variables[AML_MAX_VARS];
    size_t          var_count;
    AMLMethod       methods[AML_MAX_VARS];
    size_t          method_count;
    AMLField        fields[AML_MAX_VARS];
    size_t          field_count;
    AMLValue        stack[AML_MAX_STACK_DEPTH];
    size_t          stack_depth;
    AMLVariable     locals[8];
    AMLVariable     args[8];
    size_t          scope_id;
} AMLScope;

typedef struct {
    const uint8_t  *bytecode;
    size_t          bytecode_size;
    size_t          pos;
    AMLScope        scopes[AML_MAX_SCOPE_DEPTH];
    size_t          scope_depth;
    AMLValue        stack[AML_MAX_STACK_DEPTH];
    size_t          stack_depth;
    AMLVariable     variables[AML_MAX_VARS];
    size_t          var_count;
    AMLMethod       methods[AML_MAX_VARS];
    size_t          method_count;
    AMLMethod      *current_method;
    size_t          method_stack_depth;
    AMLMethod       method_stack[AML_MAX_METHOD_DEPTH];
    bool            return_pending;
    AMLValue        return_value;
} AMLContext;

/* _CRS resource descriptor types */
#define AML_CRS_IO_PORT        0x47
#define AML_CRS_FIXED_IO       0x4B
#define AML_CRS_MEMORY_24      0x81
#define AML_CRS_MEMORY_32      0x85
#define AML_CRS_FIXED_MEMORY_32 0x86
#define AML_CRS_DWORD_ADDR_SPACE 0x87
#define AML_CRS_DWORD_IO       0x88
#define AML_CRS_IRQ            0x22
#define AML_CRS_EXT_IRQ        0x89
#define AML_CRS_DMA            0x2A
#define AML_CRS_START_DEP_NF   0xA8
#define AML_CRS_END_DEP_NF     0x79
#define AML_CRS_END_TAG        0x79
#define AML_CRS_VENDOR_SHORT   0x71
#define AML_CRS_VENDOR_LONG    0x84

typedef struct {
    uint64_t addr_base;
    uint64_t addr_length;
    uint64_t irq_number;
    bool     has_irq;
    bool     has_mmio;
    bool     has_io;
} AMLDeviceResource;

bool aml_init(AMLContext *ctx, const uint8_t *bytecode, size_t size);
bool aml_parse(AMLContext *ctx);
bool aml_invoke_method(AMLContext *ctx, const char *method_name);
bool aml_eval_object(AMLContext *ctx, const char *object_name, AMLValue *result);
bool aml_store_value(AMLContext *ctx, const char *name, const AMLValue *value);
bool aml_scope_enter(AMLContext *ctx, const char *name);
bool aml_scope_exit(AMLContext *ctx);
bool aml_eval_name(AMLContext *ctx, const char *name, AMLValue *result);
bool aml_parse_method(AMLContext *ctx, AMLMethod *method);
bool aml_execute_method(AMLContext *ctx, const AMLMethod *method, AMLValue *result);
bool aml_parse_crs(AMLContext *ctx, const char *device_path, AMLDeviceResource *res);
bool aml_parse_sta(AMLContext *ctx, const char *device_path, uint32_t *status);
bool aml_parse_prt(AMLContext *ctx, const char *pci_bus_path);
void aml_dump_context(const AMLContext *ctx);
const char *aml_opcode_name(uint8_t opcode);

#endif
