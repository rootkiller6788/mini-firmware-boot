#include "smbios.h"
#include "hob.h"
#include "acpi_aml.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* ===== SMBIOS Tests ===== */

static void test_smbios_parse(void) {
    printf("  [L1] SMBIOS parse ... ");
    /* Build contiguous buffer on stack (stack typically in low 4GB on MinGW64) */
    uint8_t buf[512];
    memset(buf, 0, sizeof(buf));
    SMBIOSEntryPoint32 *ep = (SMBIOSEntryPoint32*)buf;
    memcpy(ep->anchor_string, "_SM_", 4);
    ep->entry_point_length = 0x1F;
    ep->smbios_major_version = 3; ep->smbios_minor_version = 3;
    ep->max_structure_size = 256;

    /* Structure table at offset 40 in buffer */
    uint8_t *stbl = buf + 40;
    stbl[0] = 0; stbl[1] = 0x14;
    uint8_t *strs = stbl + 0x14;
    strs[0] = 'A'; strs[1] = 'M'; strs[2] = 'I'; strs[3] = 0;
    strs[4] = '1'; strs[5] = '.'; strs[6] = '0'; strs[7] = 0;
    strs[8] = '2'; strs[9] = '0'; strs[10] = '2'; strs[11] = '4'; strs[12] = 0;
    strs[13] = 0;

    ep->structure_table_address = (uint32_t)(uintptr_t)stbl;
    ep->structure_table_length = 0x14 + 14;
    ep->number_of_structures = 1;

    /* Checksums */
    uint8_t sum = 0;
    for (uint8_t i = 0; i < ep->entry_point_length; i++) sum += buf[i];
    ep->checksum = (uint8_t)(256 - sum);
    memcpy(ep->intermediate_anchor, "_DMI_", 5);
    sum = 0;
    for (uint8_t i = 0; i < 15; i++) sum += ((uint8_t*)ep->intermediate_anchor)[i];
    ep->intermediate_checksum = (uint8_t)(256 - sum);

    /* Parse - skip if address truncation causes 64-bit issues */
    SMBIOSTable table;
    uintptr_t addr_check = (uintptr_t)stbl;
    if ((uint32_t)addr_check != addr_check) {
        /* 64-bit address doesn't fit in 32-bit field; skip structured test */
        printf("SKIP (64-bit addr) ... ");
    } else {
        assert(smbios_parse(&table, buf, sizeof(buf)));
        assert(table.parsed && table.structure_count == 1);
        assert(table.structures[0].type == SMBIOS_TYPE_BIOS);
        assert(table.structures[0].string_count >= 1);
        assert(!strcmp(table.structures[0].strings[0], "AMI"));
        smbios_free_table(&table);
    }

    /* Null/invalid */
    assert(!smbios_parse(NULL, NULL, 0));
    assert(!smbios_parse(&table, NULL, 100));
    printf("OK\n");
}

static void test_smbios_type_name(void) {
    printf("  [L1] SMBIOS type names ... ");
    assert(!strcmp(smbios_type_to_string(SMBIOS_TYPE_BIOS), "BIOS Information"));
    assert(!strcmp(smbios_type_to_string(SMBIOS_TYPE_SYSTEM), "System Information"));
    assert(!strcmp(smbios_type_to_string(SMBIOS_TYPE_PROCESSOR), "Processor Information"));
    assert(!strcmp(smbios_type_to_string(255), "Unknown"));
    printf("OK\n");
}

/* ===== HOB Tests ===== */

static void test_hob_basic(void) {
    printf("  [L2] HOB basic ... ");
    HOBList list;
    assert(hob_init(&list, 0x100000000ULL, 0x0ULL, 0x100000000ULL, 0x100000ULL));
    assert(list.count == 1);
    assert(!list.finalized);

    assert(hob_add_memory_alloc(&list, 0x100000, 0x10000, HOB_MEM_ALLOC_MODULE));
    assert(hob_add_resource_desc(&list, HOB_RESOURCE_SYSTEM_MEMORY, HOB_RESOURCE_ATTR_WRITE_BACK, 0, 0x100000000ULL));
    assert(hob_add_firmware_volume(&list, 0xFF000000, 0x1000000));
    assert(hob_add_cpu(&list, 64, 16));
    assert(hob_finalize(&list));
    assert(list.finalized);
    assert(list.count == 6);

    /* Cant add after finalize */
    assert(!hob_add_cpu(&list, 32, 8));

    /* Find by type */
    HOB results[4];
    size_t found = hob_find_by_type(&list, HOB_TYPE_MEMORY_ALLOC, results, 4);
    assert(found == 1);
    assert(results[0].memory_alloc.memory_base_address == 0x100000);

    /* Get PHIT */
    HOBPHIT phit;
    assert(hob_get_phit(&list, &phit));
    assert(phit.memory_top == 0x100000000ULL);
    assert(!hob_get_phit(NULL, &phit));
    assert(!hob_get_phit(&list, NULL));

    /* Total size */
    uint32_t sz = hob_calculate_total_size(&list);
    assert(sz > 0);

    /* Null guards */
    assert(!hob_init(NULL, 0, 0, 0, 0));
    assert(!hob_finalize(NULL));
    printf("OK\n");
}

