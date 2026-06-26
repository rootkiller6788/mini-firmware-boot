#include "device_tree.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

/* ===== Endianness Helpers (L1: byte-order conversion per devicetree.org §2.1) ===== */

uint32_t fdt_bswap32(uint32_t val)
{
    return ((val & 0x000000FFU) << 24) |
           ((val & 0x0000FF00U) << 8)  |
           ((val & 0x00FF0000U) >> 8)  |
           ((val & 0xFF000000U) >> 24);
}

uint32_t fdt_read_u32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) |
           ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  |
           (uint32_t)p[3];
}

uint64_t fdt_read_u64(const uint8_t *p)
{
    return ((uint64_t)fdt_read_u32(p) << 32) | (uint64_t)fdt_read_u32(p + 4);
}

static uint32_t fdt_u32(const uint8_t *dtb, size_t offset)
{
    return fdt_read_u32(&dtb[offset]);
}

/* ===== Memory Management ===== */

static FDTNode *fdt_alloc_node(const char *name)
{
    FDTNode *node = calloc(1, sizeof(FDTNode));
    if (!node) return NULL;
    if (name) {
        size_t len = strlen(name);
        if (len >= FDT_MAX_NAME_LEN) len = FDT_MAX_NAME_LEN - 1;
        memcpy(node->name, name, len);
        node->name[len] = '\0';
        node->name_len = len;
    }
    return node;
}

static const char *fdt_string(const FDTTree *tree, uint32_t offset)
{
    if (offset >= tree->strings_size) return "";
    return &tree->strings_block[offset];
}

/* ===== FDT Structure Block Parser (L2: FDT binary format, spec §5.4) ===== */

static bool fdt_parse_struct(FDTTree *tree, const uint8_t *dtb, size_t size,
                             size_t *pos, FDTNode *parent)
{
    FDTNode *node = NULL;

    while (*pos < size) {
        uint32_t token = fdt_u32(dtb, *pos);
        *pos += FDT_CELL_SIZE;

        switch (token) {
        case FDT_BEGIN_NODE: {
            const char *name = (const char *)&dtb[*pos];
            size_t name_len = strlen(name);
            *pos += ((name_len + FDT_CELL_SIZE) & ~(size_t)(FDT_CELL_SIZE - 1));

            node = fdt_alloc_node(name);
            if (!node) return false;
            node->parent = parent;

            if (parent) {
                if (parent->child_count < FDT_MAX_CHILDREN) {
                    parent->children[parent->child_count++] = node;
                }
            } else {
                tree->root = node;
            }

            if (!fdt_parse_struct(tree, dtb, size, pos, node)) return false;
            break;
        }
        case FDT_END_NODE:
            /* Post-process: cache phandle for the closing node */
            {
                FDTNode *closing = node ? node : parent;
                if (closing) {
                    for (size_t i = 0; i < closing->prop_count; i++) {
                        if (strcmp(closing->properties[i].name, "phandle") == 0 &&
                            closing->properties[i].value_len >= 4) {
                            closing->phandle = fdt_read_u32(closing->properties[i].value);
                            if (tree->phandle_count < FDT_MAX_PHANDLE_MAP) {
                                tree->phandle_map[tree->phandle_count].phandle = closing->phandle;
                                tree->phandle_map[tree->phandle_count].node = closing;
                                tree->phandle_count++;
                            }
                            break;
                        }
                    }
                }
            }
            return true;

        case FDT_PROP: {
            uint32_t len = fdt_u32(dtb, *pos); *pos += FDT_CELL_SIZE;
            uint32_t nameoff = fdt_u32(dtb, *pos); *pos += FDT_CELL_SIZE;
            const char *prop_name = fdt_string(tree, nameoff);

            /* Properties attach to the current node or parent */
            FDTNode *target = node ? node : parent;
            if (!target) return false;
            if (target->prop_count < FDT_MAX_PROPS) {
                FDTProperty *prop = &target->properties[target->prop_count++];
                size_t name_len = strlen(prop_name);
                if (name_len >= FDT_MAX_NAME_LEN) name_len = FDT_MAX_NAME_LEN - 1;
                memcpy(prop->name, prop_name, name_len);
                prop->name[name_len] = '\0';
                prop->name_len = name_len;

                /* Store property value in memory */
                prop->value_len = len;
                prop->value = malloc(len + 1);
                if (prop->value) {
                    memcpy(prop->value, &dtb[*pos], len);
                    prop->value[len] = '\0';
                }
            }
            *pos += ((len + FDT_CELL_SIZE - 1) & ~(size_t)(FDT_CELL_SIZE - 1));
            break;
        }
        case FDT_NOP:
            break;
        case FDT_END:
            tree->parsed = true;
            return true;
        default:
            return false;
        }
    }
    return true;
}

