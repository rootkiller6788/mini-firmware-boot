#include "acpi_aml.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

bool aml_init(AMLContext *ctx, const uint8_t *bytecode, size_t size)
{
    if (!ctx || !bytecode || size == 0) return false;
    memset(ctx, 0, sizeof(AMLContext));
    ctx->bytecode = bytecode;
    ctx->bytecode_size = size;
    ctx->pos = 0;
    ctx->scope_depth = 0;
    ctx->stack_depth = 0;
    ctx->var_count = 0;
    ctx->method_count = 0;
    ctx->current_method = NULL;
    ctx->method_stack_depth = 0;
    ctx->return_pending = false;
    return true;
}

static uint8_t aml_read_byte(AMLContext *ctx)
{
    if (ctx->pos >= ctx->bytecode_size) return 0;
    return ctx->bytecode[ctx->pos++];
}

static uint16_t aml_read_word(AMLContext *ctx)
{
    uint8_t lo = aml_read_byte(ctx);
    uint8_t hi = aml_read_byte(ctx);
    return (uint16_t)lo | ((uint16_t)hi << 8);
}

static uint32_t aml_read_dword(AMLContext *ctx)
{
    uint32_t b0 = aml_read_byte(ctx);
    uint32_t b1 = aml_read_byte(ctx);
    uint32_t b2 = aml_read_byte(ctx);
    uint32_t b3 = aml_read_byte(ctx);
    return b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);
}

static uint64_t aml_read_qword(AMLContext *ctx)
{
    uint64_t lo = aml_read_dword(ctx);
    uint64_t hi = aml_read_dword(ctx);
    return lo | (hi << 32);
}

static void aml_read_name(AMLContext *ctx, char *buf, size_t buf_size)
{
    if (!buf || buf_size == 0) return;
    size_t idx = 0;
    uint8_t lead = aml_read_byte(ctx);

    if (lead == 0x5C) { /* root char */
        buf[idx++] = '\\';
        buf[idx++] = '\\';
        lead = aml_read_byte(ctx);
    }
    if (lead == 0x5E) { /* parent */
        buf[idx++] = '^';
        lead = aml_read_byte(ctx);
    }
    if (lead == 0x2E) { /* dual name */
        for (int i = 0; i < 2 && idx + 4 < (int)buf_size; i++) {
            for (int j = 0; j < 4; j++) buf[idx++] = aml_read_byte(ctx);
            if (i == 0) buf[idx++] = '.';
        }
    } else if (lead == 0x2F) { /* multi name */
        uint8_t count = aml_read_byte(ctx);
        for (uint8_t i = 0; i < count && idx + 4 < (int)buf_size; i++) {
            for (int j = 0; j < 4; j++) buf[idx++] = aml_read_byte(ctx);
            if (i + 1 < count) buf[idx++] = '.';
        }
    } else {
        buf[idx++] = lead;
        for (int i = 0; i < 3 && idx < buf_size; i++) buf[idx++] = aml_read_byte(ctx);
    }
    buf[idx] = '\0';
}

static int64_t aml_read_integer(AMLContext *ctx)
{
    uint8_t op = aml_read_byte(ctx);
    switch (op) {
    case 0x00: return 0;
    case 0x01: return 1;
    case AML_ONES_OP: return 0xFFFFFFFFFFFFFFFFULL;
    case AML_BYTE_PREFIX:   return (int8_t)aml_read_byte(ctx);
    case AML_WORD_PREFIX:   return (int16_t)aml_read_word(ctx);
    case AML_DWORD_PREFIX:  return (int32_t)aml_read_dword(ctx);
    case AML_QWORD_PREFIX:  return (int64_t)aml_read_qword(ctx);
    default: return (int64_t)op;
    }
}

static bool aml_value_eq(const AMLValue *a, const AMLValue *b)
{
    if (a->type != b->type) return false;
    switch (a->type) {
    case AML_VAL_INTEGER: return a->integer == b->integer;
    case AML_VAL_STRING:
        return strncmp(a->string_or_buffer.data, b->string_or_buffer.data,
                       sizeof(a->string_or_buffer.data)) == 0;
    default: return false;
    }
}

