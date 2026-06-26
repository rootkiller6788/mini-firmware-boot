#include "device_tree.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

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

static uint32_t fdt_u32(const uint8_t *dtb, size_t offset)
{
    return fdt_read_u32(&dtb[offset]);
}

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

static bool fdt_parse_struct(FDTTree *tree, const uint8_t *dtb, size_t size,
                             size_t *pos, FDTNode *parent)
{
    size_t struct_start = tree->header.off_dt_struct;
    size_t strings_start = tree->header.off_dt_strings;
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

            if (!fdt_parse_struct(tree, dtb, size, pos, node)) {
                return false;
            }
            break;
        }
        case FDT_END_NODE:
            return true;
        case FDT_PROP: {
            uint32_t len = fdt_u32(dtb, *pos); *pos += FDT_CELL_SIZE;
            uint32_t nameoff = fdt_u32(dtb, *pos); *pos += FDT_CELL_SIZE;
            const char *prop_name = fdt_string(tree, nameoff);

            if (!node) return false;
            if (node->prop_count < FDT_MAX_PROPS) {
                FDTProperty *prop = &node->properties[node->prop_count++];
                size_t name_len = strlen(prop_name);
                if (name_len >= FDT_MAX_NAME_LEN) name_len = FDT_MAX_NAME_LEN - 1;
                memcpy(prop->name, prop_name, name_len);
                prop->name[name_len] = '\0';
                prop->name_len = name_len;
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

    size_t pos = tree->header.off_dt_struct + FDT_CELL_SIZE;
    return fdt_parse_struct(tree, dtb, size, &pos, NULL);
}

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
    if (!node) return NULL;

    for (size_t i = 0; i < node->prop_count; i++) {
        if (strcmp(node->properties[i].name, "compatible") == 0) {
            return (FDTNode *)node;
        }
    }

    for (size_t i = 0; i < node->child_count; i++) {
        FDTNode *result = fdt_find_compatible(node->children[i], compatible);
        if (result) return result;
    }
    return NULL;
}

bool fdt_read_prop_u32(const FDTNode *node, const char *prop_name, uint32_t *value)
{
    if (!node || !prop_name || !value) return false;
    for (size_t i = 0; i < node->prop_count; i++) {
        if (strcmp(node->properties[i].name, prop_name) == 0) {
            *value = 0;
            return true;
        }
    }
    return false;
}

bool fdt_read_prop_string(const FDTNode *node, const char *prop_name,
                          char *buffer, size_t buf_size)
{
    if (!node || !prop_name || !buffer || buf_size == 0) return false;
    for (size_t i = 0; i < node->prop_count; i++) {
        if (strcmp(node->properties[i].name, prop_name) == 0) {
            buffer[0] = '\0';
            return true;
        }
    }
    return false;
}

void fdt_print_tree(const FDTNode *node, int depth)
{
    if (!node) return;

    for (int d = 0; d < depth; d++) printf("  ");
    printf("%s {\n", node->name[0] ? node->name : "/");

    for (size_t i = 0; i < node->prop_count; i++) {
        for (int d = 0; d < depth + 1; d++) printf("  ");
        printf("%s;\n", node->properties[i].name);
    }

    for (size_t i = 0; i < node->child_count; i++) {
        fdt_print_tree(node->children[i], depth + 1);
    }

    for (int d = 0; d < depth; d++) printf("  ");
    printf("}\n");
}

static void fdt_free_node(FDTNode *node)
{
    if (!node) return;
    for (size_t i = 0; i < node->child_count; i++) {
        fdt_free_node(node->children[i]);
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
}