/* ===== Memory Reservation Block Parser (L3: spec §5.3) ===== */

static void fdt_parse_mem_rsvmap(FDTTree *tree, const uint8_t *dtb)
{
    size_t offset = tree->header.off_mem_rsvmap;
    tree->reserve_count = 0;

    while (tree->reserve_count < FDT_MAX_RESERVE_ENTRIES && offset < tree->header.totalsize) {
        uint64_t addr = fdt_read_u64(&dtb[offset]);
        uint64_t size = fdt_read_u64(&dtb[offset + 8]);
        offset += 16;

        if (addr == 0 && size == 0) break;  /* terminator entry */

        tree->reserve_entries[tree->reserve_count].address = addr;
        tree->reserve_entries[tree->reserve_count].size = size;
        tree->reserve_count++;
    }
}

/* ===== Main Parse Entry (L1: public API) ===== */

bool fdt_parse(FDTTree *tree, const uint8_t *dtb, size_t size)
{
    if (!tree || !dtb || size < sizeof(FDTHeader)) return false;
    memset(tree, 0, sizeof(FDTTree));

    memcpy(&tree->header, dtb, sizeof(FDTHeader));
    tree->header.magic             = fdt_bswap32(tree->header.magic);
    tree->header.totalsize         = fdt_bswap32(tree->header.totalsize);
    tree->header.off_dt_struct     = fdt_bswap32(tree->header.off_dt_struct);
    tree->header.off_dt_strings    = fdt_bswap32(tree->header.off_dt_strings);
    tree->header.off_mem_rsvmap    = fdt_bswap32(tree->header.off_mem_rsvmap);
    tree->header.version           = fdt_bswap32(tree->header.version);
    tree->header.last_comp_version = fdt_bswap32(tree->header.last_comp_version);
    tree->header.boot_cpuid_phys   = fdt_bswap32(tree->header.boot_cpuid_phys);
    tree->header.size_dt_strings   = fdt_bswap32(tree->header.size_dt_strings);
    tree->header.size_dt_struct    = fdt_bswap32(tree->header.size_dt_struct);

    if (tree->header.magic != FDT_MAGIC) return false;
    if (tree->header.version < FDT_COMPAT_VERSION) return false;

    tree->strings_block = (char *)(dtb + tree->header.off_dt_strings);
    tree->strings_size  = tree->header.size_dt_strings;

    /* Parse memory reservation block first */
    fdt_parse_mem_rsvmap(tree, dtb);

    /* Parse structure block */
    size_t pos = tree->header.off_dt_struct;
    return fdt_parse_struct(tree, dtb, size, &pos, NULL);
}

/* ===== Node Navigation (L1-L2: tree traversal) ===== */

FDTNode *fdt_find_node_by_path(const FDTTree *tree, const char *path)
{
    if (!tree || !tree->root || !path) return NULL;

    char path_copy[FDT_MAX_PATH_LEN];
    strncpy(path_copy, path, FDT_MAX_PATH_LEN - 1);
    path_copy[FDT_MAX_PATH_LEN - 1] = '\0';

    if (path_copy[0] == '\0' || strcmp(path_copy, "/") == 0) {
        return tree->root;
    }

    char *saveptr;
    char *token = strtok_r(path_copy, "/", &saveptr);

    FDTNode *current = tree->root;
    while (token && current) {
        bool found = false;
        for (size_t i = 0; i < current->child_count; i++) {
            if (strcmp(current->children[i]->name, token) == 0) {
                current = current->children[i];
                found = true;
                break;
            }
        }
        if (!found) return NULL;
        token = strtok_r(NULL, "/", &saveptr);
    }
    return current;
}