static void aml_value_copy(AMLValue *dst, const AMLValue *src)
{
    memcpy(dst, src, sizeof(AMLValue));
}

static AMLValue *aml_find_variable(AMLContext *ctx, const char *name)
{
    if (!name) return NULL;
    for (size_t i = 0; i < ctx->var_count; i++) {
        if (strcmp(ctx->variables[i].name, name) == 0) {
            return &ctx->variables[i].value;
        }
    }
    return NULL;
}

static bool aml_eval_term(AMLContext *ctx, AMLValue *result)
{
    if (!ctx || ctx->pos >= ctx->bytecode_size) return false;

    uint8_t op = ctx->bytecode[ctx->pos];
    if (op == 0x00 || op == 0x01) {
        result->type = AML_VAL_INTEGER;
        result->integer = aml_read_integer(ctx);
        return true;
    }

    switch (op) {
    case AML_BYTE_PREFIX:
    case AML_WORD_PREFIX:
    case AML_DWORD_PREFIX:
    case AML_QWORD_PREFIX: {
        result->type = AML_VAL_INTEGER;
        result->integer = aml_read_integer(ctx);
        return true;
    }
    case AML_STRING_PREFIX: {
        result->type = AML_VAL_STRING;
        size_t i = 0;
        uint8_t ch;
        ctx->pos++;
        while ((ch = aml_read_byte(ctx)) != 0 && i < sizeof(result->string_or_buffer.data) - 1) {
            result->string_or_buffer.data[i++] = ch;
        }
        result->string_or_buffer.data[i] = '\0';
        result->string_or_buffer.length = i;
        return true;
    }
    case AML_LOCAL0 ... AML_LOCAL7: {
        uint8_t idx2 = op - AML_LOCAL0;
        AMLScope *scope = &ctx->scopes[ctx->scope_depth];
        *result = scope->locals[idx2].value;
        ctx->pos++;
        return true;
    }
    case AML_ARG0 ... AML_ARG6: {
        uint8_t idx2 = op - AML_ARG0;
        AMLScope *scope = &ctx->scopes[ctx->scope_depth];
        *result = scope->args[idx2].value;
        ctx->pos++;
        return true;
    }
    default:
        return false;
    }
}

static bool aml_execute_op(AMLContext *ctx, uint8_t opcode)
{
    if (ctx->stack_depth < 2) return false;
    AMLValue b = ctx->stack[--ctx->stack_depth];
    AMLValue a = ctx->stack[--ctx->stack_depth];

    AMLScope *scope = &ctx->scopes[ctx->scope_depth];

    if (ctx->stack_depth < AML_MAX_STACK_DEPTH) {
        AMLValue *r = &ctx->stack[ctx->stack_depth++];
        r->type = AML_VAL_INTEGER;

        switch (opcode) {
        case AML_ADD_OP:
            r->integer = a.integer + b.integer; return true;
        case AML_SUBTRACT_OP:
            r->integer = a.integer - b.integer; return true;
        case AML_MULTIPLY_OP:
            r->integer = a.integer * b.integer; return true;
        case AML_DIVIDE_OP:
            r->integer = b.integer != 0 ? a.integer / b.integer : 0; return true;
        case AML_MOD_OP:
            r->integer = b.integer != 0 ? a.integer % b.integer : 0; return true;
        case AML_AND_OP:
            r->integer = a.integer & b.integer; return true;
        case AML_OR_OP:
            r->integer = a.integer | b.integer; return true;
        case AML_XOR_OP:
            r->integer = a.integer ^ b.integer; return true;
        case AML_SHIFT_LEFT_OP:
            r->integer = a.integer << (b.integer & 63); return true;
        case AML_SHIFT_RIGHT_OP:
            r->integer = a.integer >> (b.integer & 63); return true;
        case AML_LEQUAL_OP:
            r->integer = aml_value_eq(&a, &b) ? 0xFFFFFFFFFFFFFFFFULL : 0; return true;
        case AML_LGREATER_OP:
            r->integer = (a.integer > b.integer) ? 0xFFFFFFFFFFFFFFFFULL : 0; return true;
        case AML_LLESS_OP:
            r->integer = (a.integer < b.integer) ? 0xFFFFFFFFFFFFFFFFULL : 0; return true;
        case AML_LAND_OP:
            r->integer = (a.integer && b.integer) ? 0xFFFFFFFFFFFFFFFFULL : 0; return true;
        case AML_LOR_OP:
            r->integer = (a.integer || b.integer) ? 0xFFFFFFFFFFFFFFFFULL : 0; return true;
        default: return false;
        }
    }
    return false;
}