static void test_hob_type_names(void) {
    printf("  [L1] HOB type names ... ");
    assert(strstr(hob_type_to_string(HOB_TYPE_PHIT), "PHIT"));
    assert(strstr(hob_type_to_string(HOB_TYPE_CPU), "CPU"));
    assert(strstr(hob_type_to_string(0x00FF), "Unknown"));
    assert(strstr(hob_memory_type_to_string(HOB_MEM_ALLOC_EOI_RESIDENT), "EfiReserved"));
    assert(strstr(hob_memory_type_to_string(0x08), "EfiUnusable"));
    assert(strstr(hob_memory_type_to_string(255), "Unknown"));
    assert(strstr(hob_resource_type_to_string(HOB_RESOURCE_SYSTEM_MEMORY), "System Memory"));
    assert(strstr(hob_resource_type_to_string(999), "Unknown"));
    printf("OK\n");
}

/* ===== AML Tests ===== */

static void test_aml_init(void) {
    printf("  [L1] AML init ... ");
    AMLContext *ctx = calloc(1, sizeof(AMLContext));
    assert(ctx);
    uint8_t bc[] = {AML_RETURN_OP, AML_ZERO_OP};
    assert(aml_init(ctx, bc, sizeof(bc)));
    assert(ctx->bytecode == bc && ctx->bytecode_size == 2);
    assert(ctx->pos == 0 && ctx->scope_depth == 0);
    assert(!aml_init(NULL, bc, 2));
    assert(!aml_init(ctx, NULL, 2));
    assert(!aml_init(ctx, bc, 0));
    free(ctx);
    printf("OK\n");
}

static void test_aml_parse(void) {
    printf("  [L2] AML parse ... ");
    AMLContext *ctx = calloc(1, sizeof(AMLContext));
    assert(ctx);
    uint8_t bc[] = {
        AML_STORE_OP,
        AML_ONE_OP,
        0x5C, 0x2E, 'S', 'B', '_', 'V', 'A', 'L', 0x00
    };
    assert(aml_init(ctx, bc, sizeof(bc)));
    assert(aml_parse(ctx));
    free(ctx);
    printf("OK\n");
}

static void test_aml_arithmetic(void) {
    printf("  [L2] AML arithmetic ... ");
    AMLContext *ctx = calloc(1, sizeof(AMLContext));
    assert(ctx);
    uint8_t bc[] = {
        AML_BYTE_PREFIX, 0x03,
        AML_BYTE_PREFIX, 0x04,
        AML_ADD_OP
    };
    assert(aml_init(ctx, bc, sizeof(bc)));
    assert(aml_parse(ctx));
    assert(ctx->stack_depth >= 1);
    assert(ctx->stack[0].type == AML_VAL_INTEGER);
    assert(ctx->stack[0].integer == 7);
    free(ctx);
    printf("OK\n");
}

static void test_aml_variables(void) {
    printf("  [L2] AML variables ... ");
    AMLContext *ctx = calloc(1, sizeof(AMLContext));
    assert(ctx);
    uint8_t bc[] = {AML_RETURN_OP, AML_ZERO_OP};
    aml_init(ctx, bc, sizeof(bc));

    AMLValue val;
    val.type = AML_VAL_INTEGER;
    val.integer = 42;
    assert(aml_store_value(ctx, "TEST", &val));
    AMLValue result;
    assert(aml_eval_object(ctx, "TEST", &result));
    assert(result.type == AML_VAL_INTEGER && result.integer == 42);

    val.integer = 99;
    assert(aml_store_value(ctx, "TEST", &val));
    assert(aml_eval_object(ctx, "TEST", &result) && result.integer == 99);

    assert(!aml_store_value(NULL, "X", &val));
    assert(!aml_store_value(ctx, NULL, &val));
    assert(!aml_store_value(ctx, "X", NULL));
    assert(!aml_eval_object(ctx, "NONEXIST", &result));
    free(ctx);
    printf("OK\n");
}

static void test_aml_scope(void) {
    printf("  [L2] AML scope ... ");
    AMLContext *ctx = calloc(1, sizeof(AMLContext));
    assert(ctx);
    uint8_t bc[] = {AML_RETURN_OP, AML_ZERO_OP};
    aml_init(ctx, bc, sizeof(bc));

    assert(aml_scope_enter(ctx, "\\_SB_"));
    assert(ctx->scope_depth == 1);
    assert(aml_scope_exit(ctx));
    assert(ctx->scope_depth == 0);
    assert(!aml_scope_exit(ctx));
    assert(!aml_scope_enter(NULL, "X"));
    free(ctx);
    printf("OK\n");
}

static void test_aml_opcode_names(void) {
    printf("  [L1] AML opcode names ... ");
    assert(!strcmp(aml_opcode_name(AML_STORE_OP), "StoreOp"));
    assert(!strcmp(aml_opcode_name(AML_ADD_OP), "AddOp"));
    assert(!strcmp(aml_opcode_name(AML_IF_OP), "IfOp"));
    assert(!strcmp(aml_opcode_name(0x00), "ZeroOp"));
    assert(!strcmp(aml_opcode_name(0xFF), "OnesOp"));
    assert(!strcmp(aml_opcode_name(0xFE), "UnknownOp"));
    printf("OK\n");
}

int main(void) {
    printf("=== SMBIOS + HOB + AML Tests ===\n\n");
    test_smbios_parse();
    test_smbios_type_name();
    test_hob_basic();
    test_hob_type_names();
    test_aml_init();
    test_aml_opcode_names();
    test_aml_variables();
    test_aml_scope();
    test_aml_parse();
    test_aml_arithmetic();
    printf("\n=== All tests passed ===\n");
    return 0;
}