FDTNode *fdt_find_compatible(const FDTNode *node, const char *compatible)
{
    if (!node || !compatible) return NULL;

    for (size_t i = 0; i < node->prop_count; i++) {
        if (strcmp(node->properties[i].name, "compatible") == 0) {
            /* Check if compatible string matches */
            if (node->properties[i].value) {
                const char *val = (const char *)node->properties[i].value;
                size_t val_len = node->properties[i].value_len;
                const char *p = val;
                while (p < val + val_len) {
                    if (strcmp(p, compatible) == 0) return (FDTNode *)node;
                    p += strlen(p) + 1;
                }
            }
        }
    }

    for (size_t i = 0; i < node->child_count; i++) {
        FDTNode *result = fdt_find_compatible(node->children[i], compatible);
        if (result) return result;
    }
    return NULL;
}

FDTNode *fdt_find_node_by_phandle(const FDTTree *tree, uint32_t phandle)
{
    if (!tree || phandle == 0) return NULL;
    for (size_t i = 0; i < tree->phandle_count; i++) {
        if (tree->phandle_map[i].phandle == phandle) {
            return tree->phandle_map[i].node;
        }
    }
    return NULL;
}

FDTNode *fdt_get_parent(const FDTNode *node)
{
    return node ? node->parent : NULL;
}

size_t fdt_count_children(const FDTNode *node)
{
    return node ? node->child_count : 0;
}

FDTNode *fdt_first_child(const FDTNode *node)
{
    if (!node || node->child_count == 0) return NULL;
    return node->children[0];
}

FDTNode *fdt_next_sibling(const FDTNode *node)
{
    if (!node || !node->parent) return NULL;
    for (size_t i = 0; i + 1 < node->parent->child_count; i++) {
        if (node->parent->children[i] == node) {
            return node->parent->children[i + 1];
        }
    }
    return NULL;
}

FDTNode *fdt_find_child_by_name(const FDTNode *parent, const char *name)
{
    if (!parent || !name) return NULL;
    for (size_t i = 0; i < parent->child_count; i++) {
        if (strcmp(parent->children[i]->name, name) == 0) {
            return parent->children[i];
        }
    }
    return NULL;
}

/* ===== Property Access (L2: typed property reading per spec §2.3) ===== */

const uint8_t *fdt_get_prop_value(const FDTNode *node, const char *prop_name, size_t *len)
{
    if (!node || !prop_name) return NULL;
    for (size_t i = 0; i < node->prop_count; i++) {
        if (strcmp(node->properties[i].name, prop_name) == 0) {
            if (len) *len = node->properties[i].value_len;
            return node->properties[i].value;
        }
    }
    if (len) *len = 0;
    return NULL;
}

bool fdt_read_prop_u32(const FDTNode *node, const char *prop_name, uint32_t *value)
{
    size_t len = 0;
    const uint8_t *val = fdt_get_prop_value(node, prop_name, &len);
    if (!val || len < 4 || !value) return false;
    *value = fdt_read_u32(val);
    return true;
}

bool fdt_read_prop_u64(const FDTNode *node, const char *prop_name, uint64_t *value)
{
    size_t len = 0;
    const uint8_t *val = fdt_get_prop_value(node, prop_name, &len);
    if (!val || len < 8 || !value) return false;
    *value = fdt_read_u64(val);
    return true;
}

bool fdt_read_prop_string(const FDTNode *node, const char *prop_name,
                          char *buffer, size_t buf_size)
{
    size_t len = 0;
    const uint8_t *val = fdt_get_prop_value(node, prop_name, &len);
    if (!val || !buffer || buf_size == 0) return false;
    size_t copy_len = len < buf_size - 1 ? len : buf_size - 1;
    memcpy(buffer, val, copy_len);
    buffer[copy_len] = '\0';
    return true;
}

uint32_t fdt_get_phandle(const FDTNode *node)
{
    return node ? node->phandle : 0;
}

/* ===== Address Translation (L5: "ranges" property, spec §2.3.8) =====
 *
 * Per the Device Tree Specification, the "#address-cells" and "#size-cells"
 * properties define the number of 32-bit cells used for address and size
 * fields in child nodes. The "ranges" property maps child addresses to
 * parent addresses using the formula:
 *
 *   parent_addr = child_addr + range_offset
 *
 * Each range entry is:
 *   [child_addr: #address-cells] [parent_addr: #addr-cells(parent)] [size: #size-cells]
 */

uint32_t fdt_get_address_cells(const FDTNode *node)
{
    if (!node) return 2;  /* default per spec */
    uint32_t val = 0;
    if (fdt_read_prop_u32(node, "#address-cells", &val)) return val;
    /* Walk up to parent if not on this node */
    if (node->parent) return fdt_get_address_cells(node->parent);
    return 2;
}

