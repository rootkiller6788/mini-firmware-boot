#ifndef DEVICE_TREE_H
#define DEVICE_TREE_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define FDT_MAGIC              0xD00DFEED
#define FDT_BEGIN_NODE         0x00000001
#define FDT_END_NODE           0x00000002
#define FDT_PROP               0x00000003
#define FDT_NOP                0x00000004
#define FDT_END                0x00000009
#define FDT_MAX_NAME_LEN       256
#define FDT_MAX_PATH_LEN       1024
#define FDT_MAX_PROPS          128
#define FDT_MAX_CHILDREN       64
#define FDT_SUPPORTED_VERSION  17
#define FDT_COMPAT_VERSION     16
#define FDT_CELL_SIZE          4

typedef struct {
    uint32_t magic;
    uint32_t totalsize;
    uint32_t off_dt_struct;
    uint32_t off_dt_strings;
    uint32_t off_mem_rsvmap;
    uint32_t version;
    uint32_t last_comp_version;
    uint32_t boot_cpuid_phys;
    uint32_t size_dt_strings;
    uint32_t size_dt_struct;
} FDTHeader;

typedef struct {
    char   name[FDT_MAX_NAME_LEN];
    size_t name_len;
} FDTProperty;

typedef struct {
    char          name[FDT_MAX_NAME_LEN];
    size_t        name_len;
    FDTProperty   properties[FDT_MAX_PROPS];
    size_t        prop_count;
    struct FDTNode *children[FDT_MAX_CHILDREN];
    size_t        child_count;
    struct FDTNode *parent;
} FDTNode;

typedef struct {
    FDTHeader  header;
    char      *strings_block;
    size_t     strings_size;
    FDTNode   *root;
    bool       parsed;
} FDTTree;

bool fdt_parse(FDTTree *tree, const uint8_t *dtb, size_t size);
FDTNode *fdt_find_node_by_path(const FDTTree *tree, const char *path);
FDTNode *fdt_find_compatible(const FDTNode *node, const char *compatible);
bool fdt_read_prop_u32(const FDTNode *node, const char *prop_name, uint32_t *value);
bool fdt_read_prop_string(const FDTNode *node, const char *prop_name, char *buffer, size_t buf_size);
void fdt_print_tree(const FDTNode *node, int depth);
void fdt_free_tree(FDTTree *tree);

uint32_t fdt_bswap32(uint32_t val);
uint32_t fdt_read_u32(const uint8_t *p);

#endif