bool aml_parse(AMLContext *ctx)
{
    if (!ctx || ctx->pos >= ctx->bytecode_size) return false;

    while (ctx->pos < ctx->bytecode_size && !ctx->return_pending) {
        uint8_t op = aml_read_byte(ctx);

        switch (op) {
        case AML_NOOP_OP:
            break;
        case AML_ZERO_OP:
        case AML_ONE_OP:
        case AML_ONES_OP:
            ctx->pos--;
            if (ctx->stack_depth < AML_MAX_STACK_DEPTH) {
                aml_eval_term(ctx, &ctx->stack[ctx->stack_depth++]);
            }
            break;
        case AML_RETURN_OP:
            if (ctx->stack_depth > 0) {
                ctx->return_value = ctx->stack[--ctx->stack_depth];
                ctx->return_pending = true;
            }
            break;
        case AML_IF_OP: {
            uint8_t pkg_len_lead = aml_read_byte(ctx);
            size_t pkg_len = 0;
            if (pkg_len_lead < 0x40) pkg_len = pkg_len_lead;
            else if (pkg_len_lead < 0x80) {
                size_t shift = (size_t)(pkg_len_lead & 0x0F);
                for (size_t i = 0; i < shift; i++) pkg_len |= (size_t)aml_read_byte(ctx) << (i * 8);
            }

            bool condition = false;
            if (ctx->stack_depth > 0) {
                AMLValue cond = ctx->stack[--ctx->stack_depth];
                condition = (cond.type == AML_VAL_INTEGER && cond.integer != 0);
            }

            size_t if_end = ctx->pos + pkg_len - 1;
            size_t save_pos = ctx->pos;
            bool has_else = false;
            size_t else_offset = 0;

            while (ctx->pos < if_end) {
                uint8_t next = aml_read_byte(ctx);
                if (next == AML_ELSE_OP) {
                    has_else = true;
                    else_offset = ctx->pos - 1;
                }
            }
            ctx->pos = save_pos;

            if (condition) {
                size_t limit = has_else ? else_offset : if_end;
                aml_parse(ctx);
            } else if (has_else) {
                ctx->pos = else_offset + 1;
                aml_parse(ctx);
            }
            break;
        }
        case AML_WHILE_OP: {
            size_t while_start = ctx->pos;
            /* simplified: just skip for now */
            break;
        }
        case AML_STORE_OP: {
            AMLValue src, dest_name;
            if (!aml_eval_term(ctx, &src)) break;
            /* simplified store */
            if (ctx->stack_depth < AML_MAX_STACK_DEPTH) {
                ctx->stack[ctx->stack_depth++] = src;
            }
            break;
        }
        case AML_ADD_OP:
        case AML_SUBTRACT_OP:
        case AML_MULTIPLY_OP:
        case AML_DIVIDE_OP:
        case AML_MOD_OP:
        case AML_AND_OP:
        case AML_OR_OP:
        case AML_XOR_OP:
        case AML_SHIFT_LEFT_OP:
        case AML_SHIFT_RIGHT_OP:
        case AML_LEQUAL_OP:
        case AML_LGREATER_OP:
        case AML_LLESS_OP:
            aml_execute_op(ctx, op);
            break;
        case AML_NOT_OP:
            if (ctx->stack_depth > 0) {
                AMLValue *v = &ctx->stack[ctx->stack_depth - 1];
                if (v->type == AML_VAL_INTEGER) v->integer = ~v->integer;
            }
            break;
        case AML_LNOT_OP:
            if (ctx->stack_depth > 0) {
                AMLValue *v = &ctx->stack[ctx->stack_depth - 1];
                if (v->type == AML_VAL_INTEGER) v->integer = v->integer ? 0 : 1;
            }
            break;
        case AML_TO_INTEGER_OP:
            if (ctx->stack_depth > 0) {
                ctx->stack[ctx->stack_depth - 1].type = AML_VAL_INTEGER;
            }
            break;
        case AML_TO_BUFFER_OP:
            if (ctx->stack_depth > 0) {
                ctx->stack[ctx->stack_depth - 1].type = AML_VAL_BUFFER;
            }
            break;
        default:
            break;
        }
    }
    return true;
}