uint32_t fdt_get_size_cells(const FDTNode *node)
{
    if (!node) return 1;  /* default per spec */
    uint32_t val = 0;
    if (fdt_read_prop_u32(node, "#size-cells", &val)) return val;
    if (node->parent) return fdt_get_size_cells(node->parent);
    return 1;
}

bool fdt_translate_address(const FDTTree *tree, const FDTNode *node,
                           uint64_t child_addr, uint64_t *parent_addr)
{
    if (!tree || !node || !parent_addr) return false;

    /* Root node is the final parent - return identity */
    if (!node->parent) {
        *parent_addr = child_addr;
        return true;
    }

    uint32_t child_cells = fdt_get_address_cells(node);
    uint32_t parent_cells = fdt_get_address_cells(node->parent);
    uint32_t size_cells = fdt_get_size_cells(node);

    size_t ranges_len = 0;
    const uint8_t *ranges = fdt_get_prop_value(node, "ranges", &ranges_len);

    /* No ranges property: 1:1 mapping (pass through to parent) */
    if (!ranges || ranges_len == 0) {
        return fdt_translate_address(tree, node->parent, child_addr, parent_addr);
    }

    uint32_t entry_size = (child_cells + parent_cells + size_cells) * FDT_CELL_SIZE;
    size_t num_entries = ranges_len / entry_size;

    for (size_t i = 0; i < num_entries; i++) {
        const uint8_t *entry = ranges + i * entry_size;
        uint64_t child_base = 0, parent_base = 0, range_size = 0;

        /* Read child address (first child_cells cells) */
        for (uint32_t c = 0; c < child_cells; c++) {
            child_base = (child_base << 32) | fdt_read_u32(entry + c * FDT_CELL_SIZE);
        }
        /* Read parent address */
        for (uint32_t c = 0; c < parent_cells; c++) {
            parent_base = (parent_base << 32) | fdt_read_u32(entry + (child_cells + c) * FDT_CELL_SIZE);
        }
        /* Read size */
        for (uint32_t c = 0; c < size_cells; c++) {
            range_size = (range_size << 32) | fdt_read_u32(entry + (child_cells + parent_cells + c) * FDT_CELL_SIZE);
        }

        if (child_addr >= child_base && child_addr < child_base + range_size) {
            *parent_addr = parent_base + (child_addr - child_base);
            return fdt_translate_address(tree, node->parent, *parent_addr, parent_addr);
        }
    }

    /* No matching range - pass through */
    return fdt_translate_address(tree, node->parent, child_addr, parent_addr);
}

/* ===== Interrupt Parsing (L2: spec §2.4 Interrupts) =====
 *
 * Interrupt specifiers per devicetree.org §2.4:
 *   - "interrupts" property: array of interrupt specifiers
 *   - "interrupt-parent" or inherited from parent node
 *   - "interrupt-controller" flag marks controller nodes
 *   - "#interrupt-cells" defines cells per interrupt specifier
 */

FDTNode *fdt_find_interrupt_parent(const FDTTree *tree, const FDTNode *node)
{
    if (!tree || !node) return NULL;

    uint32_t phandle = 0;
    if (fdt_read_prop_u32(node, "interrupt-parent", &phandle) && phandle != 0) {
        return fdt_find_node_by_phandle(tree, phandle);
    }

    /* Walk up the tree looking for interrupt-controller or an interrupt-parent */
    FDTNode *current = node->parent;
    while (current) {
        /* Check if current node is an interrupt-controller */
        size_t len = 0;
        const uint8_t *val = fdt_get_prop_value(current, "interrupt-controller", &len);
        if (val) return current;

        if (fdt_read_prop_u32(current, "interrupt-parent", &phandle) && phandle != 0) {
            return fdt_find_node_by_phandle(tree, phandle);
        }
        current = current->parent;
    }
    return NULL;
}

