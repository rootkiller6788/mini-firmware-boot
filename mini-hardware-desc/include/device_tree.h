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
#define FDT_MAX_RESERVE_ENTRIES 32
#define FDT_MAX_PHANDLE_MAP     128
#define FDT_INTERRUPTS_MAX      8

/* FDT memory reservation entry (spec §5.3) */
typedef struct {
    uint64_t address;
    uint64_t size;
} FDTReserveEntry;

/* Forward-declare FDTNode for self-reference */
struct FDTNode;

/* FDT interrupt specifier (spec §2.4) */
typedef struct {
    uint32_t irq_number;
    uint32_t flags;
    uint32_t phandle;
} FDTInterrupt;

/* phandle-to-node resolution cache */
typedef struct {
    uint32_t  phandle;
    struct FDTNode *node;
} FDTPhandleEntry;

/* FDT Header (spec §5.2) */
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
    char      name[FDT_MAX_NAME_LEN];
    size_t    name_len;
    uint8_t  *value;
    size_t    value_len;
} FDTProperty;

typedef struct FDTNode {
    char           name[FDT_MAX_NAME_LEN];
    size_t         name_len;
    FDTProperty    properties[FDT_MAX_PROPS];
    size_t         prop_count;
    struct FDTNode *children[FDT_MAX_CHILDREN];
    size_t         child_count;
    struct FDTNode *parent;
    uint32_t       phandle;
} FDTNode;

typedef struct {
    FDTHeader         header;
    char             *strings_block;
    size_t            strings_size;
    struct FDTNode   *root;
    bool              parsed;
    FDTReserveEntry   reserve_entries[FDT_MAX_RESERVE_ENTRIES];
    size_t            reserve_count;
    FDTPhandleEntry   phandle_map[FDT_MAX_PHANDLE_MAP];
    size_t            phandle_count;
} FDTTree;

/* Core parsing */
bool fdt_parse(FDTTree *tree, const uint8_t *dtb, size_t size);

/* Node navigation */
FDTNode *fdt_find_node_by_path(const FDTTree *tree, const char *path);
FDTNode *fdt_find_compatible(const FDTNode *node, const char *compatible);
FDTNode *fdt_find_node_by_phandle(const FDTTree *tree, uint32_t phandle);
FDTNode *fdt_get_parent(const FDTNode *node);
size_t   fdt_count_children(const FDTNode *node);

/* Property access */
bool fdt_read_prop_u32(const FDTNode *node, const char *prop_name, uint32_t *value);
bool fdt_read_prop_u64(const FDTNode *node, const char *prop_name, uint64_t *value);
bool fdt_read_prop_string(const FDTNode *node, const char *prop_name, char *buffer, size_t buf_size);
const uint8_t *fdt_get_prop_value(const FDTNode *node, const char *prop_name, size_t *len);
uint32_t fdt_get_phandle(const FDTNode *node);

/* Address translation (spec §2.3.8 "ranges") */
uint32_t fdt_get_address_cells(const FDTNode *node);
uint32_t fdt_get_size_cells(const FDTNode *node);
bool fdt_translate_address(const FDTTree *tree, const FDTNode *node,
                           uint64_t child_addr, uint64_t *parent_addr);

/* Interrupt parsing (spec §2.4) */
size_t fdt_parse_interrupts(const FDTNode *node, FDTInterrupt *interrupts, size_t max_count);
FDTNode *fdt_find_interrupt_parent(const FDTTree *tree, const FDTNode *node);

/* Memory reservation block (spec §5.3) */
size_t fdt_get_reserved_memory(const FDTTree *tree, FDTReserveEntry *entries, size_t max_count);

/* Tree traversal iterators */
FDTNode *fdt_first_child(const FDTNode *node);
FDTNode *fdt_next_sibling(const FDTNode *node);
FDTNode *fdt_find_child_by_name(const FDTNode *parent, const char *name);

/* Display & cleanup */
void fdt_print_tree(const FDTNode *node, int depth);
void fdt_print_properties(const FDTNode *node);
void fdt_free_tree(FDTTree *tree);
bool fdt_validate(const FDTTree *tree);

/* Endianness helpers */
uint32_t fdt_bswap32(uint32_t val);
uint32_t fdt_read_u32(const uint8_t *p);
uint64_t fdt_read_u64(const uint8_t *p);

#endif