bool aml_invoke_method(AMLContext *ctx, const char *method_name)
{
    if (!ctx || !method_name) return false;
    for (size_t i = 0; i < ctx->method_count; i++) {
        if (strcmp(ctx->methods[i].name, method_name) == 0) {
            AMLValue result;
            return aml_execute_method(ctx, &ctx->methods[i], &result);
        }
    }
    return false;
}

bool aml_eval_object(AMLContext *ctx, const char *object_name, AMLValue *result)
{
    if (!ctx || !object_name || !result) return false;

    AMLValue *var = aml_find_variable(ctx, object_name);
    if (var) {
        aml_value_copy(result, var);
        return true;
    }

    for (size_t i = 0; i < ctx->method_count; i++) {
        if (strcmp(ctx->methods[i].name, object_name) == 0) {
            return aml_execute_method(ctx, &ctx->methods[i], result);
        }
    }

    return false;
}

bool aml_store_value(AMLContext *ctx, const char *name, const AMLValue *value)
{
    if (!ctx || !name || !value) return false;
    for (size_t i = 0; i < ctx->var_count; i++) {
        if (strcmp(ctx->variables[i].name, name) == 0) {
            aml_value_copy(&ctx->variables[i].value, (AMLValue *)value);
            return true;
        }
    }
    if (ctx->var_count < AML_MAX_VARS) {
        AMLVariable *v = &ctx->variables[ctx->var_count++];
        strncpy(v->name, name, AML_MAX_NAME_LEN - 1);
        v->name[AML_MAX_NAME_LEN - 1] = '\0';
        aml_value_copy(&v->value, (AMLValue *)value);
        v->is_arg = false;
        v->is_local = false;
        v->index = 0;
        return true;
    }
    return false;
}

bool aml_scope_enter(AMLContext *ctx, const char *name)
{
    if (!ctx || ctx->scope_depth >= AML_MAX_SCOPE_DEPTH - 1) return false;
    ctx->scope_depth++;
    AMLScope *scope = &ctx->scopes[ctx->scope_depth];
    memset(scope, 0, sizeof(AMLScope));
    if (name) {
        strncpy(scope->name, name, AML_MAX_NAME_LEN - 1);
        scope->name[AML_MAX_NAME_LEN - 1] = '\0';
    }
    scope->scope_id = ctx->scope_depth;
    return true;
}

bool aml_scope_exit(AMLContext *ctx)
{
    if (!ctx || ctx->scope_depth == 0) return false;
    ctx->scope_depth--;
    return true;
}

bool aml_eval_name(AMLContext *ctx, const char *name, AMLValue *result)
{
    if (!ctx || !name || !result) return false;
    return aml_eval_object(ctx, name, result);
}

bool aml_parse_method(AMLContext *ctx, AMLMethod *method)
{
    if (!ctx || !method) return false;
    size_t save_pos = ctx->pos;
    memcpy(method, &ctx->methods[ctx->method_count - 1], sizeof(AMLMethod));
    ctx->pos = save_pos;
    return true;
}

bool aml_execute_method(AMLContext *ctx, const AMLMethod *method, AMLValue *result)
{
    if (!ctx || !method || !method->valid) return false;

    size_t save_pos = ctx->pos;
    ctx->pos = method->start_offset;
    ctx->return_pending = false;

    aml_parse(ctx);

    if (result && ctx->return_pending) {
        *result = ctx->return_value;
        ctx->return_pending = false;
    }

    ctx->pos = save_pos;
    return true;
}