size_t fdt_parse_interrupts(const FDTNode *node, FDTInterrupt *interrupts, size_t max_count)
{
    if (!node || !interrupts || max_count == 0) return 0;

    size_t prop_len = 0;
    const uint8_t *prop = fdt_get_prop_value(node, "interrupts", &prop_len);
    if (!prop || prop_len == 0) return 0;

    /* Default #interrupt-cells = 1 (just IRQ number) */
    uint32_t int_cells = 1;
    /* Try to find interrupt-cells from parent or interrupt-parent */
    if (node->parent) {
        fdt_read_prop_u32(node->parent, "#interrupt-cells", &int_cells);
        if (int_cells == 0) int_cells = 1;
    }

    size_t count = prop_len / (int_cells * FDT_CELL_SIZE);
    if (count > max_count) count = max_count;

    for (size_t i = 0; i < count; i++) {
        const uint8_t *entry = prop + i * int_cells * FDT_CELL_SIZE;
        interrupts[i].irq_number = fdt_read_u32(entry);
        interrupts[i].flags = (int_cells >= 2) ? fdt_read_u32(entry + 4) : 0;
        interrupts[i].phandle = (int_cells >= 3) ? fdt_read_u32(entry + 8) : 0;
    }
    return count;
}

/* ===== Memory Reservation (L3: spec §5.3) ===== */

size_t fdt_get_reserved_memory(const FDTTree *tree, FDTReserveEntry *entries, size_t max_count)
{
    if (!tree || !entries || max_count == 0) return 0;
    size_t count = tree->reserve_count;
    if (count > max_count) count = max_count;
    memcpy(entries, tree->reserve_entries, count * sizeof(FDTReserveEntry));
    return count;
}

/* ===== Validation (L4: post-parse integrity check) ===== */

bool fdt_validate(const FDTTree *tree)
{
    if (!tree || !tree->parsed) return false;
    if (tree->header.magic != FDT_MAGIC) return false;
    if (tree->header.version < FDT_COMPAT_VERSION) return false;
    if (tree->header.totalsize < sizeof(FDTHeader)) return false;
    if (!tree->root) return false;
    if (tree->header.off_dt_struct >= tree->header.totalsize) return false;
    if (tree->header.off_dt_strings >= tree->header.totalsize) return false;
    return true;
}

/* ===== Display (L1: debugging/tree visualization) ===== */

void fdt_print_tree(const FDTNode *node, int depth)
{
    if (!node) return;

    for (int d = 0; d < depth; d++) printf("  ");
    printf("%s {\n", node->name[0] ? node->name : "/");

    for (size_t i = 0; i < node->prop_count; i++) {
        for (int d = 0; d < depth + 1; d++) printf("  ");
        const FDTProperty *prop = &node->properties[i];
        printf("%s", prop->name);
        if (prop->value && prop->value_len > 0 && prop->value_len <= 64) {
            /* Try to print as string if printable */
            bool printable = true;
            for (size_t k = 0; k < prop->value_len && prop->value[k]; k++) {
                if (!isprint((unsigned char)prop->value[k])) { printable = false; break; }
            }
            if (printable) {
                printf(" = \"%s\"", prop->value);
            } else if (prop->value_len == 4) {
                printf(" = <0x%08X>", fdt_read_u32(prop->value));
            } else if (prop->value_len == 8) {
                printf(" = <0x%016llX>", (unsigned long long)fdt_read_u64(prop->value));
            }
        }
        printf(";\n");
    }

    for (size_t i = 0; i < node->child_count; i++) {
        fdt_print_tree(node->children[i], depth + 1);
    }

    for (int d = 0; d < depth; d++) printf("  ");
    printf("}\n");
}

void fdt_print_properties(const FDTNode *node)
{
    if (!node) return;
    printf("Properties of '%s':\n", node->name[0] ? node->name : "/");
    for (size_t i = 0; i < node->prop_count; i++) {
        printf("  %s (%zu bytes)\n", node->properties[i].name, node->properties[i].value_len);
    }
}

/* ===== Cleanup ===== */

static void fdt_free_node(FDTNode *node)
{
    if (!node) return;
    for (size_t i = 0; i < node->child_count; i++) {
        fdt_free_node(node->children[i]);
    }
    for (size_t i = 0; i < node->prop_count; i++) {
        free(node->properties[i].value);
        node->properties[i].value = NULL;
    }
    free(node);
}

void fdt_free_tree(FDTTree *tree)
{
    if (!tree) return;
    fdt_free_node(tree->root);
    tree->root = NULL;
    tree->strings_block = NULL;
    tree->parsed = false;
    tree->phandle_count = 0;
    tree->reserve_count = 0;
}