bool aml_parse_crs(AMLContext *ctx, const char *device_path, AMLDeviceResource *res)
{
    if (!ctx || !res) return false;
    memset(res, 0, sizeof(AMLDeviceResource));

    char full_path[AML_MAX_NAME_LEN];
    snprintf(full_path, sizeof(full_path), "%s._CRS", device_path ? device_path : "");
    (void)full_path;

    /* simplified: return empty resource */
    return true;
}

bool aml_parse_sta(AMLContext *ctx, const char *device_path, uint32_t *status)
{
    if (!ctx || !status) return false;
    *status = 0x0F; /* _STA default: present, enabled, shown, functional */
    return true;
}

bool aml_parse_prt(AMLContext *ctx, const char *pci_bus_path)
{
    if (!ctx) return false;
    (void)pci_bus_path;
    return true;
}

const char *aml_opcode_name(uint8_t opcode)
{
    switch (opcode) {
    case AML_ZERO_OP:           return "ZeroOp";
    case AML_ONE_OP:            return "OneOp";
    case AML_ALIAS_OP:          return "AliasOp";
    case AML_NAME_OP:           return "NameOp";
    case AML_BYTE_PREFIX:       return "BytePrefix";
    case AML_WORD_PREFIX:       return "WordPrefix";
    case AML_DWORD_PREFIX:      return "DWordPrefix";
    case AML_STRING_PREFIX:     return "StringPrefix";
    case AML_QWORD_PREFIX:      return "QWordPrefix";
    case AML_SCOPE_OP:          return "ScopeOp";
    case AML_BUFFER_OP:         return "BufferOp";
    case AML_PACKAGE_OP:        return "PackageOp";
    case AML_METHOD_OP:         return "MethodOp";
    case AML_STORE_OP:          return "StoreOp";
    case AML_ADD_OP:            return "AddOp";
    case AML_SUBTRACT_OP:       return "SubtractOp";
    case AML_MULTIPLY_OP:       return "MultiplyOp";
    case AML_DIVIDE_OP:         return "DivideOp";
    case AML_AND_OP:            return "AndOp";
    case AML_OR_OP:             return "OrOp";
    case AML_XOR_OP:            return "XorOp";
    case AML_NOT_OP:            return "NotOp";
    case AML_IF_OP:             return "IfOp";
    case AML_ELSE_OP:           return "ElseOp";
    case AML_WHILE_OP:          return "WhileOp";
    case AML_RETURN_OP:         return "ReturnOp";
    case AML_NOOP_OP:           return "NoopOp";
    case AML_TO_INTEGER_OP:     return "ToIntegerOp";
    case AML_TO_BUFFER_OP:      return "ToBufferOp";
    case AML_INDEX_OP:          return "IndexOp";
    case AML_LAND_OP:           return "LAndOp";
    case AML_LOR_OP:            return "LOrOp";
    case AML_LNOT_OP:           return "LNotOp";
    case AML_LEQUAL_OP:         return "LEqualOp";
    case AML_LGREATER_OP:       return "LGreaterOp";
    case AML_LLESS_OP:          return "LLessOp";
    case AML_BREAK_OP:          return "BreakOp";
    case AML_BREAK_POINT_OP:    return "BreakPointOp";
    case AML_ONES_OP:           return "OnesOp";
    default:                    return "UnknownOp";
    }
}

void aml_dump_context(const AMLContext *ctx)
{
    if (!ctx) return;
    printf("=== AML Context ===\n");
    printf("Bytecode: %zu bytes, Position: %zu\n", ctx->bytecode_size, ctx->pos);
    printf("Scope depth: %zu\n", ctx->scope_depth);
    printf("Stack depth: %zu\n", ctx->stack_depth);
    printf("Variables: %zu, Methods: %zu\n", ctx->var_count, ctx->method_count);
    printf("Return pending: %s\n", ctx->return_pending ? "yes" : "no");

    for (size_t i = 0; i < ctx->var_count; i++) {
        printf("  Var[%zu] %s = ", i, ctx->variables[i].name);
        if (ctx->variables[i].value.type == AML_VAL_INTEGER) {
            printf("%llu\n", (unsigned long long)ctx->variables[i].value.integer);
        } else {
            printf("<non-integer>\n");
        }
    }
}
